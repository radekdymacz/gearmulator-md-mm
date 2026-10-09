#pragma once

#include "elektronData/mmScreen.h"

#include <cstdint>
#include <vector>

namespace mmDesk
{
	// What a Monomachine device reports and takes (P6: the adapter interface's vocabulary, apart
	// from the SYSEX RECV state machine that one adapter uses).

	// Front-panel keys the desk presses (the edge maps them to md::PanelControl).
	enum class Key : uint8_t
	{
		Exit,
		Enter,
		Up,
		Down,
		Left,
		Right,
		Global,		// FUNCTION + KIT/SONG: the GLOBAL menu
		Play,
		Stop,
		Record,			// [RECORD] (MM-P4)
		MuteWindow,		// FUNCTION + BANK GROUP: the MUTE window
		Trig9, Trig10, Trig11, Trig12, Trig13, Trig14,	// TRIG 9-14: the MIDI tracks in the MUTE window
		LiveRecord,		// hold RECORD, press PLAY: LIVE RECORDING
		BankGroup		// [BANK GROUP]: A-D / E-H (MM-P8, before a chain or pick in the other half)
	};

	// Keys pressed with another held: FUNCTION for Global and MuteWindow, RECORD for LiveRecord.
	constexpr bool isChord(const Key _k) { return _k == Key::Global || _k == Key::MuteWindow || _k == Key::LiveRecord; }
	// The key held for a chord.
	constexpr bool heldIsRecord(const Key _k) { return _k == Key::LiveRecord; }

	using Screen = elektronData::MmScreen;

	// The firmware's pattern chain (MM-P8, md::MmTelemetry): hold BANK, press the TRIG keys (manual
	// 1-46). One bank, each pattern once, loops.
	struct Chain
	{
		bool active = false;
		int next = -1;						// the entry the firmware queues next, -1 unknown
		std::vector<uint8_t> patterns;		// 0-127, play order

		bool operator==(const Chain& _o) const { return active == _o.active && next == _o.next && patterns == _o.patterns; }
		bool operator!=(const Chain& _o) const { return !(*this == _o); }
	};

	// The machine as the desk sees it, from the audio thread (md::MmTelemetry).
	struct Telemetry
	{
		bool valid = false;
		uint64_t blocks = 0;		// audio blocks the emulator has run (it stands still while this does not move); 0 = unknown
		int step = -1;
		bool running = false;
		Screen screen = Screen::Unknown;
		uint32_t recvCount = 0;
		uint32_t recvErrors = 0;
		bool recvActive = false;	// SYSEX RECV takes dumps (RAM 0x26a3c3)
		int tempo = 0;				// BPM x 24 (RAM 0x2bc2a6), 0 = unknown
		int mutes = -1;				// bit t: synth track t, bit 6 + t: MIDI track t (RAM, MM-P4); -1 = unknown
		int recording = -1;			// 0 off, 1 GRID RECORDING (RAM 0x26bbb3), 2 LIVE RECORDING (0x2bff01); -1 = unknown
		int bankGroup = -1;			// MM-P8: BANK GROUP, 0 A-D, 1 E-H (RAM 0x70000b); -1 = unknown
		int songRow = -1;			// 0.3.5: the song row that plays (RAM 0x2bdba1, SONG mode); -1 = unknown
		bool chainKnown = false;	// MM-P8: the chain is readable (RAM 0x2bc2c4)
		Chain chain;
	};
}
