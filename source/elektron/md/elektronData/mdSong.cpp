#include "mdSong.h"

#include "dumpIo.h"

namespace elektronData
{
	namespace
	{
		constexpr uint8_t g_songDumpId = 0x69;
		constexpr uint8_t g_songRequestId = 0x6a;
		constexpr uint8_t g_loadSongId = 0x6c;
		constexpr uint8_t g_saveSongId = 0x6d;

		constexpr size_t g_nameOffset = 0x0a;
		constexpr size_t g_rowsOffset = 0x1a;
		constexpr size_t g_encodedRowSize = encoded7BitSize(MdSong::g_rowSize);
	}

	MdSongRowKind songRowKind(const MdSongRow& _row, const size_t _rowIndex)
	{
		if(_row.pattern < 128)
			return MdSongRowKind::Pattern;
		if(_row.pattern == MdSongRow::g_endRow)
			return MdSongRowKind::End;
		if(_row.pattern != MdSongRow::g_loopRow)
			return MdSongRowKind::Invalid;
		if(_row.target < _rowIndex)
			return MdSongRowKind::Loop;
		return _row.target == _rowIndex ? MdSongRowKind::Halt : MdSongRowKind::Jump;
	}

	std::optional<MdSong> decodeMdSong(const std::vector<uint8_t>& _sysex)
	{
		const auto size = _sysex.size();
		if(size < g_rowsOffset + g_encodedRowSize + 5 || (size - g_rowsOffset - 5) % g_encodedRowSize)
			return {};
		if(!dumpIo::isValidDump(_sysex, g_songDumpId))
			return {};

		MdSong s;
		s.version = _sysex[7];
		s.revision = _sysex[8];
		s.position = _sysex[9];
		for(size_t i = 0; i < MdSong::g_nameSize; ++i)
			s.name[i] = _sysex[g_nameOffset + i];

		s.rows.clear();
		for(size_t offset = g_rowsOffset; offset + 5 < size; offset += g_encodedRowSize)
		{
			auto r = dumpIo::section(_sysex, offset, MdSong::g_rowSize);
			MdSongRow row;
			row.pattern = r.u8();
			row.reserved = r.u8();
			row.repeats = r.u8();
			row.target = r.u8();
			row.mutes = static_cast<uint16_t>(r.u8() << 8);
			row.mutes |= r.u8();
			row.tempo = static_cast<uint16_t>(r.u8() << 8);
			row.tempo |= r.u8();
			row.start = r.u8();
			row.end = r.u8();
			s.rows.push_back(row);
		}
		return s;
	}

	std::vector<uint8_t> encodeMdSong(const MdSong& _s)
	{
		auto m = dumpIo::header(g_songDumpId, _s.version, _s.revision, _s.position);
		m.insert(m.end(), _s.name.begin(), _s.name.end());
		for(const auto& row : _s.rows)
		{
			dumpIo::Writer w;
			w.u8(row.pattern);
			w.u8(row.reserved);
			w.u8(row.repeats);
			w.u8(row.target);
			w.u8(static_cast<uint8_t>(row.mutes >> 8));
			w.u8(static_cast<uint8_t>(row.mutes));
			w.u8(static_cast<uint8_t>(row.tempo >> 8));
			w.u8(static_cast<uint8_t>(row.tempo));
			w.u8(row.start);
			w.u8(row.end);
			w.appendEncodedTo(m);
		}
		dumpIo::finish(m);
		return m;
	}

	std::vector<uint8_t> mdSongRequest(const uint8_t _slot)
	{
		return dumpIo::request(g_songRequestId, _slot);
	}

	std::vector<uint8_t> mdLoadSong(const uint8_t _slot)
	{
		return dumpIo::request(g_loadSongId, _slot);
	}

	std::vector<uint8_t> mdSaveSong(const uint8_t _slot)
	{
		return dumpIo::request(g_saveSongId, _slot);
	}
}
