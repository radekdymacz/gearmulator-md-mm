// The DSP bridge protocol (bridgeLib) receives from a peer it does not control. Sizes, counts and enum values in a
// command are peer data: they must be checked before they size an allocation or index a buffer, and a malformed
// command must end its connection, not the process.

#include "bridgeLib/audioBuffers.h"
#include "bridgeLib/commandReader.h"
#include "bridgeLib/commands.h"
#include "bridgeLib/tcpConnection.h"
#include "bridgeLib/types.h"

#include "networkLib/exception.h"
#include "networkLib/logging.h"
#include "networkLib/stream.h"
#include "networkLib/tcpServer.h"
#include "networkLib/tcpStream.h"

#include "ptypes/pinet.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

// The largest single allocation since the last reset: proves that a huge announced size is refused before
// anything is sized from it.
namespace
{
	std::atomic<size_t> g_largestAllocation{0};
}

void* operator new(const size_t _size)
{
	auto largest = g_largestAllocation.load(std::memory_order_relaxed);
	while(_size > largest && !g_largestAllocation.compare_exchange_weak(largest, _size, std::memory_order_relaxed))
	{
	}
	if(void* p = std::malloc(_size ? _size : 1))
		return p;
	throw std::bad_alloc();
}

void operator delete(void* _p) noexcept
{
	std::free(_p);
}

void operator delete(void* _p, size_t) noexcept
{
	std::free(_p);
}

namespace
{
	int g_failures = 0;

	void check(const bool _ok, const std::string& _what)
	{
		std::cout << (_ok ? "ok   " : "FAIL ") << _what << '\n';
		if(!_ok)
			++g_failures;
	}

	template<typename F> bool throwsStdException(F&& _f)
	{
		try
		{
			_f();
		}
		catch(const std::exception&)
		{
			return true;
		}
		return false;
	}

	// Serves a fixed byte sequence, then reports the end of the connection like TcpStream does
	class ScriptedStream final : public networkLib::Stream
	{
	public:
		explicit ScriptedStream(std::vector<uint8_t> _data) : m_data(std::move(_data)) {}

		void close() override {}
		bool isValid() const override { return true; }
		bool flush() override { return true; }

		bool read(void* _buf, const uint32_t _byteSize) override
		{
			if(_byteSize > m_data.size() - m_pos)
				throw networkLib::NetException(networkLib::ConnectionClosed, "end of script");
			std::memcpy(_buf, m_data.data() + m_pos, _byteSize);
			m_pos += _byteSize;
			return true;
		}

		bool write(const void*, uint32_t) override { return true; }

	private:
		std::vector<uint8_t> m_data;
		size_t m_pos = 0;
	};

	class CountingReader final : public bridgeLib::CommandReader
	{
	public:
		CountingReader() : CommandReader(nullptr) {}

		void handleCommand(bridgeLib::Command, baseLib::BinaryStream&) override
		{
			++commands;
		}

		int commands = 0;
	};

	// A connection without a socket and without a thread: only its parsers are used
	class OfflineConnection final : public bridgeLib::TcpConnection
	{
	public:
		OfflineConnection() : TcpConnection(std::unique_ptr<networkLib::TcpStream>()) {}

	private:
		void handleException(const networkLib::NetException&) override {}
	};

	// A connection that records how it ended
	class LoopbackConnection final : public bridgeLib::TcpConnection
	{
	public:
		explicit LoopbackConnection(std::unique_ptr<networkLib::TcpStream>&& _stream) : TcpConnection(std::move(_stream))
		{
			start();
		}

		~LoopbackConnection() override
		{
			// Join the receive thread before the members it uses go away
			shutdown();
		}

		bool waitForException(const std::chrono::milliseconds _timeout)
		{
			std::unique_lock lock(m_mutex);
			return m_cv.wait_for(lock, _timeout, [this] { return m_exceptionSeen; });
		}

	private:
		void handleException(const networkLib::NetException&) override
		{
			{
				std::lock_guard lock(m_mutex);
				m_exceptionSeen = true;
			}
			m_cv.notify_all();
		}

		std::mutex m_mutex;
		std::condition_variable m_cv;
		bool m_exceptionSeen = false;
	};

	std::vector<uint8_t> commandBytes(const char (&_fourCC)[5], const uint32_t _size, const std::vector<uint8_t>& _payload = {})
	{
		std::vector<uint8_t> bytes(_fourCC, _fourCC + 4);
		const auto* size = reinterpret_cast<const uint8_t*>(&_size);
		bytes.insert(bytes.end(), size, size + sizeof(_size));
		bytes.insert(bytes.end(), _payload.begin(), _payload.end());
		return bytes;
	}

