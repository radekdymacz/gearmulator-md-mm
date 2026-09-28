#include "mmDesk.h"

#include "elektronData/mmCommands.h"
#include "elektronData/mmDump.h"
#include "elektronData/mmJson.h"
#include "elektronData/mmMachines.h"
#include "elektronData/mmValidate.h"

#include <algorithm>

namespace mmDesk
{
	namespace ed = elektronData;
	using ed::json::Value;

	namespace
	{
		const char* kindName(const int _k)
		{
			static const char* names[] = {"pattern", "kit", "song", "global"};
			return names[_k];
		}

		std::string str(const Value& _m, const char* _key)
		{
			const auto* v = _m.find(_key);
			return v && v->isString() ? v->asString() : std::string();
		}

		int num(const Value& _m, const char* _key, const int _default = -1)
		{
			const auto* v = _m.find(_key);
			return v && v->isNumber() ? static_cast<int>(v->asNumber()) : _default;
		}

		Value strings(const std::vector<std::string>& _v)
		{
			Value a = Value::array();
			for(const auto& s : _v)
				a.push(s);
			return a;
		}

		// The kit fields a live SysEx or CC reaches (everything else needs a dump).
		ed::MmKit liveFields(ed::MmKit _k)
		{
			_k.name = {};
			_k.levels = {};
			_k.machines = {};
			_k.routing = {};
			for(auto& t : _k.tracks)
			{
				t.pages = {};
				t.midi = {};
				t.multiEnv = {};
			}
			return _k;
		}
	}

	Desk::Desk(Port _port) : m_port(std::move(_port))
	{
	}

	size_t Desk::loaded() const
	{
		size_t n = 0;
		for(const auto& p : m_patterns) n += p.has_value();
		for(const auto& k : m_kits) n += k.has_value();
		for(const auto& s : m_songs) n += s.has_value();
		for(const auto& g : m_globals) n += g.has_value();
		return n;
	}

	void Desk::publish(const Value& _m) const
	{
		if(m_pageReady && m_port.toPage)
			m_port.toPage(_m);
	}

	void Desk::result(const Value& _msg, const bool _ok, const std::vector<std::string>& _errors, const std::string& _note)
	{
		Value r = Value::object();
		r.set("type", "result");
		r.set("op", str(_msg, "op"));
		r.set("id", num(_msg, "id", 0));
		r.set("ok", _ok);
		r.set("errors", strings(_errors));
		r.set("note", _note);
		publish(r);
	}

	void Desk::publishDoc(const Ref& _r, const bool _pending)
	{
		Value d;
		bool working = false;
		switch(_r.kind)
		{
		case Kind::Pattern:
			if(!m_patterns[_r.slot]) return;
			d = ed::mmPatternToJson(*m_patterns[_r.slot]);
			break;
		case Kind::Kit:
			if(static_cast<int>(_r.slot) == m_curKit && m_working)
			{
				d = ed::mmKitToJson(*m_working);
				working = true;
			}
			else if(m_kits[_r.slot])
				d = ed::mmKitToJson(*m_kits[_r.slot]);
			else
				return;
			break;
		case Kind::Song:
			if(!m_songs[_r.slot]) return;
			d = ed::mmSongToJson(*m_songs[_r.slot]);
			break;
		case Kind::Global:
			if(!m_globals[_r.slot]) return;
			d = ed::mmGlobalToJson(*m_globals[_r.slot]);
			break;
		}
		Value m = Value::object();
		m.set("type", "doc");
		m.set("kind", kindName(static_cast<int>(_r.kind)));
		m.set("slot", _r.slot);
		m.set("pending", _pending);
		m.set("working", working);
		m.set("doc", std::move(d));
		publish(m);
		m_pending[_r] = _pending;
	}

	std::string Desk::kitState() const
	{
		if(m_curKit < 0 || !m_working || !m_kits[m_curKit])
			return "unknown";
		return ed::mmKitRaw(*m_working) == ed::mmKitRaw(*m_kits[m_curKit]) ? "clean" : "edited";
	}

