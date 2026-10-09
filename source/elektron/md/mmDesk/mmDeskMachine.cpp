// MmMachine: the profiles, the facts and capabilities, the step (tick) and the machine document. The rest of the
// adapter is split along the MD's seams (review finding 9): mmDeskDelivery.cpp (documents to the machine),
// mmDeskLoad.cpp (the machine to documents), mmDeskCommands.cpp (machine commands and their questions),
// mmDeskChain.cpp, mmDeskNotes.cpp, mmDeskRecord.cpp; the small state values with pure steps in mmDeskWatch.h.
#include "mmDeskMachineParts.h"

#include "elektronData/mmCommands.h"

#include <algorithm>
#include <cstdlib>

namespace mmDesk
{
	using namespace parts;

	const Profile& emulatorProfile()
	{
		static const Profile p = []
		{
			Profile e{"emu", "EMU OS 1.32B", "Engine: the real Monomachine OS 1.32B runs inside the app. Choose HW MIDI to "
				"edit a real Monomachine instead.", false, true, true, true};
			// B-014, as the Machinedrum's: values and dumps at MIDI cable speed (GEARMULATOR_MDMM_EDIT_RATE raises it, 0: off)
			double rate = deskCore::DinPacer::g_bytesPerSecond;
			if(const char* r = std::getenv("GEARMULATOR_MDMM_EDIT_RATE"); r && *r)
				rate = std::max(0.0, std::atof(r));
			e.stream.bytesPerSecond = rate;
			e.stream.ingestBytesPerSecond = 125000;
			e.stream.settleMs = 250;
			e.stream.valueBytesPerSecond = rate;
			e.stream.valueBurstBytes = 192;
			e.stream.latestIntervalMs = rate > 0 ? 100 : 0;
			return e;
		}();
		return p;
	}

