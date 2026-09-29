#include "mmJson.h"

#include "jsonFirmware.h"

#include "mmGlobal.h"
#include "mmKit.h"
#include "mmMachines.h"
#include "mmPattern.h"
#include "mmSong.h"
#include "mmValidate.h"

#include <array>
#include <cmath>
#include <cstdio>

namespace elektronData
{
	using json::Value;

	namespace
	{
		// The codec's own layout; documents leave as g_mmContractVersion (jsonFirmware.h).
		constexpr int g_layoutVersion = 1;
		constexpr std::array<const char*, 4> g_multipliers{"1X", "2X", "3/4X", "3/2X"};
		constexpr std::array<const char*, 3> g_routingModes{"3xSTEREO+AB=MIX", "3xSTEREO", "6xMONO"};
		const Value g_null;

		Value header(const char* _schema, const uint8_t _slot, const uint8_t _version, const uint8_t _revision)
		{
			Value v = Value::object();
			v.set("schema", _schema);
			v.set("version", g_layoutVersion);
			v.set("slot", static_cast<int>(_slot));
			Value f = Value::object();
			f.set("version", static_cast<int>(_version));
			f.set("revision", static_cast<int>(_revision));
			v.set("format", std::move(f));
			return v;
		}

		Value steps(const uint64_t _bits)
		{
			Value a = Value::array();
			for(int s = 0; s < 64; ++s)
				if((_bits >> s) & 1)
					a.push(s);
			return a;
		}

		template<typename Array>
		Value numbers(const Array& _a)
		{
			Value v = Value::array();
			for(const auto b : _a)
				v.push(static_cast<int>(b));
			return v;
		}

		std::string hex(const uint8_t* _data, const size_t _size)
		{
			std::string s;
			char b[3];
			for(size_t i = 0; i < _size; ++i)
			{
				std::snprintf(b, sizeof(b), "%02x", _data[i]);
				s += b;
			}
			return s;
		}

		template<typename Array>
		std::string hex(const Array& _a) { return hex(_a.data(), _a.size()); }

		bool unhex(const std::string& _s, std::vector<uint8_t>& _out)
		{
			if(_s.size() % 2)
				return false;
			_out.clear();
			for(size_t i = 0; i < _s.size(); i += 2)
			{
				unsigned v = 0;
				if(std::sscanf(_s.c_str() + i, "%2x", &v) != 1)
					return false;
				_out.push_back(static_cast<uint8_t>(v));
			}
			return true;
		}

		// Runs of bytes that differ from _default: [[index, "hex"], ...].
		Value runs(const std::vector<uint8_t>& _bytes, const uint8_t _default)
		{
			Value a = Value::array();
			for(size_t i = 0; i < _bytes.size();)
			{
				if(_bytes[i] == _default)
				{
					++i;
					continue;
				}
				size_t j = i;
				while(j < _bytes.size() && _bytes[j] != _default)
					++j;
				Value r = Value::array();
				r.push(static_cast<unsigned long>(i));
				r.push(hex(_bytes.data() + i, j - i));
				a.push(std::move(r));
				i = j;
			}
			return a;
		}

