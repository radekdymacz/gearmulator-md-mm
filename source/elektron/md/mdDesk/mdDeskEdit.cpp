#include "mdDeskEdit.h"
#include "mdDeskLibrary.h"
#include "mdDeskModel.h"

#include "elektronData/mdJson.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mdNames.h"
#include "elektronData/mdValidate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace mdDesk
{
	namespace ed = elektronData;
	using ed::json::Value;

	DocRef refOf(const Document& _doc)
	{
		struct Visitor
		{
			DocRef operator()(const ed::MdPattern& _p) const { return {DocKind::Pattern, _p.position}; }
			DocRef operator()(const ed::MdKit& _k) const { return {DocKind::Kit, _k.position}; }
			DocRef operator()(const ed::MdSong& _s) const { return {DocKind::Song, _s.position}; }
			DocRef operator()(const ed::MdGlobal& _g) const { return {DocKind::Global, _g.position}; }
			DocRef operator()(const WorkingKit&) const { return {DocKind::WorkingKit, 0}; }
		};
		return std::visit(Visitor{}, _doc);
	}

	std::vector<std::string> problemsOf(const Document& _doc)
	{
		struct Visitor
		{
			std::vector<std::string> operator()(const WorkingKit& _w) const { return ed::validate(_w.kit); }
			std::vector<std::string> operator()(const ed::MdPattern& _v) const { return ed::validate(_v); }
			std::vector<std::string> operator()(const ed::MdKit& _v) const { return ed::validate(_v); }
			std::vector<std::string> operator()(const ed::MdSong& _v) const { return ed::validate(_v); }
			std::vector<std::string> operator()(const ed::MdGlobal& _v) const { return ed::validate(_v); }
		};
		return std::visit(Visitor{}, _doc);
	}

	std::optional<Document> Documents::get(const DocRef& _ref) const
	{
		switch(_ref.kind)
		{
		case DocKind::Pattern:
			if(const auto it = patterns.find(_ref.slot); it != patterns.end())
				return Document(it->second);
			break;
		case DocKind::Kit:
			if(const auto it = kits.find(_ref.slot); it != kits.end())
				return Document(it->second);
			break;
		case DocKind::Song:
			if(const auto it = songs.find(_ref.slot); it != songs.end())
				return Document(it->second);
			break;
		case DocKind::Global:
			if(global && global->position == _ref.slot)
				return Document(*global);
			break;
		case DocKind::WorkingKit:
			if(working)
				return Document(*working);
			break;
		}
		return {};
	}

	void Documents::set(const Document& _doc)
	{
		struct Visitor
		{
			Documents& docs;
			void operator()(const ed::MdPattern& _p) const { docs.patterns[_p.position] = _p; }
			void operator()(const ed::MdKit& _k) const { docs.kits[_k.position] = _k; }
			void operator()(const ed::MdSong& _s) const { docs.songs[_s.position] = _s; }
			void operator()(const ed::MdGlobal& _g) const { docs.global = _g; }
			void operator()(const WorkingKit& _w) const { docs.working = _w; }
		};
		std::visit(Visitor{*this}, _doc);
	}

	namespace
	{
		// A step mark (accent or slide): one pattern-wide set while the pattern's EDIT ALL is on,
		// else one set per track.
		struct Mark
		{
			const char* name;
			uint32_t ed::MdPattern::*editAll;
			uint64_t ed::MdPattern::*all;
			decltype(ed::MdPattern::trackAccent) ed::MdPattern::*perTrack;
		};
		constexpr Mark g_accent{"accent", &ed::MdPattern::accentEditAll, &ed::MdPattern::accentPattern, &ed::MdPattern::trackAccent};
		constexpr Mark g_slide{"slide", &ed::MdPattern::slideEditAll, &ed::MdPattern::slidePattern, &ed::MdPattern::trackSlide};

		uint64_t& bitsOf(ed::MdPattern& _p, const Mark& _m, const size_t _track)
		{
			return _p.*_m.editAll ? _p.*_m.all : (_p.*_m.perTrack)[_track & 15];
		}

		bool markOn(const ed::MdPattern& _p, const Mark& _m, const size_t _track, const size_t _step)
		{
			const auto bits = _p.*_m.editAll ? _p.*_m.all : (_p.*_m.perTrack)[_track & 15];
			return (bits >> _step) & 1;
		}
	}

	bool accentOn(const ed::MdPattern& _p, const size_t _track, const size_t _step) { return markOn(_p, g_accent, _track, _step); }
	bool slideOn(const ed::MdPattern& _p, const size_t _track, const size_t _step) { return markOn(_p, g_slide, _track, _step); }

	namespace
	{
		// Neutral track values for "clear sound": the machine's own defaults live in
		// the firmware and cannot be read without saving the kit.
		constexpr std::array<uint8_t, 24> g_neutral{
			64, 64, 64, 64, 64, 64, 64, 64,			// synthesis
			0, 0, 64, 64, 0, 127, 0, 0,				// AMD AMF EQF EQG FLTF FLTW FLTQ SRR
			0, 100, 64, 0, 0, 64, 0, 0};			// DIST VOL PAN DEL REV LFOS LFOD LFOM
		constexpr uint8_t g_neutralLevel = 100;

		std::string trackName(const size_t _t) { return "track " + std::to_string(_t + 1); }

		// Reads command arguments. apply has checked the command against its table row
		// (types, constant ranges, oneOf), so those are read as they are; `within` is for the
		// limits that depend on the document (visible steps, song rows, extended mode...) or on
		// another argument. Every problem becomes an error line.
		class Args
		{
		public:
			Args(const Value& _cmd, std::vector<std::string>& _errors) : m_cmd(_cmd), m_errors(_errors) {}

			// A number the table has checked; an optional argument that is absent is an error here.
			std::optional<int> integer(const char* _key) const
			{
				const auto* v = m_cmd.find(_key);
				if(!v || !v->isNumber())
				{
					m_errors.push_back(std::string(_key) + ": missing number");
					return {};
				}
				return static_cast<int>(v->asNumber());
			}

			std::optional<int> within(const char* _key, const int _min, const int _max) const
			{
				const auto v = integer(_key);
				if(v && (*v < _min || *v > _max))
				{
					m_errors.push_back(std::string(_key) + ": " + std::to_string(*v) + " is outside " + std::to_string(_min)
						+ ".." + std::to_string(_max));
					return {};
				}
				return v;
			}

			double number(const char* _key) const { return m_cmd.find(_key)->asNumber(); }
			const std::string& text(const char* _key) const { return m_cmd.find(_key)->asString(); }

			// The index of a text the table's oneOf allows, in the names it comes from (elektronData/mdNames.h).
			template<typename Names>
			size_t indexIn(const char* _key, const Names& _names) const
			{
				const auto& t = text(_key);
				for(size_t i = 0; i < _names.size(); ++i)
					if(t == _names[i])
						return i;
				return 0;	// not reached: the table's oneOf is these names
			}

			bool isNull(const char* _key) const
			{
				const auto* v = m_cmd.find(_key);
				return !v || v->isNull();
			}
			std::optional<bool> flag(const char* _key) const
			{
				const auto* v = m_cmd.find(_key);
				if(!v || !v->isBool())
					return {};
				return v->asBool();
			}
			// A flag the command must carry.
			std::optional<bool> requiredFlag(const char* _key) const
			{
				const auto f = flag(_key);
				if(!f)
					m_errors.push_back(std::string(_key) + ": expected true or false");
				return f;
			}
			const Value* value(const char* _key) const { return m_cmd.find(_key); }
			bool ok() const { return m_errors.empty(); }

		private:
			const Value& m_cmd;
			std::vector<std::string>& m_errors;
		};

		// What an edit function gets besides its document.
		struct In
		{
			const Args& a;
			std::vector<std::string>& errors;
			Clipboard& clip;
			std::string& note;
		};

		// One function per op, per document type: the edit tables below.
		template<typename T>
		using EditFn = std::optional<T> (*)(T, const In&);
		template<typename T>
		using Edits = std::map<std::string, EditFn<T>>;

		// ---- pattern ----

		ed::MdPattern clearStep(ed::MdPattern _p, const size_t _t, const size_t _s)
		{
			_p = ed::withTrig(_p, _t, _s, false);
			const auto bit = ~(uint64_t{1} << _s);
			_p.trackAccent[_t] &= bit;
			_p.trackSlide[_t] &= bit;
			_p.trackSwing[_t] &= bit;
			for(size_t param = 0; param < ed::MdKit::g_paramsPerTrack; ++param)
				_p = ed::withoutLock(_p, _t, param, _s);
			return _p;
		}

		void toggleBit(uint64_t& _bits, const size_t _s)
		{
			_bits ^= uint64_t{1} << _s;
		}

		int visibleOf(const ed::MdPattern& _p) { return static_cast<int>(ed::visibleSteps(_p)); }
		std::optional<int> stepArg(const ed::MdPattern& _p, const In& _in) { return _in.a.within("s", 0, visibleOf(_p) - 1); }

		std::optional<ed::MdPattern> trig(ed::MdPattern _p, const In& _in)
		{
			const auto t = size_t(*_in.a.integer("t"));
			const auto s = stepArg(_p, _in);
			if(!s)
				return {};
			const bool on = _in.a.flag("on").value_or(!ed::hasTrig(_p, t, size_t(*s)));
			return on ? ed::withTrig(_p, t, size_t(*s), true) : clearStep(_p, t, size_t(*s));
		}

		std::optional<ed::MdPattern> toggleMark(ed::MdPattern _p, const In& _in, const Mark& _m)
		{
			const auto t = size_t(*_in.a.integer("t"));
			const auto s = stepArg(_p, _in);
			if(!s)
				return {};
			if(!ed::hasTrig(_p, t, size_t(*s)))
			{
				_in.errors.push_back("Step " + std::to_string(*s + 1) + " of " + trackName(t) + " has no trig");
				return {};
			}
			toggleBit(bitsOf(_p, _m, t), size_t(*s));
			if(_p.*_m.editAll)
				_in.note = "Pattern-wide " + std::string(_m.name) + " (EDIT ALL is on)";
			return _p;
		}

		std::optional<ed::MdPattern> accent(ed::MdPattern _p, const In& _in) { return toggleMark(std::move(_p), _in, g_accent); }
		std::optional<ed::MdPattern> slide(ed::MdPattern _p, const In& _in) { return toggleMark(std::move(_p), _in, g_slide); }

		std::optional<ed::MdPattern> lock(ed::MdPattern _p, const In& _in)
		{
			const auto t = size_t(*_in.a.integer("t")), i = size_t(*_in.a.integer("i"));
			const auto s = stepArg(_p, _in);
			if(!s)
				return {};
			if(_in.a.isNull("v"))
				return ed::withoutLock(_p, t, i, size_t(*s));
			if(!ed::hasTrig(_p, t, size_t(*s)))
			{
				_in.errors.push_back("A step without a trig cannot hold a lock");
				return {};
			}
			const auto locked = ed::withLock(_p, t, i, size_t(*s), uint8_t(*_in.a.integer("v")));
			if(!locked)
				_in.errors.push_back("All 64 locked parameters are in use. Clear one before you lock a new parameter.");
			return locked;
		}

		std::optional<ed::MdPattern> clearLane(ed::MdPattern _p, const In& _in)
		{
			return ed::withoutLockRow(_p, size_t(*_in.a.integer("t")), size_t(*_in.a.integer("i")));
		}

		std::optional<ed::MdPattern> length(ed::MdPattern _p, const In& _in)
		{
			const auto v = _in.a.within("v", 1, visibleOf(_p));
			if(!v)
				return {};
			_p.length = uint8_t(*v);
			return _p;
		}

		std::optional<ed::MdPattern> totalLength(ed::MdPattern _p, const In& _in)
		{
			const auto v = _in.a.within("v", 16, _p.extended ? 64 : 32);
			if(!v)
				return {};
			if(*v % 16)
			{
				_in.errors.push_back("Total length is 16, 32, 48 or 64");
				return {};
			}
			_p.scale = uint8_t(*v / 16 - 1);
			_p.length = uint8_t(*v);
			return _p;
		}

		std::optional<ed::MdPattern> speed(ed::MdPattern _p, const In& _in)
		{
			_p.tempoMultiplier = uint8_t(_in.a.indexIn("v", ed::g_mdTempoMultipliers));
			return _p;
		}

		std::optional<ed::MdPattern> swing(ed::MdPattern _p, const In& _in)
		{
			_p.swingAmount = ed::swingAmountFromPercent(*_in.a.integer("v"));
			return _p;
		}

		std::optional<ed::MdPattern> accentAmount(ed::MdPattern _p, const In& _in)
		{
			_p.accentAmount = ed::accentAmountFromDisplay(*_in.a.integer("v"));
			return _p;
		}

		std::optional<ed::MdPattern> patternKit(ed::MdPattern _p, const In& _in)
		{
			_p.kit = uint8_t(*_in.a.integer("v"));
			return _p;
		}

		// A track's step range [from, to) within the visible steps, and its "track n, steps a-b".
		struct StepRange
		{
			size_t track, from, to;
			std::string where;
		};

		std::optional<StepRange> stepRange(const ed::MdPattern& _p, const In& _in)
		{
			const auto t = size_t(*_in.a.integer("t"));
			const auto from = _in.a.within("from", 0, visibleOf(_p) - 1), to = _in.a.within("to", 1, visibleOf(_p));
			if(!from || !to)
				return {};
			if(*to <= *from)
			{
				_in.errors.push_back("Empty step range");
				return {};
			}
			return StepRange{t, size_t(*from), size_t(*to), trackName(t) + ", steps " + std::to_string(*from + 1) + "-" + std::to_string(*to)};
		}

		std::optional<ed::MdPattern> clearSteps(ed::MdPattern _p, const In& _in)
		{
			const auto r = stepRange(_p, _in);
			if(!r)
				return {};
			for(auto s = r->from; s < r->to; ++s)
				_p = clearStep(_p, r->track, s);
			_in.note = "Cleared " + r->where;
			return _p;
		}

		std::optional<ed::MdPattern> copySteps(ed::MdPattern _p, const In& _in)
		{
			const auto r = stepRange(_p, _in);
			if(!r)
				return {};
			Clipboard::Steps c;
			c.length = r->to - r->from;
			for(auto s = r->from; s < r->to; ++s)
			{
				const auto bit = uint64_t{1} << (s - r->from);
				if(ed::hasTrig(_p, r->track, s))
					c.trigs |= bit;
				if(accentOn(_p, r->track, s))
					c.accent |= bit;
				if(slideOn(_p, r->track, s))
					c.slide |= bit;
				for(uint8_t param = 0; param < ed::MdKit::g_paramsPerTrack; ++param)
					if(const auto v = ed::lockValue(_p, r->track, param, s))
						c.locks[param][uint8_t(s - r->from)] = *v;
			}
			_in.clip.steps = std::move(c);
			_in.note = "Copied " + r->where;
			return _p;
		}

		std::optional<ed::MdPattern> pasteSteps(ed::MdPattern _p, const In& _in)
		{
			const auto t = size_t(*_in.a.integer("t"));
			const auto from = _in.a.within("from", 0, visibleOf(_p) - 1);
			if(!from)
				return {};
			if(!_in.clip.steps)
			{
				_in.errors.push_back("Copy a track page first");
				return {};
			}
			const auto& c = *_in.clip.steps;
			const auto start = size_t(*from);
			const auto end = std::min<size_t>(start + c.length, size_t(visibleOf(_p)));
			for(auto s = start; s < end; ++s)
				_p = clearStep(_p, t, s);
			size_t skipped = 0;
			for(auto s = start; s < end; ++s)
			{
				const auto bit = uint64_t{1} << (s - start);
				if(!(c.trigs & bit))
					continue;
				_p = ed::withTrig(_p, t, s, true);
				for(const auto& [mark, bits] : {std::pair{&g_accent, c.accent}, std::pair{&g_slide, c.slide}})
					if(bits & bit && !markOn(_p, *mark, t, s))
						toggleBit(bitsOf(_p, *mark, t), s);
			}
			for(const auto& [param, steps] : c.locks)
			{
				for(const auto& [rel, v] : steps)
				{
					const auto s = start + rel;
					if(s >= end)
						continue;
					if(const auto locked = ed::withLock(_p, t, param, s, v))
						_p = *locked;
					else
						++skipped;
				}
			}
			_in.note = "Pasted into " + trackName(t);
			if(skipped)
				_in.note += ". " + std::to_string(skipped) + " lock(s) skipped: all 64 locked parameters are in use";
			return _p;
		}

		const Edits<ed::MdPattern>& patternEdits()
		{
			static const Edits<ed::MdPattern> edits{
				{"trig", trig}, {"accent", accent}, {"slide", slide}, {"lock", lock}, {"clearLane", clearLane},
				{"length", length}, {"totalLength", totalLength}, {"speed", speed}, {"swing", swing},
				{"accentAmount", accentAmount}, {"patternKit", patternKit}, {"clearSteps", clearSteps},
				{"copySteps", copySteps}, {"pasteSteps", pasteSteps}};
			return edits;
		}

		// ---- the working kit ----

		std::optional<ed::MdKit> param(ed::MdKit _k, const In& _in)
		{
			_k.params[size_t(*_in.a.integer("t"))][size_t(*_in.a.integer("i"))] = uint8_t(*_in.a.integer("v"));
			return _k;
		}

		std::optional<ed::MdKit> level(ed::MdKit _k, const In& _in)
		{
			_k.levels[size_t(*_in.a.integer("t"))] = uint8_t(*_in.a.integer("v"));
			return _k;
		}

		std::optional<ed::MdKit> machine(ed::MdKit _k, const In& _in)
		{
			const auto t = size_t(*_in.a.integer("t"));
			const auto model = uint32_t(*_in.a.integer("model"));
			if(!ed::isKnownMdMachine(model))
			{
				_in.errors.push_back("model " + std::to_string(model) + " is not an OS 1.63 machine");
				return {};
			}
			_k.models[t] = model;
			// The synthesis values stay as they are and are sent to the new
			// machine; without "keep", effects and routing go neutral.
			if(!_in.a.flag("keepFx").value_or(true))
				for(size_t i = 8; i < 24; ++i)
					_k.params[t][i] = g_neutral[i];
			return _k;
		}

		std::optional<ed::MdKit> lfo(ed::MdKit _k, const In& _in)
		{
			const auto field = _in.a.indexIn("field", ed::g_mdLfoFields);
			const auto v = _in.a.within("v", 0, ed::g_mdLfoFieldMax[field]);
			if(!v)
				return {};
			auto& l = _k.lfos[size_t(*_in.a.integer("t"))];
			uint8_t* fields[] = {&l.track, &l.param, &l.shape1, &l.shape2, &l.update};
			*fields[field] = uint8_t(*v);
			return _k;
		}

		std::optional<ed::MdKit> group(ed::MdKit _k, const In& _in)
		{
			const auto t = *_in.a.integer("t");
			uint8_t target = ed::MdKit::g_noGroup;
			if(!_in.a.isNull("target"))
			{
				const auto v = *_in.a.integer("target");
				if(v == t)
				{
					_in.errors.push_back("A track cannot be in a group with itself");
					return {};
				}
				target = uint8_t(v);
			}
			(_in.a.text("kind") == "trig" ? _k.trigGroups : _k.muteGroups)[size_t(t)] = target;
			return _k;
		}

		std::optional<ed::MdKit> masterFx(ed::MdKit _k, const In& _in)
		{
			_k.masterFx[_in.a.indexIn("fx", ed::g_mdMasterFx)][size_t(*_in.a.integer("i"))] = uint8_t(*_in.a.integer("v"));
			return _k;
		}

		std::optional<ed::MdKit> kitName(ed::MdKit _k, const In& _in)
		{
			const auto& name = _in.a.text("name");
			if(name.size() > ed::MdKit::g_nameSize)
			{
				_in.errors.push_back("A kit name has at most 16 characters");
				return {};
			}
			_k.name.fill(0);
			std::copy(name.begin(), name.end(), _k.name.begin());
			return _k;
		}

		std::optional<ed::MdKit> copySound(ed::MdKit _k, const In& _in)
		{
			const auto t = size_t(*_in.a.integer("t"));
			Clipboard::Sound s;
			s.model = _k.models[t];
			s.params = _k.params[t];
			s.level = _k.levels[t];
			s.lfo = _k.lfos[t];
			_in.clip.sound = s;
			_in.note = "Copied the sound of " + trackName(t) + " (" + ed::mdMachineName(s.model) + ")";
			return _k;
		}

		std::optional<ed::MdKit> pasteSound(ed::MdKit _k, const In& _in)
		{
			const auto t = size_t(*_in.a.integer("t"));
			if(!_in.clip.sound)
			{
				_in.errors.push_back("Copy a track's sound first");
				return {};
			}
			const auto& s = *_in.clip.sound;
			_k.models[t] = s.model;
			_k.params[t] = s.params;
			_k.levels[t] = s.level;
			_k.lfos[t] = s.lfo;
			_k.lfos[t].track = uint8_t(t);
			_in.note = "Pasted " + ed::mdMachineName(s.model) + " onto " + trackName(t);
			return _k;
		}

		std::optional<ed::MdKit> clearSound(ed::MdKit _k, const In& _in)
		{
			const auto t = size_t(*_in.a.integer("t"));
			std::copy(g_neutral.begin(), g_neutral.end(), _k.params[t].begin());
			_k.levels[t] = g_neutralLevel;
			_in.note = trackName(t) + " reset to neutral values";
			return _k;
		}

		const Edits<ed::MdKit>& kitEdits()
		{
			static const Edits<ed::MdKit> edits{
				{"param", param}, {"level", level}, {"machine", machine}, {"lfo", lfo}, {"group", group},
				{"masterFx", masterFx}, {"kitName", kitName}, {"copySound", copySound}, {"pasteSound", pasteSound},
				{"clearSound", clearSound}};
			return edits;
		}

		// ---- song (edited as contract rows, so row semantics stay in mdJson) ----

		using Rows = std::vector<Value>;

		Value withMember(const Value& _object, const std::string& _key, Value _value)
		{
			Value out = Value::object();
			bool replaced = false;
			for(const auto& [k, v] : _object.asObject())
			{
				if(k == _key)
				{
					out.set(k, _value);
					replaced = true;
				}
				else
					out.set(k, v);
			}
			if(!replaced)
				out.set(_key, std::move(_value));
			return out;
		}

		bool isEnd(const Value& _row)
		{
			const auto* kind = _row.find("kind");
			return kind && kind->isString() && kind->asString() == "end";
		}

		// Loop and jump targets follow their rows: _map[old] = new index.
		Rows remapTargets(Rows _rows, const std::vector<size_t>& _map)
		{
			for(auto& row : _rows)
			{
				const auto* kind = row.find("kind");
				const auto* target = row.find("target");
				if(!kind || !target || !target->isNumber())
					continue;
				if(kind->asString() != "loop" && kind->asString() != "jump")
					continue;
				const auto old = static_cast<size_t>(target->asNumber());
				if(old < _map.size())
					row = withMember(row, "target", static_cast<int>(_map[old]));
			}
			return _rows;
		}

		std::optional<int> rowArg(const Rows& _rows, const In& _in, const char* _key)
		{
			return _in.a.within(_key, 0, static_cast<int>(_rows.size()) - 1);
		}

		std::optional<Rows> copyRow(Rows _rows, const In& _in)
		{
			const auto i = rowArg(_rows, _in, "i");
			if(!i)
				return {};
			if(isEnd(_rows[size_t(*i)]))
			{
				_in.errors.push_back("END cannot be copied");
				return {};
			}
			_in.clip.songRow = _rows[size_t(*i)];
			_in.note = "Copied row " + std::to_string(*i + 1);
			return _rows;
		}

		std::optional<Rows> rowSet(Rows _rows, const In& _in)
		{
			const auto i = rowArg(_rows, _in, "i");
			if(!i)
				return {};
			_rows[size_t(*i)] = *_in.a.value("row");
			return _rows;
		}

		// Insert at i; the END row can only move down.
		std::optional<Rows> insertRow(Rows _rows, const In& _in, const Value& _row)
		{
			if(_rows.size() >= ed::MdSong::g_maxRows)
			{
				_in.errors.push_back("A song holds 256 rows");
				return {};
			}
			const auto at = size_t(*_in.a.integer("i"));
			std::vector<size_t> map(_rows.size());
			for(size_t j = 0; j < map.size(); ++j)
				map[j] = j < at ? j : j + 1;
			_rows = remapTargets(std::move(_rows), map);
			_rows.insert(_rows.begin() + static_cast<std::ptrdiff_t>(at), _row);
			return _rows;
		}

		std::optional<Rows> rowInsert(Rows _rows, const In& _in)
		{
			if(!rowArg(_rows, _in, "i"))
				return {};
			const auto* row = _in.a.value("row");
			if(!row)
			{
				_in.errors.push_back("row: missing object");
				return {};
			}
			return insertRow(std::move(_rows), _in, *row);
		}

		std::optional<Rows> pasteRow(Rows _rows, const In& _in)
		{
			if(!rowArg(_rows, _in, "i"))
				return {};
			if(!_in.clip.songRow)
			{
				_in.errors.push_back("Copy a song row first");
				return {};
			}
			return insertRow(std::move(_rows), _in, *_in.clip.songRow);
		}

		std::optional<Rows> rowDelete(Rows _rows, const In& _in)
		{
			const auto i = rowArg(_rows, _in, "i");
			if(!i)
				return {};
			if(isEnd(_rows[size_t(*i)]))
			{
				_in.errors.push_back("END cannot be deleted");
				return {};
			}
			const auto at = size_t(*i);
			std::vector<size_t> map(_rows.size());
			for(size_t j = 0; j < map.size(); ++j)
				map[j] = j <= at ? j : j - 1;
			_rows.erase(_rows.begin() + static_cast<std::ptrdiff_t>(at));
			return remapTargets(std::move(_rows), map);
		}

		std::optional<Rows> rowMove(Rows _rows, const In& _in)
		{
			const auto from = rowArg(_rows, _in, "from"), to = rowArg(_rows, _in, "to");
			if(!from || !to)
				return {};
			if(isEnd(_rows[size_t(*from)]) || isEnd(_rows[size_t(*to)]))
			{
				_in.errors.push_back("END stays the last row");
				return {};
			}
			std::vector<size_t> order(_rows.size());
			for(size_t j = 0; j < order.size(); ++j)
				order[j] = j;
			const auto moved = order[size_t(*from)];
			order.erase(order.begin() + *from);
			order.insert(order.begin() + *to, moved);
			std::vector<size_t> map(_rows.size());
			for(size_t j = 0; j < order.size(); ++j)
				map[order[j]] = j;
			_rows = remapTargets(std::move(_rows), map);
			Rows reordered;
			for(const auto j : order)
				reordered.push_back(_rows[j]);
			return reordered;
		}

		const Edits<Rows>& songEdits()
		{
			static const Edits<Rows> edits{
				{"copyRow", copyRow}, {"rowSet", rowSet}, {"rowInsert", rowInsert}, {"pasteRow", pasteRow},
				{"rowDelete", rowDelete}, {"rowMove", rowMove}};
			return edits;
		}

		// A song edit runs on its contract rows; the song comes back through the codec.
		std::optional<ed::MdSong> editSong(const ed::MdSong& _song, const EditFn<Rows> _fn, const In& _in)
		{
			const auto doc = ed::songToJson(_song);
			const auto* rowsValue = doc.find("rows");
			const Rows rows = rowsValue ? rowsValue->asArray() : Rows{};
			const auto edited = _fn(rows, _in);
			if(!edited)
				return {};
			if(*edited == rows)
				return _song;
			std::vector<std::string> problems;
			auto song = ed::songFromJson(withMember(doc, "rows", Value(Value::Array(edited->begin(), edited->end()))), problems);
			if(!song)
				_in.errors.insert(_in.errors.end(), problems.begin(), problems.end());
			return song;
		}

		// ---- global ----

		std::optional<ed::MdGlobal> route(ed::MdGlobal _g, const In& _in)
		{
			_g.routing[size_t(*_in.a.integer("t"))] = uint8_t(_in.a.indexIn("out", ed::g_mdOutputs));
			return _g;
		}

		std::optional<ed::MdGlobal> tempo(ed::MdGlobal _g, const In& _in)
		{
			_g.tempo = static_cast<uint16_t>(std::lround(_in.a.number("bpm") * 24));
			return _g;
		}

		std::optional<ed::MdGlobal> extended(ed::MdGlobal _g, const In& _in)
		{
			const auto on = _in.a.requiredFlag("on");
			if(!on)
				return {};
			_g.extendedMode = *on ? 1 : 0;
			return _g;
		}

		// P5: the GLOBAL menu's settings, by name (elektronData::mdGlobalBits, measured): one setter per field.
		using GlobalField = bool (*)(ed::MdGlobal&, const In&);
		namespace gb = ed::mdGlobalBits;

		void setBit(uint8_t& _byte, const uint8_t _mask, const bool _set)
		{
			_byte = static_cast<uint8_t>(_set ? _byte | _mask : _byte & ~_mask);
		}

		// A flag field: bit _mask of the byte, set when "on" (cleared when on, if inverted).
		template<uint8_t ed::MdGlobal::*Byte, uint8_t Mask, bool Inverted = false>
		bool flagField(ed::MdGlobal& _g, const In& _in)
		{
			const auto on = _in.a.requiredFlag("on");
			if(on)
				setBit(_g.*Byte, Mask, *on != Inverted);
			return on.has_value();
		}

		bool localControl(ed::MdGlobal& _g, const In& _in)
		{
			const auto on = _in.a.requiredFlag("on");
			if(on)
				_g.localControl = *on ? 1 : 0;
			return on.has_value();
		}

		bool baseChannel(ed::MdGlobal& _g, const In& _in)
		{
			const auto v = _in.a.within("v", 0, gb::g_maxBaseChannel);
			if(v)
				_g.baseChannel = uint8_t(*v);
			return v.has_value();
		}

		bool programChangeChannel(ed::MdGlobal& _g, const In& _in)
		{
			const auto v = _in.a.within("v", 0, 16);	// 0 = BASE
			if(v)
				_g.programChange = static_cast<uint8_t>((_g.programChange & 3) | (*v << 2));
			return v.has_value();
		}

		bool trigMode(ed::MdGlobal& _g, const In& _in)
		{
			const auto v = _in.a.within("v", 0, 2);
			if(v)
				_g.trigMode = uint8_t(*v);
			return v.has_value();
		}

		bool keymap(ed::MdGlobal& _g, const In& _in)
		{
			const auto note = _in.a.integer("note");
			if(!note)
				return false;
			const auto* target = _in.a.value("target");
			if(target && target->isNull())
			{
				_g.keymap[size_t(*note)] = ed::MdGlobal::g_unmapped;
				return true;
			}
			const auto t = _in.a.integer("target");
			if(!t)
				return false;
			// A MIDI key maps one function: the target's old key is freed (the machine's rule).
			for(auto& k : _g.keymap)
				if(k == *t)
					k = ed::MdGlobal::g_unmapped;
			_g.keymap[size_t(*note)] = uint8_t(*t);
			return true;
		}

		const std::map<std::string, GlobalField>& globalFields()
		{
			static const std::map<std::string, GlobalField> fields{
				{"baseChannel", baseChannel},
				{"tempoIn", flagField<&ed::MdGlobal::syncFlags, gb::g_tempoInExternal>},
				{"ctrlIn", flagField<&ed::MdGlobal::syncFlags, gb::g_ctrlInOff, true>},
				{"tempoOut", flagField<&ed::MdGlobal::syncFlags, gb::g_tempoOut>},
				{"ctrlOut", flagField<&ed::MdGlobal::syncFlags, gb::g_ctrlOut>},
				{"programChangeIn", flagField<&ed::MdGlobal::programChange, gb::g_programChangeIn>},
				{"programChangeOut", flagField<&ed::MdGlobal::programChange, gb::g_programChangeOut>},
				{"localControl", localControl},
				{"programChangeChannel", programChangeChannel},
				{"trigMode", trigMode},
				{"keymap", keymap}};
			return fields;
		}

		std::optional<ed::MdGlobal> globalSet(ed::MdGlobal _g, const In& _in)
		{
			const auto& field = _in.a.text("field");
			const auto it = globalFields().find(field);
			if(it == globalFields().end())
			{
				_in.errors.push_back("field: unknown global setting " + field);	// the table's oneOf and this map disagree
				return {};
			}
			if(!it->second(_g, _in))
				return {};
			return _g;
		}

		const Edits<ed::MdGlobal>& globalEdits()
		{
			static const Edits<ed::MdGlobal> edits{{"route", route}, {"tempo", tempo}, {"extended", extended}, {"globalSet", globalSet}};
			return edits;
		}

		// ---- the edit: the document the command's kind names, its op's function, the change ----

		template<typename T>
		std::optional<EditFn<T>> editFor(const Edits<T>& _edits, const std::string& _op, std::vector<std::string>& _errors)
		{
			const auto it = _edits.find(_op);
			if(it == _edits.end())
			{
				_errors.push_back("no edit for command " + _op);	// a table row without its function
				return {};
			}
			return it->second;
		}

		// The document an edit starts from and the one it gives; nothing when it was refused.
		struct Edited
		{
			Document before;
			Document after;
		};

		template<typename T, typename Wrap = T>
		std::optional<Edited> run(const Edits<T>& _edits, const T& _doc, const std::string& _op, const In& _in)
		{
			const auto fn = editFor(_edits, _op, _in.errors);
			if(!fn)
				return {};
			auto after = (*fn)(_doc, _in);
			if(!after)
				return {};
			return Edited{Document(Wrap{_doc}), Document(Wrap{*after})};
		}

		std::string slotName(const char* _what, const int _slot) { return std::string(_what) + " " + std::to_string(_slot + 1); }

		std::optional<Edited> editPattern(const Documents& _docs, const std::string& _op, const In& _in, const EditContext&)
		{
			const auto p = *_in.a.integer("p");
			const auto it = _docs.patterns.find(uint8_t(p));
			if(it == _docs.patterns.end())
			{
				_in.errors.push_back("pattern " + ed::mdPatternName(unsigned(p)) + " is not loaded yet");
				return {};
			}
			return run(patternEdits(), it->second, _op, _in);
		}

		// Live kit edits change the working kit, and only for the kit that plays (the page sends them for it).
		std::optional<Edited> editWorkingKit(const Documents& _docs, const std::string& _op, const In& _in, const EditContext& _context)
		{
			const auto k = *_in.a.integer("k");
			if(!_context.currentKit || *_context.currentKit != k)
			{
				_in.errors.push_back(slotName("kit", k) + " is not the kit that plays: " + _op
					+ " edits the working kit of the kit that plays");
				return {};
			}
			const auto* kit = _docs.workingKitOf(uint8_t(k));
			if(!kit)
			{
				_in.errors.push_back("the working kit of " + slotName("kit", k) + " is not loaded yet");
				return {};
			}
			return run<ed::MdKit, WorkingKit>(kitEdits(), *kit, _op, _in);
		}

		std::optional<Edited> editSongAt(const Documents& _docs, const std::string& _op, const In& _in, const EditContext&)
		{
			const auto s = *_in.a.integer("s");
			const auto it = _docs.songs.find(uint8_t(s));
			if(it == _docs.songs.end())
			{
				_in.errors.push_back(slotName("song", s) + " is not loaded yet");
				return {};
			}
			const auto fn = editFor(songEdits(), _op, _in.errors);
			if(!fn)
				return {};
			const auto after = editSong(it->second, *fn, _in);
			if(!after)
				return {};
			return Edited{it->second, *after};
		}

		std::optional<Edited> editGlobal(const Documents& _docs, const std::string& _op, const In& _in, const EditContext&)
		{
			if(!_docs.global)
			{
				_in.errors.push_back("the global settings are not loaded yet");
				return {};
			}
			return run(globalEdits(), *_docs.global, _op, _in);
		}

		// Which document a command edits, by the table's kind column (DocKind::Kit rows are the library's).
		using KindEdit = std::optional<Edited> (*)(const Documents&, const std::string&, const In&, const EditContext&);

		std::optional<KindEdit> kindEdit(const DocKind _kind)
		{
			static const std::map<DocKind, KindEdit> edits{
				{DocKind::Pattern, editPattern}, {DocKind::WorkingKit, editWorkingKit}, {DocKind::Song, editSongAt},
				{DocKind::Global, editGlobal}};
			const auto it = edits.find(_kind);
			if(it == edits.end())
				return {};
			return it->second;
		}

		EditResult applyEdit(const Documents& _docs, const Value& _command, const deskCore::Command<>& _row, Clipboard& _clipboard,
			const EditContext& _context)
		{
			EditResult result;
			const auto& op = _command.find("op")->asString();
			const Args args(_command, result.errors);
			const In in{args, result.errors, _clipboard, result.note};

			const auto edit = kindEdit(static_cast<DocKind>(_row.kind));
			if(!edit)
			{
				result.errors.push_back("unknown command " + op);
				return result;
			}
			const auto e = (*edit)(_docs, op, in, _context);
			if(!e || !result.errors.empty())
			{
				result.note.clear();
				return result;
			}
			auto problems = problemsOf(e->after);
			if(!problems.empty())
			{
				result.errors = std::move(problems);
				result.note.clear();
				return result;
			}
			if(!(e->before == e->after))
				result.changes.push_back({e->before, e->after});
			return result;
		}
	}

	EditResult apply(const Documents& _docs, const Value& _command, const Clipboard& _clipboard, const EditContext& _context)
	{
		EditResult result;
		const auto* opValue = _command.isObject() ? _command.find("op") : nullptr;
		if(!opValue || !opValue->isString())
		{
			result.errors.push_back("command: missing op");
			return result;
		}
		const auto& op = opValue->asString();
		const auto* row = commandTable().find(op);
		if(!row || row->owner != deskCore::Owner::Core || row->core != deskCore::CoreOp::Edit || row->kind < 0)
		{
			result.errors.push_back("unknown command " + op);
			return result;
		}
		// The table's own check (the router runs it too): the edits then trust its types and ranges.
		if(auto errors = CommandTable::check(*row, _command); !errors.empty())
		{
			result.errors = std::move(errors);
			return result;
		}
		// Values in, values out: the clipboard the command leaves is returned, not changed in place.
		auto clipboard = _clipboard;
		result = row->group == g_library ? applyLibrary(_docs, _command, *row, clipboard, _context)
			: applyEdit(_docs, _command, *row, clipboard, _context);
		if(result.errors.empty() && !(clipboard == _clipboard))
			result.clipboard = std::move(clipboard);
		return result;
	}

	const std::array<uint8_t, 24>& neutralTrackValues() { return g_neutral; }
	uint8_t neutralTrackLevel() { return g_neutralLevel; }
}
