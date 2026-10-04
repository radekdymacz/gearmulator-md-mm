#include "mmPattern.h"

#include "mmDump.h"
#include "mmLayout.h"

namespace elektronData
{
	namespace
	{
		template<typename IO, typename A>
		void arpLayout(IO& _io, A& _a, const bool _trigs)
		{
			_io.bytes(_a.playOjmp);
			_io.bytes(_a.mode);
			_io.bytes(_a.range);
			_io.bytes(_a.speed);
			if(_trigs)
				_io.bytes(_a.trigs);
			_io.bytes(_a.length);
			_io.bytes(_a.steps);
		}

		template<typename IO, typename T>
		void transposeLayout(IO& _io, T& _t)
		{
			_io.bytes(_t.track);
			_io.bytes(_t.scale);
			_io.bytes(_t.key);
		}

		// The payload in order: see mmPattern.h for the offsets.
		template<typename IO, typename P>
		void layout(IO& _io, P& _p)
		{
			for(auto* m : {&_p.amp, &_p.filter, &_p.lfo, &_p.noteOff, &_p.midiTrig, &_p.midiNoteOff, &_p.pitch,
					&_p.chord, &_p.midiNote, &_p.slide, &_p.swing, &_p.midiSlide, &_p.midiSwing})
				_io.u64s(*m);
			_io.bytes(_p.x270);
			_io.u8(_p.swingAmount);
			_io.bytes(_p.lockMasks);
			_io.bytes(_p.notes);
			_io.u8(_p.length);
			_io.u8(_p.multiplier);
			_io.u8(_p.kit);
			_io.i8(_p.patternTranspose);
			transposeLayout(_io, _p.transpose);
			transposeLayout(_io, _p.midiTranspose);
			arpLayout(_io, _p.arp, true);
			arpLayout(_io, _p.midiArp, false);
			_io.bytes(_p.x54e);
			_io.u16(_p.midiNoteCount);
			_io.u8(_p.chordNoteCount);
			_io.u8(_p.x555);
			_io.u8(_p.lockRowCount);
			_io.bytes(_p.lockRows);
			_io.u8(_p.x14d7);
			_io.u16s(_p.midiNotes);
			_io.u16s(_p.chordNotes);
		}
	}

	bool MmPattern::operator==(const MmPattern& _o) const
	{
		return mmPatternRaw(*this) == mmPatternRaw(_o) && version == _o.version && revision == _o.revision
			&& position == _o.position;
	}

	std::optional<MmPattern> mmPatternFromRaw(const std::vector<uint8_t>& _raw, const uint8_t _position)
	{
		if(_raw.size() != MmPattern::g_rawSize)
			return std::nullopt;
		MmPattern p;
		p.position = _position;
		mmLayout::Reader r(_raw);
		layout(r, p);
		return r.position() == MmPattern::g_rawSize ? std::optional<MmPattern>(p) : std::nullopt;
	}

	std::vector<uint8_t> mmPatternRaw(const MmPattern& _pattern)
	{
		mmLayout::Writer w;
		layout(w, _pattern);
		return w.raw();
	}

	std::optional<MmPattern> decodeMmPattern(const std::vector<uint8_t>& _sysex)
	{
		const auto dump = unpackMmDump(_sysex);
		if(!dump || dump->command != g_mmPatternDump)
			return std::nullopt;
		auto p = mmPatternFromRaw(dump->raw, dump->position);
		if(p)
		{
			p->version = dump->version;
			p->revision = dump->revision;
		}
		return p;
	}

	std::vector<uint8_t> encodeMmPattern(const MmPattern& _pattern)
	{
		return packMmDump({g_mmPatternDump, _pattern.version, _pattern.revision, _pattern.position, mmPatternRaw(_pattern)});
	}

	std::vector<uint8_t> mmPatternRequest(const uint8_t _slot) { return mmRequest(0x68, _slot); }

	std::vector<MmLockParam> mmLockParams(const MmPattern& _pattern)
	{
		std::vector<MmLockParam> params;
		for(uint8_t t = 0; t < MmPattern::g_tracks; ++t)
			for(uint8_t pg = 0; pg < MmPattern::g_pages; ++pg)
				for(uint8_t i = 0; i < 8; ++i)
					if((_pattern.lockMasks[t][pg] >> i) & 1)
						params.push_back({t, pg, i});
		return params;
	}

	int mmLockRow(const MmPattern& _pattern, const MmLockParam& _param)
	{
		const auto params = mmLockParams(_pattern);
		// a row past the 62 of the pool (a dump with too many mask bits set) does not exist
		for(size_t r = 0; r < params.size() && r < MmPattern::g_lockRows; ++r)
			if(params[r] == _param)
				return static_cast<int>(r);
		return -1;
	}
}
