// MmMachine: the machine commands (handlers()), the questions before them (askers()), the panel keys and the
// STOP/LOAD/PLAY sequence. Split from mmDeskMachine.cpp along the MD's seams (review finding 9).
#include "mmDeskMachineParts.h"

#include "elektronData/mmCommands.h"

#include <cmath>

namespace mmDesk
{
	using namespace parts;

	// What a kit command would lose asks first (the core sends it again with force): one question
	// function per command that has one.
	const std::map<std::string, MmMachine::Handler>& MmMachine::askers()
	{
		static const std::map<std::string, Handler> map{{"loadKit", &MmMachine::askLoadKit}, {"saveKit", &MmMachine::askSaveKit},
			{"select", &MmMachine::askSelect}, {"play", &MmMachine::askPlay}};
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
		if(kitState(_view) != deskCore::KitState::Edited)
			return ok();
		if(k == m_curKit)
			return ask("reloadKit", "Reload <b>" + kitLabel(_view, k) + "</b> from the machine? Your edits go to its UNDO KIT.",
				"Reload (discard edits)");
		auto o = ask("loadKit", "Load <b>" + kitLabel(_view, k) + "</b>? Your edits to <b>" + kitLabel(_view, m_curKit)
			+ "</b> are not saved on the machine. Without saving, they go to its UNDO KIT.", "Load without saving");
		// Save and load (MM-PORT-PLAN f, as the Machinedrum's): SAVE KIT to the current slot, then LOAD KIT
		// with force (the wire keeps their order).
		Value save = Value::object();
		save.set("op", "saveKit");
		o.ask->alternatives.push_back({"Save and load", {std::move(save)}});
		return o;
	}

	// A pattern that links another kit loads it: the unsaved edits of the kit that plays are lost.
	Outcome MmMachine::askSelect(const Value& _command, const Documents& _view)
	{
		const auto p = num(_command, "p");
		Outcome chain;
		// MM-P8: a pick ends the machine's chain (manual 1-46), so it asks first
		if(chained())
		{
			std::string list;
			for(const auto c : m_tel.chain.patterns)
				list += (list.empty() ? "" : " » ") + ed::mmPatternName(c);
			chain = ask("breakChain", "Picking <b>" + ed::mmPatternName(static_cast<uint8_t>(p)) + "</b> ends the chain <b>" + list + "</b>, as on the machine.",
				"Pick it, end the chain");
			chain.ask->details.set("p", p);
		}
		const auto it = _view.patterns.find(static_cast<uint8_t>(p));
		if(it == _view.patterns.end() || static_cast<int>(it->second.kit) == m_curKit || kitState(_view) != deskCore::KitState::Edited)
			return chain;
		Outcome o = ask("discardKit", "<b>" + ed::mmPatternName(static_cast<uint8_t>(p)) + "</b> uses kit <b>" + kitLabel(_view, it->second.kit)
			+ "</b>. Your edits to <b>" + kitLabel(_view, m_curKit) + "</b> are not saved on the machine and will be lost.", "Switch and lose edits");
		o.ask->details.set("p", p);
		o.ask->details.set("kit", m_curKit);
		o.ask->details.set("target", static_cast<int>(it->second.kit));
		return deskCore::withAsk(std::move(chain), o);
	}

	// SAVE KIT n over another slot that holds a kit.
	Outcome MmMachine::askSaveKit(const Value& _command, const Documents& _view)
	{
		const auto k = num(_command, "k", m_curKit);
		if(k == m_curKit || k < 0)
			return ok();
		const auto it = _view.kits.find(static_cast<uint8_t>(k));
		if(it == _view.kits.end() || kitIsEmpty(it->second))
			return ok();
		return ask("overwriteSlot", "Overwrite <b>" + kitLabel(_view, k) + "</b> with the kit that plays, <b>" + kitLabel(_view, m_curKit)
			+ "</b>? The machine keeps the overwritten kit in its UNDO KIT.", "Overwrite");
	}

