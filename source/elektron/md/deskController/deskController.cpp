#include "deskController.h"

#include "tr06Json.h"

#include "elektronData/mdGlobal.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mmGlobal.h"
#include "elektronData/mmMachines.h"

#include <algorithm>
#include <cassert>

namespace deskController
{
	namespace
	{
		int intOf(const Value& _v, const char* _key, const int _default)
		{
			const auto* v = _v.find(_key);
			return v && v->isNumber() ? static_cast<int>(v->asNumber()) : _default;
		}

		std::string textOf(const Value& _v, const char* _key)
		{
			const auto* v = _v.find(_key);
			return v && v->isString() ? v->asString() : std::string();
		}

		size_t index(const Machine _m) { return _m == Machine::Md ? 0 : 1; }

		Profile loadTr06()
		{
			const auto doc = elektronData::json::parse(g_tr06Json);
			assert(doc && "tr06.json does not parse");
			Profile p;
			if(!doc)
				return p;
			p.id = textOf(*doc, "id");
			p.label = textOf(*doc, "label");
			p.about = textOf(*doc, "about");
			p.channel = static_cast<uint8_t>(intOf(*doc, "channel", 10) - 1);
			if(const auto* voices = doc->find("voices"); voices && voices->isArray())
				for(size_t v = 0; v < g_voices && v < voices->asArray().size(); ++v)
				{
					const auto& e = voices->asArray()[v];
					p.voices[v].id = textOf(e, "voice");
					p.voices[v].label = textOf(e, "label");
					if(const auto* notes = e.find("notes"); notes && notes->isArray())
						for(const auto& n : notes->asArray())
							p.voices[v].notes.push_back(static_cast<uint8_t>(static_cast<int>(n.asNumber()) & 0x7f));
				}
			if(const auto* knobs = doc->find("knobs"); knobs && knobs->isArray())
				for(const auto& e : knobs->asArray())
					p.knobs.push_back({static_cast<uint8_t>(intOf(e, "cc", 0) & 0x7f), textOf(e, "label")});
			if(const auto* blocked = doc->find("blocked"))
			{
				if(const auto* cc = blocked->find("cc"); cc && cc->isArray())
					for(const auto& c : cc->asArray())
						p.blockedCc[static_cast<size_t>(static_cast<int>(c.asNumber()) & 0x7f)] = true;
				if(const auto* rt = blocked->find("realtime"); rt && rt->isArray())
					for(const auto& c : rt->asArray())
						p.blockedRealtime.push_back(static_cast<uint8_t>(c.asNumber()));
			}
			if(const auto* d = doc->find("defaults"))
				for(const auto m : {Machine::Md, Machine::Mm})
					if(const auto* e = d->find(machineName(m)))
					{
						p.defaults[index(m)] = *e;
						p.defaultsAbout[index(m)] = textOf(*e, "about");
					}
			return p;
		}

		int voiceIndex(const std::string& _id)
		{
			const auto& p = tr06();
			for(size_t v = 0; v < g_voices; ++v)
				if(p.voices[v].id == _id)
					return static_cast<int>(v);
			return -1;
		}

		bool isKnob(const uint8_t _cc)
		{
			for(const auto& k : tr06().knobs)
				if(k.cc == _cc)
					return true;
			return false;
		}

