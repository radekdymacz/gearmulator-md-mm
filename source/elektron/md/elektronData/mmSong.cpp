#include "mmSong.h"

#include "mmDump.h"

#include <algorithm>

namespace elektronData
{
	std::optional<MmSong> mmSongFromRaw(const std::vector<uint8_t>& _raw, const uint8_t _position)
	{
		if(_raw.size() != MmSong::g_rawSize)
			return std::nullopt;
		MmSong s;
		s.position = _position;
		std::copy_n(_raw.begin(), 14, s.name.begin());
		std::copy_n(_raw.begin() + 14, 2, s.x0e.begin());
		for(size_t r = 0; r < MmSong::g_rows; ++r)
			std::copy_n(_raw.begin() + static_cast<std::ptrdiff_t>(16 + r * 24), 24, s.rows[r].bytes.begin());
		return s;
	}

	std::vector<uint8_t> mmSongRaw(const MmSong& _song)
	{
		std::vector<uint8_t> raw(_song.name.begin(), _song.name.end());
		raw.insert(raw.end(), _song.x0e.begin(), _song.x0e.end());
		for(const auto& r : _song.rows)
			raw.insert(raw.end(), r.bytes.begin(), r.bytes.end());
		return raw;
	}

	std::optional<MmSong> decodeMmSong(const std::vector<uint8_t>& _sysex)
	{
		const auto dump = unpackMmDump(_sysex);
		if(!dump || dump->command != g_mmSongDump)
			return std::nullopt;
		auto s = mmSongFromRaw(dump->raw, dump->position);
		if(s)
		{
			s->version = dump->version;
			s->revision = dump->revision;
		}
		return s;
	}

	std::vector<uint8_t> encodeMmSong(const MmSong& _song)
	{
		return packMmDump({g_mmSongDump, _song.version, _song.revision, _song.position, mmSongRaw(_song)});
	}

	std::vector<uint8_t> mmSongRequest(const uint8_t _slot) { return mmRequest(0x6a, _slot); }

	size_t mmSongUsedRows(const MmSong& _song)
	{
		for(size_t r = 0; r < MmSong::g_rows; ++r)
			if(_song.rows[r].pattern() == MmSong::g_end)
				return r + 1;
		return MmSong::g_rows;
	}
}