		// Name bytes: the 7-bit text up to the first NUL; "nameBytes" (hex) as well
		// whenever the bytes are not exactly that text padded with NULs.
		template<size_t N>
		void setName(Value& _v, const std::array<uint8_t, N>& _name)
		{
			std::string s;
			for(const auto c : _name)
			{
				if(c == 0)
					break;
				s += (c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '?';
			}
			_v.set("name", s);
			std::array<uint8_t, N> rebuilt{};
			for(size_t i = 0; i < s.size(); ++i)
				rebuilt[i] = static_cast<uint8_t>(s[i]);
			if(rebuilt != _name)
				_v.set("nameBytes", hex(_name));
		}

		// ---- reading ----

		class In
		{
		public:
			In(const Value& _v, std::string _path, std::vector<std::string>& _errors)
				: m_v(_v), m_path(std::move(_path)), m_errors(_errors)
			{
			}

			void error(const std::string& _what) const { m_errors.push_back(m_path + ": " + _what); }
			std::string path(const std::string& _key) const { return m_path + "." + _key; }

			const Value* get(const char* _key, const bool _required = true) const
			{
				const auto* v = m_v.find(_key);
				if(!v && _required)
					m_errors.push_back(path(_key) + ": missing");
				return v;
			}

			In child(const char* _key, const bool _required = true) const
			{
				const auto* v = get(_key, _required);
				return In(v ? *v : g_null, path(_key), m_errors);
			}

			In element(const size_t _i) const
			{
				const auto p = m_path + "[" + std::to_string(_i) + "]";
				if(!m_v.isArray() || _i >= m_v.asArray().size())
					return In(g_null, p, m_errors);
				return In(m_v.asArray()[_i], p, m_errors);
			}

			size_t size() const { return m_v.isArray() ? m_v.asArray().size() : 0; }
			bool isArray() const { return m_v.isArray(); }
			bool isNull() const { return m_v.isNull(); }
			const Value& value() const { return m_v; }

			template<typename T>
			bool toInteger(const Value& _v, const std::string& _path, T& _out, const double _min, const double _max) const
			{
				if(!_v.isNumber() || std::floor(_v.asNumber()) != _v.asNumber())
				{
					m_errors.push_back(_path + ": expected an integer");
					return false;
				}
				const auto n = _v.asNumber();
				if(n < _min || n > _max)
				{
					m_errors.push_back(_path + ": " + std::to_string(static_cast<long long>(n)) + " is outside "
						+ std::to_string(static_cast<long long>(_min)) + ".." + std::to_string(static_cast<long long>(_max)));
					return false;
				}
				_out = static_cast<T>(n);
				return true;
			}

			template<typename T>
			bool integer(const char* _key, T& _out, const double _min, const double _max, const bool _required = true) const
			{
				const auto* v = get(_key, _required);
				return v && toInteger(*v, path(_key), _out, _min, _max);
			}

			template<typename T, size_t N>
			void integers(const char* _key, std::array<T, N>& _out, const double _min, const double _max) const
			{
				const auto* v = get(_key);
				if(!v)
					return;
				if(!v->isArray() || v->asArray().size() != N)
				{
					m_errors.push_back(path(_key) + ": expected " + std::to_string(N) + " integers");
					return;
				}
				for(size_t i = 0; i < N; ++i)
					toInteger(v->asArray()[i], path(_key) + "[" + std::to_string(i) + "]", _out[i], _min, _max);
			}

			void stepSet(const char* _key, uint64_t& _out) const
			{
				const auto* v = get(_key);
				if(!v)
					return;
				if(!v->isArray())
				{
					m_errors.push_back(path(_key) + ": expected an array of steps");
					return;
				}
				_out = 0;
				for(size_t i = 0; i < v->asArray().size(); ++i)
				{
					unsigned s = 0;
					if(toInteger(v->asArray()[i], path(_key) + "[" + std::to_string(i) + "]", s, 0, 63))
						_out |= uint64_t{1} << s;
				}
			}

			template<size_t N>
			void hexBytes(const char* _key, std::array<uint8_t, N>& _out, const bool _required = true) const
			{
				const auto* v = get(_key, _required);
				if(!v)
					return;
				std::vector<uint8_t> b;
				if(!v->isString() || !unhex(v->asString(), b) || b.size() != N)
				{
					m_errors.push_back(path(_key) + ": expected " + std::to_string(N) + " bytes as hex");
					return;
				}
				std::copy(b.begin(), b.end(), _out.begin());
			}

			// [[index, "hex"], ...] onto _bytes (already filled with the default).
			void runsInto(const char* _key, std::vector<uint8_t>& _bytes) const
			{
				const auto* v = get(_key, false);
				if(!v)
					return;
				if(!v->isArray())
				{
					m_errors.push_back(path(_key) + ": expected runs");
					return;
				}
				for(const auto& r : v->asArray())
				{
					std::vector<uint8_t> b;
					if(!r.isArray() || r.asArray().size() != 2 || !r.asArray()[0].isNumber() || !r.asArray()[1].isString()
						|| !unhex(r.asArray()[1].asString(), b))
					{
						m_errors.push_back(path(_key) + ": a run must be [index, \"hex\"]");
						return;
					}
					const auto at = static_cast<size_t>(r.asArray()[0].asNumber());
					if(at + b.size() > _bytes.size())
					{
						m_errors.push_back(path(_key) + ": a run ends past the region");
						return;
					}
					std::copy(b.begin(), b.end(), _bytes.begin() + static_cast<std::ptrdiff_t>(at));
				}
			}

			template<size_t N>
			void name(std::array<uint8_t, N>& _out) const
			{
				_out.fill(0);
				if(const auto* bytes = get("nameBytes", false))
				{
					std::vector<uint8_t> b;
					if(!bytes->isString() || !unhex(bytes->asString(), b) || b.size() != N)
						error("nameBytes must be " + std::to_string(N) + " bytes as hex");
					else
						std::copy(b.begin(), b.end(), _out.begin());
					return;
				}
				const auto* n = get("name");
				if(!n)
					return;
				if(!n->isString() || n->asString().size() > N)
				{
					error("name must be a string of at most " + std::to_string(N) + " characters");
					return;
				}
				const auto& s = n->asString();
				for(size_t i = 0; i < s.size(); ++i)
				{
					const auto c = static_cast<unsigned char>(s[i]);
					if(c < 0x20 || c > 0x7e)
					{
						error("name characters must be printable 7-bit ASCII");
						return;
					}
					_out[i] = c;
				}
			}

			bool schema(const char* _expected) const
			{
				const auto* s = get("schema");
				if(!s || !s->isString() || s->asString() != _expected)
				{
					error(std::string("schema must be \"") + _expected + "\"");
					return false;
				}
				int version = 0;
				if(!integer("version", version, 1, 1000))
					return false;
				if(version != g_layoutVersion)
				{
					error("version " + std::to_string(version) + " is not supported");
					return false;
				}
				return true;
			}

		private:
			const Value& m_v;
			std::string m_path;
			std::vector<std::string>& m_errors;
		};

		template<typename Doc>
		void readHeader(const In& _in, Doc& _d, const double _slots)
		{
			_in.integer("slot", _d.position, 0, _slots - 1);
			const auto f = _in.child("format");
			f.integer("version", _d.version, 0, 127);
			f.integer("revision", _d.revision, 0, 127);
		}

		// ---- pattern ----

		Value arpToJson(const MmArpBlock& _a, const size_t _t, const bool _synth)
		{
			Value v = Value::object();
			v.set("play", _a.playOjmp[_t] & 7);
			v.set("flag", (_a.playOjmp[_t] >> 3) & 1);
			v.set("ojmp", _a.playOjmp[_t] >> 4);
			v.set("mode", _a.mode[_t]);
			v.set("range", _a.range[_t]);
			v.set("speed", _a.speed[_t]);
			if(_synth)
				v.set("trigs", _a.trigs[_t]);
			v.set("length", _a.length[_t]);
			v.set("steps", numbers(_a.steps[_t]));
			return v;
		}

		void arpFromJson(const In& _in, MmArpBlock& _a, const size_t _t, const bool _synth)
		{
			unsigned play = 0, flag = 0, ojmp = 0;
			_in.integer("play", play, 0, 7);
			_in.integer("flag", flag, 0, 1);
			_in.integer("ojmp", ojmp, 0, 15);
			_a.playOjmp[_t] = static_cast<uint8_t>(play | (flag << 3) | (ojmp << 4));
			_in.integer("mode", _a.mode[_t], 0, 255);
			_in.integer("range", _a.range[_t], 0, 255);
			_in.integer("speed", _a.speed[_t], 0, 255);
			if(_synth)
				_in.integer("trigs", _a.trigs[_t], 0, 255);
			_in.integer("length", _a.length[_t], 0, 255);
			_in.integers("steps", _a.steps[_t], 0, 255);
		}

		template<size_t N>
		std::vector<uint8_t> poolBytes(const std::array<uint16_t, N>& _pool, const size_t _from)
		{
			std::vector<uint8_t> b;
			for(size_t i = _from; i < N; ++i)
			{
				b.push_back(static_cast<uint8_t>(_pool[i] >> 8));
				b.push_back(static_cast<uint8_t>(_pool[i]));
			}
			return b;
		}

		template<size_t N>
		Value entries(const std::array<uint16_t, N>& _pool, const size_t _count)
		{
			Value a = Value::array();
			for(size_t i = 0; i < _count && i < N; ++i)
			{
				const auto e = mmNoteEntry(_pool[i]);
				a.push(Value(Value::Array{Value(e.track), Value(e.step), Value(e.note)}));
			}
			return a;
		}

		template<size_t N>
		size_t entriesFromJson(const In& _in, const char* _key, std::array<uint16_t, N>& _pool)
		{
			const auto a = _in.child(_key);
			if(!a.isArray())
			{
				a.error("expected an array of [track, step, note]");
				return 0;
			}
			if(a.size() > N)
			{
				a.error("at most " + std::to_string(N) + " notes");
				return 0;
			}
			for(size_t i = 0; i < a.size(); ++i)
			{
				const auto e = a.element(i);
				MmNoteEntry n;
				if(!e.isArray() || e.size() != 3)
				{
					e.error("expected [track, step, note]");
					continue;
				}
				e.toInteger(e.value().asArray()[0], "track", n.track, 0, 7);
				e.toInteger(e.value().asArray()[1], "step", n.step, 0, 63);
				e.toInteger(e.value().asArray()[2], "note", n.note, 0, 127);
				_pool[i] = mmNoteEntryWord(n);
			}
			return a.size();
		}

		template<size_t N>
		void poolFromRuns(const In& _in, const char* _key, std::array<uint16_t, N>& _pool, const size_t _from)
		{
			std::vector<uint8_t> b((N - _from) * 2, 0xff);
			_in.runsInto(_key, b);
			for(size_t i = _from; i < N; ++i)
				_pool[i] = static_cast<uint16_t>((b[(i - _from) * 2] << 8) | b[(i - _from) * 2 + 1]);
		}
	}

