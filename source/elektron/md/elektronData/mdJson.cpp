#include "mdJson.h"

#include "jsonFirmware.h"

#include "mdGlobal.h"
#include "mdKit.h"
#include "mdMachines.h"
#include "mdPattern.h"
#include "mdSong.h"
#include "mdValidate.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <set>

namespace elektronData
{
	using json::Value;

	namespace
	{
		// The codec's own layout; documents leave as g_mdContractVersion (jsonFirmware.h).
		constexpr int g_layoutVersion = 1;
		constexpr std::array<const char*, 4> g_tempoMultipliers{"1X", "2X", "3/4X", "3/2X"};
		constexpr std::array<const char*, 4> g_fxNames{"gateBox", "rhythmEcho", "eq", "dynamix"};
		constexpr std::array<const char*, 7> g_outputs{"A", "B", "C", "D", "E", "F", "MAIN"};

		Value header(const char* _schema, const uint8_t _slot)
		{
			Value v = Value::object();
			v.set("schema", _schema);
			v.set("version", g_layoutVersion);
			v.set("slot", static_cast<int>(_slot));
			return v;
		}

		Value format(const uint8_t _version, const uint8_t _revision)
		{
			Value v = Value::object();
			v.set("version", static_cast<int>(_version));
			v.set("revision", static_cast<int>(_revision));
			return v;
		}

		Value steps(const uint64_t _bits, const size_t _count)
		{
			Value a = Value::array();
			for(size_t s = 0; s < _count; ++s)
				if((_bits >> s) & 1)
					a.push(static_cast<int>(s));
			return a;
		}

		template<size_t N>
		Value bytes(const uint8_t* _data)
		{
			Value a = Value::array();
			for(size_t i = 0; i < N; ++i)
				a.push(static_cast<int>(_data[i]));
			return a;
		}

		template<typename Array>
		Value bytes(const Array& _a)
		{
			Value v = Value::array();
			for(const auto b : _a)
				v.push(static_cast<int>(b));
			return v;
		}

		Value group(const uint8_t _g)
		{
			return _g == MdKit::g_noGroup ? Value() : Value(static_cast<int>(_g));
		}

		// Name bytes up to the first NUL; "nameTail" only when the bytes after it
		// are not all zero (factory kits carry leftovers there).
		void setName(Value& _v, const std::array<uint8_t, 16>& _name)
		{
			size_t n = 0;
			while(n < _name.size() && _name[n])
				++n;
			_v.set("name", std::string(_name.begin(), _name.begin() + static_cast<std::ptrdiff_t>(n)));
			bool tail = false;
			for(size_t i = n + 1; i < _name.size(); ++i)
				tail |= _name[i] != 0;
			if(!tail)
				return;
			Value t = Value::array();
			for(size_t i = n + 1; i < _name.size(); ++i)
				t.push(static_cast<int>(_name[i]));
			_v.set("nameTail", std::move(t));
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

			In child(const char* _key) const
			{
				const auto* v = get(_key);
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
			const Value& value() const { return m_v; }

			// An integer member in [_min, _max].
			template<typename T>
			bool integer(const char* _key, T& _out, const double _min, const double _max,
				const bool _required = true) const
			{
				const auto* v = get(_key, _required);
				if(!v)
					return false;
				return toInteger(*v, path(_key), _out, _min, _max);
			}

			template<typename T>
			bool toInteger(const Value& _v, const std::string& _path, T& _out, const double _min,
				const double _max) const
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
						+ std::to_string(static_cast<long long>(_min)) + ".."
						+ std::to_string(static_cast<long long>(_max)));
					return false;
				}
				_out = static_cast<T>(n);
				return true;
			}

			// An array of exactly _n integers in [_min, _max].
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

			// A set of 0-based steps below _count.
			bool stepSet(const char* _key, uint64_t& _out, const size_t _count) const
			{
				const auto* v = get(_key);
				if(!v)
					return false;
				if(!v->isArray())
				{
					m_errors.push_back(path(_key) + ": expected an array of steps");
					return false;
				}
				_out = 0;
				for(size_t i = 0; i < v->asArray().size(); ++i)
				{
					size_t s = 0;
					if(toInteger(v->asArray()[i], path(_key) + "[" + std::to_string(i) + "]", s, 0,
						static_cast<double>(_count - 1)))
						_out |= uint64_t{1} << s;
				}
				return true;
			}

			// null = none, else an integer in range.
			bool optionalGroup(const char* _key, uint8_t& _out) const
			{
				const auto* v = get(_key);
				if(!v)
					return false;
				if(v->isNull())
				{
					_out = MdKit::g_noGroup;
					return true;
				}
				return toInteger(*v, path(_key), _out, 0, 15);
			}

