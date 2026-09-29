#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "mddevice.h"
#include "mdsequencerstate.h"
#include "mmtelemetry.h"

namespace md
{
	// The emulated machine as the Machinedrum and Monomachine Editors see it: md::Device, plus the
	// machine's state published for the editor pages and the timed panel key presses they send. The
	// plug-in makes this device (doc/modern-ux/UPSTREAM.md); md::Device itself is upstream's, unchanged.
	class DeskDevice : public Device
	{
	public:
		using Device::Device;

		// A key press as panel row states (md::panelKeySequence), each held for
		// _holdFrames of machine time: the audio thread sends them, so a hold does
		// not depend on UI timers (live recording needs RECORD held < 150 ms before
		// PLAY). Queued after a sequence still running. Needs the device lock.
		bool sendPanelSequence(const std::vector<PanelPacket>& _states, uint32_t _holdFrames);

		// MD OS 1.63 sequencer telemetry from emulated RAM, published by the audio
		// thread after every block and read lock-free by editors (MD Desk). -1 =
		// unknown: another firmware, or nothing published yet. Addresses: P0
		// (step) and the P2 smoke test (pattern, playing), fingerprint-gated.
		struct SequencerTelemetry
		{
			std::atomic<int> step{-1};		// current step, 0-based, wraps at the pattern length
			std::atomic<int> pattern{-1};	// switches with the status reply, ~2 steps before it is heard
			std::atomic<int> playing{-1};	// 1 playing, 0 stopped or paused
			// P3 (md::SequencerState): live recording (RECORD held + PLAY), grid edit
			// (RECORD alone) and the DATA ENTRY knob page: 0 synthesis, 1 effects, 2 routing.
			std::atomic<int> recording{-1};
			std::atomic<int> gridEdit{-1};
			std::atomic<int> knobPage{-1};
			std::atomic<uint64_t> blocks{0};
			// P6: panel packets (keys, encoder steps) queued by sendPanelSequence and not yet sent to the
			// machine. A fact for the editor: the firmware loses keys while it builds a dump, so it does
			// not ask for one while keys are on their way.
			std::atomic<int> panelPending{0};
			// P4 (md::BootAnimation, md::ChainAndMutes): the start-up animation (-1 unknown,
			// 1 running: panel keys are ignored, 0 over), the pattern mutes (bit 0 = track 1,
			// -1 unknown) and the firmware's pattern chain. The chain is published as values
			// that may tear for one read while it changes; readers see the next block's.
			std::atomic<int> bootAnimation{-1};
			std::atomic<int> mutes{-1};
			std::atomic<int> chainActive{-1};
			std::atomic<int> chainNext{-1};
			std::atomic<int> chainLength{0};
			std::array<std::atomic<uint8_t>, 16> chain{};

			// The working kit (P3, mdEditorProbeFirmwareTest workkit): patch RAM
			// 0x700008 holds the current kit number, 0x70000a the kit that plays,
			// unsaved edits included, as the kit dump's raw fields in dump order.
			// elektronData::mdWorkingKitFromMemory decodes the region. Republished
			// when it changes; a seqlock of relaxed atomics.
			static constexpr uint32_t g_workingKitAddress = 0x700008;
			static constexpr size_t g_workingKitSize = 0x462;
			std::atomic<uint32_t> workingKitSequence{0};		// odd while written, 0 = never
			std::array<std::atomic<uint8_t>, g_workingKitSize> workingKit{};

			// A consistent copy of the working kit; false while it is being written or
			// before the first publication. _sequence identifies the copy.
			bool readWorkingKit(std::vector<uint8_t>& _image, uint32_t& _sequence) const
			{
				const auto before = workingKitSequence.load(std::memory_order_acquire);
				if(before == 0 || (before & 1))
					return false;
				_image.resize(g_workingKitSize);
				for(size_t i = 0; i < g_workingKitSize; ++i)
					_image[i] = workingKit[i].load(std::memory_order_relaxed);
				std::atomic_thread_fence(std::memory_order_acquire);
				if(workingKitSequence.load(std::memory_order_relaxed) != before)
					return false;
				_sequence = before;
				return true;
			}
		};
		std::shared_ptr<const SequencerTelemetry> getSequencerTelemetry() const { return m_sequencerTelemetry; }
		// MM OS 1.32B state for the Monomachine Editor (md::MmTelemetry).
		std::shared_ptr<const MmTelemetry> getMmTelemetry() const { return m_mmTelemetry; }

	protected:
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;

	private:
		void sendPanelPackets();
		void publishSequencerTelemetry(size_t _frames);

		std::shared_ptr<SequencerTelemetry> m_sequencerTelemetry = std::make_shared<SequencerTelemetry>();
		std::shared_ptr<MmTelemetry> m_mmTelemetry = std::make_shared<MmTelemetry>();
		SequencerState m_sequencer;
		BootAnimation m_bootAnimation;
		const Hardware* m_bootAnimationOf = nullptr;
		// 128: the MM SYSEX RECV macro alone is 29 keys, about 60 row states (MM-P2).
		std::array<PanelPacket, 128> m_panelSequence{};
		size_t m_panelSequenceSize = 0;
		size_t m_panelSequenceNext = 0;
		std::array<uint32_t, 128> m_panelSequenceHolds{};
		uint64_t m_panelSequenceAt = 0;
		uint64_t m_frames = 0;
	};
}