	std::string mmPatternName(const unsigned _slot)
	{
		char b[8];
		std::snprintf(b, sizeof(b), "%c%02u", 'A' + static_cast<char>((_slot >> 4) & 7), (_slot & 15) + 1);
		return b;
	}

	Value mmPatternToJsonV1(const MmPattern& _p)
	{
		Value v = header("mm-desk/pattern", _p.position, _p.version, _p.revision);
		v.set("name", mmPatternName(_p.position));
		v.set("length", _p.length);
		v.set("multiplier", _p.multiplier < g_multipliers.size() ? Value(g_multipliers[_p.multiplier]) : Value(static_cast<int>(_p.multiplier)));
		v.set("kit", _p.kit);
		v.set("swingAmount", _p.swingAmount);
		v.set("patternTranspose", _p.patternTranspose);

		Value tracks = Value::array();
		for(size_t t = 0; t < MmPattern::g_tracks; ++t)
		{
			Value tr = Value::object();
			tr.set("trig", steps(_p.pitch[t]));
			tr.set("amp", steps(_p.amp[t]));
			tr.set("filter", steps(_p.filter[t]));
			tr.set("lfo", steps(_p.lfo[t]));
			tr.set("noteOff", steps(_p.noteOff[t]));
			tr.set("chord", steps(_p.chord[t]));
			tr.set("slide", steps(_p.slide[t]));
			tr.set("swing", steps(_p.swing[t]));
			Value notes = Value::array();
			for(size_t s = 0; s < MmPattern::g_steps; ++s)
				if(_p.notes[t][s] != MmPattern::g_noNote)
					notes.push(Value(Value::Array{Value(static_cast<int>(s)), Value(static_cast<int>(_p.notes[t][s]))}));
			tr.set("notes", std::move(notes));
			tr.set("transpose", _p.transpose.track[t]);
			tr.set("scale", _p.transpose.scale[t]);
			tr.set("key", _p.transpose.key[t]);
			tr.set("arp", arpToJson(_p.arp, t, true));
			tracks.push(std::move(tr));
		}
		v.set("tracks", std::move(tracks));

		Value midi = Value::array();
		for(size_t t = 0; t < MmPattern::g_tracks; ++t)
		{
			Value tr = Value::object();
			tr.set("trig", steps(_p.midiTrig[t]));
			tr.set("note", steps(_p.midiNote[t]));
			tr.set("noteOff", steps(_p.midiNoteOff[t]));
			tr.set("slide", steps(_p.midiSlide[t]));
			tr.set("swing", steps(_p.midiSwing[t]));
			tr.set("transpose", _p.midiTranspose.track[t]);
			tr.set("scale", _p.midiTranspose.scale[t]);
			tr.set("key", _p.midiTranspose.key[t]);
			tr.set("arp", arpToJson(_p.midiArp, t, false));
			midi.push(std::move(tr));
		}
		v.set("midiTracks", std::move(midi));

		const auto params = mmLockParams(_p);
		const auto used = std::min<size_t>(_p.lockRowCount, std::min(params.size(), MmPattern::g_lockRows));
		Value locks = Value::array();
		for(size_t r = 0; r < used; ++r)
		{
			Value l = Value::object();
			l.set("track", params[r].track);
			l.set("page", params[r].page);
			l.set("param", params[r].param);
			Value st = Value::array();
			for(size_t s = 0; s < MmPattern::g_steps; ++s)
				if(_p.lockRows[r][s] != MmPattern::g_noLock)
					st.push(Value(Value::Array{Value(static_cast<int>(s)), Value(static_cast<int>(_p.lockRows[r][s]))}));
			l.set("steps", std::move(st));
			locks.push(std::move(l));
		}
		v.set("locks", std::move(locks));
		v.set("midiNotes", entries(_p.midiNotes, _p.midiNoteCount));
		v.set("chordNotes", entries(_p.chordNotes, _p.chordNoteCount));

		Value hidden = Value::object();
		hidden.set("x270", hex(_p.x270));
		hidden.set("x54e", hex(_p.x54e));
		hidden.set("x555", _p.x555);
		hidden.set("x14d7", _p.x14d7);
		std::vector<uint8_t> rest;
		for(size_t r = used; r < MmPattern::g_lockRows; ++r)
			rest.insert(rest.end(), _p.lockRows[r].begin(), _p.lockRows[r].end());
		hidden.set("lockRows", runs(rest, 0xff));
		hidden.set("midiNotes", runs(poolBytes(_p.midiNotes, std::min<size_t>(_p.midiNoteCount, 400)), 0xff));
		hidden.set("chordNotes", runs(poolBytes(_p.chordNotes, std::min<size_t>(_p.chordNoteCount, 192)), 0xff));
		v.set("hidden", std::move(hidden));
		return v;
	}

