#pragma once

// A raw HTTP client on 127.0.0.1 for the MCP server tests (httpServerTest.cpp, mcpServerTest.cpp): ptypes sockets,
// every wait bounded, and a watchdog that ends a test that hangs instead of waiting for ctest's timeout.

#include "ptypes/pinet.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace loopbackClient
{
	using Clock = std::chrono::steady_clock;
	using Milliseconds = std::chrono::milliseconds;
	using ClientStream = std::unique_ptr<ptypes::ipstream>;

	// Ends the test when a step does not finish in time, instead of hanging until ctest gives up
	class Watchdog
	{
	public:
		Watchdog(const Milliseconds _limit, std::string _what) : m_what(std::move(_what))
		{
			m_thread = std::thread([this, _limit]
			{
				std::unique_lock lock(m_mutex);
				if(m_cv.wait_for(lock, _limit, [this] { return m_done; }))
					return;
				std::cout << "FAIL " << m_what << ": did not finish within " << _limit.count() << " ms" << std::endl;
				std::_Exit(1);
			});
		}

		~Watchdog()
		{
			{
				std::lock_guard lock(m_mutex);
				m_done = true;
			}
			m_cv.notify_all();
			m_thread.join();
		}

		Watchdog(const Watchdog&) = delete;
		Watchdog& operator=(const Watchdog&) = delete;

	private:
		std::string m_what;
		std::mutex m_mutex;
		std::condition_variable m_cv;
		bool m_done = false;
		std::thread m_thread;
	};

	inline ClientStream connect(const ptypes::ipaddress& _ip, const int _port)
	{
		auto stream = std::make_unique<ptypes::ipstream>(_ip, _port);
		try
		{
			stream->open();
			return stream;
		}
		catch(ptypes::exception* e)
		{
			delete e;
			return nullptr;
		}
	}

	inline ClientStream connectLoopback(const int _port)
	{
		return connect(ptypes::ipaddress(127, 0, 0, 1), _port);
	}

	inline bool send(ptypes::ipstream& _s, const std::string& _data)
	{
		try
		{
			_s.write(_data.data(), static_cast<int>(_data.size()));
			_s.flush();
			return true;
		}
		catch(ptypes::exception* e)
		{
			delete e;
			return false;
		}
	}

	// A POST to /mcp with a JSON body and this server's Host
	inline std::string postText(const int _port, const std::string& _body)
	{
		return "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(_port)
			+ "\r\nContent-Type: application/json\r\n"
			"Content-Length: " + std::to_string(_body.size()) + "\r\n\r\n" + _body;
	}

	// Collects what arrives until _done says the data is complete, the server closes the connection or the
	// time is up. Returns true when the connection was closed by the server.
	inline bool receive(ptypes::ipstream& _s, const Milliseconds _timeout, std::string& _data,
		const std::function<bool(const std::string&)>& _done)
	{
		const auto deadline = Clock::now() + _timeout;
		try
		{
			while(!_done(_data))
			{
				// A timeout of 0 still polls once
				const auto left = std::chrono::duration_cast<Milliseconds>(deadline - Clock::now()).count();
				if(!_s.waitfor(static_cast<int>(std::max<long long>(0, left))))
					return false;
				if(_s.get_eof())
					return true;
				std::string chunk(static_cast<size_t>(_s.get_dataavail()), '\0');
				_s.read(chunk.data(), static_cast<int>(chunk.size()));
				_data += chunk;
			}
			return false;
		}
		catch(ptypes::exception* e)
		{
			// Reset by the server: closed as well
			delete e;
			return true;
		}
	}

	inline bool waitClosed(ptypes::ipstream& _s, const Milliseconds _timeout)
	{
		std::string ignored;
		return receive(_s, _timeout, ignored, [](const std::string&) { return false; });
	}

	// One complete response (headers and a Content-Length body), or "" when none arrives in time
	inline std::string readResponse(ptypes::ipstream& _s, const Milliseconds _timeout)
	{
		std::string data;
		const auto complete = [](const std::string& _d)
		{
			const auto headerEnd = _d.find("\r\n\r\n");
			if(headerEnd == std::string::npos)
				return false;
			size_t length = 0;
			const auto pos = _d.find("Content-Length: ");
			if(pos != std::string::npos && pos < headerEnd)
				length = std::strtoul(_d.c_str() + pos + 16, nullptr, 10);
			return _d.size() >= headerEnd + 4 + length;
		};
		receive(_s, _timeout, data, complete);
		return complete(data) ? data : std::string();
	}

	inline bool isStatus(const std::string& _response, const int _status)
	{
		return _response.rfind("HTTP/1.1 " + std::to_string(_status) + ' ', 0) == 0;
	}
}
