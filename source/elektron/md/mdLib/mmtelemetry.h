#pragma once

#include "elektronData/mmScreen.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace md
{
	// Monomachine OS 1.32B machine state from emulated RAM (MM-P0-RESULT §3, §5,
	// §6; mmEditorProbeFirmwareTest), published by the audio thread after every
	// block and read lock-free by the Monomachine Editor. Fingerprint-gated by the
	// device: -1 / 0 = unknown for another firmware.
	using MmScreen = elektronData::MmScreen;

	struct MmTelemetry
	{
		static constexpr uint32_t g_stepAddress = 0x257e57;		// current step, 0-based, wraps at the length
		static constexpr uint32_t g_runningAddress = 0x26b46e;		// non-zero while the sequencer runs (not paused)
		static constexpr uint32_t g_screenAddress = 0x266ec8;		// u32: the current screen's handler
		static constexpr uint32_t g_recvCountAddress = 0x26a3c4;	// u32: messages taken on SYSEX RECV
		static constexpr uint32_t g_recvErrorAddress = 0x26a3c8;	// u32: bad messages on SYSEX RECV
		static constexpr uint32_t g_recvActiveAddress = 0x26a3c3;	// 1 while SYSEX RECV takes dumps
		static constexpr uint32_t g_tempoAddress = 0x2bc2a6;		// u16: tempo x 24 (0x61 and the TEMPO screen, MM-P3)
		// Patch RAM: 0x700023 is the current kit number, 0x700028 the working kit
		// (the kit dump's raw payload, 698 bytes, unsaved edits included).
		static constexpr uint32_t g_workingKitAddress = 0x700023;
		static constexpr size_t g_workingKitSize = 5 + 698;

		// Screen words (the handler addresses the screen word holds).
		static constexpr uint32_t g_screenBoot = 0x002c27f8;		// the start-up animation
		static constexpr uint32_t g_screenMain = 0x002c2908;
		static constexpr uint32_t g_screenGlobal = 0x002c2ee8;
		static constexpr uint32_t g_screenGlobalEdit = 0x002c3a98;	// the GLOBAL EDIT menus, SYSEX RECV among them

		static MmScreen screenOf(const uint32_t _word)
		{
			if(_word == g_screenBoot) return MmScreen::Boot;
			if(_word == g_screenMain) return MmScreen::Main;
			if(_word == g_screenGlobal) return MmScreen::Global;
			if(_word == g_screenGlobalEdit) return MmScreen::GlobalEdit;
			return _word != 0 && (_word >> 16) == 0x2c ? MmScreen::Other : MmScreen::Unknown;
		}

		std::atomic<int> step{-1};
		std::atomic<int> running{-1};		// 1 playing, 0 stopped or paused
		std::atomic<uint32_t> screen{0};
		std::atomic<uint32_t> recvCount{0};
		std::atomic<uint32_t> recvErrors{0};
		std::atomic<int> recvActive{0};
		std::atomic<int> tempo{0};			// BPM x 24, 0 = unknown
		std::atomic<uint64_t> blocks{0};

		std::atomic<uint32_t> workingKitSequence{0};		// odd while written, 0 = never
		std::array<std::atomic<uint8_t>, g_workingKitSize> workingKit{};

		// A consistent copy of the working-kit region: kit number at [0], the raw
		// kit from [5]. False while it is written or before the first publication.
		bool readWorkingKit(std::vector<uint8_t>& _region, uint32_t& _sequence) const
		{
			const auto before = workingKitSequence.load(std::memory_order_acquire);
			if(before == 0 || (before & 1))
				return false;
			_region.resize(g_workingKitSize);
			for(size_t i = 0; i < g_workingKitSize; ++i)
				_region[i] = workingKit[i].load(std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_acquire);
			if(workingKitSequence.load(std::memory_order_relaxed) != before)
				return false;
			_sequence = before;
			return true;
		}

		// Publish from the thread that owns the hardware. _read8 reads CPU memory.
		template<typename Read8>
		void publish(const Read8& _read8)
		{
			const auto read32 = [&](const uint32_t _a)
			{
				return (uint32_t(_read8(_a)) << 24) | (uint32_t(_read8(_a + 1)) << 16) | (uint32_t(_read8(_a + 2)) << 8)
					| uint32_t(_read8(_a + 3));
			};
			step.store(_read8(g_stepAddress), std::memory_order_relaxed);
			running.store(_read8(g_runningAddress) ? 1 : 0, std::memory_order_relaxed);
			screen.store(read32(g_screenAddress), std::memory_order_relaxed);
			recvCount.store(read32(g_recvCountAddress), std::memory_order_relaxed);
			recvErrors.store(read32(g_recvErrorAddress), std::memory_order_relaxed);
			recvActive.store(_read8(g_recvActiveAddress) == 1 ? 1 : 0, std::memory_order_relaxed);
			tempo.store((int(_read8(g_tempoAddress)) << 8) | _read8(g_tempoAddress + 1), std::memory_order_relaxed);
			const auto n = blocks.fetch_add(1, std::memory_order_release);
			if(n % 8)
				return;
			bool changed = workingKitSequence.load(std::memory_order_relaxed) == 0;
			for(size_t i = 0; i < g_workingKitSize && !changed; ++i)
				changed = _read8(g_workingKitAddress + static_cast<uint32_t>(i)) != workingKit[i].load(std::memory_order_relaxed);
			if(!changed)
				return;
			const auto sequence = workingKitSequence.load(std::memory_order_relaxed);
			workingKitSequence.store(sequence + 1, std::memory_order_relaxed);
			std::atomic_thread_fence(std::memory_order_release);
			for(size_t i = 0; i < g_workingKitSize; ++i)
				workingKit[i].store(_read8(g_workingKitAddress + static_cast<uint32_t>(i)), std::memory_order_relaxed);
			workingKitSequence.store(sequence + 2, std::memory_order_release);
		}

		void clear()
		{
			step.store(-1, std::memory_order_relaxed);
			running.store(-1, std::memory_order_relaxed);
			screen.store(0, std::memory_order_relaxed);
			tempo.store(0, std::memory_order_relaxed);
		}
	};
}