	// Over MIDI PLAY is MIDI Start, which the machine ignores with CONTROL IN TRANSPORT IGNORE (the
	// factory global): offer to set it (MM-P4). Confirmed, cmdPlay writes it to the active global.
	Outcome MmMachine::askPlay(const Value&, const Documents& _view)
	{
		const auto* g = m_profile.wire ? activeGlobal(_view, m_curGlobal) : nullptr;
		if(!g || g->transportIn != 0 || m_pushes.busy({Kind::Global, g->position}))
			return ok();
		return ask("transportIgnore", "PLAY is <b>MIDI Start</b> over MIDI. This Monomachine ignores it: <b>GLOBAL › CONTROL IN › "
			"TRANSPORT</b> is <b>IGNORE</b> in GLOBAL " + std::to_string(g->position + 1) + ". Set it to ACCEPT? It goes out as a global dump,"
			" so it waits for SYSEX RECV (SEND in the pattern field).", "Set TRANSPORT to ACCEPT");
	}

	// PLAY or STOP that waited for the panel: pressed once it is free, given up after g_transportWaitMs.
	void MmMachine::pumpTransport(const double _now)
	{
		if(!m_pendingTransport)
			return;
		if(_now - m_pendingTransportMs > g_transportWaitMs)
		{
			m_pendingTransport.reset();
			return;
		}
		if(pressKeys({*m_pendingTransport}))
			m_pendingTransport.reset();
	}

	bool MmMachine::pressKeys(const std::vector<Key>& _keys)
	{
		const auto s = m_recv.state();
		if(s == RecvSession::State::Entering || s == RecvSession::State::ToMain || s == RecvSession::State::Leaving)
			return false;
		// Parked on SYSEX RECV the keys work, but not while the machine is still taking a dump: a key pressed then is
		// lost (RECORD, PLAY, STOP, the MUTE window, BANK GROUP; measured: mmDeskFirmwareTest parked). Busy, as above.
		if(m_recv.taking())
			return false;
		if(!m_port.pressKeys || !m_port.pressKeys(_keys))
			return false;
		size_t states = 0;
		for(const auto k : _keys)
			states += isChord(k) ? 4 : 2;
		m_keysUntilMs = std::max(m_keysUntilMs, now()) + static_cast<double>(states) * g_keyStateMs + g_keysMarginMs;
		return true;
	}

	// ---- machine commands ----

	const std::map<std::string, MmMachine::Handler>& MmMachine::handlers()
	{
		static const std::map<std::string, Handler> map{
			{"load", &MmMachine::cmdLoad}, {"select", &MmMachine::cmdSelect}, {"loadKit", &MmMachine::cmdLoadKit},
			{"saveKit", &MmMachine::cmdSaveKit}, {"loadSong", &MmMachine::cmdLoadSong}, {"saveSong", &MmMachine::cmdSaveSong},
			{"tempo", &MmMachine::cmdTempo}, {"play", &MmMachine::cmdPlay}, {"stop", &MmMachine::cmdStop},
			{"mute", &MmMachine::cmdMute}, {"seqMode", &MmMachine::cmdSeqMode}, {"followHost", &MmMachine::cmdFollowHost}, {"muteMidi", &MmMachine::cmdMuteMidi},
			{"poly", &MmMachine::cmdPoly}, {"record", &MmMachine::cmdRecord}, {"hwSend", &MmMachine::cmdHwSend},
			{"chain", &MmMachine::cmdChain}, {"chainClear", &MmMachine::cmdChainClear},
			{"noteOn", &MmMachine::cmdNoteOn}, {"noteOff", &MmMachine::cmdNoteOff}, {"globalSlot", &MmMachine::cmdGlobalSlot}};
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
		const bool playing = m_playing;
		// MM-P8: a pick ends a chain; one still waiting for its keys is not made. A SysEx LOAD PATTERN
		// leaves the machine's chain (the pattern plays once, then the chain goes on, measured): the pick
		// is the machine's own, BANK + its TRIG key, which ends it (the LOAD PATTERN below picks the same).
		m_chain.drop();
		if(chained() && canChain() && !pressBankTrigs({p}))
			return refuse("The panel did not take the keys that end the chain; try again.");
		if(flag(_m, "now") && playing)
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
		if(k == m_curKit && kitState(_view) == deskCore::KitState::Clean)
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
		{
			m_port.sendSysex(ed::mmLoadKit(static_cast<uint8_t>(_k)));
			// Without memory (HW MIDI) a reload is known only from its slot: the kit that plays starts
			// over as the slot's next dump
			if(!m_profile.memory && _k == m_curKit)
			{
				m_working = deskCore::switched(m_working);
				request({Kind::Kit, static_cast<uint8_t>(_k)}, true);
			}
		}
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
		if(s == m_curSong)
			m_songReloadNeeded = false;
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
		const auto bpm = _m.find("bpm")->asNumber();
		m_stream.sendLatest(0x7e000000, ed::mmSetTempo(bpm), false, clock());
		m_expectTempo = deskCore::FieldExpectation<int>::sent(static_cast<int>(std::lround(bpm * 24.0)), clock());
		return ok();
	}

