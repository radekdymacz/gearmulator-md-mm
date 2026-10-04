#include "mdPattern.h"

#include "dumpIo.h"
#include "sysex7bit.h"

#include <algorithm>

namespace elektronData
{
	namespace
	{
		using dumpIo::g_mdProductId;
		using dumpIo::section;
		using dumpIo::Writer;
		constexpr uint8_t g_patternDumpId = 0x67;
		constexpr uint8_t g_patternRequestId = 0x68;

		constexpr size_t g_trigOffset = 0x0a;
		constexpr size_t g_lockMaskOffset = 0x54;
		constexpr size_t g_patternFlagsOffset = 0x9e;
		constexpr size_t g_scalarsOffset = 0xb1;
		constexpr size_t g_locksOffset = 0xb7;
		constexpr size_t g_editOffset = 0x9dc;
		constexpr size_t g_extendedOffset = 0xac6;

		constexpr size_t g_wordsSize = MdPattern::g_tracks * 4;
		constexpr size_t g_flagsSize = 4 * 4;
		constexpr size_t g_locksSize = MdPattern::g_lockRows * 32;
		constexpr size_t g_editSize = (3 + 3 * MdPattern::g_tracks) * 4;
		constexpr size_t g_extendedSize = g_wordsSize + 3 * 4 + g_locksSize + 3 * MdPattern::g_tracks * 4;

		static_assert(g_lockMaskOffset == g_trigOffset + encoded7BitSize(g_wordsSize));
		static_assert(g_patternFlagsOffset == g_lockMaskOffset + encoded7BitSize(g_wordsSize));
		static_assert(g_scalarsOffset == g_patternFlagsOffset + encoded7BitSize(g_flagsSize));
		static_assert(g_editOffset == g_locksOffset + encoded7BitSize(g_locksSize));
		static_assert(g_extendedOffset == g_editOffset + encoded7BitSize(g_editSize));
		static_assert(MdPattern::g_shortDumpSize == g_extendedOffset + 5);
		static_assert(MdPattern::g_extendedDumpSize == g_extendedOffset + encoded7BitSize(g_extendedSize) + 5);

		uint64_t lo(const uint32_t _v) { return _v; }
		uint64_t hi(const uint32_t _v) { return static_cast<uint64_t>(_v) << 32; }
		uint32_t loWord(const uint64_t _v) { return static_cast<uint32_t>(_v); }
		uint32_t hiWord(const uint64_t _v) { return static_cast<uint32_t>(_v >> 32); }

		bool isHeaderValid(const std::vector<uint8_t>& _sysex)
		{
			return _sysex[0] == 0xf0 && _sysex[1] == 0x00 && _sysex[2] == 0x20 && _sysex[3] == 0x3c
				&& _sysex[4] == g_mdProductId && _sysex[6] == g_patternDumpId;
		}
	}

	bool MdPattern::operator==(const MdPattern& _o) const
	{
		return version == _o.version && revision == _o.revision && position == _o.position
			&& extended == _o.extended && trigs == _o.trigs && lockMasks == _o.lockMasks
			&& accentPattern == _o.accentPattern && slidePattern == _o.slidePattern
			&& swingPattern == _o.swingPattern && swingAmount == _o.swingAmount
			&& accentAmount == _o.accentAmount && length == _o.length && tempoMultiplier == _o.tempoMultiplier
			&& scale == _o.scale && kit == _o.kit && lockedRows == _o.lockedRows && lockRows == _o.lockRows
			&& accentEditAll == _o.accentEditAll && slideEditAll == _o.slideEditAll
			&& swingEditAll == _o.swingEditAll && trackAccent == _o.trackAccent
			&& trackSlide == _o.trackSlide && trackSwing == _o.trackSwing;
	}