	std::optional<MmPattern> mmPatternFromJsonV1(const Value& _json, std::vector<std::string>& _errors)
	{
		const auto before = _errors.size();
		const In in(_json, "$", _errors);
		if(!in.schema("mm-desk/pattern"))
			return std::nullopt;
		MmPattern p;
		readHeader(in, p, MmPattern::g_slots);
		in.integer("length", p.length, 0, 255);
		if(const auto* m = in.get("multiplier"))
		{
			bool found = false;
			for(size_t i = 0; i < g_multipliers.size(); ++i)
				if(m->isString() && m->asString() == g_multipliers[i])
				{
					p.multiplier = static_cast<uint8_t>(i);
					found = true;
				}
			if(!found)
				in.integer("multiplier", p.multiplier, 0, 255);
		}
		in.integer("kit", p.kit, 0, 255);
		in.integer("swingAmount", p.swingAmount, 0, 255);
		in.integer("patternTranspose", p.patternTranspose, -128, 127);

		const auto tracks = in.child("tracks");
		if(tracks.size() != MmPattern::g_tracks)
			tracks.error("expected 6 tracks");
		for(size_t t = 0; t < MmPattern::g_tracks && t < tracks.size(); ++t)
		{
			const auto tr = tracks.element(t);
			tr.stepSet("trig", p.pitch[t]);
			tr.stepSet("amp", p.amp[t]);
			tr.stepSet("filter", p.filter[t]);
			tr.stepSet("lfo", p.lfo[t]);
			tr.stepSet("noteOff", p.noteOff[t]);
			tr.stepSet("chord", p.chord[t]);
			tr.stepSet("slide", p.slide[t]);
			tr.stepSet("swing", p.swing[t]);
			p.notes[t].fill(MmPattern::g_noNote);
			const auto notes = tr.child("notes");
			for(size_t i = 0; i < notes.size(); ++i)
			{
				const auto n = notes.element(i);
				unsigned s = 0, note = 0;
				if(n.size() != 2 || !n.toInteger(n.value().asArray()[0], "step", s, 0, 63)
					|| !n.toInteger(n.value().asArray()[1], "note", note, 0, 254))
				{
					n.error("expected [step, note]");
					continue;
				}
				p.notes[t][s] = static_cast<uint8_t>(note);
			}
			tr.integer("transpose", p.transpose.track[t], -128, 127);
			tr.integer("scale", p.transpose.scale[t], 0, 255);
			tr.integer("key", p.transpose.key[t], 0, 255);
			arpFromJson(tr.child("arp"), p.arp, t, true);
		}
		const auto midi = in.child("midiTracks");
		if(midi.size() != MmPattern::g_tracks)
			midi.error("expected 6 MIDI tracks");
		for(size_t t = 0; t < MmPattern::g_tracks && t < midi.size(); ++t)
		{
			const auto tr = midi.element(t);
			tr.stepSet("trig", p.midiTrig[t]);
			tr.stepSet("note", p.midiNote[t]);
			tr.stepSet("noteOff", p.midiNoteOff[t]);
			tr.stepSet("slide", p.midiSlide[t]);
			tr.stepSet("swing", p.midiSwing[t]);
			tr.integer("transpose", p.midiTranspose.track[t], -128, 127);
			tr.integer("scale", p.midiTranspose.scale[t], 0, 255);
			tr.integer("key", p.midiTranspose.key[t], 0, 255);
			arpFromJson(tr.child("arp"), p.midiArp, t, false);
		}

		const auto locks = in.child("locks");
		if(locks.size() > MmPattern::g_lockRows)
			locks.error("at most 62 locked parameters per pattern");
		for(auto& row : p.lockRows)
			row.fill(MmPattern::g_noLock);
		std::vector<MmLockParam> order;
		for(size_t r = 0; r < locks.size() && r < MmPattern::g_lockRows; ++r)
		{
			const auto l = locks.element(r);
			MmLockParam lp;
			l.integer("track", lp.track, 0, 5);
			l.integer("page", lp.page, 0, 7);
			l.integer("param", lp.param, 0, 7);
			if(!order.empty() && !(order.back() < lp))
				l.error("locks must be in (track, page, param) order, each once");
			order.push_back(lp);
			p.lockMasks[lp.track][lp.page] |= static_cast<uint8_t>(1u << lp.param);
			const auto st = l.child("steps");
			for(size_t i = 0; i < st.size(); ++i)
			{
				const auto e = st.element(i);
				unsigned s = 0, val = 0;
				if(e.size() != 2 || !e.toInteger(e.value().asArray()[0], "step", s, 0, 63)
					|| !e.toInteger(e.value().asArray()[1], "value", val, 0, 254))
				{
					e.error("expected [step, value]");
					continue;
				}
				p.lockRows[r][s] = static_cast<uint8_t>(val);
			}
		}
		p.lockRowCount = static_cast<uint8_t>(std::min<size_t>(locks.size(), MmPattern::g_lockRows));
		p.midiNotes.fill(0xffff);
		p.chordNotes.fill(0xffff);
		p.midiNoteCount = static_cast<uint16_t>(entriesFromJson(in, "midiNotes", p.midiNotes));
		p.chordNoteCount = static_cast<uint8_t>(entriesFromJson(in, "chordNotes", p.chordNotes));

		const auto hidden = in.child("hidden");
		hidden.hexBytes("x270", p.x270);
		hidden.hexBytes("x54e", p.x54e);
		hidden.integer("x555", p.x555, 0, 255);
		hidden.integer("x14d7", p.x14d7, 0, 255);
		{
			std::vector<uint8_t> rest((MmPattern::g_lockRows - p.lockRowCount) * MmPattern::g_steps, 0xff);
			hidden.runsInto("lockRows", rest);
			for(size_t r = p.lockRowCount, i = 0; r < MmPattern::g_lockRows; ++r)
				for(size_t s = 0; s < MmPattern::g_steps; ++s)
					p.lockRows[r][s] = rest[i++];
		}
		poolFromRuns(hidden, "midiNotes", p.midiNotes, p.midiNoteCount);
		poolFromRuns(hidden, "chordNotes", p.chordNotes, p.chordNoteCount);

		if(_errors.size() != before)
			return std::nullopt;
		for(const auto& e : validate(p))
			_errors.push_back("$: " + e);
		return _errors.size() == before ? std::optional<MmPattern>(p) : std::nullopt;
	}

