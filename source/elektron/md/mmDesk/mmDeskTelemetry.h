#pragma once

#include "elektronData/mmScreen.h"

#include <cstdint>

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
		LiveRecord		// hold RECORD, press PLAY: LIVE RECORDING
	};

	// Keys pressed with another held: FUNCTION for Global and MuteWindow, RECORD for LiveRecord.
	constexpr bool isChord(const Key _k) { return _k == Key::Global || _k == Key::MuteWindow || _k == Key::LiveRecord; }
	// The key held for a chord.
	constexpr bool heldIsRecord(const Key _k) { return _k == Key::LiveRecord; }

	using Screen = elektronData::MmScreen;

	// The machine as the desk sees it, from the audio thread (md::MmTelemetry).
	struct Telemetry
	{
		bool valid = false;
		int step = -1;
		bool running = false;
		Screen screen = Screen::Unknown;
		uint32_t recvCount = 0;
		uint32_t recvErrors = 0;
		bool recvActive = false;	// SYSEX RECV takes dumps (RAM 0x26a3c3)
		int tempo = 0;				// BPM x 24 (RAM 0x2bc2a6), 0 = unknown
		int mutes = -1;				// bit t: synth track t, bit 6 + t: MIDI track t (RAM, MM-P4); -1 = unknown
		int recording = -1;			// 0 off, 1 GRID RECORDING (RAM 0x26bbb3), 2 LIVE RECORDING (0x2bff01); -1 = unknown
	};
}