	void Desk::publishMachine()
	{
		static const char* engines[] = {"missing", "unsupported", "loading", "booting", "ready"};
		Value d = Value::object();
		d.set("schema", "mm-desk/machine");
		d.set("version", 1);
		d.set("engine", engines[static_cast<int>(m_engine)]);
		Value p = Value::object();
		p.set("current", m_curPattern < 0 ? Value() : Value(m_curPattern));
		p.set("queued", m_queuedPattern < 0 ? Value() : Value(m_queuedPattern));
		d.set("pattern", std::move(p));
		Value k = Value::object();
		k.set("current", m_curKit < 0 ? Value() : Value(m_curKit));
		k.set("working", kitState());
		d.set("kit", std::move(k));
		Value s = Value::object();
		s.set("current", m_curSong < 0 ? Value() : Value(m_curSong));
		s.set("songMode", m_songMode < 0 ? Value() : Value(m_songMode == 1));
		d.set("song", std::move(s));
		d.set("global", m_curGlobal < 0 ? Value() : Value(m_curGlobal));
		d.set("playing", m_tel.valid && m_tel.running);
		// 30-300 BPM in firmware units (x 24); anything else is not a tempo yet (boot).
		d.set("tempo", m_tel.tempo >= 720 && m_tel.tempo <= 7200 ? Value(m_tel.tempo / 24.0) : Value());
		Value r = Value::object();
		r.set("state", m_recv.stateName());
		size_t inFlight = 0;
		for(const auto& [ref, push] : m_pushes)
			inFlight += push.waitingRecv || push.waitingReadBack;
		r.set("sending", static_cast<unsigned long>(inFlight));
		r.set("received", static_cast<unsigned long>(m_tel.recvCount));
		r.set("errors", static_cast<unsigned long>(m_tel.recvErrors));
		d.set("recv", std::move(r));
		Value l = Value::object();
		l.set("done", static_cast<unsigned long>(loaded()));
		l.set("total", 128 + 128 + 24 + 8);
		d.set("loading", std::move(l));
		d.set("roundTripMs", m_lastRoundTripMs);
		d.set("error", m_lastError);
		Value m = Value::object();
		m.set("type", "machine");
		m.set("doc", std::move(d));
		publish(m);
		m_machineDirty = false;
	}

	Value Desk::catalogue()
	{
		Value c = Value::object();
		c.set("schema", "mm-desk/catalogue");
		c.set("version", 1);
		Value machines = Value::array();
		for(const auto& mi : ed::mmMachines())
		{
			Value m = Value::object();
			m.set("id", mi.id);
			m.set("name", mi.name);
			m.set("family", mi.family);
			Value syn = Value::array();
			Value enums = Value::object();
			for(uint8_t i = 0; i < 8; ++i)
			{
				syn.push(mi.synth[i]);
				const auto e = ed::mmSynthEnum(mi.id, i);
				if(!e.empty())
					enums.set(std::to_string(i), strings(e));
			}
			m.set("synth", std::move(syn));
			m.set("enums", std::move(enums));
			m.set("fx", mi.fx);
			m.set("mk2", mi.mk2);
			machines.push(std::move(m));
		}
		c.set("machines", std::move(machines));
		Value pages = Value::array();
		for(uint8_t pg = 1; pg < 8; ++pg)
		{
			Value names = Value::array();
			for(const auto* n : ed::mmFixedPage(pg))
				names.push(n);
			pages.push(std::move(names));
		}
		c.set("fixedPages", std::move(pages));
		Value lfo = Value::object();
		lfo.set("pages", strings(ed::mmLfoPages()));
		lfo.set("trigs", strings(ed::mmLfoTrigs()));
		lfo.set("waves", strings(ed::mmLfoWaves()));
		lfo.set("mults", strings(ed::mmLfoMults()));
		lfo.set("pitchDests", strings(ed::mmPitchDests()));
		c.set("lfo", std::move(lfo));
		c.set("enumRule", "index = floor(value * n / 128); value = ceil(index * 128 / n)");
		return c;
	}

