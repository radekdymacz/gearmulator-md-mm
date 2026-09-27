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

	std::optional<MdGlobal> decodeMdGlobal(const std::vector<uint8_t>& _sysex);
	std::vector<uint8_t> encodeMdGlobal(const MdGlobal& _global);

	std::vector<uint8_t> mdGlobalRequest(uint8_t _slot);
}