	void testCommandSize()
	{
		{
			ScriptedStream stream(commandBytes("MIDI", 0xffffffff, {1, 2, 3}));
			CountingReader reader;
			g_largestAllocation = 0;
			check(throwsStdException([&] { reader.read(stream); }) && reader.commands == 0, "a command announcing 4 GiB is refused (stream)");
			check(g_largestAllocation < 1024 * 1024, "without an allocation sized from it (largest: " + std::to_string(g_largestAllocation) + " bytes)");
		}
		{
			ScriptedStream stream(commandBytes("MIDI", bridgeLib::g_maxCommandSize + 1));
			CountingReader reader;
			check(throwsStdException([&] { reader.read(stream); }) && reader.commands == 0, "a command one byte over g_maxCommandSize is refused");
		}
		{
			const auto bytes = commandBytes("MIDI", 0xffffffff, {1, 2, 3});
			baseLib::BinaryStream in(bytes);
			CountingReader reader;
			g_largestAllocation = 0;
			check(throwsStdException([&] { reader.read(in); }) && reader.commands == 0, "a command announcing 4 GiB is refused (BinaryStream)");
			check(g_largestAllocation < 1024 * 1024, "without an allocation sized from it (largest: " + std::to_string(g_largestAllocation) + " bytes)");
		}
		{
			ScriptedStream stream(commandBytes("ping", 4, {1, 2, 3, 4}));
			CountingReader reader;
			reader.read(stream);
			check(reader.commands == 1, "a well-formed command still arrives");
		}
	}

	// Writes an audio command payload: channel count, block size, then per channel a size and that many samples
	baseLib::BinaryStream audioPayload(const uint32_t _numChannels, const uint32_t _numSamplesMax, const std::vector<uint32_t>& _channelSizes)
	{
		baseLib::BinaryStream s;
		s.write(static_cast<uint8_t>(_numChannels));
		s.write(_numSamplesMax);
		for(const auto size : _channelSizes)
		{
			s.write(size);
			const std::vector<float> samples(size, 0.5f);
			s.write(samples.data(), samples.size());
		}
		s.setReadPos(0);
		return s;
	}

	void testServerAudio()
	{
		// Two channels of 64 samples, each followed by a guard zone, and a spare third buffer: a receiver that
		// trusted the counts would write into the guards or the spare.
		constexpr uint32_t channels = 2;
		constexpr uint32_t capacity = 64;
		constexpr uint32_t guard = 16;
		constexpr float canary = -12345.0f;

		std::vector<float> buffers[3];
		for(auto& b : buffers)
			b.assign(capacity + guard, canary);
		float* outputs[3] = {buffers[0].data(), buffers[1].data(), buffers[2].data()};

		const auto guardsIntact = [&]
		{
			for(uint32_t c = 0; c < 3; ++c)
			{
				const auto first = c < channels ? capacity : 0;
				for(uint32_t i = first; i < capacity + guard; ++i)
				{
					if(buffers[c][i] != canary)
						return false;
				}
			}
			return true;
		};

		{
			auto in = audioPayload(3, 8, {8, 8, 8});
			check(throwsStdException([&] { bridgeLib::TcpConnection::handleAudio(outputs, channels, capacity, in); }) && guardsIntact(),
				"server: more channels than buffers is refused, nothing written");
		}
		{
			auto in = audioPayload(1, capacity + guard / 2, {capacity + guard / 2});
			check(throwsStdException([&] { bridgeLib::TcpConnection::handleAudio(outputs, channels, capacity, in); }) && guardsIntact(),
				"server: a block larger than the buffers is refused, nothing written past them");
		}
		{
			auto in = audioPayload(1, 8, {16});
			check(throwsStdException([&] { bridgeLib::TcpConnection::handleAudio(outputs, channels, capacity, in); }) && guardsIntact(),
				"server: a channel larger than its block is refused");
		}
		{
			auto in = audioPayload(channels, capacity, {capacity, capacity});
			const auto numSamples = bridgeLib::TcpConnection::handleAudio(outputs, channels, capacity, in);
			check(numSamples == capacity && buffers[0][0] == 0.5f && buffers[1][capacity - 1] == 0.5f && guardsIntact(),
				"server: a full block within the buffers is read");
		}
	}