	// ---- page ----

	void Desk::onPageMessage(const Value& _msg)
	{
		const auto op = str(_msg, "op");
		const auto now = m_port.nowMs();
		if(op == "ready")
		{
			m_pageReady = true;
			Value cat = Value::object();
			cat.set("type", "catalogue");
			cat.set("doc", catalogue());
			publish(cat);
			for(uint8_t i = 0; i < 128; ++i)
			{
				publishDoc({Kind::Pattern, i}, false);
				publishDoc({Kind::Kit, i}, false);
			}
			for(uint8_t i = 0; i < 24; ++i)
				publishDoc({Kind::Song, i}, false);
			for(uint8_t i = 0; i < 8; ++i)
				publishDoc({Kind::Global, i}, false);
			publishMachine();
			result(_msg, true, {}, "");
			return;
		}
		if(!m_ready)
		{
			result(_msg, false, {"The engine is not ready yet."}, "");
			return;
		}
		if(op == "set")
		{
			handleSet(_msg);
			return;
		}
		const auto sendNow = [&](const Bytes& _m) { m_port.sendSysex(_m); };
		if(op == "load")
		{
			const auto k = str(_msg, "kind");
			const auto s = num(_msg, "slot", 0);
			const Kind kind = k == "kit" ? Kind::Kit : k == "song" ? Kind::Song : k == "global" ? Kind::Global : Kind::Pattern;
			request({kind, static_cast<uint8_t>(s)}, true);
			result(_msg, true, {}, "");
			return;
		}
		if(op == "select")
		{
			const auto p = num(_msg, "p");
			if(p < 0 || p > 127)
			{
				result(_msg, false, {"select: pattern 0-127"}, "");
				return;
			}
			sendNow(ed::mmLoadPattern(static_cast<uint8_t>(p)));
			if(m_tel.valid && m_tel.running)
				m_queuedPattern = p;
			else
			{
				m_queuedPattern = -1;
				m_curPattern = p;
			}
			m_lastStatusMs = now - 800;	// status soon
			if(!m_patterns[p])
				request({Kind::Pattern, static_cast<uint8_t>(p)}, true);
			m_machineDirty = true;
			result(_msg, true, {}, m_queuedPattern >= 0 ? "queued: starts at the pattern end" : "");
			return;
		}
		if(op == "loadKit" || op == "saveKit")
		{
			const auto k = num(_msg, "k", m_curKit);
			if(k < 0 || k > 127)
			{
				result(_msg, false, {"kit 0-127"}, "");
				return;
			}
			if(op == "loadKit")
				sendNow(ed::mmLoadKit(static_cast<uint8_t>(k)));
			else
			{
				sendNow(ed::mmSaveKit(static_cast<uint8_t>(k)));
				request({Kind::Kit, static_cast<uint8_t>(k)}, true);	// the stored slot now
			}
			m_curKit = k;
			m_lastStatusMs = now - 800;
			m_machineDirty = true;
			result(_msg, true, {}, "");
			return;
		}
		if(op == "loadSong" || op == "saveSong")
		{
			const auto s = num(_msg, "s", m_curSong);
			if(s < 0 || s > 23)
			{
				result(_msg, false, {"song 0-23"}, "");
				return;
			}
			if(op == "loadSong" && m_tel.valid && m_tel.running)
			{
				result(_msg, false, {"The machine loads a song only while stopped."}, "");
				return;
			}
			sendNow(op == "loadSong" ? ed::mmLoadSong(static_cast<uint8_t>(s)) : ed::mmSaveSong(static_cast<uint8_t>(s)));
			if(op == "saveSong")
				request({Kind::Song, static_cast<uint8_t>(s)}, true);
			m_curSong = s;
			m_machineDirty = true;
			result(_msg, true, {}, "");
			return;
		}
		if(op == "tempo")
		{
			const auto* b = _msg.find("bpm");
			if(!b || !b->isNumber() || b->asNumber() < 30 || b->asNumber() > 300)
			{
				result(_msg, false, {"tempo: 30-300 BPM"}, "");
				return;
			}
			sendNow(ed::mmSetTempo(b->asNumber()));
			result(_msg, true, {}, "");
			return;
		}
		if(op == "play" || op == "stop")
		{
			const bool ok = pressKeys({op == "play" ? Key::Play : Key::Stop});
			result(_msg, ok, ok ? std::vector<std::string>{} : std::vector<std::string>{"The panel is busy (SYSEX RECV); try again."}, "");
			return;
		}
		if(op == "mute")
		{
			const auto t = num(_msg, "t");
			if(t < 0 || t > 5)
			{
				result(_msg, false, {"mute: synth track 0-5"}, "");
				return;
			}
			m_port.sendParam(static_cast<uint8_t>(t), 8, 0, num(_msg, "on", 0) ? 1 : 0);
			result(_msg, true, {}, "");
			return;
		}
		result(_msg, false, {"unknown command " + op}, "");
	}

