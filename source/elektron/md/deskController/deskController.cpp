#include "deskController.h"

#include "tr06Json.h"

#include "elektronData/mdGlobal.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mmGlobal.h"
#include "elektronData/mmJson.h"
#include "elektronData/mmKit.h"
#include "elektronData/mmMachines.h"

#include <algorithm>
#include <cassert>
#include <cctype>

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
			p.relative = textOf(*doc, "knobMode") != "absolute";
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

		// A knob's target in a document ({pg?, i}): the Machinedrum has no pages, only NOTE's pg 8.
		Target targetOf(const Value& _e, const Machine _m)
		{
			const auto* i = _e.find("i");
			const int pg = intOf(_e, "pg", -1);
			return {_m == Machine::Md && pg != Target::g_notePg ? -1 : pg, i && i->isNumber() ? static_cast<int>(i->asNumber()) : -1};
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
					const Target t = targetOf(e, _m);
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
			out.push_back({Target::note(), "NOTE"});
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
			out.push_back({Target::note(), "NOTE"});
			return out;
		}();
		return _m == Machine::Md ? md : mm;
	}

	bool validTarget(const Machine _m, const Target& _t)
	{
		if(_t.isNote())
			return true;
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

	int mdPitchParam(const int _model)
	{
		if(_model < 0)
			return -1;
		const auto names = elektronData::mdMachineParamNames(static_cast<uint32_t>(_model));
		for(int i = 0; i < 8; ++i)
		{
			const auto* n = names[static_cast<size_t>(i)];
			if(n && (std::string(n) == "PTCH" || std::string(n) == "PITCH" || std::string(n) == "TUNE"))
				return i;
		}
		return -1;
	}

	std::string modelName(const Machine _m, const int _model)
	{
		if(_model < 0)
			return {};
		if(_m == Machine::Md)
			return elektronData::mdMachineName(static_cast<uint32_t>(_model));
		const auto* info = elektronData::mmMachine(static_cast<uint8_t>(_model));
		return info && info->name ? info->name : std::string();
	}

	std::string ownName(const Machine _m, const Target& _t, const int _model)
	{
		const auto generic = targetName(_m, _t);
		if(_model < 0 || generic.empty())
			return {};
		if(_t.isNote())
		{
			if(_m == Machine::Mm)
				return {};
			const int p = mdPitchParam(_model);
			return p < 0 ? "no pitch on this machine" : elektronData::mdMachineParamNames(static_cast<uint32_t>(_model))[static_cast<size_t>(p)];
		}
		std::string own;
		if(_m == Machine::Md)
		{
			if(_t.i >= 24)
				return {};
			const auto* n = elektronData::mdMachineParamNames(static_cast<uint32_t>(_model))[static_cast<size_t>(_t.i)];
			own = n ? n : "";
		}
		else
		{
			if(_t.pg != 0)
				return {};	// the fixed pages are the same on every machine
			own = elektronData::mmParamName(static_cast<uint8_t>(_model), 0, static_cast<uint8_t>(_t.i));
		}
		if(own.empty())
			return "-";
		return own == generic ? std::string() : own;
	}

	Setup defaults(const Machine _m)
	{
		Setup s;
		s.channel = tr06().channel;
		s.relative = tr06().relative;
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
		if(_doc.find("knobMode"))
		{
			const auto mode = textOf(_doc, "knobMode");
			if(mode != "relative" && mode != "absolute")
				_errors.emplace_back("$.knobMode: expected relative or absolute");
			s.relative = mode != "absolute";
		}
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
		d.set("knobMode", _s.relative ? "relative" : "absolute");
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
			if(_m == Machine::Mm || t.isNote())
				e.set("pg", t.pg);
			e.set("i", t.i);
			knobs.push(std::move(e));
		}
		d.set("knobs", std::move(knobs));
		return d;
	}

	Route mdRoute(const Setup& _s, const elektronData::MdGlobal* _global, const bool _pads)
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
			if(_pads)
			{
				if(t >= 0 && t < 16)
				{
					r.channel[v] = base;
					r.note[v] = 36 + t;
				}
				continue;
			}
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

	KnobPump::Slot& KnobPump::slot(const uint8_t _track, const Target& _at, const double _now)
	{
		if(_now - m_lastIn > g_quietMs)
		{
			++m_gesture;
			for(auto& [k, s] : m_slots)
				s.sent = -1;
		}
		m_lastIn = _now;
		auto& s = m_slots[{_track, _at}];
		s.gesture = m_gesture;
		return s;
	}

	void KnobPump::in(const uint8_t _track, const Target& _at, const uint8_t _value, const double _now)
	{
		auto& s = slot(_track, _at, _now);
		s.relative = false;
		s.delta = 0;
		s.pending = _value;
	}

	void KnobPump::add(const uint8_t _track, const Target& _at, const int _delta, const double _now)
	{
		if(!_delta)
			return;
		auto& s = slot(_track, _at, _now);
		if(!s.relative)
			s.pending = -1;
		s.relative = true;
		s.delta += _delta;
	}

	bool KnobPump::pending() const
	{
		for(const auto& [k, s] : m_slots)
			if(s.pending >= 0 || (s.relative && s.delta))
				return true;
		return false;
	}

	std::vector<Edit> KnobPump::take(const double _now, const Current& _current)
	{
		std::vector<Edit> out;
		if(_now - m_lastOut < g_intervalMs)
			return out;
		for(auto& [k, s] : m_slots)
		{
			int value = -1;
			if(s.relative)
			{
				if(!s.delta)
					continue;
				const int from = s.sent >= 0 ? s.sent : _current ? _current(k.track, k.at) : -1;
				if(from >= 0)
					value = std::clamp(from + s.delta, 0, 127);
				s.delta = 0;
				if(value < 0)
					continue;	// the target's value is not known: nothing to move from
			}
			else
			{
				if(s.pending < 0)
					continue;
				value = s.pending;
				s.pending = -1;
			}
			if(value != s.sent)
				out.push_back({k.track, k.at, static_cast<uint8_t>(value), s.gesture});
			s.sent = value;
		}
		if(!out.empty())
			m_lastOut = _now;
		return out;
	}

	// ---- what a knob does ----

	KnobAction knobAction(const Setup& _s, const Machine _m, const uint8_t _cc, const int _model)
	{
		const auto& t = _s.knobs[_cc & 0x7f];
		if(!t.mapped() || !validTarget(_m, t))
			return {};
		if(!t.isNote())
			return {KnobAction::Kind::Param, t};
		if(_m == Machine::Mm)
			return {KnobAction::Kind::Note, t};
		// The Machinedrum's TRIGs have no pitch: its machine's pitch parameter, or nothing.
		const int p = mdPitchParam(_model);
		return p < 0 ? KnobAction{} : KnobAction{KnobAction::Kind::Param, {-1, p}};
	}

	Setup transposed(const Setup& _s, const int _track, const int _delta, const int _value)
	{
		auto s = _s;
		for(auto& v : s.voices)
			if(v.t == _track && v.note >= 0)
				v.note = std::clamp(s.relative ? v.note + _delta : _value, 0, 127);
		return s;
	}

	// ---- relative knobs ----

	int RelativeKnobs::in(const uint8_t _cc, const uint8_t _value, const double _now)
	{
		const auto cc = static_cast<size_t>(_cc & 0x7f);
		const int ref = m_ref[cc];
		const bool fresh = ref < 0 || _now - m_at[cc] > g_idleMs;
		m_ref[cc] = static_cast<int16_t>(_value & 0x7f);
		m_at[cc] = _now;
		return fresh ? 0 : (_value & 0x7f) - ref;
	}

	void RelativeKnobs::reset()
	{
		m_ref.fill(-1);
		m_at.fill(-1e18);
	}

	// ---- the page's document ----

	bool looksLikeTr06(const std::string& _name)
	{
		std::string n;
		for(const auto c : _name)
			n.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		return n.find("tr-06") != std::string::npos || n.find("tr06") != std::string::npos;
	}

	// ---- the activity ----

	std::vector<Monitor::Note> Monitor::notes() const
	{
		std::vector<Note> out;
		for(const auto& e : m_notes)
		{
			const auto v = e.load(std::memory_order_relaxed);
			if(!v)
				continue;
			Note n;
			n.n = static_cast<uint32_t>(v >> 32);
			n.ch = static_cast<uint8_t>(v & 0x0f);
			n.note = static_cast<uint8_t>(v >> 8 & 0x7f);
			n.velocity = static_cast<uint8_t>(v >> 16 & 0x7f);
			out.push_back(n);
		}
		std::sort(out.begin(), out.end(), [](const Note& _a, const Note& _b) { return _a.n > _b.n; });
		return out;
	}

	void Monitor::clear()
	{
		// Racing a MIDI thread's store is harmless: that value shows, or it comes again with the next move.
		for(auto& c : m_ccs)
			c.store(0, std::memory_order_relaxed);
		for(auto& n : m_notes)
			n.store(0, std::memory_order_relaxed);
	}

	void Activity::start(Monitor& _monitor, double)
	{
		_monitor.clear();
		m_ccs.clear();
		m_notes.clear();
		for(uint8_t ch = 0; ch < 16; ++ch)
		{
			m_counts[ch] = _monitor.last(ch).count;
			m_at[ch] = -1e9;
		}
		m_hasLast = false;
		m_channel = -1;
		m_elsewhere = -1;
		++m_seq;
	}

	bool Activity::update(const Monitor& _monitor, const uint8_t _channel, const double _now)
	{
		const int channel = _channel & 0x0f;
		bool changed = false;
		if(channel != m_channel)
		{
			// Another channel: what its predecessor got last is not this one's.
			m_channel = channel;
			m_hasLast = false;
			changed = true;
		}
		for(uint8_t ch = 0; ch < 16; ++ch)
		{
			const auto l = _monitor.last(ch);
			if(l.count == m_counts[ch])
				continue;
			m_counts[ch] = l.count;
			m_at[ch] = _now;
			if(ch == channel)
			{
				m_last = l;
				m_hasLast = true;
				changed = true;
			}
		}
		// Nothing on the controller's channel lately, something on another: the newest such channel.
		int elsewhere = -1;
		if(_now - m_at[channel] > g_elsewhereMs)
		{
			double newest = -1e18;
			for(int ch = 0; ch < 16; ++ch)
			{
				if(ch != channel && _now - m_at[ch] <= g_elsewhereMs && m_at[ch] > newest)
				{
					newest = m_at[ch];
					elsewhere = ch;
				}
			}
		}
		if(elsewhere != m_elsewhere)
		{
			m_elsewhere = elsewhere;
			changed = true;
		}
		// Every CC seen, the controller's channel first, each channel's by CC number; the last notes.
		std::vector<Cc> ccs;
		for(int pass = 0; pass < 2; ++pass)
			for(int ch = 0; ch < 16; ++ch)
			{
				if((ch == channel) != (pass == 0))
					continue;
				for(int cc = 0; cc < 128; ++cc)
					if(const int v = _monitor.cc(static_cast<uint8_t>(ch), static_cast<uint8_t>(cc)); v >= 0)
						ccs.push_back({static_cast<uint8_t>(ch), static_cast<uint8_t>(cc), static_cast<uint8_t>(v)});
			}
		if(!(ccs == m_ccs))
		{
			m_ccs = std::move(ccs);
			changed = true;
		}
		auto notes = _monitor.notes();
		const auto sameNotes = notes.size() == m_notes.size() && std::equal(notes.begin(), notes.end(), m_notes.begin(), [](const Monitor::Note& _a, const Monitor::Note& _b) { return _a.n == _b.n; });
		if(!sameNotes)
		{
			m_notes = std::move(notes);
			changed = true;
		}
		if(changed)
			++m_seq;
		return changed;
	}

	Value Activity::toJson() const
	{
		Value a = Value::object();
		a.set("seq", static_cast<double>(m_seq));
		if(m_hasLast)
		{
			Value l = Value::object();
			const uint8_t type = m_last.a & 0xf0;
			const char* kind = type == 0x90 && m_last.c > 0 ? "note" : type == 0x80 || type == 0x90 ? "off" : type == 0xb0 ? "cc" : "other";
			l.set("kind", kind);
			// the note or controller; for any other message its status byte
			l.set("n", static_cast<int>(std::string(kind) == "other" ? m_last.a : m_last.b));
			l.set("v", static_cast<int>(std::string(kind) == "other" ? m_last.b : m_last.c));
			a.set("last", std::move(l));
		}
		else
			a.set("last", Value());
		a.set("elsewhere", m_elsewhere >= 0 ? Value(m_elsewhere + 1) : Value());
		Value ccs = Value::array();
		for(const auto& c : m_ccs)
		{
			Value e = Value::object();
			e.set("ch", c.ch + 1);
			e.set("cc", static_cast<int>(c.cc));
			e.set("v", static_cast<int>(c.v));
			ccs.push(std::move(e));
		}
		a.set("ccs", std::move(ccs));
		Value notes = Value::array();
		for(const auto& n : m_notes)
		{
			Value e = Value::object();
			e.set("ch", n.ch + 1);
			e.set("n", static_cast<int>(n.note));
			e.set("v", static_cast<int>(n.velocity));
			notes.push(std::move(e));
		}
		a.set("notes", std::move(notes));
		return a;
	}

	Value pageDocument(const Setup& _s, const Machine _m, const Route& _r, const int _selected, const Value& _last, const Seen& _seen, const int _model)
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
		d.set("knobMode", _s.relative ? "relative" : "absolute");
		d.set("about", p.defaultsAbout[index(_m)]);
		d.set("tracks", tracksOf(_m));
		d.set("selected", _selected);
		d.set("machineName", modelName(_m, _model));
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
			if(_m == Machine::Mm || t.isNote())
				e.set("pg", t.pg);
			e.set("i", t.i);
			e.set("name", t.mapped() ? targetName(_m, t) : std::string());
			e.set("own", t.mapped() ? ownName(_m, t, _model) : std::string());
			knobs.push(std::move(e));
		}
		d.set("knobs", std::move(knobs));
		Value list = Value::array();
		for(const auto& t : targets(_m))
		{
			Value e = Value::object();
			if(_m == Machine::Mm || t.at.isNote())
				e.set("pg", t.at.pg);
			e.set("i", t.at.i);
			e.set("name", t.name);
			e.set("own", ownName(_m, t.at, _model));
			list.push(std::move(e));
		}
		d.set("targets", std::move(list));
		d.set("last", _last);
		d.set("named", _seen.named);
		if(_seen.named)
		{
			Value inputs = Value::array();
			for(const auto& in : _seen.inputs)
			{
				Value e = Value::object();
				e.set("name", in.name);
				e.set("on", in.on);
				e.set("tr06", looksLikeTr06(in.name));
				inputs.push(std::move(e));
			}
			d.set("inputs", std::move(inputs));
		}
		else
			d.set("inputs", Value());
		d.set("device", _seen.device);
		d.set("activity", _seen.activity);
		return d;
	}

	// ---- the edits as commands ----

	std::vector<Value> mdCommands(const std::vector<Edit>& _edits, const uint8_t _kit)
	{
		std::vector<Value> out;
		for(const auto& e : _edits)
		{
			if(!validTarget(Machine::Md, e.at) || e.at.isNote() || e.track >= 16)
				continue;
			Value m = Value::object();
			const bool level = e.at.i == 24;
			m.set("op", level ? "level" : "param");
			m.set("k", static_cast<int>(_kit));
			m.set("t", static_cast<int>(e.track));
			if(!level)
				m.set("i", e.at.i);
			m.set("v", static_cast<int>(e.value));
			m.set("g", g_gestures + e.gesture);
			out.push_back(std::move(m));
		}
		return out;
	}

	std::optional<Value> mmCommand(const std::vector<Edit>& _edits, const elektronData::MmKit& _working)
	{
		auto kit = _working;
		for(const auto& e : _edits)
		{
			if(!validTarget(Machine::Mm, e.at) || e.at.isNote() || e.track >= elektronData::MmKit::g_tracks)
				continue;
			if(e.at.pg == 7)
				kit.levels[e.track] = e.value;
			else
				kit.tracks[e.track].pages[static_cast<size_t>(e.at.pg)][static_cast<size_t>(e.at.i)] = e.value;
		}
		if(_edits.empty() || kit == _working)
			return std::nullopt;
		Value m = Value::object();
		m.set("op", "set");
		m.set("kind", "workingKit");
		m.set("doc", elektronData::mmKitToJson(kit));
		m.set("g", g_gestures + _edits.front().gesture);
		return m;
	}
}