	Outcome MmMachine::cmdPlay(const Value& _m, const Documents& _view)
	{
		const auto* g = activeGlobal(_view, m_curGlobal);
		// Over MIDI with TRANSPORT IGNORE, confirmed (askPlay): TRANSPORT ACCEPT in the active global, the
		// machine's own setting (as followHost: no undo step)
		const auto ref = g ? Ref{Kind::Global, g->position} : Ref{Kind::Global, 0};
		if(m_profile.wire && g && g->transportIn == 0 && flag(_m, "force") && !m_pushes.busy(ref))
		{
			auto accept = *g;
			accept.transportIn = 1;
			pushDump(ref, ed::encodeMmGlobal(accept));
			return ok("TRANSPORT ACCEPT waits for SYSEX RECV (SEND in the pattern field). Press PLAY again once it is in.");
		}
		if(m_reactivateGlobal && m_curGlobal >= 0)
		{
			// HW MIDI: the active global was written while the machine was on SYSEX RECV, which applies
			// it only when made active again (P7): now, before MIDI Start
			m_port.sendSysex(ed::mmSetActiveGlobal(static_cast<uint8_t>(m_curGlobal & 7)));
			m_reactivateGlobal = false;
		}
		if(!pressKeys({Key::Play}))
		{
			// 0.3.4: the panel is busy taking an edit (SYSEX RECV): PLAY goes once it is free
			m_pendingTransport = Key::Play;
			m_pendingTransportMs = now();
			return ok(g_transportWaits);
		}
		m_pendingTransport.reset();
		// P7: a machine that follows the host's clock (in a DAW) plays with the host's transport
		const bool follows = g && g->tempoSync == 1;
		return ok(follows ? "The machine follows the host: it plays when the host's transport runs." : "");
	}

	Outcome MmMachine::cmdStop(const Value&, const Documents&)
	{
		if(pressKeys({Key::Stop}))
		{
			m_pendingTransport.reset();
			return ok();
		}
		// 0.3.4: the panel is busy taking an edit (SYSEX RECV): STOP goes once it is free
		m_pendingTransport = Key::Stop;
		m_pendingTransportMs = now();
		return ok(g_transportWaits);
	}

	// P7, in a DAW: the machine follows the host's MIDI clock and Start/Stop (MmModel::hostFollowing). The
	// global goes out as a dump on SYSEX RECV (made active with 0x56 once it is read back, onDump): the
	// machine's own setting, not an edit, so no undo step.
	Outcome MmMachine::cmdFollowHost(const Value&, const Documents& _view)
	{
		if(m_curGlobal < 0)
			return ok();
		const auto ref = Ref{Kind::Global, static_cast<uint8_t>(m_curGlobal & 7)};
		const auto it = _view.globals.find(ref.slot);
		if(it == _view.globals.end() || m_pushes.busy(ref))
			return ok();
		const auto g = MmModel::hostFollowing(it->second);
		if(!g)
			return ok();
		pushDump(ref, ed::encodeMmGlobal(*g));
		return ok("The machine follows the host's tempo and transport (GLOBAL " + std::to_string(ref.slot + 1) + ": MIDI SYNC CLOCK IN, TRANSPORT IN)");
	}