		// The voices and knobs of a mapping document ({voices, knobs}) into _s; errors with JSON paths.
		void readMapping(const Value& _doc, const Machine _m, Setup& _s, std::vector<std::string>& _errors)
		{
			const int tracks = tracksOf(_m);
			if(const auto* voices = _doc.find("voices"))
			{
				if(!voices->isArray())
					_errors.emplace_back("$.voices: expected an array");
				else
					for(size_t n = 0; n < voices->asArray().size(); ++n)
					{
						const auto& e = voices->asArray()[n];
						const auto at = "$.voices[" + std::to_string(n) + "]";
						const int v = voiceIndex(textOf(e, "voice"));
						const int t = intOf(e, "t", -1);
						const int note = intOf(e, "note", -1);
						if(v < 0)
							_errors.push_back(at + ".voice: not a voice of the controller");
						else if(t < 0 || t >= tracks)
							_errors.push_back(at + ".t: expected a track 0-" + std::to_string(tracks - 1));
						else if(_m == Machine::Mm && (note < 0 || note > 127))
							_errors.push_back(at + ".note: expected a note 0-127");
						else
							_s.voices[static_cast<size_t>(v)] = {t, _m == Machine::Mm ? note : -1};
					}
			}
			if(const auto* knobs = _doc.find("knobs"))
			{
				if(!knobs->isArray())
				{
					_errors.emplace_back("$.knobs: expected an array");
					return;
				}
				_s.knobs.fill({});
				for(size_t n = 0; n < knobs->asArray().size(); ++n)
				{
					const auto& e = knobs->asArray()[n];
					const auto at = "$.knobs[" + std::to_string(n) + "]";
					const int cc = intOf(e, "cc", -1);
					const auto* i = e.find("i");
					const Target t{_m == Machine::Md ? -1 : intOf(e, "pg", -1), i && i->isNumber() ? static_cast<int>(i->asNumber()) : -1};
					if(cc < 0 || cc > 127 || !isKnob(static_cast<uint8_t>(cc)))
						_errors.push_back(at + ".cc: not a knob of the controller");
					else if(t.mapped() && !validTarget(_m, t))
						_errors.push_back(at + ": not a parameter of the machine");
					else
						_s.knobs[static_cast<size_t>(cc)] = t;
				}
			}
		}
	}

	const char* machineName(const Machine _m) { return _m == Machine::Md ? "md" : "mm"; }
	int tracksOf(const Machine _m) { return _m == Machine::Md ? 16 : 6; }

	const Profile& tr06()
	{
		static const Profile p = loadTr06();
		return p;
	}

	const std::vector<TargetInfo>& targets(const Machine _m)
	{
		static const std::vector<TargetInfo> md = []
		{
			std::vector<TargetInfo> out;
			const auto names = elektronData::mdMachineParamNames(0);	// the effects and routing pages; synthesis by number
			for(int i = 0; i < 24; ++i)
			{
				std::string name = i < 8 ? "SYN " + std::to_string(i + 1) : std::string(names[static_cast<size_t>(i)] ? names[static_cast<size_t>(i)] : "");
				out.push_back({{-1, i}, name});
			}
			out.push_back({{-1, 24}, "LEVEL"});
			return out;
		}();
		static const std::vector<TargetInfo> mm = []
		{
			static const char* pages[7]{"SYN", "AMP", "FLTR", "EFX", "LFO1", "LFO2", "LFO3"};
			std::vector<TargetInfo> out;
			for(int pg = 0; pg < 7; ++pg)
				for(int i = 0; i < 8; ++i)
				{
					const std::string name = pg == 0 ? std::string(1, static_cast<char>('A' + i)) : std::string(elektronData::mmFixedPage(static_cast<uint8_t>(pg))[static_cast<size_t>(i)]);
					out.push_back({{pg, i}, std::string(pages[pg]) + " " + name});
				}
			out.push_back({{7, 0}, "LEVEL"});
			return out;
		}();
		return _m == Machine::Md ? md : mm;
	}

	bool validTarget(const Machine _m, const Target& _t)
	{
		if(_m == Machine::Md)
			return _t.pg == -1 && _t.i >= 0 && _t.i <= 24;
		return (_t.pg >= 0 && _t.pg <= 6 && _t.i >= 0 && _t.i <= 7) || (_t.pg == 7 && _t.i == 0);
	}

	std::string targetName(const Machine _m, const Target& _t)
	{
		for(const auto& t : targets(_m))
			if(t.at == _t)
				return t.name;
		return {};
	}

	Setup defaults(const Machine _m)
	{
		Setup s;
		s.channel = tr06().channel;
		for(size_t v = 0; v < g_voices; ++v)
			s.voices[v] = {static_cast<int>(v) % tracksOf(_m), _m == Machine::Mm ? 60 : -1};
		std::vector<std::string> errors;
		readMapping(tr06().defaults[index(_m)], _m, s, errors);
		assert(errors.empty() && "tr06.json: a default mapping does not validate");
		return s;
	}

