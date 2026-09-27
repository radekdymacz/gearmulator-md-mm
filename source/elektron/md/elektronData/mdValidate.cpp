#include "mdValidate.h"

#include "mdGlobal.h"
#include "mdKit.h"
#include "mdMachines.h"
#include "mdPattern.h"
#include "mdSong.h"

#include <cmath>

namespace elektronData
{
	namespace
	{
		class Problems
		{
		public:
			template<typename... Parts>
			void add(const Parts&... _parts)
			{
				std::string text;
				(text += ... += toText(_parts));
				m_list.push_back(std::move(text));
			}

			void range(const std::string& _what, const long _value, const long _min, const long _max)
			{
				if(_value < _min || _value > _max)
					add(_what, " = ", _value, " is outside ", _min, "..", _max);
			}

			std::vector<std::string> take() { return std::move(m_list); }

		private:
			static std::string toText(const std::string& _s) { return _s; }
			static std::string toText(const char* _s) { return _s; }
			static std::string toText(const long _n) { return std::to_string(_n); }
			static std::string toText(const int _n) { return std::to_string(_n); }
			static std::string toText(const size_t _n) { return std::to_string(_n); }

			std::vector<std::string> m_list;
		};

		std::string at(const char* _what, const size_t _i)
		{
			return std::string(_what) + "[" + std::to_string(_i) + "]";
		}

		void checkName(Problems& _p, const uint8_t* _name, const size_t _size)
		{
			for(size_t i = 0; i < _size; ++i)
				if(_name[i] > 0x7f)
					_p.add("name byte ", i, " is not 7-bit");
		}

		constexpr uint32_t g_maxSwingAmount = 9830;	// 80 %
		constexpr uint16_t g_minTempo = 30 * 24;
		constexpr uint16_t g_maxTempo = 300 * 24;
	}

	int swingPercent(const uint32_t _swingAmount)
	{
		return 50 + static_cast<int>(std::lround(_swingAmount * 50.0 / 16384.0));
	}

	uint32_t swingAmountFromPercent(const int _percent)
	{
		return static_cast<uint32_t>(std::lround((_percent - 50) * 16384.0 / 50.0));
	}

	int accentDisplay(const uint8_t _accentAmount)
	{
		return static_cast<int>(std::lround(_accentAmount * 15.0 / 127.0));
	}

	uint8_t accentAmountFromDisplay(const int _display)
	{
		return static_cast<uint8_t>(std::lround(_display * 127.0 / 15.0));
	}

	std::vector<std::string> validate(const MdPattern& _p)
	{
		Problems p;
		const size_t steps = _p.extended ? 64 : 32;
		const uint64_t stepMask = steps == 64 ? ~uint64_t{0} : (uint64_t{1} << steps) - 1;

		p.range("position", _p.position, 0, 127);
		p.range("scale", _p.scale, 0, 3);
		p.range("length", _p.length, 1, 64);
		if(_p.scale <= 3 && _p.length > 16 * (_p.scale + 1))
			p.add("length ", static_cast<int>(_p.length), " exceeds the total length ", 16 * (_p.scale + 1));
		if(!_p.extended && _p.length > 32)
			p.add("a classic (32-step) dump cannot hold length ", static_cast<int>(_p.length));
		p.range("tempoMultiplier", _p.tempoMultiplier, 0, 3);
		p.range("kit", _p.kit, 0, 63);
		p.range("accentAmount", _p.accentAmount, 0, 127);
		p.range("swingAmount", static_cast<long>(_p.swingAmount), 0, g_maxSwingAmount);
		p.range("accentEditAll", static_cast<long>(_p.accentEditAll), 0, 1);
		p.range("slideEditAll", static_cast<long>(_p.slideEditAll), 0, 1);
		p.range("swingEditAll", static_cast<long>(_p.swingEditAll), 0, 1);

		for(const auto& [name, v] : {std::pair<const char*, uint64_t>{"accentPattern", _p.accentPattern},
			{"slidePattern", _p.slidePattern}, {"swingPattern", _p.swingPattern}})
			if(v & ~stepMask)
				p.add(name, " has steps beyond ", steps);

		for(size_t t = 0; t < MdPattern::g_tracks; ++t)
		{
			for(const auto& [name, v] : {std::pair<const char*, uint64_t>{"trigs", _p.trigs[t]},
				{"trackAccent", _p.trackAccent[t]}, {"trackSlide", _p.trackSlide[t]}, {"trackSwing", _p.trackSwing[t]}})
				if(v & ~stepMask)
					p.add(at(name, t), " has steps beyond ", steps);
			if(_p.lockMasks[t] & ~uint32_t{0xffffff})
				p.add(at("lockMasks", t), " locks a parameter above 24");
		}

		const auto rows = usedLockRows(_p);
		if(rows > MdPattern::g_lockRows)
			p.add("uses ", rows, " locked parameters, the limit is 64");
		for(size_t r = 0; r < MdPattern::g_lockRows; ++r)
		{
			for(size_t s = 0; s < MdPattern::g_maxSteps; ++s)
			{
				const auto v = _p.lockRows[r][s];
				// Unused rows and steps past the total length may hold residue
				// (factory patterns do); only playable lock values are checked.
				if(r < rows && s < steps && s < 16u * (_p.scale + 1u) && v != MdPattern::g_noLock && v > 0x7f)
					p.add(at("lockRows", r), " step ", s, " value ", static_cast<int>(v), " is not 0..127");
			}
		}
		return p.take();
	}