	// ---- kit ----

	Value mmKitToJsonV1(const MmKit& _k)
	{
		Value v = header("mm-desk/kit", _k.position, _k.version, _k.revision);
		setName(v, _k.name);
		v.set("levels", numbers(_k.levels));
		Value tracks = Value::array();
		for(size_t t = 0; t < MmKit::g_tracks; ++t)
		{
			const auto& tr = _k.tracks[t];
			Value o = Value::object();
			o.set("machine", _k.machines[t]);
			const auto* info = mmMachine(_k.machines[t]);
			o.set("machineName", info ? info->name : "?");
			o.set("outputs", mmRoutingOutputs(_k.routing[t]));
			o.set("input", mmRoutingInput(_k.routing[t]));
			Value pages = Value::array();
			for(const auto& pg : tr.pages)
				pages.push(numbers(pg));
			pages.push(numbers(tr.midi));
			o.set("pages", std::move(pages));
			o.set("multiEnv", numbers(tr.multiEnv));
			o.set("extra", hex(tr.extra));
			Value assign = Value::object();
			assign.set("page", numbers(_k.assignPage[t]));
			assign.set("dest", numbers(_k.assignDest[t]));
			assign.set("add", numbers(_k.assignAdd[t]));
			o.set("assign", std::move(assign));
			o.set("trigPos", _k.trigPos[t] == MmKit::g_noTrigPos ? Value() : Value(static_cast<int>(_k.trigPos[t])));
			tracks.push(std::move(o));
		}
		v.set("tracks", std::move(tracks));
		Value masks = Value::object();
		masks.set("mirror", _k.mirrorMask);
		masks.set("hpf", _k.hpfMask);
		masks.set("lpf", _k.lpfMask);
		masks.set("portamento", _k.portamentoMask);
		masks.set("legatoAmp", _k.legatoAmp);
		masks.set("legatoFilter", _k.legatoFilter);
		masks.set("legatoLfo", _k.legatoLfo);
		v.set("trackMasks", std::move(masks));
		Value multi = Value::object();
		multi.set("mode", _k.multiTrigMode);
		multi.set("timing", _k.multiTrigTiming);
		multi.set("splitKey", _k.splitKey);
		multi.set("splitTrack", _k.splitTrack);
		v.set("multiTrig", std::move(multi));
		Value hidden = Value::object();
		hidden.set("x1cd", hex(_k.x1cd));
		hidden.set("x1d1", _k.x1d1);
		v.set("hidden", std::move(hidden));
		return v;
	}

