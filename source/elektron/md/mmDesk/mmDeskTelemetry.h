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
		Stop
	};

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
	};
}