	std::optional<MdPattern> decodeMdPattern(const std::vector<uint8_t>& _sysex)
	{
		const auto size = _sysex.size();
		if(size != MdPattern::g_shortDumpSize && size != MdPattern::g_extendedDumpSize)
			return {};
		if(!isHeaderValid(_sysex) || !isDumpTrailerValid(_sysex))
			return {};
		if(std::any_of(_sysex.begin() + 1, _sysex.end() - 1, [](const uint8_t _b) { return _b > 0x7f; }))
			return {};

		MdPattern p;
		p.version = _sysex[7];
		p.revision = _sysex[8];
		p.position = _sysex[9];
		p.extended = size == MdPattern::g_extendedDumpSize;

		auto trigs = section(_sysex, g_trigOffset, g_wordsSize);
		for(auto& t : p.trigs)
			t = lo(trigs.u32());
		auto masks = section(_sysex, g_lockMaskOffset, g_wordsSize);
		for(auto& m : p.lockMasks)
			m = masks.u32();
		auto flags = section(_sysex, g_patternFlagsOffset, g_flagsSize);
		p.accentPattern = lo(flags.u32());
		p.slidePattern = lo(flags.u32());
		p.swingPattern = lo(flags.u32());
		p.swingAmount = flags.u32();

		const auto* s = _sysex.data() + g_scalarsOffset;
		p.accentAmount = s[0];
		p.length = s[1];
		p.tempoMultiplier = s[2];
		p.scale = s[3];
		p.kit = s[4];
		p.lockedRows = s[5];

		auto locks = section(_sysex, g_locksOffset, g_locksSize);
		for(auto& row : p.lockRows)
			for(size_t step = 0; step < 32; ++step)
				row[step] = locks.u8();

		auto edit = section(_sysex, g_editOffset, g_editSize);
		p.accentEditAll = edit.u32();
		p.slideEditAll = edit.u32();
		p.swingEditAll = edit.u32();
		for(auto& v : p.trackAccent)
			v = lo(edit.u32());
		for(auto& v : p.trackSlide)
			v = lo(edit.u32());
		for(auto& v : p.trackSwing)
			v = lo(edit.u32());

		if(!p.extended)
			return p;

		auto ext = section(_sysex, g_extendedOffset, g_extendedSize);
		for(auto& t : p.trigs)
			t |= hi(ext.u32());
		p.accentPattern |= hi(ext.u32());
		p.slidePattern |= hi(ext.u32());
		p.swingPattern |= hi(ext.u32());
		for(auto& row : p.lockRows)
			for(size_t step = 32; step < 64; ++step)
				row[step] = ext.u8();
		for(auto& v : p.trackAccent)
			v |= hi(ext.u32());
		for(auto& v : p.trackSlide)
			v |= hi(ext.u32());
		for(auto& v : p.trackSwing)
			v |= hi(ext.u32());
		return p;
	}

	std::vector<uint8_t> encodeMdPattern(const MdPattern& _p)
	{
		std::vector<uint8_t> m{0xf0, 0x00, 0x20, 0x3c, g_mdProductId, 0x00, g_patternDumpId,
			_p.version, _p.revision, _p.position};
		m.reserve(_p.extended ? MdPattern::g_extendedDumpSize : MdPattern::g_shortDumpSize);

		Writer trigs;
		for(const auto t : _p.trigs)
			trigs.u32(loWord(t));
		trigs.appendEncodedTo(m);

		Writer masks;
		for(const auto v : _p.lockMasks)
			masks.u32(v);
		masks.appendEncodedTo(m);

		Writer flags;
		flags.u32(loWord(_p.accentPattern));
		flags.u32(loWord(_p.slidePattern));
		flags.u32(loWord(_p.swingPattern));
		flags.u32(_p.swingAmount);
		flags.appendEncodedTo(m);

		m.insert(m.end(), {_p.accentAmount, _p.length, _p.tempoMultiplier, _p.scale, _p.kit, _p.lockedRows});

		Writer locks;
		for(const auto& row : _p.lockRows)
			for(size_t step = 0; step < 32; ++step)
				locks.u8(row[step]);
		locks.appendEncodedTo(m);

		Writer edit;
		edit.u32(_p.accentEditAll);
		edit.u32(_p.slideEditAll);
		edit.u32(_p.swingEditAll);
		for(const auto v : _p.trackAccent)
			edit.u32(loWord(v));
		for(const auto v : _p.trackSlide)
			edit.u32(loWord(v));
		for(const auto v : _p.trackSwing)
			edit.u32(loWord(v));
		edit.appendEncodedTo(m);

		if(_p.extended)
		{
			Writer ext;
			for(const auto t : _p.trigs)
				ext.u32(hiWord(t));
			ext.u32(hiWord(_p.accentPattern));
			ext.u32(hiWord(_p.slidePattern));
			ext.u32(hiWord(_p.swingPattern));
			for(const auto& row : _p.lockRows)
				for(size_t step = 32; step < 64; ++step)
					ext.u8(row[step]);
			for(const auto v : _p.trackAccent)
				ext.u32(hiWord(v));
			for(const auto v : _p.trackSlide)
				ext.u32(hiWord(v));
			for(const auto v : _p.trackSwing)
				ext.u32(hiWord(v));
			ext.appendEncodedTo(m);
		}

		m.resize(m.size() + 5);
		writeDumpTrailer(m);
		return m;
	}

	std::vector<uint8_t> mdPatternRequest(const uint8_t _position)
	{
		return {0xf0, 0x00, 0x20, 0x3c, g_mdProductId, 0x00, g_patternRequestId,
			static_cast<uint8_t>(_position & 0x7f), 0xf7};
	}

