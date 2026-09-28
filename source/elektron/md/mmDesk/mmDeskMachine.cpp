#include "mmDeskMachine.h"

#include "elektronData/mmCommands.h"
#include "elektronData/mmDump.h"
#include "elektronData/mmMachines.h"

#include <algorithm>

namespace mmDesk
{
	namespace ed = elektronData;
	using Value = ed::json::Value;
	using deskCore::Outcome;
	using deskCore::Source;

	namespace
	{
		constexpr double g_readBackTimeoutMs = 8000;
		constexpr double g_liveEditHoldMs = 200;	// our own live edit may not have landed in memory yet
		constexpr deskCore::LoadQueue<Ref>::Policy g_loadPolicy{25, 2};
		constexpr double g_bytesPerMs = 3.125;		// DIN MIDI

		size_t replyBytes(const Kind _k)
		{
			switch(_k)
			{
			case Kind::Pattern: return 3200;
			case Kind::Kit: return 820;
			case Kind::Song: return 5600;
			case Kind::Global: return 900;
			}
			return 0;
		}

		int num(const Value& _m, const char* _key, const int _default = -1)
		{
			const auto* v = _m.find(_key);
			return v && v->isNumber() ? static_cast<int>(v->asNumber()) : _default;
		}

		Outcome ok(std::string _note = {}) { return {{}, std::move(_note), {}}; }
		Outcome refuse(std::string _error) { return {{std::move(_error)}, {}, {}}; }

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

	const std::vector<Profile>& profiles()
	{
		static const std::vector<Profile> list{{"emu", "EMU OS 1.32B", false}, {"hw", "HW MIDI", true}};
		return list;
	}

	const Profile* profile(const std::string& _id)
	{
		for(const auto& p : profiles())
			if(p.id == _id)
				return &p;
		return nullptr;
	}

	MmMachine::MmMachine(Profile _profile, Port _port) : m_profile(std::move(_profile)), m_port(std::move(_port))
	{
		m_wireSinceMs = m_port.nowMs ? m_port.nowMs() : 0;
	}

	// ---- facts ----

	deskCore::LifeFacts MmMachine::facts() const
	{
		using P = deskCore::LifeFacts::Probe;
		using A = deskCore::LifeFacts::Animation;
		deskCore::LifeFacts f;
		if(m_profile.wire)
		{
			f.probe = P::Wire;
			f.replied = m_replied;
			f.animation = A::Absent;
			f.silentMs = now() - (m_replied ? m_lastReplyMs : m_wireSinceMs);
			return f;
		}
		switch(m_firmware)
		{
		case Firmware::Missing: f.probe = P::Missing; break;
		case Firmware::Unsupported: f.probe = P::Unsupported; break;
		case Firmware::Loading: f.probe = P::Loading; break;
		case Firmware::Booting: f.probe = P::Booting; break;
		case Firmware::Running:
		case Firmware::Ready: f.probe = P::Running; break;
		}
		// The Monomachine takes input once its start screen is gone (MM-P0 §6); no status reply is
		// needed for that.
		f.replied = true;
		const bool animating = m_tel.valid ? m_tel.screen == Screen::Unknown || m_tel.screen == Screen::Boot : true;
		f.animation = m_firmware == Firmware::Ready || !animating ? A::Over : A::Running;
		return f;
	}

	void MmMachine::setFirmware(const Firmware _firmware)
	{
		if(_firmware == m_firmware)
			return;
		const bool wasReady = ready();
		const bool wasRunning = m_firmware == Firmware::Running || m_firmware == Firmware::Ready;
		m_firmware = _firmware;
		const bool running = m_firmware == Firmware::Running || m_firmware == Firmware::Ready;
		if(running && !wasRunning && !m_known.empty())
			startOver();
		if(!wasReady && ready())
			m_lastStatusMs = -1e9;	// status now
	}