	void Desk::handleSet(const Value& _msg)
	{
		const auto kind = str(_msg, "kind");
		const auto* doc = _msg.find("doc");
		std::vector<std::string> errors;
		if(!doc)
		{
			result(_msg, false, {"set: no doc"}, "");
			return;
		}
		const auto now = m_port.nowMs();
		m_recv.touch(now);
		if(kind == "pattern")
		{
			auto p = ed::mmPatternFromJson(*doc, errors);
			if(!p)
			{
				result(_msg, false, errors, "");
				return;
			}
			const Ref r{Kind::Pattern, p->position};
			m_patterns[p->position] = *p;
			pushDump(r, ed::encodeMmPattern(*p));
			publishDoc(r, true);
			result(_msg, true, {}, "");
			return;
		}
		if(kind == "kit")
		{
			auto k = ed::mmKitFromJson(*doc, errors);
			if(!k)
			{
				result(_msg, false, errors, "");
				return;
			}
			const Ref r{Kind::Kit, k->position};
			std::vector<std::string> notes;
			if(static_cast<int>(k->position) == m_curKit && m_working)
			{
				deliverKitLive(*m_working, *k, notes);
				m_working = *k;
				m_liveEditMs = now;
			}
			else
			{
				m_kits[k->position] = *k;
				pushDump(r, ed::encodeMmKit(*k));
			}
			publishDoc(r, true);
			m_machineDirty = true;
			std::string note;
			for(const auto& n : notes)
				note += (note.empty() ? "" : " ") + n;
			result(_msg, true, {}, note);
			return;
		}
		if(kind == "song")
		{
			auto s = ed::mmSongFromJson(*doc, errors);
			if(!s)
			{
				result(_msg, false, errors, "");
				return;
			}
			const Ref r{Kind::Song, s->position};
			m_songs[s->position] = *s;
			pushDump(r, ed::encodeMmSong(*s));
			publishDoc(r, true);
			result(_msg, true, {}, static_cast<int>(s->position) == m_curSong ? "Heard after STOP and LOAD SONG." : "");
			return;
		}
		if(kind == "global")
		{
			auto g = ed::mmGlobalFromJson(*doc, errors);
			if(!g)
			{
				result(_msg, false, errors, "");
				return;
			}
			if(static_cast<int>(g->position) == m_curGlobal && m_globals[g->position]
				&& g->baseChannel != m_globals[g->position]->baseChannel)
			{
				result(_msg, false, {"The editor talks to the machine on the active global's base channel; change it on the machine."}, "");
				return;
			}
			const Ref r{Kind::Global, g->position};
			m_globals[g->position] = *g;
			pushDump(r, ed::encodeMmGlobal(*g));
			publishDoc(r, true);
			result(_msg, true, {}, "");
			return;
		}
		result(_msg, false, {"set: kind must be pattern, kit, song or global"}, "");
	}

