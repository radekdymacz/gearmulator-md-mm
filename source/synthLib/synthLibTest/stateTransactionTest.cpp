#include "synthLib/device.h"
#include "synthLib/plugin.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

namespace
{
	using namespace std::chrono_literals;

	struct Control
	{
		std::mutex mutex;
		std::condition_variable condition;
		uint32_t prepareStarted = 0;
		bool allowPrepare = false;
		std::function<void()> onTransactionDestroyed;
	};

	class TestDevice final : public synthLib::Device
	{
	public:
		explicit TestDevice(std::shared_ptr<Control> _control)
			: synthLib::Device({}), m_control(std::move(_control))
		{
		}

		class Transaction final : public StateTransaction
		{
		public:
			Transaction(std::shared_ptr<Control> _control, std::vector<uint8_t> _state,
				const uint64_t _generation)
				: control(std::move(_control)), state(std::move(_state))
				, generation(_generation)
			{
			}

			~Transaction() override
			{
				if(control->onTransactionDestroyed)
					control->onTransactionDestroyed();
			}

			bool prepare() override
			{
				std::unique_lock lock(control->mutex);
				++control->prepareStarted;
				control->condition.notify_all();
				control->condition.wait(lock, [&] { return control->allowPrepare; });
				return true;
			}

			std::shared_ptr<Control> control;
			std::vector<uint8_t> state;
			uint64_t generation;
		};

		float getSamplerate() const override { return 48000.0f; }
		bool isValid() const override { return true; }
		bool getState(std::vector<uint8_t>& _state, synthLib::StateType) override
		{
			_state.insert(_state.end(), m_state.begin(), m_state.end());
			return true;
		}
		bool setState(const std::vector<uint8_t>& _state, synthLib::StateType) override
		{
			m_state = _state;
			return true;
		}
		bool supportsStateTransactions() const override { return true; }
		std::unique_ptr<StateTransaction> beginStateTransaction(
			std::shared_ptr<const std::vector<uint8_t>> _state,
			synthLib::StateType) override
		{
			if(!_state)
				return {};
			return std::make_unique<Transaction>(m_control, *_state, ++m_generation);
		}
		bool finishStateTransaction(StateTransaction& _transaction) override
		{
			auto* const transaction = dynamic_cast<Transaction*>(&_transaction);
			if(!transaction || transaction->generation != m_generation)
				return false;
			m_state = transaction->state;
			return true;
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
		std::shared_ptr<Control> m_control;
		std::vector<uint8_t> m_state;
		uint64_t m_generation = 0;
	};

	int fail(const char* const _message)
	{
		std::cerr << _message << '\n';
		return 1;
	}

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
	auto control = std::make_shared<Control>();
	TestDevice device(control);
	synthLib::Plugin plugin(&device, [](synthLib::Device* const _device)
	{
		return _device;
	});

	std::atomic<bool> destructionWasUnlocked{false};
	std::atomic<bool> destructionProbeFinished{false};
	std::thread destructionProbeThread;
	control->onTransactionDestroyed = [&]
	{
		std::promise<void> acquiredPromise;
		auto acquired = acquiredPromise.get_future();
		destructionProbeThread = std::thread([&plugin, &destructionProbeFinished,
			promise = std::move(acquiredPromise)]() mutable
		{
			plugin.withDeviceLocked([](synthLib::Device*) {});
			destructionProbeFinished = true;
			promise.set_value();
		});
		destructionWasUnlocked = acquired.wait_for(500ms) == std::future_status::ready;
	};

	const std::vector<uint8_t> requested{1, synthLib::StateTypeGlobal, 0x2a};
	bool restored = false;
	std::thread restoreThread([&] { restored = plugin.setState(requested); });
	{
		std::unique_lock lock(control->mutex);
		if(!control->condition.wait_for(lock, 2s,
			[&] { return control->prepareStarted == 1; }))
		{
			control->allowPrepare = true;
			lock.unlock();
			control->condition.notify_all();
			restoreThread.join();
			return fail("state preparation did not start");
		}
	}

	// Preparation is deliberately blocked. The process/device lock must still be
	// immediately available to audio/control work on another thread.
	auto lockProbe = std::async(std::launch::async, [&]
	{
		return plugin.withDeviceLocked([](synthLib::Device* const _device)
		{
			return _device != nullptr;
		});
	});
	if(lockProbe.wait_for(500ms) != std::future_status::ready || !lockProbe.get())
	{
		{
			std::lock_guard lock(control->mutex);
			control->allowPrepare = true;
		}
		control->condition.notify_all();
		restoreThread.join();
		return fail("state preparation held the process/device lock");
	}

	{
		std::lock_guard lock(control->mutex);
		control->allowPrepare = true;
	}
	control->condition.notify_all();
	restoreThread.join();
	if(destructionProbeThread.joinable())
		destructionProbeThread.join();
	if(!restored)
		return fail("transactional state restore failed");
	if(!destructionProbeFinished || !destructionWasUnlocked)
		return fail("state transaction was destroyed under the process/device lock");

	std::vector<uint8_t> saved;
	if(!plugin.getState(saved, synthLib::StateTypeGlobal) || saved != requested)
		return fail("transactional state restore committed the wrong bytes");

	// A second request can begin while the first is preparing. The device-specific
	// generation check must make the older commit harmless even if it finishes last.
	control->onTransactionDestroyed = {};
	{
		std::lock_guard lock(control->mutex);
		control->prepareStarted = 0;
		control->allowPrepare = false;
	}
	const std::vector<uint8_t> older{1, synthLib::StateTypeGlobal, 0x31};
	const std::vector<uint8_t> newer{1, synthLib::StateTypeGlobal, 0x32};
	bool olderResult = true;
	bool newerResult = false;
	std::thread olderThread([&] { olderResult = plugin.setState(older); });
	{
		std::unique_lock lock(control->mutex);
		if(!control->condition.wait_for(lock, 2s,
			[&] { return control->prepareStarted == 1; }))
		{
			control->allowPrepare = true;
			lock.unlock();
			control->condition.notify_all();
			olderThread.join();
			return fail("older concurrent restore did not start");
		}
	}
	std::thread newerThread([&] { newerResult = plugin.setState(newer); });
	{
		std::unique_lock lock(control->mutex);
		if(!control->condition.wait_for(lock, 2s,
			[&] { return control->prepareStarted == 2; }))
		{
			control->allowPrepare = true;
			control->condition.notify_all();
			olderThread.join();
			newerThread.join();
			return fail("newer concurrent restore did not start");
		}
		control->allowPrepare = true;
	}
	control->condition.notify_all();
	olderThread.join();
	newerThread.join();
	saved.clear();
	if(olderResult || !newerResult
		|| !plugin.getState(saved, synthLib::StateTypeGlobal) || saved != newer)
		return fail("an interrupted restore superseded the newer request");

	if(const auto result = testStateCaptureEncodesUnlocked())
		return result;

	std::cout << "synthLib state transaction tests passed\n";
	return 0;
}