	std::optional<MmKit> mmKitFromJsonV1(const Value& _json, std::vector<std::string>& _errors)
	{
		const auto before = _errors.size();
		const In in(_json, "$", _errors);
		if(!in.schema("mm-desk/kit"))
			return std::nullopt;
		MmKit k;
		readHeader(in, k, MmKit::g_slots);
		in.name(k.name);
		in.integers("levels", k.levels, 0, 255);
		const auto tracks = in.child("tracks");
		if(tracks.size() != MmKit::g_tracks)
			tracks.error("expected 6 tracks");
		for(size_t t = 0; t < MmKit::g_tracks && t < tracks.size(); ++t)
		{
			const auto tr = tracks.element(t);
			tr.integer("machine", k.machines[t], 0, 255);
			{
				uint8_t outputs = 0, input = 0;
				tr.integer("outputs", outputs, 0, 7);
				tr.integer("input", input, 0, 7);
				k.routing[t] = mmRouting(outputs, input);
			}
			const auto pages = tr.child("pages");
			if(pages.size() != 8)
				pages.error("expected 8 pages (SYN AMP FLT EFX LF1 LF2 LF3 MIDI)");
			for(size_t pg = 0; pg < 8 && pg < pages.size(); ++pg)
			{
				auto& target = pg < 7 ? k.tracks[t].pages[pg] : k.tracks[t].midi;
				const auto page = pages.element(pg);
				if(page.size() != 8)
				{
					page.error("expected 8 values");
					continue;
				}
				for(size_t i = 0; i < 8; ++i)
					page.toInteger(page.value().asArray()[i], "value", target[i], 0, 255);
			}
			tr.integers("multiEnv", k.tracks[t].multiEnv, 0, 255);
			tr.hexBytes("extra", k.tracks[t].extra);
			const auto assign = tr.child("assign");
			assign.integers("page", k.assignPage[t], 0, 255);
			assign.integers("dest", k.assignDest[t], 0, 255);
			assign.integers("add", k.assignAdd[t], -128, 127);
			if(const auto* tp = tr.get("trigPos"))
			{
				if(tp->isNull())
					k.trigPos[t] = MmKit::g_noTrigPos;
				else
					tr.integer("trigPos", k.trigPos[t], 0, 254);
			}
		}
		const auto masks = in.child("trackMasks");
		masks.integer("mirror", k.mirrorMask, 0, 255);
		masks.integer("hpf", k.hpfMask, 0, 255);
		masks.integer("lpf", k.lpfMask, 0, 255);
		masks.integer("portamento", k.portamentoMask, 0, 255);
		masks.integer("legatoAmp", k.legatoAmp, 0, 255);
		masks.integer("legatoFilter", k.legatoFilter, 0, 255);
		masks.integer("legatoLfo", k.legatoLfo, 0, 255);
		const auto multi = in.child("multiTrig");
		multi.integer("mode", k.multiTrigMode, 0, 255);
		multi.integer("timing", k.multiTrigTiming, 0, 255);
		multi.integer("splitKey", k.splitKey, 0, 255);
		multi.integer("splitTrack", k.splitTrack, 0, 255);
		const auto hidden = in.child("hidden");
		hidden.hexBytes("x1cd", k.x1cd);
		hidden.integer("x1d1", k.x1d1, 0, 255);
		if(_errors.size() != before)
			return std::nullopt;
		for(const auto& e : validate(k))
			_errors.push_back("$: " + e);
		return _errors.size() == before ? std::optional<MmKit>(k) : std::nullopt;
	}

	// ---- song ----

	namespace
	{
		const char* rowKind(const MmSongRow& _r, const size_t _index)
		{
			const auto p = _r.pattern();
			if(p == MmSong::g_end)
				return "end";
			if(p == MmSong::g_loop)
			{
				const auto target = _r.bytes[mmSongRow::g_target];
				return target < _index ? "loop" : target == _index ? "halt" : "jump";
			}
			return "pattern";
		}
	}

	Value mmSongToJsonV1(const MmSong& _s)
	{
		Value v = header("mm-desk/song", _s.position, _s.version, _s.revision);
		setName(v, _s.name);
		const auto used = mmSongUsedRows(_s);
		Value rows = Value::array();
		for(size_t i = 0; i < used; ++i)
		{
			const auto& r = _s.rows[i];
			const auto& b = r.bytes;
			Value o = Value::object();
			o.set("kind", rowKind(r, i));
			o.set("pattern", b[mmSongRow::g_pattern]);
			o.set("target", b[mmSongRow::g_target]);
			o.set("repeats", b[mmSongRow::g_repeats]);
			o.set("mutes", b[mmSongRow::g_mutes]);
			o.set("midiMutes", b[mmSongRow::g_midiMutes]);
			o.set("offset", b[mmSongRow::g_offset]);
			o.set("length", b[mmSongRow::g_length]);
			o.set("transpose", static_cast<int8_t>(b[mmSongRow::g_transpose]));
			Value tt = Value::array(), mt = Value::array();
			for(size_t t = 0; t < 6; ++t)
			{
				tt.push(static_cast<int8_t>(b[mmSongRow::g_trackTranspose + t]));
				mt.push(static_cast<int8_t>(b[mmSongRow::g_midiTranspose + t]));
			}
			o.set("trackTranspose", std::move(tt));
			o.set("midiTranspose", std::move(mt));
			o.set("tempo", r.tempo() == MmSong::g_keepTempo ? Value() : Value(static_cast<int>(r.tempo())));
			o.set("x3", b[mmSongRow::g_x3]);
			o.set("x21", b[mmSongRow::g_x21]);
			rows.push(std::move(o));
		}
		v.set("rows", std::move(rows));
		Value hidden = Value::object();
		hidden.set("x0e", hex(_s.x0e));
		std::vector<uint8_t> tail;
		for(size_t i = used; i < MmSong::g_rows; ++i)
			tail.insert(tail.end(), _s.rows[i].bytes.begin(), _s.rows[i].bytes.end());
		hidden.set("rowsAfterEnd", runs(tail, 0x00));
		v.set("hidden", std::move(hidden));
		return v;
	}

