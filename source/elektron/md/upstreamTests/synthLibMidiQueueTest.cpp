// synthLib::Plugin::addMidiEvent when the MIDI ring is full (the fork's fix in synthLib/plugin.cpp,
// doc/modern-ux/UPSTREAM.md): a producer that finds the ring full waits for the process lock before it makes room.
// If the audio thread drains the ring meanwhile, the producer must not pop the now empty ring: that replays a stale
// event and loses the new one (a lost note-off is a stuck note).

#include "synthLib/device.h"
#include "synthLib/plugin.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	void require(const bool _condition, const std::string& _message)
	{
		if(!_condition)
			throw std::runtime_error(_message);
	}

	// Silent stereo out; every MIDI event the plug-in delivers is recorded, in order
	class MidiRecordingDevice final : public synthLib::Device
	{
	public:
		explicit MidiRecordingDevice(const size_t _capacity) : synthLib::Device({})
		{
			// Reserved so that recording does not allocate while the plug-in processes
			m_received.reserve(_capacity);
		}

		const std::vector<synthLib::SMidiEvent>& getReceivedMidi() const { return m_received; }

		float getSamplerate() const override { return 44100.0f; }
		bool isValid() const override { return true; }
#if SYNTHLIB_DEMO_MODE == 0
		bool getState(std::vector<uint8_t>&, synthLib::StateType) override { return false; }
		bool setState(const std::vector<uint8_t>&, synthLib::StateType) override { return false; }
#endif
		uint32_t getChannelCountIn() override { return 0; }
		uint32_t getChannelCountOut() override { return 2; }
		bool setDspClockPercent(uint32_t) override { return false; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return 100000000; }

	private:
		void readMidiOut(std::vector<synthLib::SMidiEvent>&) override {}
		bool sendMidi(const synthLib::SMidiEvent& _event, std::vector<synthLib::SMidiEvent>&) override
		{
			m_received.push_back(_event);
			return true;
		}
		void processAudio(const synthLib::TAudioInputs&, const synthLib::TAudioOutputs& _outputs,
			const size_t _samples) override
		{
			for(size_t channel = 0; channel < 2; ++channel)
			{
				if(_outputs[channel])
					std::fill_n(_outputs[channel], _samples, 0.0f);
			}
		}

		std::vector<synthLib::SMidiEvent> m_received;
	};

	struct AudioStorage
	{
		static constexpr size_t Capacity = 64;
		std::array<std::array<float, Capacity>, 2> output{};
		synthLib::TAudioInputs inputs{};
		synthLib::TAudioOutputs outputs{};

		AudioStorage()
		{
			for(size_t channel = 0; channel < output.size(); ++channel)
				outputs[channel] = output[channel].data();
		}
	};

	void verifyMidiQueueOverflowRaceKeepsEveryEventOnce()
	{
		using namespace std::chrono_literals;
		constexpr auto capacity = static_cast<uint32_t>(synthLib::Plugin::RealtimeMidiEventCapacity);
		constexpr auto sentinel = capacity;

		const auto indexedEvent = [](const uint32_t _index)
		{
			return synthLib::SMidiEvent(synthLib::MidiEventSource::Host, synthLib::M_CONTROLCHANGE,
				static_cast<uint8_t>(_index & 0x7f), static_cast<uint8_t>((_index >> 7) & 0x7f), 0);
		};
		const auto indexOf = [](const synthLib::SMidiEvent& _event)
		{
			return static_cast<uint32_t>(_event.b) | (static_cast<uint32_t>(_event.c) << 7);
		};

		auto device = std::make_unique<MidiRecordingDevice>(capacity * 2);
		synthLib::Plugin plugin(device.get(), [](synthLib::Device*) {});
		plugin.setHostSamplerate(44100.0f, 44100.0f);
		plugin.setBlockSize(AudioStorage::Capacity);
		plugin.reserveMidiEventCapacity();
		AudioStorage storage;

		for(uint32_t i = 0; i < capacity; ++i)
			plugin.addMidiEvent(indexedEvent(i));

		std::thread producer;
		plugin.withDeviceLocked([&](synthLib::Device*)
		{
			producer = std::thread([&] { plugin.addMidiEvent(indexedEvent(sentinel)); });
			// Long enough for the producer to see the full ring and block on the process lock.
			// Too short can only make the test pass falsely, never fail falsely.
			std::this_thread::sleep_for(100ms);
			plugin.process(storage.inputs, storage.outputs, AudioStorage::Capacity, 0.0f, 0.0f, false);
		});
		producer.join();
		for(size_t block = 0; block < 4; ++block)
			plugin.process(storage.inputs, storage.outputs, AudioStorage::Capacity, 0.0f, 0.0f, false);

		const auto& received = device->getReceivedMidi();
		if(received.size() != capacity + 1)
			std::cerr << "MIDI overflow race: received " << received.size() << " events, expected "
				<< capacity + 1 << '\n';
		require(received.size() == capacity + 1,
			"MIDI queue overflow race lost or duplicated an event");
		for(uint32_t i = 0; i < received.size(); ++i)
		{
			if(indexOf(received[i]) == i)
				continue;
			std::cerr << "MIDI overflow race: event " << i << " carries index " << indexOf(received[i]) << '\n';
			require(false, "MIDI queue overflow race reordered, replayed or dropped an event");
		}
	}
}

int main()
{
	try
	{
		verifyMidiQueueOverflowRaceKeepsEveryEventOnce();
		std::cout << "synthLibMidiQueueTest: PASS\n";
		return 0;
	}
	catch(const std::exception& _e)
	{
		std::cerr << "synthLibMidiQueueTest: FAIL: " << _e.what() << '\n';
		return 1;
	}
}