	void Desk::deliverKitLive(const ed::MmKit& _from, const ed::MmKit& _to, std::vector<std::string>& _notes)
	{
		if(_from.name != _to.name)
		{
			std::string n;
			for(const auto c : _to.name)
				if(c)
					n += static_cast<char>(c);
			m_port.sendSysex(ed::mmSetKitName(n));
		}
		for(uint8_t t = 0; t < 6; ++t)
		{
			const bool machine = _from.machines[t] != _to.machines[t];
			if(machine)
				m_port.sendSysex(ed::mmAssignMachine(t, _to.machines[t], 0));
			if(_from.routing[t] != _to.routing[t])
				m_port.sendSysex(ed::mmSetRouting(t, ed::mmRoutingOutputs(_to.routing[t]), ed::mmRoutingInput(_to.routing[t])));
			if(_from.levels[t] != _to.levels[t])
				m_port.sendParam(t, 7, 0, _to.levels[t]);
			for(uint8_t pg = 0; pg < 7; ++pg)
				for(uint8_t i = 0; i < 8; ++i)
					if(_from.tracks[t].pages[pg][i] != _to.tracks[t].pages[pg][i] || (machine && pg == 0))
						m_port.sendParam(t, pg, i, _to.tracks[t].pages[pg][i]);
			for(uint8_t i = 0; i < 8; ++i)
				if(_from.tracks[t].midi[i] != _to.tracks[t].midi[i])
					m_port.sendNrpn(t, static_cast<uint8_t>(0x38 + i), _to.tracks[t].midi[i]);
			for(uint8_t i = 0; i < 6; ++i)
				if(_from.tracks[t].multiEnv[i] != _to.tracks[t].multiEnv[i])
					m_port.sendNrpn(t, static_cast<uint8_t>(0x40 + i), _to.tracks[t].multiEnv[i]);
		}
		// What no live message reaches: a kit dump to the current slot, then LOAD KIT.
		if(ed::mmKitRaw(liveFields(_from)) != ed::mmKitRaw(liveFields(_to)))
		{
			auto k = _to;
			k.position = static_cast<uint8_t>(m_curKit);
			pushDump({Kind::Kit, k.position}, ed::encodeMmKit(k));
			m_recv.want(ed::mmLoadKit(k.position));
			m_kits[k.position] = k;
			_notes.push_back("Written to the kit slot and loaded (no live SysEx for this setting).");
		}
	}

	// ---- delivery ----

	void Desk::pushDump(const Ref& _r, Bytes _dump)
	{
		auto& p = m_pushes[_r];
		if(p.waitingRecv || p.waitingReadBack)
		{
			p.next = std::move(_dump);
			return;
		}
		p.sent = _dump;
		p.waitingRecv = true;
		p.sentMs = m_port.nowMs();
		m_recv.want(std::move(_dump));
		m_machineDirty = true;
	}

	void Desk::pumpRecv(const double _now)
	{
		auto out = m_recv.tick(_now, m_tel);
		if(!out.keys.empty())
		{
			m_port.pressKeys(out.keys);
			// 20 ms a key, machine time.
			m_keysBusyUntilMs = _now + 20.0 * static_cast<double>(out.keys.size()) + 100;
		}
		for(const auto& s : out.sends)
		{
			m_port.sendSysex(s);
			// Which push was this? Ask for the read-back of every push that waited for RECV.
			for(auto& [ref, push] : m_pushes)
			{
				if(push.waitingRecv && push.sent == s)
				{
					push.waitingRecv = false;
					push.waitingReadBack = true;
					request(ref, true);
				}
			}
		}
		if(!out.keys.empty() || !out.sends.empty())
			m_machineDirty = true;
		// A read-back that never came: give up on that push.
		for(auto& [ref, push] : m_pushes)
		{
			if(push.waitingReadBack && _now - push.sentMs > 8000)
			{
				push.waitingReadBack = false;
				m_lastError = std::string("No read-back for a ") + kindName(static_cast<int>(ref.kind)) + " dump.";
				m_machineDirty = true;
			}
		}
	}