	std::optional<MmSong> mmSongFromJsonV1(const Value& _json, std::vector<std::string>& _errors)
	{
		const auto before = _errors.size();
		const In in(_json, "$", _errors);
		if(!in.schema("mm-desk/song"))
			return std::nullopt;
		MmSong s;
		readHeader(in, s, MmSong::g_slots);
		in.name(s.name);
		const auto rows = in.child("rows");
		if(rows.size() > MmSong::g_rows)
			rows.error("at most 200 rows");
		const auto used = std::min(rows.size(), MmSong::g_rows);
		for(size_t i = 0; i < used; ++i)
		{
			const auto r = rows.element(i);
			auto& b = s.rows[i].bytes;
			const auto* kind = r.get("kind");
			r.integer("pattern", b[mmSongRow::g_pattern], 0, 255);
			if(kind && kind->isString())
			{
				const auto& k = kind->asString();
				if(k == "end")
					b[mmSongRow::g_pattern] = MmSong::g_end;
				else if(k == "loop" || k == "jump" || k == "halt")
					b[mmSongRow::g_pattern] = MmSong::g_loop;
				else if(k != "pattern")
					r.error("kind must be pattern, loop, jump, halt or end");
			}
			r.integer("target", b[mmSongRow::g_target], 0, 255);
			r.integer("repeats", b[mmSongRow::g_repeats], 0, 255);
			r.integer("mutes", b[mmSongRow::g_mutes], 0, 255);
			r.integer("midiMutes", b[mmSongRow::g_midiMutes], 0, 255);
			r.integer("offset", b[mmSongRow::g_offset], 0, 255);
			r.integer("length", b[mmSongRow::g_length], 0, 255);
			int8_t tr = 0;
			if(r.integer("transpose", tr, -128, 127))
				b[mmSongRow::g_transpose] = static_cast<uint8_t>(tr);
			std::array<int8_t, 6> tt{}, mt{};
			r.integers("trackTranspose", tt, -128, 127);
			r.integers("midiTranspose", mt, -128, 127);
			for(size_t t = 0; t < 6; ++t)
			{
				b[mmSongRow::g_trackTranspose + t] = static_cast<uint8_t>(tt[t]);
				b[mmSongRow::g_midiTranspose + t] = static_cast<uint8_t>(mt[t]);
			}
			uint16_t tempo = MmSong::g_keepTempo;
			if(const auto* t = r.get("tempo"))
				if(!t->isNull())
					r.integer("tempo", tempo, 0, 0xfffe);
			b[mmSongRow::g_tempo] = static_cast<uint8_t>(tempo >> 8);
			b[mmSongRow::g_tempo + 1] = static_cast<uint8_t>(tempo);
			r.integer("x3", b[mmSongRow::g_x3], 0, 255);
			r.integer("x21", b[mmSongRow::g_x21], 0, 255);
		}
		const auto hidden = in.child("hidden");
		hidden.hexBytes("x0e", s.x0e);
		std::vector<uint8_t> tail((MmSong::g_rows - used) * 24, 0x00);
		hidden.runsInto("rowsAfterEnd", tail);
		for(size_t i = used, j = 0; i < MmSong::g_rows; ++i)
			for(size_t k = 0; k < 24; ++k)
				s.rows[i].bytes[k] = tail[j++];
		if(_errors.size() != before)
			return std::nullopt;
		for(const auto& e : validate(s))
			_errors.push_back("$: " + e);
		return _errors.size() == before ? std::optional<MmSong>(s) : std::nullopt;
	}

	// ---- global ----

	Value mmGlobalToJsonV1(const MmGlobal& _g)
	{
		Value v = header("mm-desk/global", _g.position, _g.version, _g.revision);
		Value ch = Value::object();
		ch.set("auto", _g.autoChannel);
		ch.set("base", _g.baseChannel);
		ch.set("span", _g.channelSpan);
		ch.set("multiTrig", _g.multiTrigChannel);
		ch.set("multiMap", _g.multiMapChannel);
		v.set("channels", std::move(ch));
		Value ms = Value::object();
		ms.set("channels", numbers(_g.midiSeqChannels));
		Value ccs = Value::array();
		for(const auto& c : _g.midiSeqCcs)
			ccs.push(numbers(c));
		ms.set("ccs", std::move(ccs));
		v.set("midiSeq", std::move(ms));
		v.set("routingMode", _g.routingMode < g_routingModes.size() ? Value(g_routingModes[_g.routingMode]) : Value(static_cast<int>(_g.routingMode)));
		v.set("masterTune", _g.masterTune);
		Value map = Value::array();
		for(const auto& f : _g.multiMap)
			map.push(numbers(f));
		v.set("multiMap", std::move(map));
		v.set("control", hex(_g.control));
		Value controlIn = Value::object();
		controlIn.set("tempoSync", _g.tempoSync);
		controlIn.set("transport", _g.transportIn);
		v.set("controlIn", std::move(controlIn));
		Value hidden = Value::object();
		hidden.set("x07", hex(_g.x07));
		hidden.set("x30", hex(_g.x30));
		hidden.set("xfd", hex(_g.xfd));
		v.set("hidden", std::move(hidden));
		return v;
	}