	const Profile& wireProfile()
	{
		static const Profile p{"hw", "HW MIDI", "Engine: a real Monomachine on the plug-in's MIDI in and out, at MIDI speed. "
			"Pattern, song, global and stored-kit dumps need it on GLOBAL › FILE › SYSEX RECV: they wait (SEND n in the pattern field) "
			"until you put it there. PLAY/STOP are MIDI Start/Stop.", true, false, false, false};
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
		, m_stream(m_port.sendSysex, m_profile.stream)
	{
		// B-014: everything to the machine through the one stream, in order: SysEx (a machine change, then its values),
		// values on the budget (the newest of a parameter wins), notes first. Panel keys stay the RECV session's.
		// B-031: a request already waiting is not queued twice (the status polls behind a long backlog).
		if(m_port.sendSysex)
			m_port.sendSysex = [this](const Bytes& _b)
			{
				if(ed::mmIsRequest(_b))
					m_stream.ask(_b, false, clock());
				else
					m_stream.send(_b, false, clock());
			};
		if(m_port.sendParam)
			m_port.sendParam = [this, send = std::move(m_port.sendParam)](const uint8_t _t, const uint8_t _p, const uint8_t _i, const uint8_t _v)
			{
				m_stream.value((_t << 12) | (_p << 8) | _i, 3, [send, _t, _p, _i, _v] { send(_t, _p, _i, _v); }, clock());
			};
		if(m_port.sendNrpn)
			m_port.sendNrpn = [this, send = std::move(m_port.sendNrpn)](const uint8_t _t, const uint8_t _p, const uint8_t _v)
			{
				m_stream.value(0x100000 | (_t << 8) | _p, 12, [send, _t, _p, _v] { send(_t, _p, _v); }, clock());
			};
		if(m_port.sendNote)
			m_port.sendNote = [this, send = std::move(m_port.sendNote)](const uint8_t _c, const uint8_t _n, const uint8_t _v)
			{
				m_stream.priority([send, _c, _n, _v] { send(_c, _n, _v); }, clock());
			};
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
		m_stream.clear();
		m_loads = {};
		m_backgroundQueued = false;
		m_recv = {};
		m_manual.clear();
		m_reactivateGlobal = false;
		m_poly = -1;
		m_recordReads = {};
		m_expectMute = {};
		m_expectPoly = {};
		m_expectTempo = {};
		m_curPattern = m_curKit = m_curSong = m_curGlobal = m_songMode = m_queuedPattern = -1;
		m_sequence.clear();
		m_chain.drop();
		m_keysUntilMs = -1e9;
		m_notes = {};
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
		// MM-P4: what only the machine's panel and RAM reach (the MUTE window, RECORD). Over MIDI the
		// Monomachine has no message for them (Appendix B and C).
		c.set("midiMutes", machineState(), "Over MIDI the MIDI track mutes cannot be set: the Monomachine has no MIDI message for them"
			" (Appendix B: CC 3 mutes the six synth tracks only; Appendix C has no mute SysEx). Use FUNCTION + BANK GROUP on the machine.");
		c.set("gridRecord", machineState(), "Over MIDI the recording modes cannot be switched: RECORD has no MIDI message (Appendix C)."
			" Press RECORD on the Monomachine.");
		// MM-P8: pattern chaining is the machine's keys, and the chain is read from its RAM.
		c.set("chains", canChain(), g_noChains);
		// SysEx and kit or global data on every engine.
		c.set("poly", true);
		c.set("multiTrig", true);
		c.set("multiMap", true);
		c.set("portamento", true);
		c.values.emplace_back("dumps", manualDumps() ? "manual" : "recv");
		return c;
	}

	bool MmMachine::busy() const
	{
		if(m_pushes.anyBusy())
			return true;
		// A live edit the machine's memory does not show yet is on the wire too.
		return m_working.expect.expecting(now());
	}

	// B-019: an imported file's message as it is. A dump goes as the editor's own do: on SYSEX RECV, which the
	// adapter opens on the emulator and the person opens over HW MIDI (it waits for SEND meanwhile), in order after
	// what waits there; anything else (a read-back's request) into the stream.
	std::string MmMachine::sendAsIs(const Bytes& _message, const bool _dump)
	{
		if(!m_stream.open())
			return "This engine cannot send SysEx to the machine.";
		if(_dump)
			afterDumps(_message);
		else
			m_port.sendSysex(_message);
		// B-026: a dump of the active global is stored, not applied, until its slot is made active (0x56): without it
		// the machine keeps its old channels while every document says the new ones. As after the editor's own global
		// writes, once SYSEX RECV is left.
		if(_message.size() > 9 && _message[0] == 0xf0 && _message[6] == 0x50 && m_curGlobal >= 0 && _message[9] == m_curGlobal)
			m_activateGlobal = m_curGlobal;
		return {};
	}

	MmMachine::AsIs MmMachine::asIs() const
	{
		AsIs a;
		a.queued = m_stream.waiting() + m_recv.queued() + m_manual.size();
		a.busy = a.queued > 0 || m_stream.sending(clock()) || m_recv.taking();
		if(!m_manual.empty())
			a.waitsFor = "The Monomachine takes dumps only on GLOBAL > FILE > SYSEX RECV: open it on the machine, then press SEND "
				"in the editor.";
		return a;
	}

	deskCore::KitState MmMachine::kitState(const Documents& _view) const
	{
		const auto stored = m_curKit >= 0 ? _view.kits.find(static_cast<uint8_t>(m_curKit)) : _view.kits.end();
		// the stored slot as LOAD KIT plays it (a never-written slot plays as NEW KIT): loaded and untouched is clean
		return deskCore::kitStateOf(_view.workingKitOf(m_curKit), stored == _view.kits.end() ? nullptr : &stored->second,
			[](const ed::MmKit& _working, const ed::MmKit& _stored) { return ed::mmKitRaw(_working) == ed::mmKitRaw(ed::mmKitAsLoaded(_stored)); });
	}


	void MmMachine::tick(const double _now, const Documents& _view)
	{
		// 0.3.4: cable speed only while the sequencer plays
		m_stream.setPlaying(m_playing, _now);
		m_stream.pump(_now);
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
		pumpTransport(_now);
		pumpChain();
		pumpSongReload(_now);
		if(m_activateGlobal >= 0 && (m_profile.wire || m_recv.state() == RecvSession::State::Idle))
		{
			m_port.sendSysex(ed::mmSetActiveGlobal(static_cast<uint8_t>(m_activateGlobal)));
			m_activateGlobal = -1;
		}
		pumpLoads(_now);
		applyWorkingKit(_now, _view);
		pumpPushes(_now);
		publishTransport(_now, readWhileRecording(_now));
	}

	// 0.3.5: the firmware plays the song it loaded; a dump into the current song's slot is heard after LOAD SONG,
	// which it takes only while stopped. Once the dumps have landed (SYSEX RECV done, nothing pending) and the machine
	// is stopped in SONG mode, the desk loads the song again by itself; a playing machine gets it at its next stop.
	void MmMachine::pumpSongReload(const double _now)
	{
		if(!m_songReloadNeeded || m_curSong < 0)
			return;
		if(m_playing || m_songMode != 1 || _now - m_songEditedMs < g_songReloadQuietMs || busy() || !m_stream.open()
			|| m_recv.state() != RecvSession::State::Idle || m_stream.waiting() > 0 || m_recv.queued() > 0)
			return;
		m_port.sendSysex(ed::mmLoadSong(static_cast<uint8_t>(m_curSong)));
		m_songReloadNeeded = false;
		requestStatus();
		m_lastStatusMs = _now;
	}

	// The playhead, at most every g_telemetryMinMs, and the recording mode when it changes (the transport).
	void MmMachine::publishTransport(const double _now, const bool _recordChanged)
	{
		if(!m_tel.valid)
			return;
		const auto due = telemetryDue(m_telemetryOut, m_tel.step, m_playing, _recordChanged, _now, g_telemetryMinMs);
		if(!due)
			return;
		m_telemetryOut = *due;
		static const char* modes[] = {"off", "grid", "live"};
		Value t = Value::object();
		t.set("step", m_tel.step);
		t.set("playing", m_playing);
		t.set("record", m_tel.recording < 0 || m_tel.recording > 2 ? Value() : Value(modes[m_tel.recording]));
		const int row = m_songRow.update(m_tel.songRow, m_tel.step, m_playing);
		t.set("songRow", row >= 0 ? Value(row) : Value());
		publishTelemetry(std::move(t));
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
		k.set("working", deskCore::kitStateName(kitState(_view)));
		d.set("kit", std::move(k));
		Value s = Value::object();
		s.set("current", m_curSong < 0 ? Value() : Value(m_curSong));
		s.set("songMode", m_songMode < 0 ? Value() : Value(m_songMode == 1));
		s.set("reloadNeeded", m_songReloadNeeded);
		d.set("song", std::move(s));
		Value g = Value::object();
		g.set("current", m_curGlobal < 0 ? Value() : Value(m_curGlobal));
		d.set("global", std::move(g));
		// DESIGN-UNIFY.md 4.4: the tempo, the mutes and POLY are memory's, but a value the editor set is
		// said from the moment its command is taken until memory shows it (or settleMs passed: memory wins)
		const double t = clock();
		// 30-300 BPM in firmware units (x 24); anything else is not a tempo yet (boot).
		const auto tempo = m_expectTempo.shown(tempoInMemory(), t, m_profile.settleMs);
		d.set("tempo", tempo ? Value(*tempo / 24.0) : Value());
		// MM-P4: the machine's own mutes (RAM; made on its panel too) and its audio mode
		Value mutes = Value::object();
		int bits = 0;
		for(size_t i = 0; i < m_expectMute.size(); ++i)
			bits |= m_expectMute[i].shown(muteInMemory(static_cast<int>(i)), t, m_profile.settleMs).value_or(false) ? 1 << i : 0;
		mutes.set("synth", m_tel.mutes < 0 ? Value() : Value(bits & 0x3f));
		mutes.set("midi", m_tel.mutes < 0 ? Value() : Value((bits >> 6) & 0x3f));
		d.set("mutes", std::move(mutes));
		const auto poly = m_expectPoly.shown(polyInMemory(), t, m_profile.settleMs);
		d.set("poly", poly ? Value(*poly) : Value());
		// MM-P8: the machine's own chain and BANK GROUP (RAM), as the Machinedrum's machine.desk
		Value desk = Value::object();
		if(m_tel.chainKnown)
		{
			Value chain = Value::object();
			chain.set("active", m_tel.chain.active);
			chain.set("next", m_tel.chain.next);
			Value list = Value::array();
			for(const auto p : m_tel.chain.patterns)
				list.push(static_cast<int>(p));
			chain.set("patterns", std::move(list));
			desk.set("chain", std::move(chain));
		}
		else
			desk.set("chain", Value());
		desk.set("bankGroup", m_tel.bankGroup);
		d.set("desk", std::move(desk));
		Value r = Value::object();
		r.set("state", manualDumps() ? (m_manual.empty() ? "idle" : "waitingUser") : m_recv.stateName());
		r.set("waiting", static_cast<unsigned long>(m_manual.size()));
		size_t inFlight = 0;
		for(const auto& [ref, push] : m_pushes)
			inFlight += push.slot.busy();
		r.set("sending", static_cast<unsigned long>(inFlight));
		r.set("received", static_cast<unsigned long>(m_tel.recvCount));
		r.set("errors", static_cast<unsigned long>(m_tel.recvErrors));
		d.set("recv", std::move(r));
		// done: the slots read, plus those whose read was given up (failed), so the progress ends; the working
		// kit is no slot.
		Value l = Value::object();
		const size_t read = knownCount() - (known({Kind::WorkingKit, 0}) ? 1 : 0);
		l.set("done", static_cast<unsigned long>(read + unreadCount()));
		l.set("failed", static_cast<unsigned long>(unreadCount()));
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