	bool Desk::pressKeys(const std::vector<Key>& _keys)
	{
		const auto s = m_recv.state();
		if(s == RecvSession::State::Entering || s == RecvSession::State::ToMain || s == RecvSession::State::Leaving)
			return false;
		if(!m_port.pressKeys(_keys))
			return false;
		m_keysBusyUntilMs = m_port.nowMs() + 20.0 * static_cast<double>(_keys.size()) + 100;
		return true;
	}

	// ---- loading ----

	void Desk::request(const Ref& _r, const bool _urgent)
	{
		if(m_loading && *m_loading == _r)
			return;
		const auto it = std::find(m_loadQueue.begin(), m_loadQueue.end(), _r);
		if(it != m_loadQueue.end())
		{
			if(!_urgent)
				return;
			m_loadQueue.erase(it);
		}
		if(_urgent)
			m_loadQueue.push_front(_r);
		else
			m_loadQueue.push_back(_r);
	}

	void Desk::requestStatus()
	{
		for(const auto p : {ed::MmStatus::Pattern, ed::MmStatus::Kit, ed::MmStatus::Song, ed::MmStatus::Global, ed::MmStatus::SongMode})
			m_port.sendSysex(ed::mmStatusRequest(p));
	}

	void Desk::pumpLoads(const double _now)
	{
		if(m_loading)
		{
			if(_now - m_loadSentMs < 400)
				return;
			if(++m_loadRetries > 2)
			{
				m_loading.reset();
				m_loadRetries = 0;
			}
			else
			{
				m_loadSentMs = _now;
				m_lastRequestMs = _now;
				m_port.sendSysex(m_loading->kind == Kind::Pattern ? ed::mmPatternRequest(m_loading->slot)
					: m_loading->kind == Kind::Kit ? ed::mmKitRequest(m_loading->slot)
					: m_loading->kind == Kind::Song ? ed::mmSongRequest(m_loading->slot) : ed::mmGlobalRequest(m_loading->slot));
				return;
			}
		}
		if(m_loadQueue.empty() || _now - m_lastRequestMs < 25)
			return;
		m_loading = m_loadQueue.front();
		m_loadQueue.pop_front();
		m_loadRetries = 0;
		m_loadSentMs = _now;
		m_lastRequestMs = _now;
		const auto& r = *m_loading;
		m_port.sendSysex(r.kind == Kind::Pattern ? ed::mmPatternRequest(r.slot) : r.kind == Kind::Kit ? ed::mmKitRequest(r.slot)
			: r.kind == Kind::Song ? ed::mmSongRequest(r.slot) : ed::mmGlobalRequest(r.slot));
	}

	// ---- machine ----

	void Desk::onDeviceSysex(const Bytes& _m)
	{
		if(_m.size() < 8 || _m[0] != 0xf0 || _m[4] != ed::g_mmProductId)
			return;
		const auto cmd = _m[6];
		if(cmd == 0x72)
		{
			if(const auto s = ed::parseMmStatusResponse(_m))
				onStatus(static_cast<uint8_t>(s->param), s->value);
			return;
		}
		if(_m.size() < 15)
			return;
		const auto slot = _m[9];
		switch(cmd)
		{
		case ed::g_mmPatternDump: onDump({Kind::Pattern, static_cast<uint8_t>(slot & 127)}, _m); break;
		case ed::g_mmKitDump: onDump({Kind::Kit, static_cast<uint8_t>(slot & 127)}, _m); break;
		case ed::g_mmSongDump: onDump({Kind::Song, static_cast<uint8_t>(slot % 24)}, _m); break;
		case ed::g_mmGlobalDump: onDump({Kind::Global, static_cast<uint8_t>(slot & 7)}, _m); break;
		default: break;
		}
	}

