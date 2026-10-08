#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace elektronData
{
	// Machinedrum OS 1.63 global settings dump (SysEx command 0x50), as a plain value.
	//   0x000   10   F0 00 20 3C 02 00 50 <version 6> <revision 1> <position 0-7>
	//   0x00a   16   per-track output: 0-5 = individual outputs A-F, 6 = MAIN
	//                (verified: SysEx 0x5c "set track routing" writes byte 0x0a + track)
	//   0x01a  147   7-bit: 128-entry MIDI note -> track map, 0xff = unmapped
	//   0x0ad   19   raw settings, in order:
	//                base channel, (unused), tempo high, tempo low (BPM x 24, verified
	//                with SysEx 0x61), extended mode (verified with SET STATUS 0x20),
	//                sync flags, local control, 10 input-routing bytes, program change,
	//                trig mode. Only the verified fields carry meaning in the UI
	//                contract; the rest round-trip untouched.
	//   0x0c0    5   checksum, length, F7
	struct MdGlobal
	{
		static constexpr size_t g_tracks = 16;
		static constexpr size_t g_dumpSize = 0xc5;
		static constexpr size_t g_slots = 8;
		static constexpr uint8_t g_mainOutput = 6;
		static constexpr uint8_t g_unmapped = 0xff;
		// The MAP EDITOR's targets (keymap below): tracks, then the 128 patterns, then START and STOP.
		static constexpr uint8_t g_keymapFirstPattern = 16;
		static constexpr uint8_t g_keymapStart = 144;
		static constexpr uint8_t g_keymapStop = 145;
		static constexpr uint8_t g_maxKeymapTarget = 254;	// any byte but "--": the firmware keeps it as sent

		uint8_t version = 6;
		uint8_t revision = 1;
		uint8_t position = 0;

		std::array<uint8_t, g_tracks> routing{};
		std::array<uint8_t, 128> keymap{};
		uint8_t baseChannel = 0;
		uint8_t unused = 0;
		uint16_t tempo = 120 * 24;
		uint8_t extendedMode = 1;
		uint8_t syncFlags = 0;
		uint8_t localControl = 1;
		std::array<uint8_t, 10> inputSettings{};
		uint8_t programChange = 0;
		uint8_t trigMode = 1;

		bool operator==(const MdGlobal& _o) const;
		bool operator!=(const MdGlobal& _o) const { return !(*this == _o); }
	};

	// The meaning of the raw settings, measured on MD OS 1.63 (P5, mdP4ProbeFirmwareTest globals):
	// a global dump is stored at once but applied only when its slot is made active (SysEx 0x56).
	//  - syncFlags: 0x01 TEMPO IN external (MIDI Start waits for MIDI clock), 0x10 CTRL IN off (MIDI
	//    Start/Stop ignored), 0x20 TEMPO OUT (sends MIDI clock), 0x40 CTRL OUT (sends Start/Stop).
	//    0x02, 0x04, 0x08: no effect found.
	//  - programChange: 0x01 IN, 0x02 OUT, bits 2-6 the channel: 0 = BASE (in on the four base
	//    channels, out on the first), n = channel n (1-16).
	//  - baseChannel: 0-12 = channels 1-4 .. 13-16 (verified with CCs on 1 and 3).
	//  - trigMode (MAP EDITOR TRIG): 0 GATE (the pattern stops on note off, verified), 1 START, 2 QUE.
	//  - keymap (MAP EDITOR, mdDeskFirmwareTest keymap, 0.3.5): 0-15 = a track, 16-143 = a pattern (16 + slot: 17
	//    selects A02, 47 B16, 143 H16; GROUP PAT A-H), 144 = START, 145 = STOP (GROUP CTRL), 255 = "--". The firmware
	//    stores every byte as sent; 146-254 had no effect (a 2008 backup holds targets up to 47).
	//  - localControl: stored; no effect on TRIG keys was seen in the emulator (not verified).
	//  - inputSettings: TRIG IN A/B (GATE, SENS, VMIN, VMAX, DEST in the manual); not verified.
	namespace mdGlobalBits
	{
		constexpr uint8_t g_tempoInExternal = 0x01;
		constexpr uint8_t g_ctrlInOff = 0x10;
		constexpr uint8_t g_tempoOut = 0x20;
		constexpr uint8_t g_ctrlOut = 0x40;
		constexpr uint8_t g_programChangeIn = 0x01;
		constexpr uint8_t g_programChangeOut = 0x02;
		constexpr uint8_t g_maxBaseChannel = 12;
	}

	std::vector<uint8_t> mdSetActiveGlobal(uint8_t _slot);	// SysEx 0x56

	std::optional<MdGlobal> decodeMdGlobal(const std::vector<uint8_t>& _sysex);
	std::vector<uint8_t> encodeMdGlobal(const MdGlobal& _global);

	std::vector<uint8_t> mdGlobalRequest(uint8_t _slot);
}
