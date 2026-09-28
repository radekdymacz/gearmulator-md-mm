#include "mmDeskMachine.h"

#include "elektronData/mmCommands.h"
#include "elektronData/mmDump.h"
#include "elektronData/mmJson.h"
#include "elektronData/mmMachines.h"

#include "deskCore/deskKinds.h"
#include "deskCore/deskPacer.h"

#include <cstdio>

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
		constexpr deskCore::LoadQueue<Ref>::Policy g_loadPolicy{25, 2};
		constexpr double g_loadTimeoutMs = 400;

		// The dump a request brings back, for timeouts at DIN speed (the kind's record).
		size_t replyBytes(const Kind _k)
		{
			const auto* s = deskCore::kindSpec<MmModel>(_k);
			return s ? s->replyBytes : 0;
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

	const Profile& emulatorProfile()
	{
		static const Profile p{"emu", "EMU OS 1.32B", "Engine: the real Monomachine OS 1.32B runs inside the app. Choose HW MIDI to "
			"edit a real Monomachine instead.", false, true, true, true};
		return p;
	}

	const Profile& wireProfile()
	{
		static const Profile p{"hw", "HW MIDI", "Engine: a real Monomachine on the plug-in's MIDI in and out, at MIDI speed. "
			"Pattern, song and global dumps need it on GLOBAL › FILE › SYSEX RECV; PLAY/STOP are MIDI Start/Stop.", true, false, false, false};
		return p;
	}

	bool reflects(const ed::MmKit& _image, const ed::MmKit& _before, const ed::MmKit& _after)
	{
		const auto image = ed::mmKitRaw(_image), from = ed::mmKitRaw(_before), to = ed::mmKitRaw(_after);
		if(image.size() != to.size() || from.size() != to.size())
			return true;
		for(size_t i = 0; i < to.size(); ++i)
			if(from[i] != to[i] && image[i] != to[i])
				return false;
		return true;
	}

	MmMachine::MmMachine(Profile _profile, Port _port)
		: m_profile(std::move(_profile))
		, m_port(std::move(_port))
		, m_wire(m_port.nowMs ? m_port.nowMs() : 0)
	{
	}

	// ---- facts ----

	deskCore::LifeFacts MmMachine::facts() const
	{
		using P = deskCore::LifeFacts::Probe;
		using A = deskCore::LifeFacts::Animation;
		if(m_profile.wire)
			return m_wire.facts(now());
		deskCore::LifeFacts f;
		f.probe = m_probe;
		// The Monomachine takes input once its start screen is gone (MM-P0 §6); no status reply is
		// needed for that.
		f.replied = true;
		const bool animating = m_tel.valid ? m_tel.screen == Screen::Unknown || m_tel.screen == Screen::Boot : true;
		f.animation = animating ? A::Running : A::Over;
		return f;
	}

	void MmMachine::setProbe(const Probe _probe)
	{
		if(_probe == m_probe)
			return;
		const bool wasReady = ready();
		const bool restarted = _probe == Probe::Running && m_probe != Probe::Running && knowsAnything();
		m_probe = _probe;
		if(restarted)
			startOver();
		if(!wasReady && ready())
			m_lastStatusMs = -1e9;	// status now
	}

	// The machine booted again (a restored project, a new device): read everything again.
	void MmMachine::startOver()
	{
		m_working = {};
		m_pushes.clear();
		m_loads = {};
		m_backgroundQueued = false;
		m_recv = {};
		m_curPattern = m_curKit = m_curSong = m_curGlobal = m_songMode = m_queuedPattern = -1;
		m_sequence.clear();
		startedOver();
	}

	deskCore::Capabilities MmMachine::capabilities() const
	{
		deskCore::Capabilities c;
		c.engine = m_profile.id;
		c.label = m_profile.label;
		c.about = m_profile.about;
		const bool panel = m_port.pressKeys && m_profile.panel;
		c.set("transport", static_cast<bool>(m_port.pressKeys), "This engine has no transport.");
		c.set("panelKeys", panel, "The editor cannot press this machine's keys.");
		c.set("recvSession", panel, "The Monomachine takes dumps only on GLOBAL › FILE › SYSEX RECV, and the editor cannot put it there:"
			" put it there to send patterns, songs and globals.");
		c.set("lcd", m_profile.memory, "The machine's own LCD is on the machine.");
		c.set("workingKitMemory", m_profile.memory, "The working kit is the stored slot plus the edits the editor saw.");
		c.set("telemetry", m_profile.telemetry, "This engine reports no playhead to follow.");
		c.values.emplace_back("dumps", m_profile.panel ? "recv" : "manual");
		return c;
	}

	bool MmMachine::busy() const
	{
		for(const auto& [ref, push] : m_pushes)
			if(push.slot.busy())
				return true;
		// A live edit the machine's memory does not show yet is on the wire too.
		return m_working.expect.expecting(now());
	}

	std::string MmMachine::kitState(const Documents& _view) const
	{
		const auto* working = _view.workingKitOf(m_curKit);
		const auto stored = m_curKit >= 0 ? _view.kits.find(static_cast<uint8_t>(m_curKit)) : _view.kits.end();
		if(!working || stored == _view.kits.end())
			return "unknown";
		return ed::mmKitRaw(*working) == ed::mmKitRaw(stored->second) ? "clean" : "edited";
	}

	namespace
	{
		Outcome ask(const char* _what, std::string _message, const char* _confirm)
		{
			Outcome o;
			o.ask = deskCore::Ask{_what, std::move(_message), _confirm, Value::object()};
			return o;
		}

		// A slot counts as empty in the library: no name (or the erased 0xff one).
		bool emptyKit(const ed::MmKit& _k)
		{
			for(const auto c : _k.name)
				if(c && c != 0xff && c != ' ')
					return false;
			return true;
		}

		std::string kitLabel(const Documents& _view, const int _slot)
		{
			char k[8];
			std::snprintf(k, sizeof(k), "K%03d", _slot + 1);
			const auto* w = _view.workingKitOf(_slot);
			const auto it = _view.kits.find(static_cast<uint8_t>(_slot));
			const auto* kit = w ? w : it != _view.kits.end() ? &it->second : nullptr;
			std::string name;
			if(kit)
				for(const auto c : kit->name)
					if(c)
						name += static_cast<char>(c);
			return std::string(k) + (name.empty() ? std::string() : " " + name);
		}
	}

	// What a kit command would lose asks first (the core sends it again with force): one question
	// function per command that has one.
	const std::map<std::string, MmMachine::Handler>& MmMachine::askers()
	{
		static const std::map<std::string, Handler> map{{"loadKit", &MmMachine::askLoadKit}, {"saveKit", &MmMachine::askSaveKit},
			{"select", &MmMachine::askSelect}};
		return map;
	}

	std::vector<std::string> MmMachine::commandsAsking()
	{
		std::vector<std::string> ops;
		for(const auto& [op, h] : askers())
			ops.push_back(op);
		return ops;
	}

	Outcome MmMachine::askFor(const Value& _command, const Documents& _view)
	{
		const auto it = askers().find(deskCore::opOf(_command));
		return it == askers().end() ? ok() : (this->*(it->second))(_command, _view);
	}

	// LOAD KIT (the playing kit's: a reload) drops the unsaved edits of the kit that plays.
	Outcome MmMachine::askLoadKit(const Value& _command, const Documents& _view)
	{
		const auto k = num(_command, "k", m_curKit);
		if(kitState(_view) != "edited")
			return ok();
		if(k == m_curKit)
			return ask("reloadKit", "Reload <b>" + kitLabel(_view, k) + "</b> from the machine? Your edits go to its UNDO KIT.",
				"Reload (discard edits)");
		return ask("loadKit", "Load <b>" + kitLabel(_view, k) + "</b>? Your edits to <b>" + kitLabel(_view, m_curKit)
			+ "</b> are not saved on the machine. They go to its UNDO KIT.", "Load (edits to UNDO KIT)");
	}

	// A pattern that links another kit loads it: the unsaved edits of the kit that plays are lost.
	Outcome MmMachine::askSelect(const Value& _command, const Documents& _view)
	{
		const auto p = num(_command, "p");
		const auto it = _view.patterns.find(static_cast<uint8_t>(p));
		if(it == _view.patterns.end() || static_cast<int>(it->second.kit) == m_curKit || kitState(_view) != "edited")
			return ok();
		Outcome o = ask("discardKit", "<b>" + ed::mmPatternName(static_cast<uint8_t>(p)) + "</b> uses kit <b>" + kitLabel(_view, it->second.kit)
			+ "</b>. Your edits to <b>" + kitLabel(_view, m_curKit) + "</b> are not saved on the machine and will be lost.", "Switch and lose edits");
		o.ask->details.set("p", p);
		o.ask->details.set("kit", m_curKit);
		o.ask->details.set("target", static_cast<int>(it->second.kit));
		return o;
	}

	// SAVE KIT n over another slot that holds a kit.
	Outcome MmMachine::askSaveKit(const Value& _command, const Documents& _view)
	{
		const auto k = num(_command, "k", m_curKit);
		if(k == m_curKit || k < 0)
			return ok();
		const auto it = _view.kits.find(static_cast<uint8_t>(k));
		if(it == _view.kits.end() || emptyKit(it->second))
			return ok();
		return ask("overwriteSlot", "Overwrite <b>" + kitLabel(_view, k) + "</b> with the kit that plays, <b>" + kitLabel(_view, m_curKit)
			+ "</b>? The machine keeps the overwritten kit in its UNDO KIT.", "Overwrite");
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
			// A stored slot: its dump on SYSEX RECV.
			pushDump(ref, ed::encodeMmKit(std::get<ed::MmKit>(_change.after)));
			return ok();
		case Kind::WorkingKit:
		{
			const auto& before = std::get<WorkingKit>(_change.before).kit;
			const auto& after = std::get<WorkingKit>(_change.after).kit;
			if(after.position != m_curKit)
				return refuse("Only the kit that plays can be edited live");
			std::vector<std::string> notes;
			deliverKitLive(before, after, notes);
			// Pending until memory shows it (or it is too old to wait for); without memory nothing
			// reads it back: done as sent.
			if(m_profile.memory)
				m_working.expect.sent(before, after, now());
			else
				settle(WorkingKit{after}, Source::Tracked);
			std::string note;
			for(const auto& n : notes)
				note += (note.empty() ? "" : " ") + n;
			return ok(note);
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
		const auto tag = m_nextRecvTag++;
		m_recvRefs[tag] = _ref;
		m_recv.want(std::move(_dump), tag);
	}

	// ---- machine commands ----

	const std::map<std::string, MmMachine::Handler>& MmMachine::handlers()
	{
		static const std::map<std::string, Handler> map{
			{"load", &MmMachine::cmdLoad}, {"select", &MmMachine::cmdSelect}, {"loadKit", &MmMachine::cmdLoadKit},
			{"saveKit", &MmMachine::cmdSaveKit}, {"loadSong", &MmMachine::cmdLoadSong}, {"saveSong", &MmMachine::cmdSaveSong},
			{"tempo", &MmMachine::cmdTempo}, {"play", &MmMachine::cmdPlay}, {"stop", &MmMachine::cmdStop},
			{"mute", &MmMachine::cmdMute}};
		return map;
	}

	std::vector<std::string> MmMachine::commandsHandled()
	{
		std::vector<std::string> ops;
		for(const auto& [op, h] : handlers())
			ops.push_back(op);
		return ops;
	}

	Outcome MmMachine::command(const Value& _command, const Documents& _view)
	{
		const auto it = handlers().find(deskCore::opOf(_command));
		if(it == handlers().end())
			return refuse("unknown command " + deskCore::opOf(_command));
		return (this->*(it->second))(_command, _view);
	}

	Outcome MmMachine::cmdLoad(const Value& _m, const Documents&)
	{
		const auto* k = _m.find("kind");
		const auto kind = k && k->isString() ? kindFromName(k->asString()) : std::nullopt;
		if(!kind || *kind == Kind::WorkingKit)
			return refuse("kind: expected pattern, kit, song or global");
		request({*kind, static_cast<uint8_t>(num(_m, "slot", 0))}, true);
		return ok();
	}

	Outcome MmMachine::cmdSelect(const Value& _m, const Documents&)
	{
		const auto p = num(_m, "p");
		const auto* nowFlag = _m.find("now");
		const bool playing = m_playing;
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
		// The current pattern is what the machine reports; while it plays the switch waits for the
		// pattern end (the queue is the editor's request until status says so).
		m_queuedPattern = playing ? p : -1;
		requestStatus();
		m_lastStatusMs = now();
		if(!known({Kind::Pattern, static_cast<uint8_t>(p)}))
			request({Kind::Pattern, static_cast<uint8_t>(p)}, true);
		return ok(m_queuedPattern >= 0 ? "queued: starts at the pattern end" : "");
	}

	Outcome MmMachine::cmdLoadKit(const Value& _m, const Documents& _view)
	{
		const auto k = num(_m, "k", m_curKit);
		if(k == m_curKit && kitState(_view) == "clean")
			return refuse("The kit that plays matches its saved slot. Nothing to reload.");
		return kitAction(k, false);
	}
	Outcome MmMachine::cmdSaveKit(const Value& _m, const Documents&) { return kitAction(num(_m, "k", m_curKit), true); }

	Outcome MmMachine::kitAction(const int _k, const bool _save)
	{
		if(_k < 0 || _k > 127)
			return refuse("kit 0-127");
		if(_save)
		{
			m_port.sendSysex(ed::mmSaveKit(static_cast<uint8_t>(_k)));
			request({Kind::Kit, static_cast<uint8_t>(_k)}, true);	// the stored slot now
		}
		else
			m_port.sendSysex(ed::mmLoadKit(static_cast<uint8_t>(_k)));
		// The current kit is what the machine reports (status, memory). LOAD KIT and SAVE KIT relink the
		// current pattern to the kit (as on the MD): read it back; the firmware takes MIDI in order, so
		// the answer already shows the relink.
		requestStatus();
		m_lastStatusMs = now();
		if(m_curPattern >= 0)
			request({Kind::Pattern, static_cast<uint8_t>(m_curPattern)}, true);
		return ok();
	}

	Outcome MmMachine::cmdLoadSong(const Value& _m, const Documents&)
	{
		const auto s = num(_m, "s", m_curSong);
		if(s < 0 || s > 23)
			return refuse("song 0-23");
		if(m_playing)
			return refuse("The machine loads a song only while stopped.");
		m_port.sendSysex(ed::mmLoadSong(static_cast<uint8_t>(s)));
		requestStatus();	// the current song is what the machine reports
		m_lastStatusMs = now();
		return ok();
	}

	Outcome MmMachine::cmdSaveSong(const Value& _m, const Documents&)
	{
		const auto s = num(_m, "s", m_curSong);
		if(s < 0 || s > 23)
			return refuse("song 0-23");
		m_port.sendSysex(ed::mmSaveSong(static_cast<uint8_t>(s)));
		request({Kind::Song, static_cast<uint8_t>(s)}, true);
		requestStatus();
		m_lastStatusMs = now();
		return ok();
	}

	Outcome MmMachine::cmdTempo(const Value& _m, const Documents&)
	{
		m_port.sendSysex(ed::mmSetTempo(_m.find("bpm")->asNumber()));
		return ok();
	}

	Outcome MmMachine::cmdPlay(const Value&, const Documents&)
	{
		return pressKeys({Key::Play}) ? ok() : refuse("The panel is busy (SYSEX RECV); try again.");
	}

	Outcome MmMachine::cmdStop(const Value&, const Documents&)
	{
		return pressKeys({Key::Stop}) ? ok() : refuse("The panel is busy (SYSEX RECV); try again.");
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
		f.playing = m_playing;
		f.statusReplies = m_wire.statusReplies;
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
		const double timeout = g_loadTimeoutMs + (m_profile.wire && inFlight ? 1.5 * deskCore::DinPacer::wireMs(replyBytes(inFlight->kind)) : 0);
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
			m_port.sendSysex(s.bytes);
			// The push this dump answers (its tag): ask for its read-back.
			const auto tagged = m_recvRefs.find(s.tag);
			if(tagged == m_recvRefs.end())
				continue;
			const auto ref = tagged->second;
			m_recvRefs.erase(tagged);
			if(const auto it = m_pushes.find(ref); it != m_pushes.end() && it->second.onRecv)
			{
				it->second.onRecv = false;
				request(ref, true);
			}
		}
	}

	// ---- device -> machine ----

	void MmMachine::onSysex(const Bytes& _m)
	{
		if(_m.size() < 8 || _m[0] != 0xf0 || _m[4] != ed::g_mmProductId)
			return;
		m_wire.heard(now());
		const auto cmd = _m[6];
		if(cmd == 0x72)
		{
			if(const auto s = ed::parseMmStatusResponse(_m))
			{
				m_wire.statusReply(now());
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
		case Kind::Kit: if(auto v = ed::decodeMmKit(_sysex)) doc = *v; break;
		case Kind::Song: if(auto v = ed::decodeMmSong(_sysex)) doc = *v; break;
		case Kind::Global: if(auto v = ed::decodeMmGlobal(_sysex)) doc = *v; break;
		case Kind::WorkingKit: break;
		}
		m_loads.arrived(_r);
		if(!doc)
			return;
		if(_r.kind == Kind::Global && static_cast<int>(_r.slot) == (m_curGlobal & 7))
			setBaseChannel(std::get<ed::MmGlobal>(*doc));
		auto action = deskCore::ReadBackAction::Observe;
		const auto it = m_pushes.find(_r);
		if(it != m_pushes.end() && it->second.slot.busy() && !it->second.onRecv)
		{
			auto& push = it->second;
			action = deskCore::readBack(push.slot, _sysex);
			if(action == deskCore::ReadBackAction::Wait)
				return;	// an older reply: the read-back timeout fails the push
			m_lastRoundTripMs = now() - push.sentMs;
			if(action == deskCore::ReadBackAction::ObserveSendNext)
			{
				// The newer value goes out through RECV again.
				auto next = *push.slot.inFlight();
				push.slot.abandon();
				pushDump(_r, std::move(next));
			}
		}
		if(action == deskCore::ReadBackAction::Settle)
			settle(*doc, Source::Dump);
		else
			observe(*doc, Source::Dump);
		// Until memory shows it (or on a device without memory), the kit that plays starts as its slot.
		if(_r.kind == Kind::Kit && static_cast<int>(_r.slot) == m_curKit)
		{
			auto [next, seed] = deskCore::fromDump(std::move(m_working), std::get<ed::MmKit>(*doc));
			m_working = std::move(next);
			if(seed)
				observe(WorkingKit{*seed}, Source::Dump);
		}
	}

	void MmMachine::kitSwitched(const int _from, const int _to)
	{
		if(_from == _to)
			return;
		// The kit that played is gone; the new one comes from memory, or from its slot's dump.
		if(_from >= 0)
			forget({Kind::WorkingKit, 0});
		m_working = deskCore::switched(m_working);
		if(_to >= 0)
			request({Kind::Kit, static_cast<uint8_t>(_to)}, true);
	}

	void MmMachine::setBaseChannel(const ed::MmGlobal& _g)
	{
		if(m_port.baseChannel)
			m_port.baseChannel(static_cast<uint8_t>(_g.baseChannel & 0x0f));
	}

	void MmMachine::onStatus(const uint8_t _param, const uint8_t _value)
	{
		switch(static_cast<ed::MmStatus>(_param))
		{
		case ed::MmStatus::Pattern:
			if(m_curPattern != _value)
			{
				m_curPattern = _value;
				if(!known({Kind::Pattern, _value}))
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
				if(!known({Kind::Song, static_cast<uint8_t>(_value % 24)}))
					request({Kind::Song, static_cast<uint8_t>(_value % 24)}, true);
			}
			break;
		case ed::MmStatus::Global:
			if(m_curGlobal != _value)
			{
				m_curGlobal = _value;
				if(!known({Kind::Global, static_cast<uint8_t>(_value & 7)}))
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
			// Every slot of every kind the page may load (the kinds' records), the kits first.
			for(const auto kind : {Kind::Kit, Kind::Pattern, Kind::Song, Kind::Global})
				for(int i = 0; i < deskCore::kindSpec<MmModel>(kind)->slots; ++i)
					request({kind, static_cast<uint8_t>(i)}, false);
		}
	}

	void MmMachine::onTelemetry(const Telemetry& _t)
	{
		m_tel = _t;
		// Playing = the RAM flag, or the step byte advancing: two single steps forward (or a wrap to 0)
		// in a row, each within three step times at the tempo (a 3/4X pattern included). A stop that
		// resets the step to 0 is one move, so it never reads as playing. Derived; m_tel stays as read.
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
		const bool stepping = m_stepMoves >= 2 && t - m_stepMovedMs < window;
		if(!stepping && t - m_stepMovedMs >= window)
			m_stepMoves = 0;
		m_playing = _t.valid && (_t.running || stepping);
		if(!m_playing && m_queuedPattern >= 0)
			m_lastStatusMs = -1e9;	// stopped: LOAD PATTERN switches at once; status will say
		pumpSequence(t);
	}

	void MmMachine::onWorkingKit(const Bytes& _region)
	{
		m_working.region = _region;
	}

	void MmMachine::sendModulation(const uint8_t _track, const uint8_t _param, const uint8_t _value, const Documents& _view)
	{
		const auto page = static_cast<uint8_t>(_param / 8), index = static_cast<uint8_t>(_param % 8);
		if(m_curKit < 0 || _track > 5 || page > 7 || (page == 7 && index != 0) || !m_port.sendParam)
			return;
		m_port.sendParam(_track, page, index, _value);
		const auto* w = _view.workingKitOf(m_curKit);
		if(!w)
			return;
		auto k = *w;
		(page == 7 ? k.levels[_track] : k.tracks[_track].pages[page][index]) = _value;
		if(!(k == *w))
			observe(WorkingKit{k}, Source::Tracked);
	}

	// The working kit from memory, through the one working-copy policy (deskCore::fromImage): status
	// says which kit plays; an image of another kit waits and asks for status.
	void MmMachine::applyWorkingKit(const double _now, const Documents& _view)
	{
		const auto& region = m_working.region;
		if(!region || region->size() < 5 + ed::MmKit::g_rawSize)
			return;
		const auto kitNumber = static_cast<int>((*region)[0] & 127);
		const std::vector<uint8_t> raw(region->begin() + 5, region->begin() + 5 + ed::MmKit::g_rawSize);
		const auto k = ed::mmKitFromRaw(raw, static_cast<uint8_t>(kitNumber));
		if(!k)
		{
			m_working.region.reset();
			return;
		}
		const auto current = m_curKit >= 0 ? std::optional<int>(m_curKit) : std::nullopt;
		auto r = deskCore::fromImage(std::move(m_working), *k, kitNumber, current, _view.workingKitOf(m_curKit), _now, reflects,
			[](const ed::MmKit& _k) { return _k; });
		m_working = std::move(r.next);
		if(r.askStatus && _now - m_lastStatusMs > 200)
		{
			m_lastStatusMs = _now;
			requestStatus();
		}
		if(!r.take)
			return;
		if(r.settles)
			settle(WorkingKit{*r.take}, Source::Memory);
		else
			observe(WorkingKit{*r.take}, Source::Memory);
		if(!known({Kind::Kit, static_cast<uint8_t>(kitNumber)}))
			request({Kind::Kit, static_cast<uint8_t>(kitNumber)}, true);
	}

	void MmMachine::tick(const double _now, const Documents& _view)
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
		applyWorkingKit(_now, _view);
		// A read-back that never came: give up on that push.
		for(auto& [ref, push] : m_pushes)
		{
			if(!push.slot.busy() || push.onRecv || _now - push.sentMs <= g_readBackTimeoutMs)
				continue;
			push.slot.abandon();
			fail(ref, std::string("The machine did not read back the ") + kindName(ref.kind) + " that was sent. Showing what it holds.");
			request(ref, true);
		}
		// The playhead, at most every 25 ms.
		const bool playing = m_playing;
		if(m_tel.valid && (m_tel.step != m_lastStep || playing != m_lastPlaying) && _now - m_lastTelemetryMs > 25)
		{
			m_lastStep = m_tel.step;
			m_lastPlaying = playing;
			m_lastTelemetryMs = _now;
			Value t = Value::object();
			t.set("step", m_tel.step);
			t.set("playing", playing);
			publishTelemetry(std::move(t));
		}
	}

	// ---- the machine document ----

	Value MmMachine::state(const Documents& _view) const
	{
		Value d = Value::object();
		d.set("schema", "mm-desk/machine");
		d.set("version", 1);
		Value p = Value::object();
		p.set("current", m_curPattern < 0 ? Value() : Value(m_curPattern));
		p.set("queued", m_queuedPattern < 0 ? Value() : Value(m_queuedPattern));
		d.set("pattern", std::move(p));
		Value k = Value::object();
		k.set("current", m_curKit < 0 ? Value() : Value(m_curKit));
		k.set("working", kitState(_view));
		d.set("kit", std::move(k));
		Value s = Value::object();
		s.set("current", m_curSong < 0 ? Value() : Value(m_curSong));
		s.set("songMode", m_songMode < 0 ? Value() : Value(m_songMode == 1));
		d.set("song", std::move(s));
		Value g = Value::object();
		g.set("current", m_curGlobal < 0 ? Value() : Value(m_curGlobal));
		d.set("global", std::move(g));
		d.set("playing", m_playing);
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
		l.set("done", static_cast<unsigned long>(knownCount()));
		int total = 0;
		for(const auto& k : MmModel::kinds())
			if(k.loadable)
				total += k.slots;
		l.set("total", total);
		d.set("loading", std::move(l));
		d.set("roundTripMs", m_lastRoundTripMs);
		return d;
	}

	Value MmMachine::status() const
	{
		Value v = Value::object();
		v.set("pattern", m_curPattern);
		v.set("kit", m_curKit);
		v.set("song", m_curSong);
		v.set("global", m_curGlobal);
		v.set("loaded", static_cast<int>(knownCount()));
		v.set("recv", m_recv.stateName());
		v.set("parked", m_recv.parked());
		v.set("roundTripMs", m_lastRoundTripMs);
		return v;
	}
}