	// The machine booted again (a restored project, a new device): read everything again.
	void MmMachine::startOver()
	{
		m_known.clear();
		m_kits = {};
		m_working.reset();
		m_workingRegion.reset();
		m_pushes.clear();
		m_loads = {};
		m_backgroundQueued = false;
		m_recv = {};
		m_curPattern = m_curKit = m_curSong = m_curGlobal = m_songMode = m_queuedPattern = -1;
		m_sequence.clear();
		m_events.push_back(Ev::reset());
	}

	deskCore::Capabilities MmMachine::capabilities() const
	{
		deskCore::Capabilities c;
		c.engine = m_profile.id;
		c.label = m_profile.label;
		const bool panel = m_port.pressKeys && !m_profile.wire;
		c.set("transport", static_cast<bool>(m_port.pressKeys), "No transport here.");
		c.set("panelKeys", panel, "Over HW MIDI the editor cannot press the machine's keys.");
		c.set("recvSession", panel, "A real Monomachine takes dumps only on GLOBAL › FILE › SYSEX RECV: put it there to send "
			"patterns, songs and globals.");
		c.set("lcd", !m_profile.wire, "Over HW MIDI the machine's own LCD is on the machine.");
		c.set("workingKitMemory", !m_profile.wire, "Over HW MIDI the working kit is the stored slot plus the edits the editor saw.");
		c.set("telemetry", !m_profile.wire, "Over HW MIDI there is no playhead to follow.");
		// What the editor does not do on either engine yet (MM-P3), with the reason.
		c.set("midiMutes", false, "MIDI track mutes are set on the machine (FUNCTION + a track key in MIDI mode). The plug-in has "
			"no command for them yet.");
		c.set("poly", false, "POLY is switched on the machine. The editor does not drive it yet.");
		c.set("multiTrig", false, "MULTI TRIG mode, split and timing are settings the editor does not decode yet: set them on the "
			"machine. The keys here do play on the MULTI TRIG channel.");
		c.set("multiMap", false, "MULTI MAP ranges live in the global slot. The editor reads each range's upper key and pattern; "
			"offset, length, transpose and timing are not decoded yet, so edit the map on the machine (GLOBAL › CONTROL › "
			"MULTIMAP EDIT). The keys here do play on the MULTI MAP channel.");
		c.set("portamento", false, "PORTAMENTO mode (ALWAYS / ONLY LEGATO) is not decoded in the kit yet: set it on the machine.");
		c.set("gridRecord", false, "GRID RECORD and LIVE RECORD run on the machine. In the editor you draw steps directly.");
		c.values.emplace_back("dumps", m_profile.wire ? "manual" : "recv");
		return c;
	}

	bool MmMachine::busy() const
	{
		for(const auto& [ref, push] : m_pushes)
			if(push.slot.busy())
				return true;
		return false;
	}

	std::vector<MmMachine::Ev> MmMachine::drain()
	{
		auto e = std::move(m_events);
		m_events.clear();
		return e;
	}

	void MmMachine::observe(const Document& _doc, const Source _source)
	{
		m_known.insert(refOf(_doc));
		m_events.push_back(Ev::observed(_doc, _source));
	}

	void MmMachine::forget(const Ref& _ref)
	{
		m_known.erase(_ref);
		m_events.push_back(Ev::forget(_ref));
	}

	std::string MmMachine::kitState() const
	{
		if(m_curKit < 0 || !m_working || !m_kits[m_curKit])
			return "unknown";
		return ed::mmKitRaw(*m_working) == ed::mmKitRaw(*m_kits[m_curKit]) ? "clean" : "edited";
	}

	bool MmMachine::pressKeys(const std::vector<Key>& _keys)
	{
		const auto s = m_recv.state();
		if(s == RecvSession::State::Entering || s == RecvSession::State::ToMain || s == RecvSession::State::Leaving)
			return false;
		return m_port.pressKeys && m_port.pressKeys(_keys);
	}

	// ---- core -> machine ----

