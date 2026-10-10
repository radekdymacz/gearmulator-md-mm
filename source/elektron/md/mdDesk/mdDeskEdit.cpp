#include "mdDeskEdit.h"

#include "elektronData/factoryGlobals.h"
#include "mdDeskLibrary.h"
#include "mdDeskModel.h"

#include "deskCore/deskBlocks.h"
#include "deskCore/deskEdits.h"

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
		constexpr Mark g_swing{"swing", &ed::MdPattern::swingEditAll, &ed::MdPattern::swingPattern, &ed::MdPattern::trackSwing};

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

			const Value& command() const { return m_cmd; }

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

		// Every lock of track t (all its parameters), in one edit.
		ed::MdPattern withoutTrackLocks(ed::MdPattern _p, const size_t _t)
		{
			for(size_t param = 0; param < ed::MdKit::g_paramsPerTrack; ++param)
				_p = ed::withoutLockRow(_p, _t, param);
			return _p;
		}

		std::optional<ed::MdPattern> clearLocks(ed::MdPattern _p, const In& _in)
		{
			const auto t = size_t(*_in.a.integer("t"));
			_in.note = "Cleared every lock of " + trackName(t);
			return withoutTrackLocks(std::move(_p), t);
		}

		// The whole pattern's steps: every track's trigs, accents, slides, swings and locks, and the
		// pattern-wide marks. Length, speed, swing and accent amounts and the kit stay.
		std::optional<ed::MdPattern> clearPattern(ed::MdPattern _p, const In& _in)
		{
			for(size_t t = 0; t < ed::MdPattern::g_tracks; ++t)
			{
				_p = withoutTrackLocks(std::move(_p), t);
				_p.trigs[t] = 0;
				_p.trackAccent[t] = _p.trackSlide[t] = _p.trackSwing[t] = 0;
			}
			_p.accentPattern = _p.slidePattern = _p.swingPattern = 0;
			_in.note = "Cleared the pattern: every track's trigs and locks";
			return _p;
		}

		// An integer member of a row the table cannot see into (an array's elements): _min.._max or nothing.
		std::optional<int> intIn(const Value* _v, const int _min, const int _max)
		{
			if(!_v || !_v->isNumber())
				return {};
			const double d = _v->asNumber();
			if(d != std::floor(d) || d < _min || d > _max)
				return {};
			return static_cast<int>(d);
		}

		// A row's step list as bits, every step in [_from, _to); nothing (and an error line) otherwise.
		std::optional<uint64_t> stepBits(const Value* _list, const std::string& _where, const size_t _from, const size_t _to,
			std::vector<std::string>& _errors)
		{
			if(!_list || !_list->isArray())
			{
				_errors.push_back(_where + ": expected a list of steps");
				return {};
			}
			uint64_t bits = 0;
			for(const auto& s : _list->asArray())
			{
				const auto step = intIn(&s, static_cast<int>(_from), static_cast<int>(_to) - 1);
				if(!step)
				{
					_errors.push_back(_where + ": a step outside " + std::to_string(_from) + ".." + std::to_string(_to - 1));
					return {};
				}
				bits |= uint64_t{1} << *step;
			}
			return bits;
		}

		// The generators' edit (DESIGN-generators.md §4.3): each row's track has exactly its steps on in
		// [from, to) (default: the visible steps). A step turned off loses its locks and marks, as a trig
		// turned off does (clearStep); a step kept on keeps them; slides stay. acc: the track's accents in
		// the range become exactly those (only on its steps); with EDIT ALL on, accent is pattern-wide, so
		// acc is left out and the note says so. One pattern change for every row: one dump, one undo step.
		std::optional<ed::MdPattern> steps(ed::MdPattern _p, const In& _in)
		{
			const int visible = visibleOf(_p);
			const auto from = _in.a.value("from") ? _in.a.within("from", 0, visible - 1) : std::optional<int>(0);
			const auto to = _in.a.value("to") ? _in.a.within("to", 1, visible) : std::optional<int>(visible);
			if(!from || !to)
				return {};
			if(*to <= *from)
			{
				_in.errors.push_back("Empty step range");
				return {};
			}
			const auto& rows = _in.a.value("rows")->asArray();
			if(rows.empty() || rows.size() > ed::MdPattern::g_tracks)
			{
				_in.errors.push_back("rows: 1 to 16 rows");
				return {};
			}
			uint64_t range = 0;
			for(auto s = size_t(*from); s < size_t(*to); ++s)
				range |= uint64_t{1} << s;
			struct Row { size_t t; uint64_t on; std::optional<uint64_t> acc; };
			std::vector<Row> parsed;
			uint32_t seen = 0;
			for(size_t j = 0; j < rows.size(); ++j)
			{
				const auto where = "rows[" + std::to_string(j) + "]";
				const auto& r = rows[j];
				const auto t = r.isObject() ? intIn(r.find("t"), 0, 15) : std::nullopt;
				if(!t)
				{
					_in.errors.push_back(where + ": expected {t: 0-15, on: [...]}");
					return {};
				}
				if(seen >> *t & 1)
				{
					_in.errors.push_back(where + ": " + trackName(size_t(*t)) + " is in two rows");
					return {};
				}
				seen |= 1u << *t;
				const auto on = stepBits(r.find("on"), where + ".on", size_t(*from), size_t(*to), _in.errors);
				if(!on)
					return {};
				std::optional<uint64_t> acc;
				if(const auto* a = r.find("acc"); a && !a->isNull())
				{
					acc = stepBits(a, where + ".acc", size_t(*from), size_t(*to), _in.errors);
					if(!acc)
						return {};
					if(*acc & ~*on)
					{
						_in.errors.push_back(where + ".acc: an accent on a step without a trig");
						return {};
					}
				}
				parsed.push_back({size_t(*t), *on, acc});
			}
			bool accentsLeft = false;
			for(const auto& r : parsed)
			{
				for(auto s = size_t(*from); s < size_t(*to); ++s)
				{
					const bool want = r.on >> s & 1, have = ed::hasTrig(_p, r.t, s);
					if(have && !want)
						_p = clearStep(_p, r.t, s);
					else if(!have && want)
						_p = ed::withTrig(_p, r.t, s, true);
				}
				if(!r.acc)
					continue;
				if(_p.accentEditAll)
					accentsLeft = true;
				else
					_p.trackAccent[r.t] = (_p.trackAccent[r.t] & ~range) | *r.acc;
			}
			_in.note = parsed.size() == 1 ? "Set the steps of " + trackName(parsed[0].t) : "Set the steps of " + std::to_string(parsed.size()) + " tracks";
			if(accentsLeft)
				_in.note += ". Accents left as they are: EDIT ALL is on (accent is pattern-wide)";
			return _p;
		}

		// DESIGN-generators.md §7.2: track t's steps move _by steps later (earlier when negative), wrapping at
		// the pattern's length: its trigs, its accents, slides and swings and every lock of the track go with
		// them. Steps from the length on stay. The pattern-wide marks (EDIT ALL) belong to every track and stay.
		std::optional<ed::MdPattern> rotate(ed::MdPattern _p, const In& _in)
		{
			const auto t = size_t(*_in.a.integer("t"));
			const int by = *_in.a.integer("by");
			const size_t len = std::min<size_t>(_p.length, ed::visibleSteps(_p));
			if(len < 2 || by % int(len) == 0)
				return _p;
			_p.trigs[t] = deskCore::rotatedBits(_p.trigs[t], by, len);
			_p.trackAccent[t] = deskCore::rotatedBits(_p.trackAccent[t], by, len);
			_p.trackSlide[t] = deskCore::rotatedBits(_p.trackSlide[t], by, len);
			_p.trackSwing[t] = deskCore::rotatedBits(_p.trackSwing[t], by, len);
			for(size_t param = 0; param < ed::MdKit::g_paramsPerTrack; ++param)
			{
				const auto row = ed::lockRowIndex(_p, t, param);
				if(!row)
					continue;
				deskCore::rotateRow(_p.lockRows[*row], by, len);
			}
			_in.note = "Rotated " + trackName(t) + (by > 0 ? " later" : " earlier");
			const bool shared = (_p.accentEditAll && _p.accentPattern) || (_p.slideEditAll && _p.slidePattern) || (_p.swingEditAll && _p.swingPattern);
			if(shared)
				_in.note += ". Pattern-wide accents, slides and swings stay (EDIT ALL)";
			return _p;
		}

		// DESIGN-generators.md §7.6: the pattern twice as long, its steps copied into the new half: every track's
		// trigs, marks and locks, and the pattern-wide marks. The total length grows to hold it; above 32 steps
		// only an EXTENDED pattern can. Hidden steps that the longer pattern would show are cleared first.
		std::optional<ed::MdPattern> doublePattern(ed::MdPattern _p, const In& _in)
		{
			const size_t len = _p.length, was = ed::visibleSteps(_p), most = _p.extended ? 64 : 32;
			if(len * 2 > most)
			{
				_in.errors.push_back(_p.extended ? "A pattern of " + std::to_string(len) + " steps cannot double: 64 steps is the longest"
					: "Doubling above 32 steps needs an EXTENDED pattern");
				return {};
			}
			const size_t total = std::max(was, (len * 2 + 15) / 16 * 16);
			_p.scale = uint8_t(total / 16 - 1);
			const size_t clearFrom = std::max(was, len * 2);	// steps the longer pattern shows that held hidden residue
			const auto copy = [&](uint64_t& _bits) { _bits = deskCore::doubledBits(_bits, len, clearFrom, total); };
			for(size_t t = 0; t < ed::MdPattern::g_tracks; ++t)
				for(auto* bits : {&_p.trigs[t], &_p.trackAccent[t], &_p.trackSlide[t], &_p.trackSwing[t]})
					copy(*bits);
			copy(_p.accentPattern);
			copy(_p.slidePattern);
			copy(_p.swingPattern);
			for(size_t t = 0; t < ed::MdPattern::g_tracks; ++t)
			{
				for(size_t param = 0; param < ed::MdKit::g_paramsPerTrack; ++param)
				{
					const auto row = ed::lockRowIndex(_p, t, param);
					if(!row)
						continue;
					auto& r = _p.lockRows[*row];
					deskCore::doubleRow(r, len, ed::MdPattern::g_noLock, clearFrom, total);
					if(std::all_of(r.begin(), r.begin() + std::ptrdiff_t(total), [](const uint8_t _v) { return _v == ed::MdPattern::g_noLock; }))
						_p = ed::withoutLockRow(_p, t, param);
				}
			}
			_p.length = uint8_t(len * 2);
			_in.note = "Doubled the pattern: " + std::to_string(len) + " to " + std::to_string(len * 2) + " steps, the new half a copy";
			return _p;
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

		// A block of steps [from, to) on tracks [track, track + rows) within the visible steps (a selection, steps x
		// tracks; one row without "n"), and its "track n, steps a-b".
		struct StepRange
		{
			size_t track, rows, from, to;
			std::string where;
		};

		std::string tracksName(const size_t _t, const size_t _rows)
		{
			return _rows == 1 ? trackName(_t) : "tracks " + std::to_string(_t + 1) + "-" + std::to_string(_t + _rows);
		}

		std::optional<StepRange> stepRange(const ed::MdPattern& _p, const In& _in)
		{
			const auto t = size_t(*_in.a.integer("t"));
			const auto n = _in.a.value("n") ? _in.a.within("n", 1, int(ed::MdPattern::g_tracks - t)) : std::optional<int>(1);
			const auto from = _in.a.within("from", 0, visibleOf(_p) - 1), to = _in.a.within("to", 1, visibleOf(_p));
			if(!from || !to || !n)
				return {};
			if(*to <= *from)
			{
				_in.errors.push_back("Empty step range");
				return {};
			}
			return StepRange{t, size_t(*n), size_t(*from), size_t(*to),
				tracksName(t, size_t(*n)) + ", steps " + std::to_string(*from + 1) + "-" + std::to_string(*to)};
		}

		// The block's steps as values: trigs, accents, slides, swings and every lock, per track.
		Clipboard::Steps takeSteps(const ed::MdPattern& _p, const StepRange& _r)
		{
			Clipboard::Steps c;
			c.length = _r.to - _r.from;
			for(auto t = _r.track; t < _r.track + _r.rows; ++t)
			{
				Clipboard::Steps::Row row;
				for(auto s = _r.from; s < _r.to; ++s)
				{
					const auto bit = uint64_t{1} << (s - _r.from);
					if(ed::hasTrig(_p, t, s))
						row.trigs |= bit;
					if(markOn(_p, g_accent, t, s))
						row.accent |= bit;
					if(markOn(_p, g_slide, t, s))
						row.slide |= bit;
					if(markOn(_p, g_swing, t, s))
						row.swing |= bit;
					for(uint8_t param = 0; param < ed::MdKit::g_paramsPerTrack; ++param)
						if(const auto v = ed::lockValue(_p, t, param, s))
							row.locks[param][uint8_t(s - _r.from)] = *v;
				}
				c.rows.push_back(std::move(row));
			}
			return c;
		}

		// What a block put down at a step and a track: where it landed and what did not fit (deskCore::blockLanding),
		// and the locks skipped.
		struct Put
		{
			deskCore::BlockLanding land;
			size_t skippedLocks = 0;
		};

		// Puts a block down with its first step at _at on track _track: each of its rows replaces what the steps
		// under it held. It stops at the pattern's length and at track 16; locks that need a new row while all 64
		// are in use are skipped (counted).
		ed::MdPattern putSteps(ed::MdPattern _p, const Clipboard::Steps& _c, const size_t _track, const size_t _at, Put& _put)
		{
			_put.land = deskCore::blockLanding(_c.rows.size(), _c.length, _track, _at, ed::MdPattern::g_tracks, _p.length);
			const auto end = _at + _put.land.steps;
			for(size_t r = 0; r < _put.land.rows; ++r)
			{
				const auto t = _track + r;
				const auto& row = _c.rows[r];
				for(auto s = _at; s < end; ++s)
					_p = clearStep(_p, t, s);
				for(auto s = _at; s < end; ++s)
				{
					const auto bit = uint64_t{1} << (s - _at);
					if(!(row.trigs & bit))
						continue;
					_p = ed::withTrig(_p, t, s, true);
					for(const auto& [mark, bits] : {std::pair{&g_accent, row.accent}, std::pair{&g_slide, row.slide}, std::pair{&g_swing, row.swing}})
						if(bits & bit && !markOn(_p, *mark, t, s))
							toggleBit(bitsOf(_p, *mark, t), s);
				}
				for(const auto& [param, steps] : row.locks)
				{
					for(const auto& [rel, v] : steps)
					{
						const auto s = _at + rel;
						if(s >= end || !ed::hasTrig(_p, t, s))
							continue;
						if(const auto locked = ed::withLock(_p, t, param, s, v))
							_p = *locked;
						else
							++_put.skippedLocks;
					}
				}
			}
			return _p;
		}

		// "Pasted into ..." and what did not fit, for the result's note.
		std::string putNote(const std::string& _verb, const Put& _put, const size_t _track, const size_t _at, const ed::MdPattern& _p)
		{
			auto note = deskCore::blockNote(_verb, tracksName(_track, _put.land.rows), _at, _put.land, _p.length, trackName(ed::MdPattern::g_tracks - 1));
			if(_put.skippedLocks)
				note += ". " + std::to_string(_put.skippedLocks) + " lock(s) skipped: all 64 locked parameters are in use";
			return note;
		}

		// The first step a block is put down at: within the pattern's length.
		std::optional<size_t> putAt(const ed::MdPattern& _p, const In& _in, const char* _key)
		{
			const auto at = _in.a.within(_key, 0, visibleOf(_p) - 1);
			if(!at)
				return {};
			if(size_t(*at) >= _p.length)
			{
				_in.errors.push_back("Step " + std::to_string(*at + 1) + " is past the pattern's length (" + std::to_string(_p.length) + ")");
				return {};
			}
			return size_t(*at);
		}

		std::optional<ed::MdPattern> clearSteps(ed::MdPattern _p, const In& _in)
		{
			const auto r = stepRange(_p, _in);
			if(!r)
				return {};
			for(auto t = r->track; t < r->track + r->rows; ++t)
				for(auto s = r->from; s < r->to; ++s)
					_p = clearStep(_p, t, s);
			_in.note = "Cleared " + r->where;
			return _p;
		}

		std::optional<ed::MdPattern> copySteps(ed::MdPattern _p, const In& _in)
		{
			const auto r = stepRange(_p, _in);
			if(!r)
				return {};
			_in.clip.steps = takeSteps(_p, *r);
			_in.note = "Copied " + r->where;
			return _p;
		}

		std::optional<ed::MdPattern> pasteSteps(ed::MdPattern _p, const In& _in)
		{
			const auto t = size_t(*_in.a.integer("t"));
			const auto from = putAt(_p, _in, "from");
			if(!from)
				return {};
			if(!_in.clip.steps)
			{
				_in.errors.push_back("Copy some steps first");
				return {};
			}
			Put put;
			_p = putSteps(std::move(_p), *_in.clip.steps, t, *from, put);
			_in.note = putNote("Pasted into", put, t, *from, _p);
			return _p;
		}

		// A block of this pattern copied to another place in it (step "at", track "dt"; its own track without
		// it), the clipboard untouched: duplicate (at = to) and the Alt-drag of a selection.
		std::optional<ed::MdPattern> copyStepsTo(ed::MdPattern _p, const In& _in)
		{
			const auto r = stepRange(_p, _in);
			const auto at = putAt(_p, _in, "at");
			const auto dt = _in.a.value("dt") ? _in.a.integer("dt") : std::optional<int>(r ? int(r->track) : 0);
			if(!r || !at || !dt)
				return {};
			const auto block = takeSteps(_p, *r);
			Put put;
			_p = putSteps(std::move(_p), block, size_t(*dt), *at, put);
			_in.note = putNote("Copied " + r->where + " to", put, size_t(*dt), *at, _p);
			return _p;
		}

		const Edits<ed::MdPattern>& patternEdits()
		{
			static const Edits<ed::MdPattern> edits{
				{"trig", trig}, {"accent", accent}, {"slide", slide}, {"lock", lock}, {"clearLane", clearLane},
				{"length", length}, {"totalLength", totalLength}, {"speed", speed}, {"swing", swing},
				{"accentAmount", accentAmount}, {"patternKit", patternKit}, {"clearSteps", clearSteps},
				{"clearLocks", clearLocks}, {"clearPattern", clearPattern}, {"steps", steps}, {"rotate", rotate}, {"doublePattern", doublePattern}, {"copySteps", copySteps}, {"pasteSteps", pasteSteps}, {"copyStepsTo", copyStepsTo}};
			return edits;
		}

		// ---- the working kit ----

		std::optional<ed::MdKit> param(ed::MdKit _k, const In& _in)
		{
			_k.params[size_t(*_in.a.integer("t"))][size_t(*_in.a.integer("i"))] = uint8_t(*_in.a.integer("v"));
			return _k;
		}

		// Many kit parameters in one working-kit change (DESIGN-generators.md §4.3, sound mutation): every
		// [t, i, v] is checked, then set; kitDelivery sends a CC for each value that changed, nothing else.
		std::optional<ed::MdKit> params(ed::MdKit _k, const In& _in)
		{
			const auto& values = _in.a.value("values")->asArray();
			if(values.size() > ed::MdKit::g_tracks * ed::MdKit::g_paramsPerTrack)
			{
				_in.errors.push_back("values: at most 384 (every parameter of the kit once)");
				return {};
			}
			std::vector<std::array<int, 3>> set;
			for(size_t j = 0; j < values.size(); ++j)
			{
				const auto& e = values[j];
				const auto* a = e.isArray() && e.asArray().size() == 3 ? &e.asArray() : nullptr;
				const auto t = a ? intIn(&(*a)[0], 0, 15) : std::nullopt, i = a ? intIn(&(*a)[1], 0, 23) : std::nullopt,
					v = a ? intIn(&(*a)[2], 0, 127) : std::nullopt;
				if(!t || !i || !v)
				{
					_in.errors.push_back("values[" + std::to_string(j) + "]: expected [t 0-15, i 0-23, v 0-127]");
					return {};
				}
				set.push_back({*t, *i, *v});
			}
			for(const auto& [t, i, v] : set)
				_k.params[size_t(t)][size_t(i)] = uint8_t(v);
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

		// Control All (manual p.37, FUNCTION + a DATA ENTRY knob): the same knob of one page on every track
		// the machine's own gesture reaches, moved by d and held at 0..127 (controlAllReaches). "t", the
		// track the gesture is on, only says which track leads it on the machine (the adapter's).
		std::optional<ed::MdKit> tweak(ed::MdKit _k, const In& _in)
		{
			const auto& g = _in.a.text("group");
			const size_t index = (g == "fx" ? 8u : g == "rt" ? 16u : 0u) + size_t(*_in.a.integer("knob"));
			const int d = *_in.a.integer("d");
			for(size_t t = 0; t < ed::MdKit::g_tracks; ++t)
				if(controlAllReaches(_k.models[t], index))
					_k.params[t][index] = uint8_t(std::clamp(int(_k.params[t][index]) + d, 0, 127));
			return _k;
		}

		const Edits<ed::MdKit>& kitEdits()
		{
			static const Edits<ed::MdKit> edits{
				{"param", param}, {"params", params}, {"level", level}, {"machine", machine}, {"lfo", lfo}, {"group", group},
				{"masterFx", masterFx}, {"kitName", kitName}, {"copySound", copySound}, {"pasteSound", pasteSound},
				{"clearSound", clearSound}, {"tweak", tweak}};
			return edits;
		}

		// ---- song (edited as contract rows, so row semantics stay in mdJson; the row ops are deskCore's, shared
		// with the Monomachine: deskCore/deskEdits.h) ----

		using Rows = deskCore::songRows::Rows;
		using deskCore::songRows::withMember;

		const std::map<std::string, deskCore::songRows::Fn>& songEdits() { return deskCore::songRows::edits(); }

		// A song edit runs on its contract rows; the song comes back through the codec.
		std::optional<ed::MdSong> editSong(const ed::MdSong& _song, const deskCore::songRows::Fn _fn, const In& _in)
		{
			const auto doc = ed::songToJson(_song);
			const auto* rowsValue = doc.find("rows");
			const Rows rows = rowsValue ? rowsValue->asArray() : Rows{};
			const deskCore::songRows::Edit e{_in.a.command(), _in.errors, _in.note, _in.clip.songRow, ed::MdSong::g_maxRows};
			const auto edited = _fn(rows, e);
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

		// B-051, F3: GLOBAL › Reset to defaults: the global the machine ships with, measured (elektronData::mdFactoryGlobal)
		std::optional<ed::MdGlobal> globalReset(ed::MdGlobal _g, const In&)
		{
			return ed::mdFactoryGlobal(_g.position);
		}

		const Edits<ed::MdGlobal>& globalEdits()
		{
			static const Edits<ed::MdGlobal> edits{{"route", route}, {"tempo", tempo}, {"extended", extended}, {"globalSet", globalSet},
				{"globalReset", globalReset}};
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
			const auto fn = songEdits().find(_op);
			if(fn == songEdits().end())
			{
				_in.errors.push_back("no edit for command " + _op);	// a table row without its function
				return {};
			}
			const auto after = editSong(it->second, fn->second, _in);
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

	std::vector<std::string> editOps()
	{
		std::vector<std::string> ops = libraryOps();
		const auto add = [&ops](const auto& _edits)
		{
			for(const auto& [op, fn] : _edits)
				ops.push_back(op);
		};
		add(patternEdits());
		add(kitEdits());
		add(songEdits());
		add(globalEdits());
		return ops;
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

	bool controlAllReaches(const uint32_t _model, const size_t _index)
	{
		if(_index >= 24)
			return false;
		const auto facts = ed::mdMachineFacts(_model);
		if(!facts.audio)
			return false;
		// A RAM recorder's synthesis page is its recording setup: left out; its effects and routing move.
		return !facts.recorder || _index >= 8;
	}

	bool controlAllLeads(const uint32_t _model)
	{
		return controlAllReaches(_model, 0);
	}

	const std::array<uint8_t, 24>& neutralTrackValues() { return g_neutral; }
	uint8_t neutralTrackLevel() { return g_neutralLevel; }
}