	void Desk::onDump(const Ref& _r, const Bytes& _sysex)
	{
		bool ok = false;
		switch(_r.kind)
		{
		case Kind::Pattern: if(auto v = ed::decodeMmPattern(_sysex)) { m_patterns[_r.slot] = *v; ok = true; } break;
		case Kind::Kit: if(auto v = ed::decodeMmKit(_sysex)) { m_kits[_r.slot] = *v; ok = true; } break;
		case Kind::Song: if(auto v = ed::decodeMmSong(_sysex)) { m_songs[_r.slot] = *v; ok = true; } break;
		case Kind::Global: if(auto v = ed::decodeMmGlobal(_sysex)) { m_globals[_r.slot] = *v; ok = true; } break;
		}
		if(m_loading && *m_loading == _r)
			m_loading.reset();
		if(!ok)
			return;
		bool pending = false;
		const auto it = m_pushes.find(_r);
		if(it != m_pushes.end() && it->second.waitingReadBack)
		{
			auto& push = it->second;
			push.waitingReadBack = false;
			if(_sysex == push.sent)
			{
				m_lastRoundTripMs = m_port.nowMs() - push.sentMs;
				m_lastError.clear();
			}
			else
				m_lastError = std::string("The machine holds a different ") + kindName(static_cast<int>(_r.kind)) + " than was sent.";
			if(push.next)
			{
				auto next = std::move(*push.next);
				push.next.reset();
				pushDump(_r, std::move(next));
				pending = true;
			}
			m_machineDirty = true;
		}
		// The current kit's page document is the working kit; a stored-slot dump changes its edited state.
		if(_r.kind == Kind::Kit && static_cast<int>(_r.slot) == m_curKit && m_working)
		{
			m_machineDirty = true;
			return;
		}
		publishDoc(_r, pending);
	}

	void Desk::onStatus(const uint8_t _param, const uint8_t _value)
	{
		switch(static_cast<ed::MmStatus>(_param))
		{
		case ed::MmStatus::Pattern:
			if(m_curPattern != _value)
			{
				m_curPattern = _value;
				if(!m_patterns[_value])
					request({Kind::Pattern, _value}, true);
			}
			if(m_queuedPattern == _value)
				m_queuedPattern = -1;
			break;
		case ed::MmStatus::Kit:
			if(m_curKit != _value)
			{
				m_curKit = _value;
				if(!m_kits[_value])
					request({Kind::Kit, _value}, true);
			}
			break;
		case ed::MmStatus::Song:
			if(m_curSong != _value)
			{
				m_curSong = _value;
				if(!m_songs[_value % 24])
					request({Kind::Song, static_cast<uint8_t>(_value % 24)}, true);
			}
			break;
		case ed::MmStatus::Global:
			if(m_curGlobal != _value)
			{
				m_curGlobal = _value;
				if(!m_globals[_value & 7])
					request({Kind::Global, static_cast<uint8_t>(_value & 7)}, true);
			}
			break;
		case ed::MmStatus::SongMode:
			m_songMode = _value;
			break;
		default:
			return;
		}
		m_machineDirty = true;
		if(!m_backgroundQueued && m_curPattern >= 0 && m_curKit >= 0)
		{
			m_backgroundQueued = true;
			for(uint8_t i = 0; i < 128; ++i)
				request({Kind::Kit, i}, false);
			for(uint8_t i = 0; i < 128; ++i)
				request({Kind::Pattern, i}, false);
			for(uint8_t i = 0; i < 24; ++i)
				request({Kind::Song, i}, false);
			for(uint8_t i = 0; i < 8; ++i)
				request({Kind::Global, i}, false);
		}
	}