	Outcome MmMachine::review(const Value&, const std::vector<Change>& _changes, const Documents& _view)
	{
		for(const auto& c : _changes)
		{
			if(c.ref().kind != Kind::Global || static_cast<int>(c.ref().slot) != m_curGlobal)
				continue;
			const auto it = _view.globals.find(c.ref().slot);
			if(it != _view.globals.end() && std::get<ed::MmGlobal>(c.after).baseChannel != it->second.baseChannel)
				return refuse("The editor talks to the machine on the active global's base channel; change it on the machine.");
		}
		return ok();
	}

	Outcome MmMachine::submit(const Change& _change, const Documents&)
	{
		const auto ref = _change.ref();
		m_recv.touch(now());
		switch(ref.kind)
		{
		case Kind::Pattern:
			pushDump(ref, ed::encodeMmPattern(std::get<ed::MmPattern>(_change.after)));
			return ok();
		case Kind::Song:
			pushDump(ref, ed::encodeMmSong(std::get<ed::MmSong>(_change.after)));
			return ok(static_cast<int>(ref.slot) == m_curSong ? "Heard after STOP and LOAD SONG." : "");
		case Kind::Global:
			pushDump(ref, ed::encodeMmGlobal(std::get<ed::MmGlobal>(_change.after)));
			return ok();
		case Kind::Kit:
		{
			const auto& k = std::get<ed::MmKit>(_change.after);
			if(!_change.slotWrite && m_working)
			{
				std::vector<std::string> notes;
				deliverKitLive(std::get<ed::MmKit>(_change.before), k, notes);
				m_working = k;
				m_liveEditMs = now();
				// No read-back exists for a live edit: the working kit is what was sent, until memory shows it.
				observe(k, Source::Tracked);
				m_events.push_back(Ev::settled(ref, true));
				std::string note;
				for(const auto& n : notes)
					note += (note.empty() ? "" : " ") + n;
				return ok(note);
			}
			m_kits[ref.slot] = k;
			pushDump(ref, ed::encodeMmKit(k));
			return ok();
		}
		}
		return ok();
	}