			bool name(std::array<uint8_t, 16>& _out) const
			{
				const auto* n = get("name");
				if(!n)
					return false;
				if(!n->isString() || n->asString().size() > 16)
				{
					error("name must be a string of at most 16 characters");
					return false;
				}
				_out.fill(0);
				const auto& s = n->asString();
				for(size_t i = 0; i < s.size(); ++i)
				{
					const auto c = static_cast<unsigned char>(s[i]);
					if(c == 0 || c > 0x7f)
					{
						error("name characters must be 7-bit and not NUL");
						return false;
					}
					_out[i] = c;
				}
				const auto* tail = get("nameTail", false);
				if(!tail)
					return true;
				const auto room = s.size() < 16 ? 16 - s.size() - 1 : 0;
				if(!tail->isArray() || tail->asArray().size() > room)
				{
					error("nameTail must be an array of at most " + std::to_string(room) + " bytes");
					return false;
				}
				for(size_t i = 0; i < tail->asArray().size(); ++i)
					toInteger(tail->asArray()[i], path("nameTail"), _out[s.size() + 1 + i], 0, 127);
				return true;
			}

			void schema(const char* _schema) const
			{
				const auto* s = get("schema");
				if(s && (!s->isString() || s->asString() != _schema))
					error(std::string("schema must be \"") + _schema + "\"");
				int version = 0;
				if(integer("version", version, 1, 1e9) && version != g_layoutVersion)
					error("unsupported contract version " + std::to_string(version));
			}

		private:
			static const Value g_null;