	std::optional<Setup> setupFromJson(const Value& _doc, const Machine _m, std::vector<std::string>& _errors)
	{
		const auto before = _errors.size();
		if(textOf(_doc, "schema") != "desk/controller")
			_errors.emplace_back("$.schema: expected desk/controller");
		if(intOf(_doc, "version", 0) != 1)
			_errors.emplace_back("$.version: expected 1");
		if(textOf(_doc, "machine") != machineName(_m))
			_errors.push_back(std::string("$.machine: expected ") + machineName(_m));
		auto s = defaults(_m);
		const auto profile = textOf(_doc, "profile");
		if(profile != "off" && profile != tr06().id)
			_errors.emplace_back("$.profile: expected off or " + tr06().id);
		s.on = profile == tr06().id;
		const int ch = intOf(_doc, "channel", tr06().channel + 1);
		if(ch < 1 || ch > 16)
			_errors.emplace_back("$.channel: expected 1-16");
		else
			s.channel = static_cast<uint8_t>(ch - 1);
		readMapping(_doc, _m, s, _errors);
		if(_errors.size() != before)
			return std::nullopt;
		return s;
	}

	Value setupToJson(const Setup& _s, const Machine _m)
	{
		Value d = Value::object();
		d.set("schema", "desk/controller");
		d.set("version", 1);
		d.set("machine", machineName(_m));
		d.set("profile", _s.on ? tr06().id : std::string("off"));
		d.set("channel", _s.channel + 1);
		Value voices = Value::array();
		for(size_t v = 0; v < g_voices; ++v)
		{
			Value e = Value::object();
			e.set("voice", tr06().voices[v].id);
			e.set("t", _s.voices[v].t);
			if(_m == Machine::Mm)
				e.set("note", _s.voices[v].note);
			voices.push(std::move(e));
		}
		d.set("voices", std::move(voices));
		Value knobs = Value::array();
		for(const auto& k : tr06().knobs)
		{
			const auto& t = _s.knobs[k.cc];
			if(!t.mapped())
				continue;
			Value e = Value::object();
			e.set("cc", static_cast<int>(k.cc));
			if(_m == Machine::Mm)
				e.set("pg", t.pg);
			e.set("i", t.i);
			knobs.push(std::move(e));
		}
		d.set("knobs", std::move(knobs));
		return d;
	}

	Route mdRoute(const Setup& _s, const elektronData::MdGlobal* _global)
	{
		Route r;
		r.channel.fill(-1);
		r.note.fill(-1);
		if(!_global)
			return r;
		r.known = true;
		const int base = _global->baseChannel & 0x0f;
		for(int c = 0; c < 4 && base + c < 16; ++c)
			r.listens.emplace_back(base + c, "base channel " + std::to_string(base + 1) + "-" + std::to_string(std::min(base + 4, 16)));
		for(size_t v = 0; v < g_voices; ++v)
		{
			const int t = _s.voices[v].t;
			for(int n = 0; n < 128; ++n)
				if(_global->keymap[static_cast<size_t>(n)] == t)
				{
					r.channel[v] = base;
					r.note[v] = n;
					break;
				}
		}
		return r;
	}

	Route mmRoute(const Setup& _s, const elektronData::MmGlobal* _global)
	{
		Route r;
		r.channel.fill(-1);
		r.note.fill(-1);
		if(!_global)
			return r;
		r.known = true;
		const int base = _global->baseChannel & 0x0f;
		const int span = std::max(1, std::min<int>(_global->channelSpan, 6));
		for(int c = 0; c < span && base + c < 16; ++c)
			r.listens.emplace_back(base + c, "track " + std::to_string(c + 1));
		const auto extra = [&](const int _ch, const char* _why)
		{
			if(_ch >= 0 && _ch < 16)
				r.listens.emplace_back(_ch, _why);
		};
		extra(_global->autoChannel, "AUTO");
		extra(_global->multiTrigChannel, "MULTI TRIG");
		extra(_global->multiMapChannel, "MULTI MAP");
		for(size_t v = 0; v < g_voices; ++v)
		{
			const int ch = base + _s.voices[v].t;
			if(ch > 15 || _s.voices[v].note < 0)
				continue;
			r.channel[v] = ch;
			r.note[v] = _s.voices[v].note;
		}
		return r;
	}