	void MmMachine::deliverKitLive(const ed::MmKit& _from, const ed::MmKit& _to, std::vector<std::string>& _notes)
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
			if(m_profile.wire)
				m_port.sendSysex(ed::mmLoadKit(k.position));
			else
				m_recv.want(ed::mmLoadKit(k.position));
			m_kits[k.position] = k;
			_notes.push_back("Written to the kit slot and loaded (no live SysEx for this setting).");
		}
	}

	void MmMachine::pushDump(const Ref& _ref, Bytes _dump)
	{
		auto& p = m_pushes[_ref];
		if(!p.slot.want(_dump))
			return;	// waits for the read-back in flight; latest wins
		p.sentMs = now();
		if(m_profile.wire)
		{
			// HW MIDI: straight to the wire; the machine takes it only on SYSEX RECV (the user parks it there).
			m_port.sendSysex(_dump);
			p.onRecv = false;
			request(_ref, true);
			return;
		}
		p.onRecv = true;
		m_recv.want(std::move(_dump));
	}

	// ---- machine commands ----

	const std::map<std::string, MmMachine::Handler>& MmMachine::handlers()
	{
		static const std::map<std::string, Handler> map{
			{"load", &MmMachine::cmdLoad},
			{"select", &MmMachine::cmdSelect},
			{"loadKit", &MmMachine::cmdKit},
			{"saveKit", &MmMachine::cmdKit},
			{"loadSong", &MmMachine::cmdSong},
			{"saveSong", &MmMachine::cmdSong},
			{"tempo", &MmMachine::cmdTempo},
			{"play", &MmMachine::cmdTransport},
			{"stop", &MmMachine::cmdTransport},
			{"mute", &MmMachine::cmdMute}};
		return map;
	}

	Outcome MmMachine::command(const Value& _command, const Documents& _view)
	{
		const auto op = deskCore::opOf(_command);
		const auto it = handlers().find(op);
		if(it == handlers().end())
			return refuse("unknown command " + op);
		return (this->*(it->second))(_command, _view);
	}

	Outcome MmMachine::cmdLoad(const Value& _m, const Documents&)
	{
		const auto* k = _m.find("kind");
		const auto kind = k && k->isString() ? kindFromName(k->asString()) : std::nullopt;
		request({kind.value_or(Kind::Pattern), static_cast<uint8_t>(num(_m, "slot", 0))}, true);
		return ok();
	}

	Outcome MmMachine::cmdSelect(const Value& _m, const Documents&)
	{
		const auto p = num(_m, "p");
		const auto* nowFlag = _m.find("now");
		const bool playing = m_tel.valid && m_tel.running;
		if(nowFlag && nowFlag->isBool() && nowFlag->asBool() && playing)
		{
			// The machine only switches at the pattern end: STOP, LOAD PATTERN, PLAY, each when the
			// machine shows the one before (P6: in the adapter, not the page).
			m_sequence.start({{Act::Stop, 0, deskCore::Wait::Stopped, 0, 1000},
				{Act::SelectPattern, p, deskCore::Wait::PatternIs, p, 1000},
				{Act::Play}});
			pumpSequence(now());
			return ok();
		}
		m_port.sendSysex(ed::mmLoadPattern(static_cast<uint8_t>(p)));
		if(playing)
			m_queuedPattern = p;
		else
		{
			m_queuedPattern = -1;
			m_curPattern = p;
		}
		m_lastStatusMs = now() - 800;	// status soon
		if(!m_known.count({Kind::Pattern, static_cast<uint8_t>(p)}))
			request({Kind::Pattern, static_cast<uint8_t>(p)}, true);
		return ok(m_queuedPattern >= 0 ? "queued: starts at the pattern end" : "");
	}

	Outcome MmMachine::cmdKit(const Value& _m, const Documents&)
	{
		const auto k = num(_m, "k", m_curKit);
		if(k < 0 || k > 127)
			return refuse("kit 0-127");
		if(deskCore::opOf(_m) == "loadKit")
			m_port.sendSysex(ed::mmLoadKit(static_cast<uint8_t>(k)));
		else
		{
			m_port.sendSysex(ed::mmSaveKit(static_cast<uint8_t>(k)));
			request({Kind::Kit, static_cast<uint8_t>(k)}, true);	// the stored slot now
		}
		const auto from = m_curKit;
		m_curKit = k;
		kitSwitched(from, k);
		m_lastStatusMs = now() - 800;
		return ok();
	}

	Outcome MmMachine::cmdSong(const Value& _m, const Documents&)
	{
		const auto s = num(_m, "s", m_curSong);
		if(s < 0 || s > 23)
			return refuse("song 0-23");
		const bool load = deskCore::opOf(_m) == "loadSong";
		if(load && m_tel.valid && m_tel.running)
			return refuse("The machine loads a song only while stopped.");
		m_port.sendSysex(load ? ed::mmLoadSong(static_cast<uint8_t>(s)) : ed::mmSaveSong(static_cast<uint8_t>(s)));
		if(!load)
			request({Kind::Song, static_cast<uint8_t>(s)}, true);
		m_curSong = s;
		return ok();
	}

	Outcome MmMachine::cmdTempo(const Value& _m, const Documents&)
	{
		m_port.sendSysex(ed::mmSetTempo(_m.find("bpm")->asNumber()));
		return ok();
	}

	Outcome MmMachine::cmdTransport(const Value& _m, const Documents&)
	{
		const bool play = deskCore::opOf(_m) == "play";
		return pressKeys({play ? Key::Play : Key::Stop}) ? ok() : refuse("The panel is busy (SYSEX RECV); try again.");
	}

	Outcome MmMachine::cmdMute(const Value& _m, const Documents&)
	{
		m_port.sendParam(static_cast<uint8_t>(num(_m, "t")), 8, 0, num(_m, "on", 0) ? 1 : 0);
		return ok();
	}

	void MmMachine::pumpSequence(const double _now)
	{
		if(!m_sequence.running())
			return;
		deskCore::SeqFacts f;
		f.playing = m_tel.valid && m_tel.running;
		f.statusReplies = m_statusReplies;
		f.pattern = m_curPattern;
		for(const auto& d : m_sequence.due(_now, f))
		{
			switch(d.action)
			{
			case Act::Stop: pressKeys({Key::Stop}); break;
			case Act::Play: pressKeys({Key::Play}); break;
			case Act::SelectPattern:
				m_port.sendSysex(ed::mmLoadPattern(static_cast<uint8_t>(d.arg)));
				m_queuedPattern = -1;
				requestStatus();
				m_lastStatusMs = _now;
				break;
			}
		}
	}

	// ---- loading ----

	void MmMachine::request(const Ref& _r, const bool _urgent)
	{
		m_loads.want(_r, _urgent);
	}

	void MmMachine::requestStatus()
	{
		for(const auto p : {ed::MmStatus::Pattern, ed::MmStatus::Kit, ed::MmStatus::Song, ed::MmStatus::Global, ed::MmStatus::SongMode})
			m_port.sendSysex(ed::mmStatusRequest(p));
	}

	void MmMachine::pumpLoads(const double _now)
	{
		const auto& inFlight = m_loads.loading();
		const double timeout = 400 + (m_profile.wire && inFlight ? 1.5 * static_cast<double>(replyBytes(inFlight->kind)) / g_bytesPerMs : 0);
		const auto next = m_loads.next(_now, timeout, true, g_loadPolicy);
		if(!next)
			return;
		const auto& r = *next;
		m_port.sendSysex(r.kind == Kind::Pattern ? ed::mmPatternRequest(r.slot) : r.kind == Kind::Kit ? ed::mmKitRequest(r.slot)
			: r.kind == Kind::Song ? ed::mmSongRequest(r.slot) : ed::mmGlobalRequest(r.slot));
	}

	void MmMachine::pumpRecv(const double _now)
	{
		if(m_profile.wire)
			return;
		auto out = m_recv.tick(_now, m_tel);
		if(!out.keys.empty() && m_port.pressKeys)
			m_port.pressKeys(out.keys);
		for(const auto& s : out.sends)
		{
			m_port.sendSysex(s);
			// Which push was this? Ask for the read-back of every push that waited for RECV.
			for(auto& [ref, push] : m_pushes)
				if(push.onRecv && push.slot.inFlight() && *push.slot.inFlight() == s)
				{
					push.onRecv = false;
					request(ref, true);
				}
		}
	}

	// ---- device -> machine ----

	void MmMachine::onSysex(const Bytes& _m)
	{
		if(_m.size() < 8 || _m[0] != 0xf0 || _m[4] != ed::g_mmProductId)
			return;
		m_lastReplyMs = now();
		const auto cmd = _m[6];
		if(cmd == 0x72)
		{
			if(const auto s = ed::parseMmStatusResponse(_m))
			{
				++m_statusReplies;
				m_replied = true;
				onStatus(static_cast<uint8_t>(s->param), s->value);
			}
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

	void MmMachine::onDump(const Ref& _r, const Bytes& _sysex)
	{
		std::optional<Document> doc;
		switch(_r.kind)
		{
		case Kind::Pattern: if(auto v = ed::decodeMmPattern(_sysex)) doc = *v; break;
		case Kind::Kit: if(auto v = ed::decodeMmKit(_sysex)) { m_kits[_r.slot] = *v; doc = *v; } break;
		case Kind::Song: if(auto v = ed::decodeMmSong(_sysex)) doc = *v; break;
		case Kind::Global: if(auto v = ed::decodeMmGlobal(_sysex)) doc = *v; break;
		}
		m_loads.arrived(_r);
		if(!doc)
			return;
		const auto it = m_pushes.find(_r);
		if(it != m_pushes.end() && it->second.slot.busy() && !it->second.onRecv)
		{
			auto& push = it->second;
			const auto t = now();
			switch(push.slot.onReadBack(_sysex))
			{
			case deskCore::PushSlot<Bytes>::ReadBack::Confirmed:
				m_lastRoundTripMs = t - push.sentMs;
				m_lastError.clear();
				m_events.push_back(Ev::settled(_r, true));
				break;
			case deskCore::PushSlot<Bytes>::ReadBack::ConfirmedSendNext:
			{
				m_lastRoundTripMs = t - push.sentMs;
				m_lastError.clear();
				auto next = *push.slot.inFlight();
				push.slot.abandon();
				pushDump(_r, std::move(next));
				break;
			}
			default:
			{
				// The read-back after our push holds something else: say so, show what it holds.
				m_lastError = std::string("The machine holds a different ") + kindName(_r.kind) + " than was sent.";
				auto next = push.slot.next();
				push.slot.abandon();
				if(next)
					pushDump(_r, std::move(*next));
				else
					m_events.push_back(Ev::settled(_r, true));
				break;
			}
			}
		}
		// The current kit's page document is the working kit; a stored-slot dump changes its edited state only.
		if(_r.kind == Kind::Kit && static_cast<int>(_r.slot) == m_curKit && m_working)
			return;
		observe(*doc, Source::Dump);
	}

	void MmMachine::kitSwitched(const int _from, const int _to)
	{
		if(_from == _to)
			return;
		// The kit that played is its stored slot again; the new one is its working kit once memory says so.
		if(_from >= 0)
		{
			if(m_kits[_from])
				observe(*m_kits[_from], Source::Dump);
			else
				forget({Kind::Kit, static_cast<uint8_t>(_from)});
		}
		if(_to >= 0 && !m_kits[_to])
			request({Kind::Kit, static_cast<uint8_t>(_to)}, true);
	}

	void MmMachine::onStatus(const uint8_t _param, const uint8_t _value)
	{
		switch(static_cast<ed::MmStatus>(_param))
		{
		case ed::MmStatus::Pattern:
			if(m_curPattern != _value)
			{
				m_curPattern = _value;
				if(!m_known.count({Kind::Pattern, _value}))
					request({Kind::Pattern, _value}, true);
			}
			if(m_queuedPattern == _value)
				m_queuedPattern = -1;
			break;
		case ed::MmStatus::Kit:
			if(m_curKit != _value)
			{
				const auto from = m_curKit;
				m_curKit = _value;
				kitSwitched(from, _value);
			}
			break;
		case ed::MmStatus::Song:
			if(m_curSong != _value)
			{
				m_curSong = _value;
				if(!m_known.count({Kind::Song, static_cast<uint8_t>(_value % 24)}))
					request({Kind::Song, static_cast<uint8_t>(_value % 24)}, true);
			}
			break;
		case ed::MmStatus::Global:
			if(m_curGlobal != _value)
			{
				m_curGlobal = _value;
				if(!m_known.count({Kind::Global, static_cast<uint8_t>(_value & 7)}))
					request({Kind::Global, static_cast<uint8_t>(_value & 7)}, true);
			}
			break;
		case ed::MmStatus::SongMode:
			m_songMode = _value;
			break;
		default:
			return;
		}
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

	void MmMachine::onTelemetry(const Telemetry& _t)
	{
		m_tel = _t;
		// Playing = the RAM flag, or the step byte advancing: two single steps forward (or a wrap to 0)
		// in a row, each within three step times at the tempo (a 3/4X pattern included). A stop that
		// resets the step to 0 is one move, so it never reads as playing.
		const double t = now();
		const double stepMs = _t.tempo > 0 ? 360000.0 / _t.tempo : 250.0;
		const double window = std::max(250.0, 3.0 * stepMs);
		if(_t.valid && _t.step != m_rawStep)
		{
			const bool forward = m_rawStep >= 0 && (_t.step == m_rawStep + 1 || (_t.step == 0 && m_rawStep > 0));
			m_stepMoves = forward && t - m_stepMovedMs < window ? std::min(m_stepMoves + 1, 2) : (forward ? 1 : 0);
			m_stepMovedMs = t;
			m_rawStep = _t.step;
		}
		if(m_stepMoves >= 2 && t - m_stepMovedMs < window)
			m_tel.running = m_tel.running || m_tel.valid;
		else if(t - m_stepMovedMs >= window)
			m_stepMoves = 0;
		if(!m_tel.running && m_queuedPattern >= 0)
			m_lastStatusMs = -1e9;	// stopped: LOAD PATTERN switches at once; status will say
		pumpSequence(t);
	}

	void MmMachine::onWorkingKit(const Bytes& _region)
	{
		m_workingRegion = _region;
	}

	void MmMachine::applyWorkingKit(const double _now)
	{
		// The working kit from memory, unless our own live edit may not have landed yet.
		if(!m_workingRegion || _now - m_liveEditMs <= g_liveEditHoldMs || m_workingRegion->size() < 5 + ed::MmKit::g_rawSize)
			return;
		const auto kitNumber = static_cast<int>((*m_workingRegion)[0] & 127);
		const std::vector<uint8_t> raw(m_workingRegion->begin() + 5, m_workingRegion->begin() + 5 + ed::MmKit::g_rawSize);
		m_workingRegion.reset();
		auto k = ed::mmKitFromRaw(raw, static_cast<uint8_t>(kitNumber));
		if(!k)
			return;
		const bool changed = !m_working || ed::mmKitRaw(*m_working) != raw || m_working->position != k->position
			|| m_curKit != kitNumber || !m_known.count({Kind::Kit, static_cast<uint8_t>(kitNumber)});
		if(m_curKit != kitNumber)
		{
			const auto from = m_curKit;
			m_curKit = kitNumber;
			kitSwitched(from, kitNumber);
		}
		if(!changed)
			return;
		m_working = *k;
		observe(*k, Source::Memory);
		if(!m_kits[m_curKit])
			request({Kind::Kit, static_cast<uint8_t>(m_curKit)}, true);
	}

	void MmMachine::tick(const double _now, const Documents&)
	{
		pumpSequence(_now);
		// The emulator takes requests once its start screen is gone; over HW MIDI the status polls
		// are what finds the machine.
		if(!m_profile.wire && !ready())
			return;
		// Status: once a second, four times while a pattern is queued.
		if(_now - m_lastStatusMs > (m_queuedPattern >= 0 ? 250 : 1000))
		{
			m_lastStatusMs = _now;
			requestStatus();
		}
		pumpRecv(_now);
		pumpLoads(_now);
		applyWorkingKit(_now);
		// A read-back that never came: give up on that push.
		for(auto& [ref, push] : m_pushes)
		{
			if(!push.slot.busy() || push.onRecv || _now - push.sentMs <= g_readBackTimeoutMs)
				continue;
			push.slot.abandon();
			m_lastError = std::string("No read-back for a ") + kindName(ref.kind) + " dump.";
			m_events.push_back(Ev::settled(ref, true));
		}
		// The playhead, at most every 25 ms.
		const bool playing = m_tel.valid && m_tel.running;
		if(m_tel.valid && (m_tel.step != m_lastStep || playing != m_lastPlaying) && _now - m_lastTelemetryMs > 25)
		{
			m_lastStep = m_tel.step;
			m_lastPlaying = playing;
			m_lastTelemetryMs = _now;
			Value t = Value::object();
			t.set("type", "tel");
			t.set("step", m_tel.step);
			t.set("playing", m_tel.running);
			m_events.push_back(Ev::noticeOf(std::move(t)));
		}
	}

	// ---- the machine document ----

	Value MmMachine::state(const Documents&) const
	{
		Value d = Value::object();
		d.set("schema", "mm-desk/machine");
		d.set("version", 1);
		d.set("engine", deskCore::legacyFirmware(lifecycle()));
		d.set("link", deskCore::legacyLink(lifecycle(), m_profile.wire));
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
			inFlight += push.slot.busy();
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
		Value engines = Value::array();
		for(const auto& e : profiles())
			engines.push(deskCore::EngineChoice{e.id, e.label, true, {}}.toJson());
		d.set("engines", std::move(engines));
		return d;
	}
}