	bool hasTrig(const MdPattern& _p, const size_t _track, const size_t _step)
	{
		if(_track >= MdPattern::g_tracks || _step >= MdPattern::g_maxSteps)
			return false;
		return (_p.trigs[_track] >> _step) & 1;
	}

	MdPattern withTrig(const MdPattern& _p, const size_t _track, const size_t _step, const bool _on)
	{
		if(_track >= MdPattern::g_tracks || _step >= MdPattern::g_maxSteps)
			return _p;
		auto result = _p;
		const auto bit = uint64_t(1) << _step;
		result.trigs[_track] = _on ? (result.trigs[_track] | bit) : (result.trigs[_track] & ~bit);
		return result;
	}

	namespace
	{
		size_t bitCount(uint32_t _v)
		{
			size_t n = 0;
			for(; _v; _v &= _v - 1)
				++n;
			return n;
		}

		// Rows below (_track, _param) in canonical order.
		size_t rowsBefore(const MdPattern& _p, const size_t _track, const size_t _param)
		{
			size_t rows = 0;
			for(size_t t = 0; t < _track; ++t)
				rows += bitCount(_p.lockMasks[t]);
			const auto below = _param >= 32 ? ~0u : ((1u << _param) - 1u);
			return rows + bitCount(_p.lockMasks[_track] & below);
		}
	}

	size_t usedLockRows(const MdPattern& _p)
	{
		return rowsBefore(_p, MdPattern::g_tracks - 1, 32);
	}

	std::optional<size_t> lockRowIndex(const MdPattern& _p, const size_t _track, const size_t _param)
	{
		if(_track >= MdPattern::g_tracks || _param >= 32 || !((_p.lockMasks[_track] >> _param) & 1))
			return {};
		// a dump with more than 64 mask bits set (corrupt or hostile) has rows past the pool: they do not exist
		const auto row = rowsBefore(_p, _track, _param);
		if(row >= MdPattern::g_lockRows)
			return {};
		return row;
	}

	std::optional<uint8_t> lockValue(const MdPattern& _p, const size_t _track, const size_t _param,
		const size_t _step)
	{
		const auto row = lockRowIndex(_p, _track, _param);
		if(!row || _step >= MdPattern::g_maxSteps)
			return {};
		const auto value = _p.lockRows[*row][_step];
		if(value == MdPattern::g_noLock)
			return {};
		return value;
	}

	std::optional<MdPattern> withLock(const MdPattern& _p, const size_t _track, const size_t _param,
		const size_t _step, const uint8_t _value)
	{
		if(_track >= MdPattern::g_tracks || _param >= 32 || _step >= MdPattern::g_maxSteps || _value > 0x7f)
			return {};
		auto result = _p;
		if(!lockRowIndex(result, _track, _param))
		{
			if(usedLockRows(result) >= MdPattern::g_lockRows)
				return {};
			const auto row = rowsBefore(result, _track, _param);
			auto& rows = result.lockRows;
			std::move_backward(rows.begin() + static_cast<std::ptrdiff_t>(row), rows.end() - 1, rows.end());
			rows[row].fill(MdPattern::g_noLock);
			result.lockMasks[_track] |= 1u << _param;
		}
		result.lockRows[*lockRowIndex(result, _track, _param)][_step] = _value;
		return result;
	}

	size_t visibleSteps(const MdPattern& _p)
	{
		const size_t count = _p.extended ? MdPattern::g_maxSteps : MdPattern::g_maxSteps / 2;
		const size_t total = 16 * (static_cast<size_t>(_p.scale & 3) + 1);
		return total < count ? total : count;
	}

	MdPattern withoutLockRow(const MdPattern& _p, const size_t _track, const size_t _param)
	{
		const auto row = lockRowIndex(_p, _track, _param);
		if(!row)
			return _p;
		auto result = _p;
		auto& rows = result.lockRows;
		std::move(rows.begin() + static_cast<std::ptrdiff_t>(*row) + 1, rows.end(),
			rows.begin() + static_cast<std::ptrdiff_t>(*row));
		rows.back().fill(0);
		result.lockMasks[_track] &= ~(1u << _param);
		return result;
	}

	MdPattern withoutLock(const MdPattern& _p, const size_t _track, const size_t _param, const size_t _step)
	{
		const auto row = lockRowIndex(_p, _track, _param);
		if(!row || _step >= MdPattern::g_maxSteps)
			return _p;
		auto result = _p;
		result.lockRows[*row][_step] = MdPattern::g_noLock;
		const auto visible = visibleSteps(result);
		for(size_t s = 0; s < visible; ++s)
			if(result.lockRows[*row][s] != MdPattern::g_noLock)
				return result;
		return withoutLockRow(result, _track, _param);
	}
}