	std::string overlapWarning(const Setup& _s, const Route& _r, const Machine _m)
	{
		std::string why;
		for(const auto& [ch, what] : _r.listens)
			if(ch == _s.channel)
				why += (why.empty() ? "" : ", ") + what;
		if(why.empty())
			return {};
		const char* machine = _m == Machine::Md ? "Machinedrum" : "Monomachine";
		return "Channel " + std::to_string(_s.channel + 1) + " is also the " + machine + "'s (" + why + "). The profile takes every message on it, so anything else sent on that channel no longer reaches the machine. Set the TR-06 or the machine to another channel.";
	}

	// ---- the input ----

	Input::Input()
	{
		for(size_t n = 0; n < 128; ++n)
		{
			m_noteOut[n].store(0, std::memory_order_relaxed);
			m_sounding[n].store(0, std::memory_order_relaxed);
			m_cc[n].store(CcBlock, std::memory_order_relaxed);
			m_latest[n].store(-1, std::memory_order_relaxed);
		}
	}

	void Input::configure(const Setup& _setup, const Route& _route, const Profile& _profile)
	{
		std::array<uint16_t, 128> notes{};
		for(size_t v = 0; v < g_voices; ++v)
		{
			if(!_route.known || _route.channel[v] < 0 || _route.note[v] < 0)
				continue;
			const auto out = static_cast<uint16_t>(g_route | (_route.channel[v] & 0x0f) << 8 | (_route.note[v] & 0x7f));
			for(const auto n : _profile.voices[v].notes)
				notes[n] = out;
		}
		for(size_t n = 0; n < 128; ++n)
		{
			m_noteOut[n].store(notes[n], std::memory_order_relaxed);
			m_cc[n].store(_setup.knobs[n].mapped() && !_profile.blockedCc[n] ? CcKnob : CcBlock, std::memory_order_relaxed);
		}
		bool sensing = false;
		for(const auto r : _profile.blockedRealtime)
			sensing |= r == 0xfe;
		m_blockSensing.store(sensing, std::memory_order_relaxed);
		m_channel.store(_setup.channel & 0x0f, std::memory_order_relaxed);
		m_on.store(_setup.on, std::memory_order_release);
	}

	Input::Result Input::translate(const uint8_t _a, const uint8_t _b, const uint8_t _c)
	{
		if(!isOn() || _a < 0x80)
			return {};
		if(_a >= 0xf0)
		{
			// Clock, Start, Continue, Stop and Song Position go on: the machine's own sync settings decide.
			if(_a == 0xfe && m_blockSensing.load(std::memory_order_relaxed))
			{
				m_blocked.fetch_add(1, std::memory_order_relaxed);
				return {Verdict::Block};
			}
			return {};
		}
		if((_a & 0x0f) != m_channel.load(std::memory_order_relaxed))
			return {};
		const uint8_t type = _a & 0xf0;
		const uint8_t data = _b & 0x7f;
		const auto block = [this]
		{
			m_blocked.fetch_add(1, std::memory_order_relaxed);
			return Result{Verdict::Block};
		};
		const auto note = [this](const uint16_t _out, const uint8_t _status, const uint8_t _velocity)
		{
			m_notes.fetch_add(1, std::memory_order_relaxed);
			return Result{Verdict::Note, static_cast<uint8_t>(_status | (_out >> 8 & 0x0f)), static_cast<uint8_t>(_out & 0x7f), _velocity};
		};
		switch(type)
		{
		case 0x90:
			if(_c > 0)
			{
				const auto out = m_noteOut[data].load(std::memory_order_relaxed);
				m_sounding[data].store(out, std::memory_order_relaxed);
				return out ? note(out, 0x90, _c & 0x7f) : block();
			}
			[[fallthrough]];
		case 0x80:
		{
			// The note-off goes where its note-on went, even when the mapping changed between them.
			auto out = m_sounding[data].exchange(0, std::memory_order_relaxed);
			if(!out)
				out = m_noteOut[data].load(std::memory_order_relaxed);
			return out ? note(out, type, _c & 0x7f) : block();
		}
		case 0xb0:
			if(m_cc[data].load(std::memory_order_relaxed) != CcKnob)
				return block();
			m_latest[data].store(static_cast<int16_t>(_c & 0x7f), std::memory_order_release);
			m_moved.store(true, std::memory_order_release);
			return {Verdict::Knob};
		default:
			return block();
		}
	}