	std::vector<std::string> validate(const MdKit& _k)
	{
		Problems p;
		p.range("position", _k.position, 0, 63);
		checkName(p, _k.name.data(), _k.name.size());
		for(size_t t = 0; t < MdKit::g_tracks; ++t)
		{
			const auto track = at("tracks", t);
			if(!isKnownMdMachine(_k.models[t]))
				p.add(track, " model ", static_cast<long>(_k.models[t]), " is not an OS 1.63 machine");
			for(size_t i = 0; i < MdKit::g_paramsPerTrack; ++i)
				p.range(track + " param " + std::to_string(i), _k.params[t][i], 0, 127);
			p.range(track + " level", _k.levels[t], 0, 127);
			const auto& l = _k.lfos[t];
			p.range(track + " lfo.track", l.track, 0, 15);
			p.range(track + " lfo.param", l.param, 0, 23);
			p.range(track + " lfo.shape1", l.shape1, 0, 5);
			p.range(track + " lfo.shape2", l.shape2, 0, 5);
			p.range(track + " lfo.update", l.update, 0, 2);
			if(_k.trigGroups[t] != MdKit::g_noGroup)
				p.range(track + " trigGroup", _k.trigGroups[t], 0, 15);
			if(_k.muteGroups[t] != MdKit::g_noGroup)
				p.range(track + " muteGroup", _k.muteGroups[t], 0, 15);
		}
		for(size_t f = 0; f < MdKit::MasterFxCount; ++f)
			for(size_t i = 0; i < 8; ++i)
				p.range(at("masterFx", f) + " param " + std::to_string(i), _k.masterFx[f][i], 0, 127);
		return p.take();
	}

	std::vector<std::string> validate(const MdSong& _s)
	{
		Problems p;
		p.range("position", _s.position, 0, 31);
		checkName(p, _s.name.data(), _s.name.size());
		if(_s.rows.empty())
			p.add("a song needs at least its END row");
		if(_s.rows.size() > MdSong::g_maxRows)
			p.add("song has ", _s.rows.size(), " rows, the limit is ", MdSong::g_maxRows);
		if(!_s.rows.empty() && _s.rows.back().pattern != MdSongRow::g_endRow)
			p.add("the last row must be END");
		for(size_t i = 0; i < _s.rows.size(); ++i)
		{
			const auto& r = _s.rows[i];
			const auto row = at("rows", i);
			switch(songRowKind(r, i))
			{
			case MdSongRowKind::Pattern:
				p.range(row + " repeats", r.repeats, 0, 63);
				p.range(row + " end", r.end, 1, 64);
				if(r.start >= r.end)
					p.add(row, " start ", static_cast<int>(r.start), " is not before end ", static_cast<int>(r.end));
				if(r.tempo != MdSongRow::g_noTempo)
					p.range(row + " tempo (BPM x 24)", r.tempo, g_minTempo, g_maxTempo);
				break;
			case MdSongRowKind::Loop:
			case MdSongRowKind::Jump:
				p.range(row + " repeats", r.repeats, 0, 63);
				if(r.target >= _s.rows.size())
					p.add(row, " goes to row ", static_cast<int>(r.target), " past the end of the song");
				break;
			case MdSongRowKind::Halt:
				break;
			case MdSongRowKind::End:
				if(i + 1 != _s.rows.size())
					p.add(row, " END before the last row");
				break;
			case MdSongRowKind::Invalid:
				p.add(row, " pattern ", static_cast<int>(r.pattern), " is not a pattern, LOOP or END");
				break;
			}
		}
		return p.take();
	}

	std::vector<std::string> validate(const MdGlobal& _g)
	{
		Problems p;
		p.range("position", _g.position, 0, 7);
		for(size_t t = 0; t < MdGlobal::g_tracks; ++t)
			p.range(at("routing", t), _g.routing[t], 0, MdGlobal::g_mainOutput);
		for(size_t n = 0; n < _g.keymap.size(); ++n)
			if(_g.keymap[n] != MdGlobal::g_unmapped)
				p.range(at("keymap", n), _g.keymap[n], 0, 31);
		p.range("tempo (BPM x 24)", _g.tempo, g_minTempo, g_maxTempo);
		p.range("extendedMode", _g.extendedMode, 0, 1);
		return p.take();
	}
}