	void testClientAudio()
	{
		OfflineConnection connection;
		constexpr auto outputChannels = static_cast<uint32_t>(std::tuple_size_v<synthLib::TAudioOutputs>);
		{
			bridgeLib::AudioBuffers buffers;
			auto in = audioPayload(outputChannels + 1, 8, std::vector<uint32_t>(outputChannels + 1, 8));
			check(throwsStdException([&] { connection.handleAudio(buffers, in); }) && buffers.getOutputSize() == 0,
				"client: more channels than outputs is refused");
		}
		{
			bridgeLib::AudioBuffers buffers;
			auto in = audioPayload(1, bridgeLib::AudioBuffers::BufferSize + 1, {bridgeLib::AudioBuffers::BufferSize + 1});
			check(throwsStdException([&] { connection.handleAudio(buffers, in); }) && buffers.getOutputSize() == 0,
				"client: a block larger than the ring buffers is refused");
		}
		{
			bridgeLib::AudioBuffers buffers;
			auto in = audioPayload(1, 8, {16});
			check(throwsStdException([&] { connection.handleAudio(buffers, in); }) && buffers.getOutputSize() == 0,
				"client: a channel larger than its block is refused");
		}
		{
			bridgeLib::AudioBuffers buffers;
			auto in = audioPayload(2, 32, {32, 32});
			connection.handleAudio(buffers, in);
			check(buffers.getOutputSize() == 32, "client: a well-formed block is read");
		}
	}

	void testEnums()
	{
		const auto stream = [](const uint32_t _value)
		{
			baseLib::BinaryStream s;
			s.write(_value);
			s.write(std::vector<uint8_t>{1, 2, 3});	// state, or the error message's length and text
			s.setReadPos(0);
			return s;
		};

		{
			auto s = stream(2);
			bridgeLib::DeviceState state;
			check(throwsStdException([&] { state.read(s); }), "a device state of an unknown type is refused");
		}
		{
			auto s = stream(0xffffffff);
			bridgeLib::RequestDeviceState request;
			check(throwsStdException([&] { request.read(s); }), "a device state request of an unknown type is refused");
		}
		{
			auto s = stream(static_cast<uint32_t>(bridgeLib::ErrorCode::FailedToCreateDevice) + 1);
			bridgeLib::Error error;
			check(throwsStdException([&] { error.read(s); }), "an unknown error code is refused");
		}
		{
			auto s = stream(synthLib::StateTypeCurrentProgram);
			bridgeLib::DeviceState state;
			state.read(s);
			check(state.type == synthLib::StateTypeCurrentProgram && state.state.size() == 3, "a known state type is read");
		}
		{
			auto s = stream(static_cast<uint32_t>(bridgeLib::ErrorCode::FailedToCreateDevice));
			bridgeLib::Error error;
			check(!throwsStdException([&] { error.read(s); }) && error.code == bridgeLib::ErrorCode::FailedToCreateDevice, "a known error code is read");
		}
	}

	// A peer sends "MIDI" with no payload: reading the event runs off the end of the command. That must end this
	// connection (handleException) and leave the process running.
	void testMalformedCommandOverLoopback()
	{
		std::mutex mutex;
		std::condition_variable cv;
		std::unique_ptr<LoopbackConnection> connection;

		std::unique_ptr<networkLib::TcpServer> server;
		int port = 0;
		for(int p = 46300; p < 46400 && !server; ++p)
		{
			try
			{
				server = std::make_unique<networkLib::TcpServer>([&](std::unique_ptr<networkLib::TcpStream> _stream)
				{
					auto c = std::make_unique<LoopbackConnection>(std::move(_stream));
					{
						std::lock_guard lock(mutex);
						connection = std::move(c);
					}
					cv.notify_all();
				}, p, networkLib::BindScope::Loopback);
				port = p;
			}
			catch(const std::exception&)
			{
			}
		}
		if(!server)
		{
			check(false, "loopback: a free port for the server");
			return;
		}

		ptypes::ipstream client(ptypes::ipaddress(127, 0, 0, 1), port);
		try
		{
			client.open();
			const auto bytes = commandBytes("MIDI", 0);
			client.write(bytes.data(), static_cast<int>(bytes.size()));
			client.flush();
		}
		catch(ptypes::exception* e)
		{
			std::cout << "     client error: " << static_cast<const char*>(e->get_message()) << '\n';
			delete e;
		}

		LoopbackConnection* c = nullptr;
		{
			std::unique_lock lock(mutex);
			cv.wait_for(lock, std::chrono::seconds(5), [&] { return connection != nullptr; });
			c = connection.get();
		}
		check(c != nullptr, "loopback: the server accepted the connection");
		check(c && c->waitForException(std::chrono::seconds(5)), "loopback: a malformed command ends its connection through handleException, the process lives on");

		client.close();
		server.reset();
		connection.reset();
	}
}

int main()
{
	networkLib::setLogFunc([](const networkLib::LogLevel _level, const char*, int, const std::string& _message)
	{
		if(_level >= networkLib::LogLevel::Warning)
			std::cout << "     log: " << _message << '\n';
	});

	testCommandSize();
	testServerAudio();
	testClientAudio();
	testEnums();
	testMalformedCommandOverLoopback();

	if(g_failures)
	{
		std::cout << g_failures << " check(s) failed\n";
		return 1;
	}
	std::cout << "bridgeLibTest: all checks passed\n";
	return 0;
}