	// 0.3.5: the Song page's PATTERN | SONG switch: SET STATUS 0x10 (in the MM's Appendix C: over HW MIDI too), then
	// the status asked for; the page shows the machine's answer
	Outcome MmMachine::cmdSeqMode(const Value& _m, const Documents&)
	{
		const bool song = flag(_m, "song");
		m_port.sendSysex(ed::mmSetStatus(ed::MmStatus::SongMode, song ? 1 : 0));
		m_port.sendSysex(ed::mmStatusRequest(ed::MmStatus::SongMode));
		return ok(song ? "SONG mode: the machine plays the song." : "PATTERN mode: the machine plays the pattern.");
	}

	// B-051, F3: the GLOBAL page's slot keys: SET ACTIVE GLOBAL (0x56, not while the machine is on SYSEX RECV), then the
	// machine says which is active (status) and the editor reads that slot when it does not know it
	Outcome MmMachine::cmdGlobalSlot(const Value& _m, const Documents&)
	{
		const auto slot = static_cast<uint8_t>(num(_m, "slot") & 7);
		if(m_profile.wire || m_recv.state() == RecvSession::State::Idle)
			m_port.sendSysex(ed::mmSetActiveGlobal(slot));
		else
			m_activateGlobal = slot;
		m_port.sendSysex(ed::mmStatusRequest(ed::MmStatus::Global));
		if(!known({Kind::Global, slot}))
			request({Kind::Global, slot}, true);
		return ok("GLOBAL " + std::to_string(slot + 1) + " is active");
	}

	Outcome MmMachine::cmdMute(const Value& _m, const Documents& _view)
	{
		const auto t = num(_m, "t");
		// B-026: a mute is the track's CC on the machine's channels: none when the base channel is OFF, and then the
		// page must not show a mute the machine never took
		// measured with a 2008 backup's global (CHANNEL SPAN 0): T3's mute CC muted T1 (elektronData::mmTrackChannel)
		if(const auto why = noChannelReason(_view, t); !why.empty())
			return refuse(why);
		const bool mute = flag(_m, "on");
		m_port.sendParam(static_cast<uint8_t>(t), 8, 0, mute ? 1 : 0);
		m_expectMute[static_cast<size_t>(t)] = deskCore::FieldExpectation<bool>::sent(mute, clock());
		return ok();
	}

	// MM-P4: a MIDI sequencer track's mute. The MUTE window (FUNCTION + BANK GROUP) and its TRIG 9-14
	// keys (each press toggles), pressed only when the mute the machine will have differs: memory's, or what
	// the keys still on their way make it (the expectation). Measured: a mute taken back at once was lost while
	// this compared with memory alone (the first keys not through yet, so "no change", then they muted it).
	// EXIT closes the window.
	Outcome MmMachine::cmdMuteMidi(const Value& _m, const Documents&)
	{
		if(!machineState())
			return refuse(capabilities().reason("midiMutes"));
		if(m_tel.mutes < 0)
			return refuse("The machine's mutes are not known yet.");
		const auto t = num(_m, "t");
		const bool mute = flag(_m, "on");
		const auto i = static_cast<size_t>(6 + t);
		if(m_expectMute[i].shown(muteInMemory(6 + t), clock(), m_profile.settleMs) == std::optional<bool>(mute))
			return ok();
		if(!pressKeys({Key::MuteWindow, static_cast<Key>(static_cast<int>(Key::Trig9) + t), Key::Exit}))
			return refuse(g_panelBusy);
		m_expectMute[static_cast<size_t>(6 + t)] = deskCore::FieldExpectation<bool>::sent(mute, clock());
		return ok();
	}

	// MM-P4: POLY is the machine's audio mode, SET STATUS 0x20 (0 mono, 1 POLY), read back by status.
	Outcome MmMachine::cmdPoly(const Value& _m, const Documents&)
	{
		const bool poly = flag(_m, "on");
		m_port.sendSysex(ed::mmSetStatus(ed::MmStatus::Poly, poly ? 1 : 0));
		m_port.sendSysex(ed::mmStatusRequest(ed::MmStatus::Poly));
		m_expectPoly = deskCore::FieldExpectation<bool>::sent(poly, clock());
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
}
