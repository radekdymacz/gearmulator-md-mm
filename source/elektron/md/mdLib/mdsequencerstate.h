#pragma once

#include "mdtypes.h"

#include <cstdint>

namespace md
{
	// MD OS 1.63 transport state from RAM (P3, mdEditorProbeFirmwareTest playing).
	// Pure: feed it the bytes and the frames since the last call.
	//  - Playing: the "stopped" byte 0x28cdaf reads 1 right after STOP, but 0 again
	//    after a second STOP or a PLAY pause, so the playhead must also move: a step
	//    change within 0.7 s (longer than the slowest step, 30 BPM at 3/4X).
	//  - Live recording (RECORD held + PLAY) has no byte of its own that was found;
	//    it shows as the RECORD LED blinking (about 0.4 s on, 0.5 s off) while
	//    playing. Grid edit (RECORD alone) is the same LED, steady.
	class SequencerState
	{
	public:
		static constexpr uint32_t g_stepAddress = 0x261aa7;			// P0: current step
		static constexpr uint32_t g_stoppedAddress = 0x28cdaf;		// 1 right after STOP
		static constexpr uint32_t g_recordLedAddress = 0x27f977;	// bit 4
		static constexpr uint32_t g_knobPageAddress = 0x2818e1;		// 0 synthesis, 1 effects, 2 routing
		static constexpr uint64_t g_moveWindowFrames = uint64_t(g_samplerate) * 700 / 1000;
		static constexpr uint64_t g_blinkWindowFrames = uint64_t(g_samplerate) * 600 / 1000;

		void update(const uint8_t _step, const uint8_t _stopped, const uint8_t _recordLed, const uint64_t _frames)
		{
			m_frames += _frames;
			if(_step != m_step)
			{
				m_step = _step;
				m_movedAt = m_frames;
			}
			m_stopped = _stopped != 0;
			const bool record = (_recordLed & 0x10) != 0;
			if(record != m_record)
			{
				m_record = record;
				m_ledChangedAt = m_frames;
			}
		}

		bool playing() const { return !m_stopped && m_movedAt > 0 && m_frames - m_movedAt < g_moveWindowFrames; }
		bool recording() const { return playing() && m_ledChangedAt > 0 && m_frames - m_ledChangedAt < g_blinkWindowFrames; }
		bool gridEdit() const { return m_record && !recording(); }

	private:
		uint64_t m_frames = 0;
		uint64_t m_movedAt = 0;
		uint64_t m_ledChangedAt = 0;
		int m_step = -1;
		bool m_stopped = true;
		bool m_record = false;
	};

	// MD OS 1.63 start-up animation (P4, mdP4ProbeFirmwareTest boot/keys/bootui). The firmware
	// takes MIDI while the animation still runs, but ignores panel keys until it is nearly
	// over (PLAY first taken 12.7 s after MIDI ready on a fresh machine, 9.3 s with a restored
	// project). RAM 0x28998a is 00 from boot until the main screen starts (13.4 s / 9.4 s),
	// then non-zero: the first non-zero value is "over". It is a one-shot: feed the byte once
	// per block while MIDI is ready, and reset for a new machine. (0x2a68e3, which also goes
	// 02 -> 00 at the end of the animation, is set again by every SysEx dump, so it is not used.)
	class BootAnimation
	{
	public:
		static constexpr uint32_t g_mainScreenAddress = 0x28998a;
		// Never seen longer than 14 s; after this it counts as over whatever the byte says.
		static constexpr uint64_t g_timeoutFrames = uint64_t(g_samplerate) * 30;

		void reset() { *this = BootAnimation(); }

		void update(const uint8_t _mainScreen, const uint64_t _frames)
		{
			if(m_over)
				return;
			m_frames += _frames;
			m_seen = true;
			m_over = _mainScreen != 0 || m_frames >= g_timeoutFrames;
		}

		// -1 not known yet, 1 the animation runs (keys are ignored), 0 over.
		int state() const { return m_over ? 0 : m_seen ? 1 : -1; }

	private:
		uint64_t m_frames = 0;
		bool m_seen = false;
		bool m_over = false;
	};

	// Pattern chain and mutes (P4, mdP4ProbeFirmwareTest chain4/chain5, mutes3).
	//  - Chain (BANK held + TRIG keys held together, in order): MC68331 internal SRAM, 32-bit
	//    big-endian values: 0x1001f5c active (0/1), 0x1001f60 the next entry to queue,
	//    0x1001f64 the length, 0x1001f68 + 4 n the patterns 0-127. One bank, each pattern
	//    once, so at most 16. SysEx LOAD PATTERN or a single TRIG clears "active".
	//  - Mutes: main RAM 0x28b34a, 16 bits big-endian, bit 0 = track 1. CC 12-15 and the
	//    panel's MUTE window write it.
	struct ChainAndMutes
	{
		static constexpr uint32_t g_chainAddress = 0x1001f5c;
		static constexpr uint32_t g_muteAddress = 0x28b34a;
		static constexpr size_t g_maxChain = 16;
	};
}