	// ---- the knob pump ----

	void KnobPump::in(const uint8_t _track, const Target& _at, const uint8_t _value, const double _now)
	{
		if(_now - m_lastIn > g_quietMs)
		{
			++m_gesture;
			for(auto& [k, s] : m_slots)
				s.sent = -1;
		}
		m_lastIn = _now;
		auto& s = m_slots[{_track, _at}];
		s.pending = _value;
		s.gesture = m_gesture;
	}

	bool KnobPump::pending() const
	{
		for(const auto& [k, s] : m_slots)
			if(s.pending >= 0)
				return true;
		return false;
	}

	std::vector<Edit> KnobPump::take(const double _now)
	{
		std::vector<Edit> out;
		if(_now - m_lastOut < g_intervalMs)
			return out;
		for(auto& [k, s] : m_slots)
		{
			if(s.pending < 0)
				continue;
			if(s.pending != s.sent)
				out.push_back({k.track, k.at, static_cast<uint8_t>(s.pending), s.gesture});
			s.sent = s.pending;
			s.pending = -1;
		}
		if(!out.empty())
			m_lastOut = _now;
		return out;
	}

	// ---- the page's document ----

	Value pageDocument(const Setup& _s, const Machine _m, const Route& _r, const int _selected, const Value& _last)
	{
		const auto& p = tr06();
		Value d = Value::object();
		d.set("schema", "desk/controller");
		d.set("version", 1);
		d.set("machine", machineName(_m));
		d.set("profile", _s.on ? p.id : std::string("off"));
		Value profiles = Value::array();
		{
			Value off = Value::object();
			off.set("id", "off");
			off.set("label", "Off");
			profiles.push(std::move(off));
			Value on = Value::object();
			on.set("id", p.id);
			on.set("label", p.label);
			profiles.push(std::move(on));
		}
		d.set("profiles", std::move(profiles));
		d.set("channel", _s.channel + 1);
		d.set("about", p.defaultsAbout[index(_m)]);
		d.set("tracks", tracksOf(_m));
		d.set("selected", _selected);
		d.set("known", _r.known);
		d.set("warning", _s.on ? overlapWarning(_s, _r, _m) : std::string());
		Value voices = Value::array();
		for(size_t v = 0; v < g_voices; ++v)
		{
			Value e = Value::object();
			e.set("voice", p.voices[v].id);
			e.set("label", p.voices[v].label);
			Value notes = Value::array();
			for(const auto n : p.voices[v].notes)
				notes.push(static_cast<int>(n));
			e.set("notes", std::move(notes));
			e.set("t", _s.voices[v].t);
			if(_m == Machine::Mm)
				e.set("note", _s.voices[v].note);
			if(_r.known && _r.channel[v] >= 0)
			{
				Value out = Value::object();
				out.set("ch", _r.channel[v] + 1);
				out.set("note", _r.note[v]);
				e.set("out", std::move(out));
			}
			else
				e.set("out", Value());
			voices.push(std::move(e));
		}
		d.set("voices", std::move(voices));
		Value knobs = Value::array();
		for(const auto& k : p.knobs)
		{
			const auto& t = _s.knobs[k.cc];
			Value e = Value::object();
			e.set("cc", static_cast<int>(k.cc));
			e.set("label", k.label);
			if(_m == Machine::Mm)
				e.set("pg", t.pg);
			e.set("i", t.i);
			e.set("name", t.mapped() ? targetName(_m, t) : std::string());
			knobs.push(std::move(e));
		}
		d.set("knobs", std::move(knobs));
		Value list = Value::array();
		for(const auto& t : targets(_m))
		{
			Value e = Value::object();
			if(_m == Machine::Mm)
				e.set("pg", t.at.pg);
			e.set("i", t.at.i);
			e.set("name", t.name);
			list.push(std::move(e));
		}
		d.set("targets", std::move(list));
		d.set("last", _last);
		return d;
	}
}