	std::optional<MmGlobal> mmGlobalFromJsonV1(const Value& _json, std::vector<std::string>& _errors)
	{
		const auto before = _errors.size();
		const In in(_json, "$", _errors);
		if(!in.schema("mm-desk/global"))
			return std::nullopt;
		MmGlobal g;
		readHeader(in, g, MmGlobal::g_slots);
		const auto ch = in.child("channels");
		ch.integer("auto", g.autoChannel, 0, 255);
		ch.integer("base", g.baseChannel, 0, 255);
		ch.integer("span", g.channelSpan, 0, 255);
		ch.integer("multiTrig", g.multiTrigChannel, 0, 255);
		ch.integer("multiMap", g.multiMapChannel, 0, 255);
		const auto ms = in.child("midiSeq");
		ms.integers("channels", g.midiSeqChannels, 0, 255);
		const auto ccs = ms.child("ccs");
		if(ccs.size() != 6)
			ccs.error("expected 6 tracks of 4 CC numbers");
		for(size_t t = 0; t < 6 && t < ccs.size(); ++t)
		{
			const auto c = ccs.element(t);
			if(c.size() != 4)
			{
				c.error("expected 4 CC numbers");
				continue;
			}
			for(size_t i = 0; i < 4; ++i)
				c.toInteger(c.value().asArray()[i], "cc", g.midiSeqCcs[t][i], 0, 255);
		}
		if(const auto* r = in.get("routingMode"))
		{
			bool found = false;
			for(size_t i = 0; i < g_routingModes.size(); ++i)
				if(r->isString() && r->asString() == g_routingModes[i])
				{
					g.routingMode = static_cast<uint8_t>(i);
					found = true;
				}
			if(!found)
				in.integer("routingMode", g.routingMode, 0, 255);
		}
		in.integer("masterTune", g.masterTune, 0, 0xffff);
		const auto map = in.child("multiMap");
		if(map.size() != 6)
			map.error("expected 6 fields of 32 ranges");
		for(size_t f = 0; f < 6 && f < map.size(); ++f)
		{
			const auto m = map.element(f);
			if(m.size() != MmGlobal::g_mapRanges)
			{
				m.error("expected 32 values");
				continue;
			}
			for(size_t i = 0; i < MmGlobal::g_mapRanges; ++i)
				m.toInteger(m.value().asArray()[i], "value", g.multiMap[f][i], 0, 255);
		}
		in.hexBytes("control", g.control);
		const auto controlIn = in.child("controlIn");
		controlIn.integer("tempoSync", g.tempoSync, 0, 255);
		controlIn.integer("transport", g.transportIn, 0, 255);
		const auto hidden = in.child("hidden");
		hidden.hexBytes("x07", g.x07);
		hidden.hexBytes("x30", g.x30);
		hidden.hexBytes("xfd", g.xfd);
		if(_errors.size() != before)
			return std::nullopt;
		for(const auto& e : validate(g))
			_errors.push_back("$: " + e);
		return _errors.size() == before ? std::optional<MmGlobal>(g) : std::nullopt;
	}

	// ---- contract version 2: "format" and the undecoded "hidden" bytes under "firmware" ----

	namespace
	{
		const json::FirmwareLayout g_mmFw{{"format"}, {}, "hidden"};
	}

	Value mmPatternToJson(const MmPattern& _p) { return json::groupFirmware(mmPatternToJsonV1(_p), g_mmFw, g_mmContractVersion); }
	Value mmKitToJson(const MmKit& _k) { return json::groupFirmware(mmKitToJsonV1(_k), g_mmFw, g_mmContractVersion); }
	Value mmSongToJson(const MmSong& _s) { return json::groupFirmware(mmSongToJsonV1(_s), g_mmFw, g_mmContractVersion); }
	Value mmGlobalToJson(const MmGlobal& _g) { return json::groupFirmware(mmGlobalToJsonV1(_g), g_mmFw, g_mmContractVersion); }

	std::optional<MmPattern> mmPatternFromJson(const Value& _json, std::vector<std::string>& _errors)
	{
		return mmPatternFromJsonV1(json::ungroupFirmware(_json, g_mmFw, g_mmContractVersion, g_layoutVersion), _errors);
	}
	std::optional<MmKit> mmKitFromJson(const Value& _json, std::vector<std::string>& _errors)
	{
		return mmKitFromJsonV1(json::ungroupFirmware(_json, g_mmFw, g_mmContractVersion, g_layoutVersion), _errors);
	}
	std::optional<MmSong> mmSongFromJson(const Value& _json, std::vector<std::string>& _errors)
	{
		return mmSongFromJsonV1(json::ungroupFirmware(_json, g_mmFw, g_mmContractVersion, g_layoutVersion), _errors);
	}
	std::optional<MmGlobal> mmGlobalFromJson(const Value& _json, std::vector<std::string>& _errors)
	{
		return mmGlobalFromJsonV1(json::ungroupFirmware(_json, g_mmFw, g_mmContractVersion, g_layoutVersion), _errors);
	}
}