			const Value& m_v;
			std::string m_path;
			std::vector<std::string>& m_errors;
		};

		const Value In::g_null;

		template<typename T>
		std::optional<T> finish(T&& _value, const size_t _errorsBefore, std::vector<std::string>& _errors)
		{
			if(_errors.size() != _errorsBefore)
				return {};
			for(auto& problem : validate(_value))
				_errors.push_back("$: " + problem);
			if(_errors.size() != _errorsBefore)
				return {};
			return std::move(_value);
		}

		Value stepFlags(const uint32_t _editAll, const uint64_t _steps, const size_t _count)
		{
			Value v = Value::object();
			v.set("editAll", static_cast<unsigned>(_editAll));
			v.set("steps", steps(_steps, _count));
			return v;
		}

		void readStepFlags(const In& _in, const char* _key, uint32_t& _editAll, uint64_t& _steps, const size_t _count)
		{
			const auto f = _in.child(_key);
			f.integer("editAll", _editAll, 0, 1);
			f.stepSet("steps", _steps, _count);
		}
	}

	std::string mdPatternName(const unsigned _slot)
	{
		char text[8];
		std::snprintf(text, sizeof(text), "%c%02u", 'A' + static_cast<char>((_slot >> 4) & 7), (_slot & 15) + 1);
		return text;
	}

	// ---- pattern ----

	namespace
	{
		// Lock steps the UI sees: the pattern's total length, within the dump.
		size_t visibleLockSteps(const MdPattern& _p)
		{
			const size_t count = _p.extended ? 64 : 32;
			const size_t total = 16 * (static_cast<size_t>(_p.scale) + 1);
			return total < count ? total : count;
		}

		// Pool bytes outside the semantic view: unused rows (every step) and used
		// rows beyond the visible steps. Factory patterns leave 0x00/0xff residue
		// there; firmware edits leave unused rows zero.
		template<typename Visit>
		void forEachHiddenLockByte(const MdPattern& _p, Visit _visit)
		{
			const size_t count = _p.extended ? 64 : 32;
			const auto used = usedLockRows(_p);
			const auto visible = visibleLockSteps(_p);
			for(size_t r = 0; r < MdPattern::g_lockRows; ++r)
				for(size_t s = r < used ? visible : 0; s < count; ++s)
					_visit(r, s);
		}

		std::vector<uint8_t> hiddenLockBytes(const MdPattern& _p)
		{
			std::vector<uint8_t> bytes;
			forEachHiddenLockByte(_p, [&](const size_t _r, const size_t _s) { bytes.push_back(_p.lockRows[_r][_s]); });
			return bytes;
		}

		MdPattern withCanonicalHiddenLocks(MdPattern _p)
		{
			const auto used = usedLockRows(_p);
			forEachHiddenLockByte(_p, [&](const size_t _r, const size_t _s)
			{
				_p.lockRows[_r][_s] = _r < used ? MdPattern::g_noLock : 0;
			});
			return _p;
		}
	}

	Value patternToJsonV1(const MdPattern& _p)
	{
		const size_t count = _p.extended ? 64 : 32;
		auto v = header("md-desk/pattern", _p.position);
		v.set("name", mdPatternName(_p.position));
		auto f = format(_p.version, _p.revision);
		f.set("extended", _p.extended);
		v.set("format", std::move(f));
		v.set("length", static_cast<int>(_p.length));
		v.set("totalLength", 16 * (static_cast<int>(_p.scale) + 1));
		v.set("tempoMultiplier", _p.tempoMultiplier < g_tempoMultipliers.size()
			? Value(g_tempoMultipliers[_p.tempoMultiplier]) : Value(static_cast<int>(_p.tempoMultiplier)));
		v.set("kit", static_cast<int>(_p.kit));
		v.set("accentAmount", static_cast<int>(_p.accentAmount));
		v.set("swingAmount", static_cast<unsigned>(_p.swingAmount));
		v.set("lockedRowsField", static_cast<int>(_p.lockedRows));
		v.set("accent", stepFlags(_p.accentEditAll, _p.accentPattern, count));
		v.set("slide", stepFlags(_p.slideEditAll, _p.slidePattern, count));
		v.set("swing", stepFlags(_p.swingEditAll, _p.swingPattern, count));

		Value tracks = Value::array();
		for(size_t t = 0; t < MdPattern::g_tracks; ++t)
		{
			Value track = Value::object();
			track.set("trigs", steps(_p.trigs[t], count));
			track.set("accent", steps(_p.trackAccent[t], count));
			track.set("slide", steps(_p.trackSlide[t], count));
			track.set("swing", steps(_p.trackSwing[t], count));
			tracks.push(std::move(track));
		}
		v.set("tracks", std::move(tracks));

		const auto visible = visibleLockSteps(_p);
		Value locks = Value::array();
		for(size_t t = 0; t < MdPattern::g_tracks; ++t)
		{
			for(size_t param = 0; param < 32; ++param)
			{
				const auto row = lockRowIndex(_p, t, param);
				if(!row || *row >= MdPattern::g_lockRows)
					continue;
				Value values = Value::array();
				for(size_t s = 0; s < visible; ++s)
				{
					const auto value = _p.lockRows[*row][s];
					if(value != MdPattern::g_noLock)
						values.push(Value(Value::Array{static_cast<int>(s), static_cast<int>(value)}));
				}
				Value lock = Value::object();
				lock.set("track", static_cast<int>(t));
				lock.set("param", static_cast<int>(param));
				lock.set("steps", std::move(values));
				locks.push(std::move(lock));
			}
		}
		v.set("locks", std::move(locks));

		const auto hidden = hiddenLockBytes(_p);
		if(hidden != hiddenLockBytes(withCanonicalHiddenLocks(_p)))
		{
			// Run-length [count, byte] pairs, pool order (row-major).
			Value rle = Value::array();
			for(size_t i = 0; i < hidden.size();)
			{
				size_t n = 1;
				while(i + n < hidden.size() && hidden[i + n] == hidden[i])
					++n;
				rle.push(Value(Value::Array{static_cast<int>(n), static_cast<int>(hidden[i])}));
				i += n;
			}
			v.set("lockPoolHidden", std::move(rle));
		}
		return v;
	}

	std::optional<MdPattern> patternFromJsonV1(const Value& _json, std::vector<std::string>& _errors)
	{
		const auto before = _errors.size();
		const In in(_json, "$", _errors);
		in.schema("md-desk/pattern");

		MdPattern p;
		in.integer("slot", p.position, 0, 127);
		const auto f = in.child("format");
		f.integer("version", p.version, 0, 127);
		f.integer("revision", p.revision, 0, 127);
		if(const auto* e = f.get("extended"))
		{
			if(e->isBool())
				p.extended = e->asBool();
			else
				f.error("extended must be true or false");
		}
		const size_t count = p.extended ? 64 : 32;

		in.integer("length", p.length, 1, 64);
		int total = 0;
		if(in.integer("totalLength", total, 16, 64))
		{
			if(total % 16)
				in.error("totalLength must be 16, 32, 48 or 64");
			p.scale = static_cast<uint8_t>(total / 16 - 1);
		}
		if(const auto* m = in.get("tempoMultiplier"))
		{
			bool found = false;
			for(size_t i = 0; i < g_tempoMultipliers.size(); ++i)
			{
				if(m->isString() && m->asString() == g_tempoMultipliers[i])
				{
					p.tempoMultiplier = static_cast<uint8_t>(i);
					found = true;
				}
			}
			if(!found)
				in.error("tempoMultiplier must be one of 1X, 2X, 3/4X, 3/2X");
		}
		in.integer("kit", p.kit, 0, 63);
		in.integer("accentAmount", p.accentAmount, 0, 127);
		in.integer("swingAmount", p.swingAmount, 0, 16383);
		in.integer("lockedRowsField", p.lockedRows, 0, 127);
		readStepFlags(in, "accent", p.accentEditAll, p.accentPattern, count);
		readStepFlags(in, "slide", p.slideEditAll, p.slidePattern, count);
		readStepFlags(in, "swing", p.swingEditAll, p.swingPattern, count);

		const auto tracks = in.child("tracks");
		if(tracks.size() != MdPattern::g_tracks)
			tracks.error("expected 16 tracks");
		for(size_t t = 0; t < MdPattern::g_tracks && t < tracks.size(); ++t)
		{
			const auto track = tracks.element(t);
			track.stepSet("trigs", p.trigs[t], count);
			track.stepSet("accent", p.trackAccent[t], count);
			track.stepSet("slide", p.trackSlide[t], count);
			track.stepSet("swing", p.trackSwing[t], count);
		}

		// Masks first, then rows in the firmware's canonical (track, param) order.
		const auto locks = in.child("locks");
		if(!locks.value().isArray())
			locks.error("expected an array");
		std::set<std::pair<size_t, size_t>> seen;
		for(size_t i = 0; i < locks.size(); ++i)
		{
			const auto lock = locks.element(i);
			size_t track = 0, param = 0;
			if(!lock.integer("track", track, 0, 15) || !lock.integer("param", param, 0, 23))
				continue;
			if(!seen.insert({track, param}).second)
				lock.error("duplicate lock for this track and parameter");
			p.lockMasks[track] |= 1u << param;
		}
		if(seen.size() > MdPattern::g_lockRows)
			locks.error(std::to_string(seen.size()) + " locked parameters, the limit is 64");
		else
		{
			for(size_t i = 0; i < locks.size(); ++i)
			{
				const auto lock = locks.element(i);
				size_t track = 0, param = 0;
				if(!lock.integer("track", track, 0, 15) || !lock.integer("param", param, 0, 23))
					continue;
				const auto row = lockRowIndex(p, track, param);
				if(!row)
					continue;
				auto& values = p.lockRows[*row];
				for(size_t s = 0; s < count; ++s)
					values[s] = MdPattern::g_noLock;
				const auto visible = visibleLockSteps(p);
				const auto stepList = lock.child("steps");
				for(size_t k = 0; k < stepList.size(); ++k)
				{
					const auto& pair = stepList.element(k).value();
					if(!pair.isArray() || pair.asArray().size() != 2)
					{
						stepList.error("each lock step is [step, value]");
						continue;
					}
					size_t step = 0;
					uint8_t value = 0;
					if(lock.toInteger(pair.asArray()[0], lock.path("steps"), step, 0, static_cast<double>(visible) - 1)
						&& lock.toInteger(pair.asArray()[1], lock.path("steps"), value, 0, 127))
						values[step] = value;
				}
			}
		}
		p = withCanonicalHiddenLocks(p);
		if(const auto* hidden = in.get("lockPoolHidden", false))
		{
			std::vector<uint8_t> bytes;
			for(const auto& pair : hidden->isArray() ? hidden->asArray() : Value::Array{})
			{
				size_t n = 0;
				uint8_t b = 0;
				if(!pair.isArray() || pair.asArray().size() != 2
					|| !in.toInteger(pair.asArray()[0], in.path("lockPoolHidden"), n, 1, 4096)
					|| !in.toInteger(pair.asArray()[1], in.path("lockPoolHidden"), b, 0, 255))
					break;
				bytes.insert(bytes.end(), n, b);
			}
			// Only meaningful for the lock layout it was taken from: after an edit
			// that adds or removes a locked parameter the residue is dropped.
			if(bytes.size() == hiddenLockBytes(p).size())
			{
				size_t i = 0;
				forEachHiddenLockByte(p, [&](const size_t _r, const size_t _s) { p.lockRows[_r][_s] = bytes[i++]; });
			}
		}
		return finish(std::move(p), before, _errors);
	}

	// ---- kit ----

	Value kitToJsonV1(const MdKit& _k)
	{
		auto v = header("md-desk/kit", _k.position);
		v.set("format", format(_k.version, _k.revision));
		setName(v, _k.name);

		Value tracks = Value::array();
		for(size_t t = 0; t < MdKit::g_tracks; ++t)
		{
			Value track = Value::object();
			track.set("model", static_cast<unsigned>(_k.models[t]));
			track.set("machine", mdMachineName(_k.models[t]));
			track.set("level", static_cast<int>(_k.levels[t]));
			track.set("synth", bytes<8>(&_k.params[t][0]));
			track.set("effects", bytes<8>(&_k.params[t][8]));
			track.set("routing", bytes<8>(&_k.params[t][16]));
			const auto& l = _k.lfos[t];
			Value lfo = Value::object();
			lfo.set("track", static_cast<int>(l.track));
			lfo.set("param", static_cast<int>(l.param));
			lfo.set("shape1", static_cast<int>(l.shape1));
			lfo.set("shape2", static_cast<int>(l.shape2));
			lfo.set("update", static_cast<int>(l.update));
			lfo.set("state", hex(l.state.data(), l.state.size()));
			track.set("lfo", std::move(lfo));
			track.set("trigGroup", group(_k.trigGroups[t]));
			track.set("muteGroup", group(_k.muteGroups[t]));
			tracks.push(std::move(track));
		}
		v.set("tracks", std::move(tracks));

		Value fx = Value::object();
		for(size_t i = 0; i < MdKit::MasterFxCount; ++i)
			fx.set(g_fxNames[i], bytes(_k.masterFx[i]));
		v.set("masterFx", std::move(fx));
		return v;
	}

	std::optional<MdKit> kitFromJsonV1(const Value& _json, std::vector<std::string>& _errors)
	{
		const auto before = _errors.size();
		const In in(_json, "$", _errors);
		in.schema("md-desk/kit");

		MdKit k;
		in.integer("slot", k.position, 0, 63);
		const auto f = in.child("format");
		f.integer("version", k.version, 0, 127);
		f.integer("revision", k.revision, 0, 127);
		in.name(k.name);

		const auto tracks = in.child("tracks");
		if(tracks.size() != MdKit::g_tracks)
			tracks.error("expected 16 tracks");
		for(size_t t = 0; t < MdKit::g_tracks && t < tracks.size(); ++t)
		{
			const auto track = tracks.element(t);
			track.integer("model", k.models[t], 0, 255);
			track.integer("level", k.levels[t], 0, 127);
			std::array<uint8_t, 8> page{};
			for(size_t pg = 0; pg < 3; ++pg)
			{
				track.integers(pg == 0 ? "synth" : pg == 1 ? "effects" : "routing", page, 0, 127);
				for(size_t i = 0; i < 8; ++i)
					k.params[t][pg * 8 + i] = page[i];
			}
			const auto lfo = track.child("lfo");
			auto& l = k.lfos[t];
			lfo.integer("track", l.track, 0, 15);
			lfo.integer("param", l.param, 0, 23);
			lfo.integer("shape1", l.shape1, 0, 5);
			lfo.integer("shape2", l.shape2, 0, 5);
			lfo.integer("update", l.update, 0, 2);
			if(const auto* s = lfo.get("state"))
			{
				const auto& text = s->isString() ? s->asString() : std::string();
				if(text.size() != l.state.size() * 2)
					lfo.error("state must be 62 hex digits");
				else
					for(size_t i = 0; i < l.state.size(); ++i)
						l.state[i] = static_cast<uint8_t>(std::strtoul(text.substr(i * 2, 2).c_str(), nullptr, 16));
			}
			track.optionalGroup("trigGroup", k.trigGroups[t]);
			track.optionalGroup("muteGroup", k.muteGroups[t]);
		}

		const auto fx = in.child("masterFx");
		for(size_t i = 0; i < MdKit::MasterFxCount; ++i)
			fx.integers(g_fxNames[i], k.masterFx[i], 0, 127);
		return finish(std::move(k), before, _errors);
	}

	// ---- song ----

	namespace
	{
		const char* rowKindName(const MdSongRowKind _kind)
		{
			switch(_kind)
			{
			case MdSongRowKind::Pattern: return "pattern";
			case MdSongRowKind::Loop: return "loop";
			case MdSongRowKind::Jump: return "jump";
			case MdSongRowKind::Halt: return "halt";
			case MdSongRowKind::End: return "end";
			case MdSongRowKind::Invalid: return "invalid";
			}
			return "invalid";
		}

		// The bytes the firmware writes for a row of this kind when only its
		// meaningful fields are set (observed on firmware dumps).
		MdSongRow canonicalRow(const MdSongRow& _r, const MdSongRowKind _kind)
		{
			MdSongRow c;
			switch(_kind)
			{
			case MdSongRowKind::Pattern:
				c = _r;
				c.reserved = 0;
				c.target = 0;
				break;
			case MdSongRowKind::Loop:
			case MdSongRowKind::Jump:
			case MdSongRowKind::Halt:
				c.pattern = MdSongRow::g_loopRow;
				c.repeats = _kind == MdSongRowKind::Loop ? _r.repeats : 0;
				c.target = _r.target;
				c.end = 0;
				break;
			case MdSongRowKind::End:
			case MdSongRowKind::Invalid:
				c = MdSongRow{};
				break;
			}
			return c;
		}

		Value tempoJson(const uint16_t _tempo)
		{
			return _tempo == MdSongRow::g_noTempo ? Value() : Value(_tempo / 24.0);
		}

		bool readTempo(const In& _in, const char* _key, uint16_t& _out, const bool _nullable)
		{
			const auto* t = _in.get(_key);
			if(!t)
				return false;
			if(t->isNull() && _nullable)
			{
				_out = MdSongRow::g_noTempo;
				return true;
			}
			if(!t->isNumber() || t->asNumber() < 30 || t->asNumber() > 300)
			{
				_in.error(std::string(_key) + " must be a BPM from 30 to 300" + (_nullable ? " or null" : ""));
				return false;
			}
			_out = static_cast<uint16_t>(std::lround(t->asNumber() * 24.0));
			return true;
		}

		Value trackList(const uint16_t _mask)
		{
			Value a = Value::array();
			for(int t = 0; t < 16; ++t)
				if((_mask >> t) & 1)
					a.push(t);
			return a;
		}
	}

	Value songToJsonV1(const MdSong& _s)
	{
		auto v = header("md-desk/song", _s.position);
		v.set("format", format(_s.version, _s.revision));
		setName(v, _s.name);
		Value rows = Value::array();
		for(size_t i = 0; i < _s.rows.size(); ++i)
		{
			const auto& r = _s.rows[i];
			const auto kind = songRowKind(r, i);
			Value row = Value::object();
			row.set("kind", rowKindName(kind));
			switch(kind)
			{
			case MdSongRowKind::Pattern:
				row.set("pattern", static_cast<int>(r.pattern));
				row.set("repeats", static_cast<int>(r.repeats));
				row.set("start", static_cast<int>(r.start));
				row.set("end", static_cast<int>(r.end));
				row.set("tempo", tempoJson(r.tempo));
				row.set("mutes", trackList(r.mutes));
				break;
			case MdSongRowKind::Loop:
				row.set("target", static_cast<int>(r.target));
				row.set("repeats", static_cast<int>(r.repeats));
				break;
			case MdSongRowKind::Jump:
				row.set("target", static_cast<int>(r.target));
				break;
			case MdSongRowKind::Halt:
			case MdSongRowKind::End:
			case MdSongRowKind::Invalid:
				break;
			}
			// Lossless escape hatch: the row's ten bytes when the kind's fields
			// alone would not rebuild it.
			if(canonicalRow(r, kind) != r)
			{
				row.set("raw", Value(Value::Array{static_cast<int>(r.pattern), static_cast<int>(r.reserved),
					static_cast<int>(r.repeats), static_cast<int>(r.target), static_cast<int>(r.mutes),
					static_cast<int>(r.tempo), static_cast<int>(r.start), static_cast<int>(r.end)}));
			}
			rows.push(std::move(row));
		}
		v.set("rows", std::move(rows));
		return v;
	}

	std::optional<MdSong> songFromJsonV1(const Value& _json, std::vector<std::string>& _errors)
	{
		const auto before = _errors.size();
		const In in(_json, "$", _errors);
		in.schema("md-desk/song");

		MdSong s;
		in.integer("slot", s.position, 0, 31);
		const auto f = in.child("format");
		f.integer("version", s.version, 0, 127);
		f.integer("revision", s.revision, 0, 127);
		in.name(s.name);

		const auto rows = in.child("rows");
		if(!rows.value().isArray())
			rows.error("expected an array");
		s.rows.clear();
		for(size_t i = 0; i < rows.size(); ++i)
		{
			const auto row = rows.element(i);
			MdSongRow r;
			if(const auto* raw = row.get("raw", false))
			{
				std::array<uint16_t, 8> b{};
				if(!raw->isArray() || raw->asArray().size() != 8)
					row.error("raw must hold 8 numbers");
				else
					for(size_t k = 0; k < 8; ++k)
						row.toInteger(raw->asArray()[k], row.path("raw"), b[k], 0, k == 4 || k == 5 ? 65535 : 255);
				r = MdSongRow{static_cast<uint8_t>(b[0]), static_cast<uint8_t>(b[1]), static_cast<uint8_t>(b[2]),
					static_cast<uint8_t>(b[3]), b[4], b[5], static_cast<uint8_t>(b[6]), static_cast<uint8_t>(b[7])};
			}
			const auto* kind = row.get("kind");
			const std::string k = kind && kind->isString() ? kind->asString() : std::string();
			if(k == "pattern")
			{
				row.integer("pattern", r.pattern, 0, 127);
				row.integer("repeats", r.repeats, 0, 63);
				row.integer("start", r.start, 0, 63);
				row.integer("end", r.end, 1, 64);
				readTempo(row, "tempo", r.tempo, true);
				uint64_t mutes = 0;
				row.stepSet("mutes", mutes, 16);
				r.mutes = static_cast<uint16_t>(mutes);
			}
			else if(k == "loop" || k == "jump" || k == "halt")
			{
				if(!row.get("raw", false))
					r = canonicalRow(MdSongRow{}, MdSongRowKind::Halt);
				r.pattern = MdSongRow::g_loopRow;
				if(k == "loop")
				{
					row.integer("target", r.target, 0, static_cast<double>(i) - 1);
					row.integer("repeats", r.repeats, 0, 63);
				}
				else if(k == "jump")
				{
					row.integer("target", r.target, static_cast<double>(i) + 1, 255);
					r.repeats = 0;
				}
				else
				{
					r.target = static_cast<uint8_t>(i);
					r.repeats = 0;
				}
			}
			else if(k == "end")
			{
				if(!row.get("raw", false))
					r = MdSongRow{};
				r.pattern = MdSongRow::g_endRow;
			}
			else
				row.error("kind must be pattern, loop, jump, halt or end");
			s.rows.push_back(r);
		}
		return finish(std::move(s), before, _errors);
	}

	// ---- global ----

	Value globalToJsonV1(const MdGlobal& _g)
	{
		auto v = header("md-desk/global", _g.position);
		v.set("format", format(_g.version, _g.revision));
		Value routing = Value::array();
		for(const auto r : _g.routing)
			routing.push(r < g_outputs.size() ? Value(g_outputs[r]) : Value(static_cast<int>(r)));
		v.set("routing", std::move(routing));
		v.set("tempo", _g.tempo / 24.0);
		v.set("extendedMode", _g.extendedMode <= 1 ? Value(_g.extendedMode == 1)
			: Value(static_cast<int>(_g.extendedMode)));
		v.set("baseChannel", static_cast<int>(_g.baseChannel));
		Value keymap = Value::array();
		for(const auto k : _g.keymap)
			keymap.push(k == MdGlobal::g_unmapped ? Value() : Value(static_cast<int>(k)));
		v.set("keymap", std::move(keymap));
		Value other = Value::object();
		other.set("unused", static_cast<int>(_g.unused));
		other.set("syncFlags", static_cast<int>(_g.syncFlags));
		other.set("localControl", static_cast<int>(_g.localControl));
		other.set("inputSettings", bytes(_g.inputSettings));
		other.set("programChange", static_cast<int>(_g.programChange));
		other.set("trigMode", static_cast<int>(_g.trigMode));
		v.set("settings", std::move(other));
		// P5: the measured meaning of the settings, derived, read-only (settings is the value).
		namespace b = mdGlobalBits;
		Value c = Value::object();
		c.set("tempoIn", _g.syncFlags & b::g_tempoInExternal ? "external" : "internal");
		c.set("ctrlIn", !(_g.syncFlags & b::g_ctrlInOff));
		c.set("tempoOut", (_g.syncFlags & b::g_tempoOut) != 0);
		c.set("ctrlOut", (_g.syncFlags & b::g_ctrlOut) != 0);
		c.set("programChangeIn", (_g.programChange & b::g_programChangeIn) != 0);
		c.set("programChangeOut", (_g.programChange & b::g_programChangeOut) != 0);
		c.set("programChangeChannel", (_g.programChange >> 2) ? Value(static_cast<int>(_g.programChange >> 2)) : Value());
		c.set("trigMode", _g.trigMode == 0 ? Value("gate") : _g.trigMode == 1 ? Value("start") : _g.trigMode == 2 ? Value("que")
			: Value(static_cast<int>(_g.trigMode)));
		c.set("localControl", _g.localControl != 0);
		v.set("control", std::move(c));
		return v;
	}

	std::optional<MdGlobal> globalFromJsonV1(const Value& _json, std::vector<std::string>& _errors)
	{
		const auto before = _errors.size();
		const In in(_json, "$", _errors);
		in.schema("md-desk/global");

		MdGlobal g;
		in.integer("slot", g.position, 0, 7);
		const auto f = in.child("format");
		f.integer("version", g.version, 0, 127);
		f.integer("revision", g.revision, 0, 127);

		const auto routing = in.child("routing");
		if(routing.size() != MdGlobal::g_tracks)
			routing.error("expected 16 outputs");
		for(size_t t = 0; t < MdGlobal::g_tracks && t < routing.size(); ++t)
		{
			const auto& r = routing.element(t).value();
			bool found = false;
			for(size_t o = 0; o < g_outputs.size(); ++o)
			{
				if(r.isString() && r.asString() == g_outputs[o])
				{
					g.routing[t] = static_cast<uint8_t>(o);
					found = true;
				}
			}
			if(!found)
				routing.element(t).error("output must be A-F or MAIN");
		}
		readTempo(in, "tempo", g.tempo, false);
		if(const auto* e = in.get("extendedMode"))
		{
			if(e->isBool())
				g.extendedMode = e->asBool() ? 1 : 0;
			else
				in.error("extendedMode must be true or false");
		}
		in.integer("baseChannel", g.baseChannel, 0, 127);
		const auto keymap = in.child("keymap");
		if(keymap.size() != g.keymap.size())
			keymap.error("expected 128 entries");
		for(size_t n = 0; n < g.keymap.size() && n < keymap.size(); ++n)
		{
			const auto& k = keymap.element(n).value();
			if(k.isNull())
				g.keymap[n] = MdGlobal::g_unmapped;
			else
				keymap.toInteger(k, keymap.path(std::to_string(n)), g.keymap[n], 0, 31);
		}
		const auto other = in.child("settings");
		other.integer("unused", g.unused, 0, 127);
		other.integer("syncFlags", g.syncFlags, 0, 127);
		other.integer("localControl", g.localControl, 0, 127);
		other.integers("inputSettings", g.inputSettings, 0, 127);
		other.integer("programChange", g.programChange, 0, 127);
		other.integer("trigMode", g.trigMode, 0, 127);
		return finish(std::move(g), before, _errors);
	}

	// ---- contract version 2: the firmware's pass-through fields under "firmware" ----

	namespace
	{
		const json::FirmwareLayout g_patternFw{{"format", "lockedRowsField", "lockPoolHidden"}, {}, {}};
		const json::FirmwareLayout g_kitFw{{"format", "nameTail"}, {{"lfo.state", "lfoState"}}, {}};
		const json::FirmwareLayout g_songFw{{"format", "nameTail"}, {}, {}};
		const json::FirmwareLayout g_globalFw{{"format"}, {}, {}};
	}

	Value patternToJson(const MdPattern& _p) { return json::groupFirmware(patternToJsonV1(_p), g_patternFw, g_mdContractVersion); }
	Value kitToJson(const MdKit& _k) { return json::groupFirmware(kitToJsonV1(_k), g_kitFw, g_mdContractVersion); }
	Value songToJson(const MdSong& _s) { return json::groupFirmware(songToJsonV1(_s), g_songFw, g_mdContractVersion); }
	Value globalToJson(const MdGlobal& _g) { return json::groupFirmware(globalToJsonV1(_g), g_globalFw, g_mdContractVersion); }

	std::optional<MdPattern> patternFromJson(const Value& _json, std::vector<std::string>& _errors)
	{
		return patternFromJsonV1(json::ungroupFirmware(_json, g_patternFw, g_mdContractVersion, g_layoutVersion), _errors);
	}
	std::optional<MdKit> kitFromJson(const Value& _json, std::vector<std::string>& _errors)
	{
		return kitFromJsonV1(json::ungroupFirmware(_json, g_kitFw, g_mdContractVersion, g_layoutVersion), _errors);
	}
	std::optional<MdSong> songFromJson(const Value& _json, std::vector<std::string>& _errors)
	{
		return songFromJsonV1(json::ungroupFirmware(_json, g_songFw, g_mdContractVersion, g_layoutVersion), _errors);
	}
	std::optional<MdGlobal> globalFromJson(const Value& _json, std::vector<std::string>& _errors)
	{
		return globalFromJsonV1(json::ungroupFirmware(_json, g_globalFw, g_mdContractVersion, g_layoutVersion), _errors);
	}
}
