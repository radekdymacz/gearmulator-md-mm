#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace elektronData
{
	// Monomachine OS 1.32B song (SysEx 0x69, format 2/1), as a plain value. The
	// raw payload is 4,816 bytes: a 16-byte header, then 200 rows of 24 bytes.
	// Found in the song editor with SAVE SONG and a dump diff (MM-P1-RESULT §4).
	//   header 0x00  14  name (factory empty songs start with 0xff), 0x0e 2 unknown (kept)
	//   row +0   pattern 0-127, 0xfe LOOP/JUMP/HALT, 0xff END
	//       +1   loop/jump target row
	//       +2   repeats: plays repeats + 1 times; loop count (0 = infinite)
	//       +3   unknown (kept)
	//       +4   synth track mutes, bit t
	//       +5   MIDI track mutes, bit t (inferred)
	//       +6   offset (start step), +7 length (steps)
	//       +8   row transpose (signed), +9 6 synth track transposes, +15 6 MIDI track transposes (inferred)
	//       +21  unknown (kept), +22 tempo, BPM as u16, 0xffff = keep the tempo
	struct MmSongRow
	{
		std::array<uint8_t, 24> bytes{};

		uint8_t pattern() const { return bytes[0]; }
		uint16_t tempo() const { return static_cast<uint16_t>((bytes[22] << 8) | bytes[23]); }

		bool operator==(const MmSongRow& _o) const { return bytes == _o.bytes; }
	};

	struct MmSong
	{
		static constexpr size_t g_rows = 200;
		static constexpr size_t g_rawSize = 4816;
		static constexpr size_t g_slots = 24;
		static constexpr uint8_t g_loop = 0xfe;
		static constexpr uint8_t g_end = 0xff;
		static constexpr uint16_t g_keepTempo = 0xffff;

		uint8_t version = 2;
		uint8_t revision = 1;
		uint8_t position = 0;

		std::array<uint8_t, 14> name{};
		std::array<uint8_t, 2> x0e{};
		std::array<MmSongRow, g_rows> rows{};

		bool operator==(const MmSong& _o) const
		{
			return version == _o.version && revision == _o.revision && position == _o.position && name == _o.name
				&& x0e == _o.x0e && rows == _o.rows;
		}
		bool operator!=(const MmSong& _o) const { return !(*this == _o); }
	};

	// Named row fields (offsets above).
	namespace mmSongRow
	{
		constexpr size_t g_pattern = 0, g_target = 1, g_repeats = 2, g_x3 = 3, g_mutes = 4, g_midiMutes = 5,
			g_offset = 6, g_length = 7, g_transpose = 8, g_trackTranspose = 9, g_midiTranspose = 15, g_x21 = 21,
			g_tempo = 22;
	}

	std::optional<MmSong> decodeMmSong(const std::vector<uint8_t>& _sysex);
	std::vector<uint8_t> encodeMmSong(const MmSong& _song);
	std::optional<MmSong> mmSongFromRaw(const std::vector<uint8_t>& _raw, uint8_t _position);
	std::vector<uint8_t> mmSongRaw(const MmSong& _song);

	std::vector<uint8_t> mmSongRequest(uint8_t _slot);
	// The rows up to and including the first END (all 200 when there is none).
	size_t mmSongUsedRows(const MmSong& _song);
}