	void Desk::onTelemetry(const Telemetry& _t)
	{
		const bool wasRunning = m_tel.running;
		const int wasTempo = m_tel.tempo;
		m_tel = _t;
		// Playing = the RAM flag, or the step byte advancing: two single steps forward (or a wrap
		// to 0) in a row, each within three step times at the tempo (a 3/4X pattern included).
		// A stop that resets the step to 0 is one move, so it never reads as playing.
		const double now = m_port.nowMs();
		const double stepMs = _t.tempo > 0 ? 360000.0 / _t.tempo : 250.0;
		const double window = std::max(250.0, 3.0 * stepMs);
		if(_t.valid && _t.step != m_rawStep)
		{
			const bool forward = m_rawStep >= 0 && (_t.step == m_rawStep + 1 || (_t.step == 0 && m_rawStep > 0));
			m_stepMoves = forward && now - m_stepMovedMs < window ? std::min(m_stepMoves + 1, 2) : (forward ? 1 : 0);
			m_stepMovedMs = now;
			m_rawStep = _t.step;
		}
		if(m_stepMoves >= 2 && now - m_stepMovedMs < window)
			m_tel.running = m_tel.running || m_tel.valid;
		else if(now - m_stepMovedMs >= window)
			m_stepMoves = 0;
		if(wasRunning != _t.running || wasTempo != _t.tempo)
			m_machineDirty = true;
		if(!_t.running && m_queuedPattern >= 0)
		{
			// Stopped: LOAD PATTERN switches at once; status will say.
			m_lastStatusMs = -1e9;
		}
	}

	void Desk::onWorkingKit(const Bytes& _region)
	{
		m_workingRegion = _region;
	}

	void Desk::onHostParam(const uint8_t _track, const uint8_t _page, const uint8_t _index, const uint8_t _value)
	{
		// The working kit from memory follows by itself; nothing to track here.
		(void)_track; (void)_page; (void)_index; (void)_value;
	}

	void Desk::setEngine(const Engine _engine)
	{
		if(_engine == m_engine)
			return;
		m_engine = _engine;
		m_machineDirty = true;
		if(_engine == Engine::Ready && !m_ready)
		{
			m_ready = true;
			m_lastStatusMs = -1e9;
		}
		if(_engine != Engine::Ready)
			m_ready = false;
	}

	void Desk::tick()
	{
		const auto now = m_port.nowMs();
		if(!m_ready)
		{
			if(m_machineDirty)
				publishMachine();
			return;
		}
		// Status: once a second, four times while a pattern is queued.
		if(now - m_lastStatusMs > (m_queuedPattern >= 0 ? 250 : 1000))
		{
			m_lastStatusMs = now;
			requestStatus();
		}
		pumpRecv(now);
		pumpLoads(now);

		// The working kit from memory, unless our own live edit may not have landed yet.
		if(m_workingRegion && now - m_liveEditMs > 200 && m_workingRegion->size() >= 5 + ed::MmKit::g_rawSize)
		{
			const auto kitNumber = (*m_workingRegion)[0];
			const std::vector<uint8_t> raw(m_workingRegion->begin() + 5, m_workingRegion->begin() + 5 + ed::MmKit::g_rawSize);
			m_workingRegion.reset();
			if(auto k = ed::mmKitFromRaw(raw, kitNumber & 127))
			{
				const bool changed = !m_working || ed::mmKitRaw(*m_working) != raw || m_working->position != k->position
					|| m_curKit != (kitNumber & 127);
				m_curKit = kitNumber & 127;
				if(changed)
				{
					m_working = *k;
					publishDoc({Kind::Kit, static_cast<uint8_t>(m_curKit)}, false);
					m_machineDirty = true;
					if(!m_kits[m_curKit])
						request({Kind::Kit, static_cast<uint8_t>(m_curKit)}, true);
				}
			}
		}

		if(m_tel.valid && m_tel.step != m_lastStep && now - m_lastTelemetryMs > 25)
		{
			m_lastStep = m_tel.step;
			m_lastTelemetryMs = now;
			Value t = Value::object();
			t.set("type", "tel");
			t.set("step", m_tel.step);
			t.set("playing", m_tel.running);
			publish(t);
		}
		if(m_machineDirty)
			publishMachine();
	}
}
