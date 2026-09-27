#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace elektronData
{
	// One Machinedrum song row: 10 bytes, each row 7-bit packed on its own.
	// Semantics verified by playing crafted songs on MD OS 1.63 (P1-RESULT.md):
	//   pattern   0-127 pattern, g_loopRow = LOOP/JUMP/HALT, g_endRow = END
	//   reserved  always 0 in firmware data; kept as-is
	//   repeats   pattern rows: extra passes (0 = play once, 2 = three times)
	//             loop rows: extra passes of the loop body (0 = infinite)
	//   target    loop rows: row to go to. target < row = LOOP, target > row = JUMP,
	//             target == row = HALT (the manual's rule, reproduced by the firmware)
	//   mutes     u16 big-endian, bit n = track n + 1 muted for this row
	//   tempo     u16 big-endian, BPM x 24; g_noTempo keeps the previous tempo
	//   start     first step played (offset)
	//   end       step after the last one played (exclusive)
	struct MdSongRow
	{
		static constexpr uint8_t g_loopRow = 0xfe;
		static constexpr uint8_t g_endRow = 0xff;
		static constexpr uint16_t g_noTempo = 0xffff;

		uint8_t pattern = g_endRow;
		uint8_t reserved = 0;
		uint8_t repeats = 0;
		uint8_t target = 0;
		uint16_t mutes = 0;
		uint16_t tempo = g_noTempo;
		uint8_t start = 0;
		uint8_t end = 16;

		bool operator==(const MdSongRow& _o) const
		{
			return pattern == _o.pattern && reserved == _o.reserved && repeats == _o.repeats && target == _o.target
				&& mutes == _o.mutes && tempo == _o.tempo && start == _o.start && end == _o.end;
		}
		bool operator!=(const MdSongRow& _o) const { return !(*this == _o); }
	};

	// Machinedrum OS 1.63 song dump (SysEx command 0x69), as a plain value.
	//   0x000   10   F0 00 20 3C 02 00 69 <version 2> <revision 2> <position 0-31>
	//   0x00a   16   name, 7-bit ASCII, NUL terminated; bytes after the NUL kept as-is
	//   0x01a   12n  n rows, 12 encoded bytes each (one 7-bit group per row)
	//   end      5   checksum, length, F7
	// An empty song is a single END row (43 bytes).
	struct MdSong
	{
		static constexpr size_t g_nameSize = 16;
		static constexpr size_t g_slots = 32;
		static constexpr size_t g_maxRows = 256;
		static constexpr size_t g_rowSize = 10;

		uint8_t version = 2;
		uint8_t revision = 2;
		uint8_t position = 0;
		std::array<uint8_t, g_nameSize> name{};
		std::vector<MdSongRow> rows{MdSongRow{}};

		bool operator==(const MdSong& _o) const
		{
			return version == _o.version && revision == _o.revision && position == _o.position && name == _o.name
				&& rows == _o.rows;
		}
		bool operator!=(const MdSong& _o) const { return !(*this == _o); }
	};

	enum class MdSongRowKind
	{
		Pattern,
		Loop,
		Jump,
		Halt,
		End,
		Invalid
	};

	MdSongRowKind songRowKind(const MdSongRow& _row, size_t _rowIndex);

	std::optional<MdSong> decodeMdSong(const std::vector<uint8_t>& _sysex);
	std::vector<uint8_t> encodeMdSong(const MdSong& _song);

	std::vector<uint8_t> mdSongRequest(uint8_t _slot);
	std::vector<uint8_t> mdLoadSong(uint8_t _slot);
	std::vector<uint8_t> mdSaveSong(uint8_t _slot);
}
