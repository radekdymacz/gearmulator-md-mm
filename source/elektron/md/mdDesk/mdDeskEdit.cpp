#include "mdDeskEdit.h"

#include "elektronData/mdJson.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mdValidate.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

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
		};
		std::visit(Visitor{*this}, _doc);
	}

	bool accentOn(const ed::MdPattern& _p, const size_t _track, const size_t _step)
	{
		const auto bits = _p.accentEditAll ? _p.accentPattern : _p.trackAccent[_track & 15];
		return (bits >> _step) & 1;
	}

	bool slideOn(const ed::MdPattern& _p, const size_t _track, const size_t _step)
	{
		const auto bits = _p.slideEditAll ? _p.slidePattern : _p.trackSlide[_track & 15];
		return (bits >> _step) & 1;
	}

	namespace
	{
		constexpr std::array<const char*, 4> g_speeds{"1X", "2X", "3/4X", "3/2X"};
		constexpr std::array<const char*, 4> g_masterFx{"gateBox", "rhythmEcho", "eq", "dynamix"};
		constexpr std::array<const char*, 7> g_outputs{"A", "B", "C", "D", "E", "F", "MAIN"};
		constexpr std::array<const char*, 5> g_lfoFields{"track", "param", "shape1", "shape2", "update"};
		constexpr std::array<int, 5> g_lfoMax{15, 23, 5, 5, 2};

		// Neutral track values for "clear sound": the machine's own defaults live in
		// the firmware and cannot be read without saving the kit.
		constexpr std::array<uint8_t, 24> g_neutral{
			64, 64, 64, 64, 64, 64, 64, 64,			// synthesis
			0, 0, 64, 64, 0, 127, 0, 0,				// AMD AMF EQF EQG FLTF FLTW FLTQ SRR
			0, 100, 64, 0, 0, 64, 0, 0};			// DIST VOL PAN DEL REV LFOS LFOD LFOM
		constexpr uint8_t g_neutralLevel = 100;

		std::string trackName(const size_t _t) { return "track " + std::to_string(_t + 1); }

		// Reads command arguments; every problem becomes an error line.
		class Args
		{
		public:
			Args(const Value& _cmd, std::vector<std::string>& _errors) : m_cmd(_cmd), m_errors(_errors) {}

			std::optional<int> integer(const char* _key, const int _min, const int _max) const
			{
				const auto* v = m_cmd.find(_key);
				if(!v || !v->isNumber())
				{
					m_errors.push_back(std::string(_key) + ": missing number");
					return {};
				}
				const double d = v->asNumber();
				if(d != std::floor(d) || d < _min || d > _max)
				{
					m_errors.push_back(std::string(_key) + ": " + std::to_string(static_cast<long long>(d))
						+ " is outside " + std::to_string(_min) + ".." + std::to_string(_max));
					return {};
				}
				return static_cast<int>(d);
			}

			std::optional<double> number(const char* _key, const double _min, const double _max) const
			{
				const auto* v = m_cmd.find(_key);
				if(!v || !v->isNumber() || v->asNumber() < _min || v->asNumber() > _max)
				{
					m_errors.push_back(std::string(_key) + ": expected a number in " + std::to_string(_min) + ".."
						+ std::to_string(_max));
					return {};
				}
				return v->asNumber();
			}

			std::optional<std::string> text(const char* _key) const
			{
				const auto* v = m_cmd.find(_key);
				if(!v || !v->isString())
				{
					m_errors.push_back(std::string(_key) + ": missing text");
					return {};
				}
				return v->asString();
			}

			bool has(const char* _key) const { return m_cmd.find(_key) != nullptr; }
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
			const Value* value(const char* _key) const { return m_cmd.find(_key); }
			bool ok() const { return m_errors.empty(); }

		private:
			const Value& m_cmd;
			std::vector<std::string>& m_errors;
		};

		template<typename Names>
		std::optional<size_t> indexOf(const Names& _names, const std::string& _name)
		{
			for(size_t i = 0; i < _names.size(); ++i)
				if(_name == _names[i])
					return i;
			return {};
		}

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

		struct PatternEdit
		{
			ed::MdPattern value;
			std::string note;
		};

		std::optional<PatternEdit> editPattern(ed::MdPattern _p, const std::string& _op, const Args& _a,
			std::vector<std::string>& _errors, Clipboard& _clip)
		{
			const auto visible = static_cast<int>(ed::visibleSteps(_p));
			const auto track = [&] { return _a.integer("t", 0, 15); };
			const auto step = [&] { return _a.integer("s", 0, visible - 1); };

			if(_op == "trig")
			{
				const auto t = track(), s = step();
				if(!_a.ok())
					return {};
				const bool on = _a.flag("on").value_or(!ed::hasTrig(_p, size_t(*t), size_t(*s)));
				return PatternEdit{on ? ed::withTrig(_p, size_t(*t), size_t(*s), true) : clearStep(_p, size_t(*t),
					size_t(*s)), {}};
			}
			if(_op == "accent" || _op == "slide")
			{
				const auto t = track(), s = step();
				if(!_a.ok())
					return {};
				if(!ed::hasTrig(_p, size_t(*t), size_t(*s)))
				{
					_errors.push_back("Step " + std::to_string(*s + 1) + " of " + trackName(size_t(*t))
						+ " has no trig");
					return {};
				}
				const bool all = _op == "accent" ? _p.accentEditAll : _p.slideEditAll;
				auto& bits = _op == "accent" ? (all ? _p.accentPattern : _p.trackAccent[size_t(*t)])
					: (all ? _p.slidePattern : _p.trackSlide[size_t(*t)]);
				toggleBit(bits, size_t(*s));
				return PatternEdit{_p, all ? "Pattern-wide " + _op + " (EDIT ALL is on)" : std::string()};
			}
			if(_op == "lock")
			{
				const auto t = track(), i = _a.integer("i", 0, 23), s = step();
				if(!_a.ok())
					return {};
				if(_a.isNull("v"))
					return PatternEdit{ed::withoutLock(_p, size_t(*t), size_t(*i), size_t(*s)), {}};
				const auto v = _a.integer("v", 0, 127);
				if(!v)
					return {};
				if(!ed::hasTrig(_p, size_t(*t), size_t(*s)))
				{
					_errors.push_back("A step without a trig cannot hold a lock");
					return {};
				}
				const auto locked = ed::withLock(_p, size_t(*t), size_t(*i), size_t(*s), uint8_t(*v));
				if(!locked)
				{
					_errors.push_back("All 64 locked parameters are in use. Clear one before you lock a new parameter.");
					return {};
				}
				return PatternEdit{*locked, {}};
			}
			if(_op == "clearLane")
			{
				const auto t = track(), i = _a.integer("i", 0, 23);
				if(!_a.ok())
					return {};
				return PatternEdit{ed::withoutLockRow(_p, size_t(*t), size_t(*i)), {}};
			}
			if(_op == "length")
			{
				const auto v = _a.integer("v", 1, visible);
				if(!v)
					return {};
				_p.length = uint8_t(*v);
				return PatternEdit{_p, {}};
			}
			if(_op == "totalLength")
			{
				const auto v = _a.integer("v", 16, _p.extended ? 64 : 32);
				if(!v)
					return {};
				if(*v % 16)
				{
					_errors.push_back("Total length is 16, 32, 48 or 64");
					return {};
				}
				_p.scale = uint8_t(*v / 16 - 1);
				_p.length = uint8_t(*v);
				return PatternEdit{_p, {}};
			}
			if(_op == "speed")
			{
				const auto v = _a.text("v");
				const auto index = v ? indexOf(g_speeds, *v) : std::nullopt;
				if(!index)
				{
					_errors.push_back("Speed is 1X, 2X, 3/4X or 3/2X");
					return {};
				}
				_p.tempoMultiplier = uint8_t(*index);
				return PatternEdit{_p, {}};
			}
			if(_op == "swing")
			{
				const auto v = _a.integer("v", 50, 80);
				if(!v)
					return {};
				_p.swingAmount = ed::swingAmountFromPercent(*v);
				return PatternEdit{_p, {}};
			}
			if(_op == "accentAmount")
			{
				const auto v = _a.integer("v", 0, 15);
				if(!v)
					return {};
				_p.accentAmount = ed::accentAmountFromDisplay(*v);
				return PatternEdit{_p, {}};
			}
			if(_op == "patternKit")
			{
				const auto v = _a.integer("v", 0, 63);
				if(!v)
					return {};
				_p.kit = uint8_t(*v);
				return PatternEdit{_p, {}};
			}
			if(_op == "clearSteps" || _op == "copySteps")
			{
				const auto t = track(), from = _a.integer("from", 0, visible - 1), to = _a.integer("to", 1, visible);
				if(!_a.ok())
					return {};
				if(*to <= *from)
				{
					_errors.push_back("Empty step range");
					return {};
				}
				const auto where = trackName(size_t(*t)) + ", steps " + std::to_string(*from + 1) + "-"
					+ std::to_string(*to);
				if(_op == "clearSteps")
				{
					for(int s = *from; s < *to; ++s)
						_p = clearStep(_p, size_t(*t), size_t(s));
					return PatternEdit{_p, "Cleared " + where};
				}
				Clipboard::Steps c;
				c.length = size_t(*to - *from);
				for(int s = *from; s < *to; ++s)
				{
					const auto bit = uint64_t{1} << (s - *from);
					if(ed::hasTrig(_p, size_t(*t), size_t(s)))
						c.trigs |= bit;
					if(accentOn(_p, size_t(*t), size_t(s)))
						c.accent |= bit;
					if(slideOn(_p, size_t(*t), size_t(s)))
						c.slide |= bit;
					for(uint8_t param = 0; param < ed::MdKit::g_paramsPerTrack; ++param)
						if(const auto v = ed::lockValue(_p, size_t(*t), param, size_t(s)))
							c.locks[param][uint8_t(s - *from)] = *v;
				}
				_clip.steps = std::move(c);
				return PatternEdit{_p, "Copied " + where};
			}
			if(_op == "pasteSteps")
			{
				const auto t = track(), from = _a.integer("from", 0, visible - 1);
				if(!_a.ok())
					return {};
				if(!_clip.steps)
				{
					_errors.push_back("Copy a track page first");
					return {};
				}
				const auto& c = *_clip.steps;
				const auto end = std::min<size_t>(size_t(*from) + c.length, size_t(visible));
				for(size_t s = size_t(*from); s < end; ++s)
					_p = clearStep(_p, size_t(*t), s);
				size_t skipped = 0;
				for(size_t s = size_t(*from); s < end; ++s)
				{
					const auto bit = uint64_t{1} << (s - size_t(*from));
					if(!(c.trigs & bit))
						continue;
					_p = ed::withTrig(_p, size_t(*t), s, true);
					if(c.accent & bit && !accentOn(_p, size_t(*t), s))
						toggleBit(_p.accentEditAll ? _p.accentPattern : _p.trackAccent[size_t(*t)], s);
					if(c.slide & bit && !slideOn(_p, size_t(*t), s))
						toggleBit(_p.slideEditAll ? _p.slidePattern : _p.trackSlide[size_t(*t)], s);
				}
				for(const auto& [param, steps] : c.locks)
				{
					for(const auto& [rel, v] : steps)
					{
						const auto s = size_t(*from) + rel;
						if(s >= end)
							continue;
						if(const auto locked = ed::withLock(_p, size_t(*t), param, s, v))
							_p = *locked;
						else
							++skipped;
					}
				}
				std::string note = "Pasted into " + trackName(size_t(*t));
				if(skipped)
					note += ". " + std::to_string(skipped) + " lock(s) skipped: all 64 locked parameters are in use";
				return PatternEdit{_p, note};
			}
			_errors.push_back("unknown pattern command " + _op);
			return {};
		}

		// ---- kit ----

		std::optional<ed::MdKit> editKit(ed::MdKit _k, const std::string& _op, const Args& _a,
			std::vector<std::string>& _errors, Clipboard& _clip, std::string& _note)
		{
			const auto track = [&] { return _a.integer("t", 0, 15); };
			if(_op == "param")
			{
				const auto t = track(), i = _a.integer("i", 0, 23), v = _a.integer("v", 0, 127);
				if(!_a.ok())
					return {};
				_k.params[size_t(*t)][size_t(*i)] = uint8_t(*v);
				return _k;
			}
			if(_op == "level")
			{
				const auto t = track(), v = _a.integer("v", 0, 127);
				if(!_a.ok())
					return {};
				_k.levels[size_t(*t)] = uint8_t(*v);
				return _k;
			}
			if(_op == "machine")
			{
				const auto t = track(), model = _a.integer("model", 0, 255);
				if(!_a.ok())
					return {};
				if(!ed::isKnownMdMachine(uint32_t(*model)))
				{
					_errors.push_back("model " + std::to_string(*model) + " is not an OS 1.63 machine");
					return {};
				}
				_k.models[size_t(*t)] = uint32_t(*model);
				// The synthesis values stay as they are and are sent to the new
				// machine; without "keep", effects and routing go neutral.
				if(!_a.flag("keepFx").value_or(true))
					for(size_t i = 8; i < 24; ++i)
						_k.params[size_t(*t)][i] = g_neutral[i];
				return _k;
			}
			if(_op == "lfo")
			{
				const auto t = track();
				const auto field = _a.text("field");
				const auto index = field ? indexOf(g_lfoFields, *field) : std::nullopt;
				if(!index)
				{
					_errors.push_back("field: expected track, param, shape1, shape2 or update");
					return {};
				}
				const auto v = _a.integer("v", 0, g_lfoMax[*index]);
				if(!_a.ok())
					return {};
				auto& lfo = _k.lfos[size_t(*t)];
				uint8_t* fields[] = {&lfo.track, &lfo.param, &lfo.shape1, &lfo.shape2, &lfo.update};
				*fields[*index] = uint8_t(*v);
				return _k;
			}
			if(_op == "group")
			{
				const auto t = track();
				const auto kind = _a.text("kind");
				if(!_a.ok())
					return {};
				if(*kind != "trig" && *kind != "mute")
				{
					_errors.push_back("kind: expected trig or mute");
					return {};
				}
				uint8_t target = ed::MdKit::g_noGroup;
				if(!_a.isNull("target"))
				{
					const auto v = _a.integer("target", 0, 15);
					if(!v)
						return {};
					if(*v == *t)
					{
						_errors.push_back("A track cannot be in a group with itself");
						return {};
					}
					target = uint8_t(*v);
				}
				(*kind == "trig" ? _k.trigGroups : _k.muteGroups)[size_t(*t)] = target;
				return _k;
			}
			if(_op == "masterFx")
			{
				const auto fx = _a.text("fx");
				const auto index = fx ? indexOf(g_masterFx, *fx) : std::nullopt;
				const auto i = _a.integer("i", 0, 7), v = _a.integer("v", 0, 127);
				if(!index)
					_errors.push_back("fx: expected gateBox, rhythmEcho, eq or dynamix");
				if(!_a.ok() || !index)
					return {};
				_k.masterFx[*index][size_t(*i)] = uint8_t(*v);
				return _k;
			}
			if(_op == "kitName")
			{
				const auto name = _a.text("name");
				if(!name)
					return {};
				if(name->size() > ed::MdKit::g_nameSize)
				{
					_errors.push_back("A kit name has at most 16 characters");
					return {};
				}
				_k.name.fill(0);
				std::copy(name->begin(), name->end(), _k.name.begin());
				return _k;
			}
			if(_op == "copySound")
			{
				const auto t = track();
				if(!t)
					return {};
				Clipboard::Sound s;
				s.model = _k.models[size_t(*t)];
				s.params = _k.params[size_t(*t)];
				s.level = _k.levels[size_t(*t)];
				s.lfo = _k.lfos[size_t(*t)];
				_clip.sound = s;
				_note = "Copied the sound of " + trackName(size_t(*t)) + " (" + ed::mdMachineName(s.model) + ")";
				return _k;
			}
			if(_op == "pasteSound")
			{
				const auto t = track();
				if(!t)
					return {};
				if(!_clip.sound)
				{
					_errors.push_back("Copy a track's sound first");
					return {};
				}
				const auto& s = *_clip.sound;
				_k.models[size_t(*t)] = s.model;
				_k.params[size_t(*t)] = s.params;
				_k.levels[size_t(*t)] = s.level;
				_k.lfos[size_t(*t)] = s.lfo;
				_k.lfos[size_t(*t)].track = uint8_t(*t);
				_note = "Pasted " + ed::mdMachineName(s.model) + " onto " + trackName(size_t(*t));
				return _k;
			}
			if(_op == "clearSound")
			{
				const auto t = track();
				if(!t)
					return {};
				std::copy(g_neutral.begin(), g_neutral.end(), _k.params[size_t(*t)].begin());
				_k.levels[size_t(*t)] = g_neutralLevel;
				_note = trackName(size_t(*t)) + " reset to neutral values";
				return _k;
			}
			_errors.push_back("unknown kit command " + _op);
			return {};
		}

		// ---- song (edited as contract rows, so row semantics stay in mdJson) ----

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
		std::vector<Value> remapTargets(std::vector<Value> _rows, const std::vector<size_t>& _map)
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

		std::optional<ed::MdSong> editSong(const ed::MdSong& _song, const std::string& _op, const Args& _a,
			std::vector<std::string>& _errors, Clipboard& _clip, std::string& _note)
		{
			const auto doc = ed::songToJson(_song);
			const auto* rowsValue = doc.find("rows");
			std::vector<Value> rows = rowsValue ? rowsValue->asArray() : std::vector<Value>{};
			const auto count = static_cast<int>(rows.size());
			const auto rowArg = [&](const char* _key) { return _a.integer(_key, 0, count - 1); };

			if(_op == "copyRow")
			{
				const auto i = rowArg("i");
				if(!i)
					return {};
				if(isEnd(rows[size_t(*i)]))
				{
					_errors.push_back("END cannot be copied");
					return {};
				}
				_clip.songRow = rows[size_t(*i)];
				_note = "Copied row " + std::to_string(*i + 1);
				return _song;
			}

			std::vector<size_t> map(rows.size());
			for(size_t j = 0; j < map.size(); ++j)
				map[j] = j;

			if(_op == "rowSet")
			{
				const auto i = rowArg("i");
				const auto* row = _a.value("row");
				if(!i || !row || !row->isObject())
				{
					if(!row || !row->isObject())
						_errors.push_back("row: missing object");
					return {};
				}
				rows[size_t(*i)] = *row;
			}
			else if(_op == "rowInsert" || _op == "pasteRow")
			{
				const auto i = _a.integer("i", 0, count - 1);
				if(!i)
					return {};
				Value row;
				if(_op == "pasteRow")
				{
					if(!_clip.songRow)
					{
						_errors.push_back("Copy a song row first");
						return {};
					}
					row = *_clip.songRow;
				}
				else
				{
					const auto* r = _a.value("row");
					if(!r || !r->isObject())
					{
						_errors.push_back("row: missing object");
						return {};
					}
					row = *r;
				}
				if(rows.size() >= ed::MdSong::g_maxRows)
				{
					_errors.push_back("A song holds 256 rows");
					return {};
				}
				// Insert at i; the END row can only move down.
				const auto at = size_t(*i);
				for(size_t j = 0; j < map.size(); ++j)
					map[j] = j < at ? j : j + 1;
				rows = remapTargets(std::move(rows), map);
				rows.insert(rows.begin() + static_cast<std::ptrdiff_t>(at), row);
			}
			else if(_op == "rowDelete")
			{
				const auto i = rowArg("i");
				if(!i)
					return {};
				if(isEnd(rows[size_t(*i)]))
				{
					_errors.push_back("END cannot be deleted");
					return {};
				}
				const auto at = size_t(*i);
				for(size_t j = 0; j < map.size(); ++j)
					map[j] = j <= at ? (j == at ? at : j) : j - 1;
				rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(at));
				std::vector<size_t> shrunk(map.begin(), map.end());
				rows = remapTargets(std::move(rows), shrunk);
			}
			else if(_op == "rowMove")
			{
				const auto from = rowArg("from"), to = rowArg("to");
				if(!from || !to)
					return {};
				if(isEnd(rows[size_t(*from)]) || isEnd(rows[size_t(*to)]))
				{
					_errors.push_back("END stays the last row");
					return {};
				}
				std::vector<size_t> order(rows.size());
				for(size_t j = 0; j < order.size(); ++j)
					order[j] = j;
				const auto moved = order[size_t(*from)];
				order.erase(order.begin() + *from);
				order.insert(order.begin() + *to, moved);
				for(size_t j = 0; j < order.size(); ++j)
					map[order[j]] = j;
				rows = remapTargets(std::move(rows), map);
				std::vector<Value> reordered;
				for(const auto j : order)
					reordered.push_back(rows[j]);
				rows = std::move(reordered);
			}
			else
			{
				_errors.push_back("unknown song command " + _op);
				return {};
			}

			const auto edited = withMember(doc, "rows", Value(Value::Array(rows.begin(), rows.end())));
			std::vector<std::string> problems;
			auto song = ed::songFromJson(edited, problems);
			if(!song)
			{
				_errors.insert(_errors.end(), problems.begin(), problems.end());
				return {};
			}
			return song;
		}

		// ---- global ----

		std::optional<ed::MdGlobal> editGlobal(ed::MdGlobal _g, const std::string& _op, const Args& _a,
			std::vector<std::string>& _errors)
		{
			if(_op == "route")
			{
				const auto t = _a.integer("t", 0, 15);
				const auto out = _a.text("out");
				const auto index = out ? indexOf(g_outputs, *out) : std::nullopt;
				if(!index)
					_errors.push_back("out: expected A-F or MAIN");
				if(!t || !index)
					return {};
				_g.routing[size_t(*t)] = uint8_t(*index);
				return _g;
			}
			if(_op == "tempo")
			{
				const auto bpm = _a.number("bpm", 30, 300);
				if(!bpm)
					return {};
				_g.tempo = static_cast<uint16_t>(std::lround(*bpm * 24));
				return _g;
			}
			if(_op == "extended")
			{
				const auto on = _a.flag("on");
				if(!on)
				{
					_errors.push_back("on: expected true or false");
					return {};
				}
				_g.extendedMode = *on ? 1 : 0;
				return _g;
			}
			_errors.push_back("unknown global command " + _op);
			return {};
		}

		bool isOneOf(const std::string& _op, std::initializer_list<const char*> _ops)
		{
			for(const auto* o : _ops)
				if(_op == o)
					return true;
			return false;
		}

		std::vector<std::string> problemsOf(const Document& _doc)
		{
			return std::visit([](const auto& _v) { return ed::validate(_v); }, _doc);
		}
	}

	EditResult apply(const Documents& _docs, const Value& _command, Clipboard& _clipboard)
	{
		EditResult result;
		const auto* opValue = _command.find("op");
		if(!_command.isObject() || !opValue || !opValue->isString())
		{
			result.errors.push_back("command: missing op");
			return result;
		}
		const auto& op = opValue->asString();
		const Args args(_command, result.errors);

		std::optional<Document> before;
		std::optional<Document> after;

		if(isOneOf(op, {"trig", "accent", "slide", "lock", "clearLane", "length", "totalLength", "speed", "swing",
			"accentAmount", "patternKit", "clearSteps", "copySteps", "pasteSteps"}))
		{
			const auto p = args.integer("p", 0, 127);
			if(!p)
				return result;
			const auto it = _docs.patterns.find(uint8_t(*p));
			if(it == _docs.patterns.end())
			{
				result.errors.push_back("pattern " + ed::mdPatternName(unsigned(*p)) + " is not loaded yet");
				return result;
			}
			auto edit = editPattern(it->second, op, args, result.errors, _clipboard);
			if(!edit)
				return result;
			result.note = edit->note;
			before = it->second;
			after = edit->value;
		}
		else if(isOneOf(op, {"param", "level", "machine", "lfo", "group", "masterFx", "kitName", "copySound",
			"pasteSound", "clearSound"}))
		{
			const auto k = args.integer("k", 0, 63);
			if(!k)
				return result;
			const auto it = _docs.kits.find(uint8_t(*k));
			if(it == _docs.kits.end())
			{
				result.errors.push_back("kit " + std::to_string(*k + 1) + " is not loaded yet");
				return result;
			}
			auto edit = editKit(it->second, op, args, result.errors, _clipboard, result.note);
			if(!edit)
				return result;
			before = it->second;
			after = *edit;
		}
		else if(isOneOf(op, {"rowSet", "rowInsert", "rowDelete", "rowMove", "copyRow", "pasteRow"}))
		{
			const auto s = args.integer("s", 0, 31);
			if(!s)
				return result;
			const auto it = _docs.songs.find(uint8_t(*s));
			if(it == _docs.songs.end())
			{
				result.errors.push_back("song " + std::to_string(*s + 1) + " is not loaded yet");
				return result;
			}
			auto edit = editSong(it->second, op, args, result.errors, _clipboard, result.note);
			if(!edit)
				return result;
			before = it->second;
			after = *edit;
		}
		else if(isOneOf(op, {"route", "tempo", "extended"}))
		{
			if(!_docs.global)
			{
				result.errors.push_back("the global settings are not loaded yet");
				return result;
			}
			auto edit = editGlobal(*_docs.global, op, args, result.errors);
			if(!edit)
				return result;
			before = *_docs.global;
			after = *edit;
		}
		else
		{
			result.errors.push_back("unknown command " + op);
			return result;
		}

		if(!result.errors.empty())
			return result;
		auto problems = problemsOf(*after);
		if(!problems.empty())
		{
			result.errors = std::move(problems);
			result.note.clear();
			return result;
		}
		const bool same = std::visit([&](const auto& _a)
		{
			using T = std::decay_t<decltype(_a)>;
			return _a == std::get<T>(*before);
		}, *after);
		if(!same)
			result.changes.push_back({*before, *after});
		return result;
	}

	const std::array<uint8_t, 24>& neutralTrackValues() { return g_neutral; }
	uint8_t neutralTrackLevel() { return g_neutralLevel; }
}
