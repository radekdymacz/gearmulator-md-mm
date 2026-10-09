// synthLib::Plugin::getState with a Device::StateCapture (the fork's hook in synthLib/device.h and plugin.cpp,
// doc/modern-ux/UPSTREAM.md): the device captures under the process/device lock and encodes after it is released,
// so the audio thread is not held up by a state save. md::Device is the one device that captures (mdstatecapture.h).

#include "synthLib/device.h"
#include "synthLib/plugin.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
	using namespace std::chrono_literals;

	struct CaptureControl
	{
		std::mutex mutex;
		std::condition_variable condition;
		bool encodeStarted = false;
		bool allowEncode = false;
		std::function<void()> onCaptureDestroyed;
	};

	// Saves through a capture whose encode waits for the test, as md::Device does (without the wait)
	class CaptureTestDevice final : public synthLib::Device
	{
	public:
		CaptureTestDevice(std::shared_ptr<CaptureControl> _control, std::vector<uint8_t> _payload)
			: synthLib::Device({}), m_control(std::move(_control)), m_payload(std::move(_payload))
		{
		}

		class Capture final : public StateCapture
		{
		public:
			Capture(std::shared_ptr<CaptureControl> _control, std::vector<uint8_t> _payload)
				: control(std::move(_control)), payload(std::move(_payload))
			{
			}

			~Capture() override
			{
				if(control->onCaptureDestroyed)
					control->onCaptureDestroyed();
			}

			bool encode(std::vector<uint8_t>& _state) override
			{
				std::unique_lock lock(control->mutex);
				control->encodeStarted = true;
				control->condition.notify_all();
				control->condition.wait(lock, [&] { return control->allowEncode; });
				_state.insert(_state.end(), payload.begin(), payload.end());
				return true;
			}

			std::shared_ptr<CaptureControl> control;
			std::vector<uint8_t> payload;
		};

		float getSamplerate() const override { return 48000.0f; }
		bool isValid() const override { return true; }
		// The synchronous path is the capture encoded at once, so with a Plugin that ignored captures the
		// encode would run under its lock
		bool getState(std::vector<uint8_t>& _state, const synthLib::StateType _type) override
		{
			auto capture = beginStateCapture(_type);
			return capture && capture->encode(_state);
		}
		bool setState(const std::vector<uint8_t>&, synthLib::StateType) override { return false; }
		std::unique_ptr<StateCapture> beginStateCapture(synthLib::StateType) override
		{
			return std::make_unique<Capture>(m_control, m_payload);
		}
		uint32_t getChannelCountIn() override { return 0; }
		uint32_t getChannelCountOut() override { return 2; }
		bool setDspClockPercent(uint32_t) override { return true; }
		uint32_t getDspClockPercent() const override { return 100; }
		uint64_t getDspClockHz() const override { return 0; }

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>&) override {}
		void processAudio(const synthLib::TAudioInputs&,
			const synthLib::TAudioOutputs&, size_t) override {}
		bool sendMidi(const synthLib::SMidiEvent&,
			std::vector<synthLib::SMidiEvent>&) override { return true; }

	private:
		std::shared_ptr<CaptureControl> m_control;
		std::vector<uint8_t> m_payload;
	};

	int fail(const char* const _message)
	{
		std::cerr << _message << '\n';
		return 1;
	}

	// A state save encodes without the process/device lock (the audio thread waits for it), and drops its
	// capture, which may own copies of whole memories, after the lock is released.
	int testStateCaptureEncodesUnlocked()
	{
		auto control = std::make_shared<CaptureControl>();
		const std::vector<uint8_t> payload{0x4d, 0x44, 0x53, 0x54};
		CaptureTestDevice device(control, payload);
		synthLib::Plugin plugin(&device, [](synthLib::Device* const _device)
		{
			return _device;
		});

		std::atomic<bool> destructionWasUnlocked{false};
		std::thread destructionProbeThread;
		control->onCaptureDestroyed = [&]
		{
			std::promise<void> acquiredPromise;
			auto acquired = acquiredPromise.get_future();
			destructionProbeThread = std::thread([&plugin, promise = std::move(acquiredPromise)]() mutable
			{
				plugin.withDeviceLocked([](synthLib::Device*) {});
				promise.set_value();
			});
			destructionWasUnlocked = acquired.wait_for(500ms) == std::future_status::ready;
		};
		const auto releaseEncode = [&]
		{
			{
				std::lock_guard lock(control->mutex);
				control->allowEncode = true;
			}
			control->condition.notify_all();
		};

		std::vector<uint8_t> saved;
		bool savedOk = false;
		std::thread saveThread([&] { savedOk = plugin.getState(saved, synthLib::StateTypeGlobal); });
		{
			std::unique_lock lock(control->mutex);
			if(!control->condition.wait_for(lock, 2s, [&] { return control->encodeStarted; }))
			{
				lock.unlock();
				releaseEncode();
				saveThread.join();
				if(destructionProbeThread.joinable())
					destructionProbeThread.join();
				return fail("state encode did not start");
			}
		}

		// The encode is deliberately blocked. Audio and control work must still get the lock at once.
		auto lockProbe = std::async(std::launch::async, [&]
		{
			return plugin.withDeviceLocked([](synthLib::Device* const _device)
			{
				return _device != nullptr;
			});
		});
		const bool lockWasFree = lockProbe.wait_for(500ms) == std::future_status::ready;
		releaseEncode();
		saveThread.join();
		const bool probeSawDevice = lockProbe.get();
		if(destructionProbeThread.joinable())
			destructionProbeThread.join();

		if(!lockWasFree || !probeSawDevice)
			return fail("state encode held the process/device lock");
		if(!destructionWasUnlocked)
			return fail("state capture was destroyed under the process/device lock");
		const std::vector<uint8_t> expected{1, synthLib::StateTypeGlobal, 0x4d, 0x44, 0x53, 0x54};
		if(!savedOk || saved != expected)
			return fail("captured state is not the plug-in header followed by the device payload");
		return 0;
	}
}

int main()
{
	if(const auto result = testStateCaptureEncodesUnlocked())
		return result;

	std::cout << "synthLib state capture tests passed\n";
	return 0;
}
