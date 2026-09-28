#include "mdDeskMachine.h"

#include "mdDeskChain.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdGlobal.h"
#include "elektronData/mdJson.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mdWorkingKit.h"

#include <algorithm>
#include <cstdio>

namespace mdDesk
{
	namespace ed = elektronData;
	using Value = ed::json::Value;
	using deskCore::Outcome;
	using deskCore::Source;

	namespace
	{
		constexpr double g_loadTimeoutMs = 800;
		constexpr double g_pushTimeoutMs = 2000;
		constexpr double g_statusIdleMs = 1000;		// P1: poll gently, a burst costs ~1.5 ms of step jitter
		constexpr double g_statusQueuedMs = 250;
		// Keys the device never reports as pending (no telemetry, a stopped device) are over after this.
		constexpr double g_keysTimeoutMs = 1000;
		constexpr deskCore::LoadQueue<DocRef>::Policy g_loadPolicy{30, 1};	// P1: ~30 ms per pattern in the background

		// The dump a request brings back, for timeouts at DIN speed.
		size_t replyBytes(const DocKind _k)
		{
			switch(_k)
			{
			case DocKind::Pattern: return 5410;
			case DocKind::Kit: return 1233;
			case DocKind::Song: return 3100;
			case DocKind::Global: return 197;
			case DocKind::WorkingKit: return 0;	// from memory, never requested
			}
			return 0;
		}

		std::optional<int> intOf(const Value& _m, const char* _key)
		{
			const auto* v = _m.find(_key);
			if(!v || !v->isNumber())
				return {};
			return static_cast<int>(v->asNumber());
		}

		bool flagOf(const Value& _m, const char* _key)
		{
			const auto* v = _m.find(_key);
			return v && ((v->isBool() && v->asBool()) || (v->isNumber() && v->asNumber() != 0));
		}

		Outcome ok(std::string _note = {}) { return {{}, std::move(_note), {}}; }
		Outcome refuse(std::string _error) { return {{std::move(_error)}, {}, {}}; }
		Outcome ask(Value _ask) { return {{}, {}, std::move(_ask)}; }

		// Why the panel-key features are not here (the capabilities' reasons and the refusals).
		constexpr const char* g_noLiveRecordWire = "Live recording needs the machine's panel keys: the emulator only";
		constexpr const char* g_noLiveRecordTelemetry = "Live recording needs MD OS 1.63 telemetry";
		constexpr const char* g_noChains = "Chaining is made with the machine's keys: it needs the local emulated MD OS 1.63";

		Value askOf(const char* _what, const Value& _command, const std::optional<uint8_t> _kit)
		{
			Value a = Value::object();
			a.set("type", "ask");
			a.set("ask", _what);
			a.set("command", _command);
			a.set("kit", _kit ? Value(static_cast<int>(*_kit)) : Value());
			return a;
		}
	}

	const Profile& emulatorProfile()
	{
		static const Profile p{"emu", "EMU OS 1.63", "Engine: the real Machinedrum OS 1.63 runs inside the app. Choose HW MIDI to "
			"edit a real Machinedrum instead.", false, false, true};
		return p;
	}

	const Profile& wireProfile()
	{
		static const Profile p{"hw", "HW MIDI", "Engine: a real Machinedrum on the plug-in's MIDI in and out, at MIDI speed (a "
			"pattern takes about 1.7 s each way). No live recording, chains or boot screen over MIDI; PLAY/STOP are MIDI "
			"Start/Stop.", true, true, false};
		return p;
	}

	MdMachine::MdMachine(Profile _profile, Port _port)
		: m_profile(std::move(_profile))
		, m_port(std::move(_port))
		, m_session([this](const Bytes& _b)
		{
			if(m_port.sendSysex)
				m_port.sendSysex(_b);
		})
	{
		wireSession();
		m_wire = deskCore::WireFacts(m_port.nowMs ? m_port.nowMs() : 0);
	}

	void MdMachine::wireSession()
	{
		m_session.onPattern = [this](const ed::MdPattern& _p) { onPattern(_p); };
		m_session.onKit = [this](const ed::MdKit& _k) { onKit(_k); };
		m_session.onSong = [this](const ed::MdSong& _s) { onSong(_s); };
		m_session.onGlobal = [this](const ed::MdGlobal& _g) { onGlobal(_g); };
		m_session.onState = [this](const mdDataLink::Session::State& _s) { onState(_s); };
	}

	// ---- facts ----

	deskCore::LifeFacts MdMachine::facts() const
	{
		using P = deskCore::LifeFacts::Probe;
		using A = deskCore::LifeFacts::Animation;
		if(m_profile.wire)
			return m_wire.facts(now());
		deskCore::LifeFacts f;
		f.probe = m_probe;
		f.replied = m_wire.replied;
		// Without telemetry (another firmware) the first status reply is all there is.
		f.animation = !m_telemetrySeen ? A::Unseen : !m_telemetry.valid ? A::Absent
			: m_telemetry.bootAnimation == 0 ? A::Over : A::Running;
		return f;
	}

	void MdMachine::setProbe(const Probe _probe)
	{
		if(m_probe == _probe)
			return;
		const bool restarted = _probe == Probe::Running && m_probe != Probe::Running && knowsAnything();
		m_probe = _probe;
		if(_probe != Probe::Running)
			m_wire.replied = false;
		if(restarted)
			startOver();
	}

	// The machine booted again (a restored project, a new device): what it held before is not
	// known any more. The session outlives the machine, so it reads everything again.
	void MdMachine::startOver()
	{
		m_loads = {};
		m_backgroundQueued = false;
		m_pushes.clear();
		m_memory = switched();
		m_seedWorking = true;
		m_keys = {};
		m_lastKit.reset();
		m_lastPattern.reset();
		m_audibleQueue.reset();
		m_knobs.reset();
		m_recLock.reset();
		m_sequence.clear();
		m_telemetrySeen = false;
		m_session = mdDataLink::Session([this](const Bytes& _b)
		{
			if(m_port.sendSysex)
				m_port.sendSysex(_b);
		});
		wireSession();
		startedOver();
	}

	deskCore::Capabilities MdMachine::capabilities() const
	{
		deskCore::Capabilities c;
		c.engine = m_profile.id;
		c.label = m_profile.label;
		c.about = m_profile.about;
		const bool keys = panelKeys();
		const bool telemetry = m_telemetry.valid;
		static const std::string noKeys = "It needs the machine's panel keys: the emulator only (over HW MIDI there are none).";
		c.set("transport", static_cast<bool>(m_port.pressKey), "No transport here.");
		c.set("panelKeys", keys, noKeys);
		c.set("liveRecord", canLiveRecord(), m_profile.wire ? g_noLiveRecordWire : g_noLiveRecordTelemetry);
		c.set("chains", canChain(), g_noChains);
		c.set("lcd", m_profile.memory, "The machine's own LCD is on the machine.");
		c.set("workingKitMemory", m_profile.memory, "The working kit is the stored slot plus the edits the editor saw.");
		c.set("mutesFromMemory", m_profile.memory && m_telemetry.mutes >= 0, "The mutes are the ones the editor sent.");
		c.set("sampleNames", true);
		c.set("modulators", telemetry, "The app modulators move with the emulator's playhead; over HW MIDI there is none to follow.");
		c.values.emplace_back("dumps", "direct");
		return c;
	}

	bool MdMachine::busy() const
	{
		for(const auto& [ref, push] : m_pushes)
			if(push.slot.busy())
				return true;
		// A live edit the machine's memory does not show yet is on the wire too.
		return m_memory.expect.expecting(now());
	}

	// Panel keys are lost while the firmware builds a dump (measured in the plug-in: PLAY during the
	// background song loads did nothing). So a key waits for the dump request in flight to be
	// answered, and no dump is asked for while keys are on their way: facts from the load queue and
	// the device's telemetry (panelPending), with a timeout for a device that never reports.
	bool MdMachine::keysOnTheirWay() const
	{
		if(!m_keys.waiting.empty())
			return true;
		if(!m_keys.sent || m_telemetry.panelPending < 0)
			return false;
		return now() - m_keys.sentMs < g_keysTimeoutMs && (!m_keys.seenPending || m_telemetry.panelPending > 0);
	}

	void MdMachine::releaseKeys()
	{
		if(m_keys.waiting.empty() || m_loads.loading())
			return;
		auto keys = std::move(m_keys.waiting);
		m_keys.waiting.clear();
		for(const auto& k : keys)
			pressKey(k);
	}

	bool MdMachine::pressKey(const std::string& _key)
	{
		if(!m_port.pressKey)
			return false;
		if(m_loads.loading() && m_telemetry.panelPending >= 0)
		{
			m_keys.waiting.push_back(_key);
			return true;
		}
		if(!m_port.pressKey(_key))
			return false;
		m_keys.sent = true;
		m_keys.seenPending = false;
		m_keys.sentMs = now();
		return true;
	}

	// ---- core -> machine ----

	Outcome MdMachine::review(const Value& _command, const std::vector<Change>& _changes, const Documents&)
	{
		// While live recording the firmware writes the playing pattern itself; a dump from the
		// editor would overwrite what it just recorded.
		if(m_telemetry.recording)
			for(const auto& c : _changes)
				if(c.ref().kind == DocKind::Pattern && m_session.state().pattern == c.ref().slot)
					return refuse("Live recording owns this pattern: play the tracks, turn the knobs. Press REC to stop"
						" recording, then edit the grid.");
		// Slot writes that would silently lose something on the machine ask first (the page sends
		// them again with force): a kit written into the kit that plays while it holds unsaved
		// edits, or a pattern dump into the current pattern that links another kit (the firmware
		// then loads that kit, measured).
		const bool edited = m_session.state().workingKit == mdDataLink::Session::WorkingKit::Edited;
		const auto kit = currentKit();
		for(const auto& c : _changes)
		{
			const auto ref = c.ref();
			if(ref.kind == DocKind::Kit && kit && *kit == ref.slot && edited)
				return ask(askOf("overwriteKit", _command, kit));
			if(ref.kind == DocKind::Pattern && m_session.state().pattern == ref.slot && edited
				&& std::get<ed::MdPattern>(c.after).kit != std::get<ed::MdPattern>(c.before).kit)
				return ask(askOf("relinkKit", _command, kit));
		}
		return ok();
	}

	Outcome MdMachine::pushDump(const Document& _doc)
	{
		const auto ref = refOf(_doc);
		auto& push = m_pushes[ref];
		if(!push.slot.want(_doc))
			return ok();	// waits for the read-back in flight; latest wins
		const auto problems = ref.kind == DocKind::Pattern ? m_session.pushPattern(std::get<ed::MdPattern>(_doc))
			: m_session.pushSong(std::get<ed::MdSong>(_doc));
		if(!problems.empty())
		{
			push.slot.abandon();
			return {problems, {}, {}};
		}
		push.sentMs = now();
		return ok();
	}

	Outcome MdMachine::submit(const Change& _change, const Documents&)
	{
		const auto ref = _change.ref();
		switch(ref.kind)
		{
		case DocKind::Pattern:
		case DocKind::Song:
			return pushDump(_change.after);
		case DocKind::Kit:
		{
			// A stored-slot dump (the kit library); into the kit that plays also LOAD KIT.
			const auto kit = currentKit();
			const auto& after = std::get<ed::MdKit>(_change.after);
			const bool playing = kit && *kit == ref.slot;
			auto problems = m_session.pushKit(after, playing ? mdDataLink::Session::KitApply::StoreAndLoad
				: mdDataLink::Session::KitApply::Store);
			if(!problems.empty())
				return {problems, {}, {}};
			// No read-back is asked for a stored kit: it holds what was sent.
			settle(after, Source::Tracked);
			// LOAD KIT makes it the working kit: memory shows it, or (no memory) it is what was sent.
			if(playing)
			{
				m_memory = switched();
				m_seedWorking = false;
				observe(WorkingKit{after}, Source::Tracked);
			}
			return ok();
		}
		case DocKind::WorkingKit:
		{
			const auto kit = currentKit();
			const auto& before = std::get<WorkingKit>(_change.before).kit;
			const auto& after = std::get<WorkingKit>(_change.after).kit;
			if(!kit || *kit != after.position)
				return refuse("Only the kit that plays can be edited live" + (kit ? " (kit " + std::to_string(*kit + 1) + " plays)"
					: std::string()));
			const auto delivery = kitDelivery(before, after);
			// While live recording a parameter becomes DATA ENTRY turns: the firmware records knob
			// turns as locks, not CCs (P3). Which route is data: the machine's mode, then a pure split.
			const auto route = routeKitEdits(delivery.edits, m_telemetry.recording && static_cast<bool>(m_port.turnKnob));
			for(const auto& e : route.knobs)
				m_knobs.want(e.track, e.index, e.value);
			for(const auto& e : route.live)
			{
				if(e.kind == LiveEdit::Kind::Param || e.kind == LiveEdit::Kind::Level)
				{
					if(m_port.sendKitParam)
						m_port.sendKitParam(e.track, e.kind == LiveEdit::Kind::Level ? 24 : e.index, e.value);
				}
				else if(const auto sysex = liveEditSysex(e); !sysex.empty() && m_port.sendSysex)
					m_port.sendSysex(sysex);
			}
			std::string note;
			for(const auto& n : delivery.notLive)
				note += (note.empty() ? "" : ". ") + n + " stays as it is on the machine";
			if(!delivery.edits.empty())
				m_session.noteWorkingKitEdited();
			// The edit is pending until the machine's memory shows it (or it is too old to wait for);
			// without memory nothing reads it back: it is done as sent.
			if(m_profile.memory && !delivery.edits.empty())
				m_memory.expect.sent(before, after, now());
			else
				settle(WorkingKit{after}, Source::Tracked);
			return ok(note);
		}
		case DocKind::Global:
		{
			const auto& before = std::get<ed::MdGlobal>(_change.before);
			const auto& after = std::get<ed::MdGlobal>(_change.after);
			const auto delivery = globalDelivery(before, after);
			for(const auto& e : delivery.edits)
				if(const auto sysex = liveEditSysex(e); !sysex.empty() && m_port.sendSysex)
					m_port.sendSysex(sysex);
			if(!delivery.notLive.empty())
			{
				// A global dump is stored at once but applied only when its slot is made active
				// (P5, measured): 0x56 right after it. pushGlobal asks for the slot back.
				if(auto problems = m_session.pushGlobal(after); !problems.empty())
					return {problems, {}, {}};
				if(m_port.sendSysex && (!m_session.state().globalSlot || *m_session.state().globalSlot == ref.slot))
					m_port.sendSysex(ed::mdSetActiveGlobal(ref.slot));
			}
			else
			{
				// Live edits only: the firmware takes MIDI in order, so the slot asked for now
				// shows them.
				m_session.requestGlobal(ref.slot);
			}
			auto& push = m_pushes[ref];
			push.slot.abandon();
			push.slot.want(_change.after);
			push.sentMs = now();
			return ok();
		}
		}
		return ok();
	}

	// ---- machine commands ----

	const std::map<std::string, MdMachine::Handler>& MdMachine::handlers()
	{
		static const std::map<std::string, Handler> map{
			{"load", &MdMachine::cmdLoad}, {"select", &MdMachine::cmdSelect}, {"saveKit", &MdMachine::cmdSaveKit},
			{"reloadKit", &MdMachine::cmdReloadKit}, {"kitLoad", &MdMachine::cmdKitLoad}, {"kitSaveAs", &MdMachine::cmdKitSaveAs},
			{"record", &MdMachine::cmdRecord}, {"recTrig", &MdMachine::cmdRecTrig}, {"chain", &MdMachine::cmdChain},
			{"chainClear", &MdMachine::cmdChainClear}, {"globalSlot", &MdMachine::cmdGlobalSlot},
			{"selectSong", &MdMachine::cmdSelectSong}, {"reloadSong", &MdMachine::cmdReloadSong},
			{"sampleName", &MdMachine::cmdSampleName}, {"play", &MdMachine::cmdPlay}, {"stop", &MdMachine::cmdStop},
			{"mute", &MdMachine::cmdMute}};
		return map;
	}

	std::vector<std::string> MdMachine::commandsHandled()
	{
		std::vector<std::string> ops;
		for(const auto& [op, h] : handlers())
			ops.push_back(op);
		return ops;
	}

	Outcome MdMachine::command(const Value& _command, const Documents& _view)
	{
		const auto it = handlers().find(deskCore::opOf(_command));
		if(it == handlers().end())
			return refuse("unknown command " + deskCore::opOf(_command));
		return (this->*(it->second))(_command, _view);
	}

	// What a machine command would lose asks first (the core sends it again with force).
	Outcome MdMachine::askFor(const Value& _command, const Documents&)
	{
		const auto op = deskCore::opOf(_command);
		if(op == "select")
		{
			const auto slot = static_cast<uint8_t>(*intOf(_command, "p"));
			if(!flagOf(_command, "chainOk") && m_telemetry.chainKnown && m_telemetry.chain.active)
			{
				Value a = Value::object();
				a.set("type", "ask");
				a.set("ask", "breakChain");
				a.set("p", slot);
				return ask(a);
			}
			if(m_session.selectWouldDiscardKitEdits(slot))
			{
				Value a = Value::object();
				a.set("type", "ask");
				a.set("ask", "discardKit");
				a.set("p", slot);
				a.set("kit", static_cast<int>(*m_session.state().kit));
				a.set("target", static_cast<int>(m_session.state().patternKits.at(slot)));
				return ask(a);
			}
		}
		if(op == "kitLoad" && m_session.state().workingKit == mdDataLink::Session::WorkingKit::Edited)
			return ask(askOf("loadKit", _command, currentKit()));
		return ok();
	}

	Outcome MdMachine::cmdLoad(const Value& _m, const Documents&)
	{
		const auto* kind = _m.find("kind");
		const auto k = kind && kind->isString() ? kindFromName(kind->asString()) : std::nullopt;
		if(!k || *k == DocKind::WorkingKit)
			return refuse("kind: expected pattern, kit, song or global");
		load({*k, static_cast<uint8_t>(*intOf(_m, "slot"))}, true);
		return ok();
	}

	Outcome MdMachine::cmdSelect(const Value& _m, const Documents&)
	{
		const auto slot = static_cast<uint8_t>(*intOf(_m, "p"));
		if(flagOf(_m, "now") && m_telemetry.playing)
		{
			// Switch now: STOP, LOAD PATTERN, PLAY, each step when the machine shows the one before.
			runSequence({{Act::Stop, 0, deskCore::Wait::Stopped, 0, 1000},
				{Act::SelectPattern, slot, deskCore::Wait::PatternIs, slot, 1000},
				{Act::Play}});
			m_audibleQueue.reset();
			load({DocKind::Pattern, slot}, true);
			return ok("Switched now: STOP, LOAD PATTERN, PLAY");
		}
		m_session.selectPattern(slot);
		// While the sequencer plays the switch waits for the end of the pattern.
		m_switchReportedMs = -1;
		if(m_telemetry.playing && m_session.state().pattern != slot)
			m_audibleQueue = slot;
		else
			m_audibleQueue.reset();
		load({DocKind::Pattern, slot}, true);
		return ok();
	}

	Outcome MdMachine::cmdSaveKit(const Value&, const Documents& _view)
	{
		const auto kit = currentKit();
		if(!kit)
			return refuse("The current kit is not known yet");
		m_session.saveKit(*kit);
		// The stored slot now holds the working kit.
		if(const auto* w = _view.workingKitOf(*kit))
			observe(*w, Source::Tracked);
		return ok("Saved kit " + std::to_string(*kit + 1) + " on the machine");
	}

	Outcome MdMachine::cmdReloadKit(const Value&, const Documents&)
	{
		const auto kit = currentKit();
		if(!kit)
			return refuse("The current kit is not known yet");
		m_session.loadKit(*kit);
		// The working kit is the stored slot again: from memory, or from the slot's next dump.
		m_memory = switched();
		m_seedWorking = true;
		forget({DocKind::WorkingKit, 0});
		load({DocKind::Kit, *kit}, true);
		return ok("Reloaded kit " + std::to_string(*kit + 1) + " from the machine");
	}

	// LOAD KIT and SAVE KIT n from the kit library. Measured (mdP4ProbeFirmwareTest library):
	// both make the slot the current kit and, in EXTENDED mode, relink the current pattern to
	// it; LOAD KIT replaces unsaved edits (the machine keeps them in its UNDO KIT; askFor asks).
	Outcome MdMachine::cmdKitLoad(const Value& _m, const Documents&)
	{
		const auto slot = static_cast<uint8_t>(*intOf(_m, "k"));
		m_session.loadKit(slot);
		return kitSwitchedBy(slot, true);
	}

	Outcome MdMachine::cmdKitSaveAs(const Value& _m, const Documents& _view)
	{
		const auto slot = static_cast<uint8_t>(*intOf(_m, "k"));
		m_session.saveKit(slot);
		// The stored slot now holds the working kit.
		if(const auto cur = currentKit())
			if(const auto* w = _view.workingKitOf(*cur))
			{
				auto saved = *w;
				saved.position = slot;
				observe(saved, Source::Tracked);
			}
		return kitSwitchedBy(slot, false);
	}

	// The machine switched kits and relinked the pattern: read them back.
	Outcome MdMachine::kitSwitchedBy(const uint8_t _slot, const bool _load)
	{
		m_session.requestStatus();
		load({DocKind::Kit, _slot}, true);
		if(const auto p = m_session.state().pattern)
			load({DocKind::Pattern, *p}, true);
		return ok(_load ? "Loaded kit " + std::to_string(_slot + 1) : "Saved as kit " + std::to_string(_slot + 1)
			+ ": it is now the current kit");
	}

	// REC works as on the machine: hold RECORD and press PLAY to live record (from STOP; while
	// playing the firmware does not enter it, so the adapter stops first), PLAY again to leave
	// recording and keep playing.
	Outcome MdMachine::cmdRecord(const Value&, const Documents&)
	{
		if(!canLiveRecord())
			return refuse(m_profile.wire ? g_noLiveRecordWire : g_noLiveRecordTelemetry);
		if(m_telemetry.recording)
			return pressKey("play") ? ok("Recording off, the pattern keeps playing") : refuse("No panel here");
		if(m_telemetry.playing)
		{
			runSequence({{Act::Stop, 0, deskCore::Wait::Stopped, 0, 1500}, {Act::RecordPlay}});
			return ok("Live recording from step 1 (the Machinedrum starts it from STOP)");
		}
		return pressKey("recordPlay") ? ok("Live recording: play the tracks, turn the knobs") : refuse("No panel here");
	}

	// recTrig plays a track like its TRIG key, which the firmware records.
	Outcome MdMachine::cmdRecTrig(const Value& _m, const Documents&)
	{
		const auto t = *intOf(_m, "t");
		if(!m_telemetry.recording)
			return refuse("Not recording: press REC first");
		return pressKey("trig" + std::to_string(t + 1)) ? ok() : refuse("TRIG keys need the local emulated machine");
	}

	// Chaining as on the machine: hold BANK, press the TRIG keys (mdDeskChain.h). The chain is
	// the firmware's; the page sees it through the telemetry.
	Outcome MdMachine::cmdChain(const Value& _m, const Documents&)
	{
		std::vector<int> patterns;
		if(const auto* list = _m.find("patterns"); list && list->isArray())
			for(const auto& v : list->asArray())
				patterns.push_back(v.isNumber() ? static_cast<int>(v.asNumber()) : -1);
		auto errors = validateChain(patterns);
		if(errors.empty() && !canChain())
			errors.emplace_back(g_noChains);
		const auto keys = errors.empty() ? chainKeys(patterns, m_telemetry.bankGroup) : std::vector<std::string>{};
		if(errors.empty() && keys.empty())
			errors.emplace_back("The machine's BANK GROUP (A-D / E-H) is not known yet");
		if(!errors.empty())
			return {errors, {}, {}};
		bool pressed = true;
		for(const auto& k : keys)
			pressed = pressed && pressKey(k);
		m_audibleQueue.reset();
		if(!pressed)
			return refuse("The panel did not take the keys");
		return ok(m_telemetry.playing ? "Chained: the machine plays them in this order from the pattern end, and loops"
			: "Chained: PLAY starts at " + ed::mdPatternName(static_cast<uint8_t>(patterns.front())) + ", then loops");
	}

	// CLEAR is LOAD PATTERN of the current pattern, which is what ends a chain on the machine.
	Outcome MdMachine::cmdChainClear(const Value&, const Documents&)
	{
		const auto current = m_session.state().pattern;
		if(!current)
			return refuse("The current pattern is not known yet");
		m_session.selectPattern(*current);
		return ok("Chain cleared: " + ed::mdPatternName(*current) + " plays on");
	}

	// The machine's active GLOBAL slot (0x56), then its settings are read.
	Outcome MdMachine::cmdGlobalSlot(const Value& _m, const Documents&)
	{
		const auto slot = static_cast<uint8_t>(*intOf(_m, "slot"));
		if(m_port.sendSysex)
			m_port.sendSysex(ed::mdSetActiveGlobal(slot));
		m_session.requestStatus();
		load({DocKind::Global, slot}, true);
		return ok("Global " + std::to_string(slot + 1) + " is active");
	}

	// P1: the Machinedrum ignores LOAD SONG while it plays.
	Outcome MdMachine::cmdSelectSong(const Value& _m, const Documents&)
	{
		const auto s = static_cast<uint8_t>(*intOf(_m, "s"));
		if(m_telemetry.playing)
			return refuse("Stop first: the Machinedrum loads another song only when stopped");
		m_session.loadSong(s);
		load({DocKind::Song, s}, true);
		return ok("Song " + std::to_string(s + 1) + " loaded");
	}

	// P1: the playing song ignores dumps and LOAD SONG: stop, load, play, each when the machine
	// shows the step before it (P6: not after fixed delays).
	Outcome MdMachine::cmdReloadSong(const Value&, const Documents&)
	{
		const auto song = m_session.state().song;
		if(!song)
			return refuse("The current song is not known yet");
		if(m_telemetry.playing)
			runSequence({{Act::Stop, 0, deskCore::Wait::Stopped, 0, 1000},
				{Act::LoadSong, *song, deskCore::Wait::StatusReply, 0, 1000},
				{Act::Play}});
		else
			m_session.loadSong(*song);
		return ok("Song reloaded: stop, load, play");
	}

	// UW ROM slot names: the firmware takes 0x73 but never reports names.
	Outcome MdMachine::cmdSampleName(const Value& _m, const Documents&)
	{
		const auto slot = *intOf(_m, "slot");
		const auto* name = _m.find("name");
		const auto bytes = name && name->isString() ? ed::mdSetSampleName(static_cast<uint8_t>(slot), name->asString())
			: std::vector<uint8_t>{};
		if(bytes.empty())
			return refuse("A sample name is 1-4 letters (A-Z, 0-9, space, punctuation) for ROM slot 1-48");
		if(m_port.sendSysex)
			m_port.sendSysex(bytes);
		char label[8];
		std::snprintf(label, sizeof(label), "ROM-%02d", slot + 1);
		return ok(std::string("Sent the name to ") + label + ". The Machinedrum shows it in SAMPLE MGR; it cannot report "
			"names back, so the editor does not read them.");
	}

	Outcome MdMachine::cmdPlay(const Value&, const Documents&)
	{
		return pressKey("play") ? ok() : refuse("Transport keys need the local emulated machine");
	}

	Outcome MdMachine::cmdStop(const Value&, const Documents&)
	{
		return pressKey("stop") ? ok() : refuse("Transport keys need the local emulated machine");
	}

	Outcome MdMachine::cmdMute(const Value& _m, const Documents&)
	{
		const auto t = static_cast<uint8_t>(*intOf(_m, "t"));
		const bool on = flagOf(_m, "on");
		m_mutes[t] = on;
		if(m_port.sendMute)
			m_port.sendMute(t, on);
		return ok();
	}

	// ---- sequences ----

	void MdMachine::runSequence(std::vector<deskCore::SeqStep<Act>> _steps)
	{
		m_sequence.start(std::move(_steps));
		pumpSequence(now());
	}

	void MdMachine::pumpSequence(const double _now)
	{
		if(!m_sequence.running())
			return;
		deskCore::SeqFacts f;
		f.playing = m_telemetry.playing;
		f.statusReplies = m_wire.statusReplies;
		f.pattern = m_session.state().pattern ? *m_session.state().pattern : -1;
		for(const auto& d : m_sequence.due(_now, f))
		{
			switch(d.action)
			{
			case Act::Stop: pressKey("stop"); break;
			case Act::Play: pressKey("play"); break;
			case Act::RecordPlay:
				if(!m_telemetry.playing)
					pressKey("recordPlay");
				break;
			case Act::LoadSong:
				m_session.loadSong(static_cast<uint8_t>(d.arg));
				m_session.requestStatus();
				break;
			case Act::SelectPattern:
				m_session.selectPattern(static_cast<uint8_t>(d.arg));
				break;
			}
		}
	}

	// ---- loading ----

	void MdMachine::load(const DocRef& _ref, const bool _urgent)
	{
		if(!_urgent && known(_ref))
			return;
		m_loads.want(_ref, _urgent);
	}

	void MdMachine::request(const DocRef& _ref)
	{
		switch(_ref.kind)
		{
		case DocKind::Pattern: m_session.requestPattern(_ref.slot); break;
		case DocKind::Kit: m_session.requestKit(_ref.slot); break;
		case DocKind::Song: m_session.requestSong(_ref.slot); break;
		case DocKind::Global: m_session.requestGlobal(_ref.slot); break;
		case DocKind::WorkingKit: break;	// from memory, or its slot's dump
		}
	}

	void MdMachine::pumpLoads(const double _now)
	{
		const auto& inFlight = m_loads.loading();
		const double timeout = g_loadTimeoutMs + (m_profile.wire && inFlight ? deskCore::DinPacer::wireMs(replyBytes(inFlight->kind)) * 1.5 : 0);
		// Loads wait while an edit is on the wire (they would delay its read-back) and around
		// panel key presses.
		const bool mayStart = !busy() && !keysOnTheirWay();
		if(const auto next = m_loads.next(_now, timeout, mayStart, g_loadPolicy))
			request(*next);
	}

	void MdMachine::pumpPushes(const double _now)
	{
		for(auto& [ref, push] : m_pushes)
		{
			if(!push.slot.busy())
				continue;
			const double timeout = g_pushTimeoutMs + (m_profile.wire ? 2.5 * deskCore::DinPacer::wireMs(replyBytes(ref.kind)) : 0);
			if(_now - push.sentMs < timeout)
				continue;
			push.slot.abandon();
			fail(ref, std::string("Push failed: the machine did not read back ") + kindName(ref.kind)
				+ " " + (ref.kind == DocKind::Pattern ? ed::mdPatternName(ref.slot) : std::to_string(ref.slot + 1))
				+ ". Showing what it holds.");
			load(ref, true);
		}
	}

	// ---- device -> machine ----

	void MdMachine::onSysex(const Bytes& _message)
	{
		m_wire.heard(now());
		if(ed::parseMdStatusResponse(_message) && (m_profile.wire || m_probe == Probe::Running))
			m_wire.statusReply(now());
		m_session.onSysex(_message);
	}

	void MdMachine::onDumpReadBack(const Document& _doc)
	{
		const auto ref = refOf(_doc);
		m_loads.arrived(ref);
		const auto it = m_pushes.find(ref);
		if(it == m_pushes.end())
		{
			observe(_doc, Source::Dump);
			return;
		}
		auto& push = it->second;
		const auto t = now();
		switch(deskCore::readBack(push.slot, _doc))
		{
		case deskCore::ReadBackAction::Observe:
			observe(_doc, Source::Dump);
			break;
		case deskCore::ReadBackAction::Settle:
			m_lastRoundTripMs = t - push.sentMs;
			settle(_doc, Source::Dump);
			break;
		case deskCore::ReadBackAction::ObserveSendNext:
			m_lastRoundTripMs = t - push.sentMs;
			observe(_doc, Source::Dump);
			if(ref.kind == DocKind::Pattern)
				m_session.pushPattern(std::get<ed::MdPattern>(*push.slot.inFlight()));
			else
				m_session.pushSong(std::get<ed::MdSong>(*push.slot.inFlight()));
			push.sentMs = t;
			break;
		case deskCore::ReadBackAction::Wait:
			break;
		}
	}

	void MdMachine::onPattern(const ed::MdPattern& _p)
	{
		onDumpReadBack(_p);
		// The current pattern names the kit the Sound and Mix workspaces edit.
		if(m_session.state().pattern == _p.position)
			if(const auto kit = currentKit())
				load({DocKind::Kit, *kit}, true);
	}

	void MdMachine::onSong(const ed::MdSong& _s)
	{
		onDumpReadBack(_s);
	}

	// A dump is the stored slot. The kit that plays is the working kit: its slot's dump seeds it
	// when it starts playing, memory (when the device offers it) is the truth from then on.
	void MdMachine::onKit(const ed::MdKit& _k)
	{
		m_loads.arrived({DocKind::Kit, _k.position});
		observe(_k, Source::Dump);
		if(currentKit() != _k.position)
			return;
		judgeWorkingKit(_k);
		// Until memory shows it (or on a device without memory), the kit that plays starts as its slot.
		if(m_seedWorking)
		{
			m_seedWorking = false;
			observe(WorkingKit{_k}, Source::Dump);
		}
	}

	void MdMachine::onGlobal(const ed::MdGlobal& _g)
	{
		const DocRef ref{DocKind::Global, _g.position};
		m_loads.arrived(ref);
		if(m_session.state().globalSlot && *m_session.state().globalSlot != _g.position)
			return;
		observe(_g, Source::Dump);
		setBaseChannel(_g);
		// The read-back after a global edit: what the firmware stored, whatever it normalised.
		if(const auto it = m_pushes.find(ref); it != m_pushes.end() && it->second.slot.busy())
		{
			m_lastRoundTripMs = now() - it->second.sentMs;
			it->second.slot.abandon();
			settle(_g, Source::Dump);
		}
	}

	void MdMachine::onState(const mdDataLink::Session::State& _s)
	{
		if(_s.kit && _s.kit != m_lastKit)
		{
			// Another kit plays now: the old working kit is gone; the new one comes from memory, or
			// from its slot's dump.
			if(m_lastKit)
				forget({DocKind::WorkingKit, 0});
			load({DocKind::Kit, *_s.kit}, true);
			m_lastKit = _s.kit;
			m_memory = switched(m_memory);
			m_seedWorking = true;
		}
		if(_s.pattern && _s.pattern != m_lastPattern)
		{
			m_lastPattern = _s.pattern;
			if(!known({DocKind::Pattern, *_s.pattern}))
				load({DocKind::Pattern, *_s.pattern}, true);
			if(!m_telemetry.valid && m_audibleQueue == _s.pattern)
				m_audibleQueue.reset();
		}
		if(_s.globalSlot && !known({DocKind::Global, *_s.globalSlot}))
			load({DocKind::Global, *_s.globalSlot}, true);
		if(_s.song && !known({DocKind::Song, *_s.song}))
			load({DocKind::Song, *_s.song}, true);
		if(!m_backgroundQueued && _s.pattern && _s.kit)
		{
			// Everything else in the background, so the song palette and the kit-link warnings
			// know every pattern, and the kit library every slot's name (P4). Over DIN MIDI a
			// pattern takes 1.7 s: the small kits first there.
			m_backgroundQueued = true;
			const auto kits = [this]
			{
				for(unsigned k = 0; k < 64; ++k)
					load({DocKind::Kit, static_cast<uint8_t>(k)}, false);
			};
			if(m_profile.kitsFirst)
				kits();
			for(unsigned p = 0; p < 128; ++p)
				load({DocKind::Pattern, static_cast<uint8_t>(p)}, false);
			for(unsigned s = 0; s < 32; ++s)
				load({DocKind::Song, static_cast<uint8_t>(s)}, false);
			if(!m_profile.kitsFirst)
				kits();
		}
	}

	void MdMachine::onHostKitParam(const uint8_t _track, const uint8_t _index, const uint8_t _value, const Documents& _view)
	{
		const auto kit = currentKit();
		if(!kit || _track > 15 || _index > 24)
			return;
		const auto* w = _view.workingKitOf(*kit);
		if(!w)
			return;
		auto k = *w;
		auto& slot = _index == 24 ? k.levels[_track] : k.params[_track][_index];
		if(slot == _value)
			return;
		slot = _value;
		m_session.noteWorkingKitEdited();
		// Something else moved the kit that plays: the machine holds it (memory will show it too).
		observe(WorkingKit{k}, Source::Tracked);
	}

	void MdMachine::onHostMute(const uint8_t _track, const bool _muted)
	{
		if(_track < 16)
			m_mutes[_track] = _muted;
	}

	void MdMachine::sendModulation(const uint8_t _track, const uint8_t _param, const uint8_t _value, const Documents& _view)
	{
		if(!currentKit() || !m_port.sendKitParam)
			return;
		m_port.sendKitParam(_track, _param, _value);
		onHostKitParam(_track, _param, _value, _view);
	}

	void MdMachine::onWorkingKitMemory(const Bytes& _region, const Documents& _view)
	{
		m_memory.region = _region;
		takeWorkingKit(&_view);
	}

	void MdMachine::takeWorkingKit(const Documents* _view)
	{
		if(!m_memory.region || m_probe != Probe::Running || !m_profile.memory)
			return;
		const auto kit = currentKit();
		const ed::MdKit* stored = nullptr;
		if(kit && _view)
			if(const auto it = _view->kits.find(*kit); it != _view->kits.end())
				stored = &it->second;
		auto r = takeMemory(std::move(m_memory), kit, stored, m_knobs.targets(), now());
		m_memory = std::move(r.next);
		if(r.askKitStatus && now() - m_kitStatusAskedMs > 200 && m_port.sendSysex)
		{
			m_kitStatusAskedMs = now();
			m_port.sendSysex(ed::mdStatusRequest(ed::MdStatus::Kit));
		}
		if(r.take)
		{
			m_seedWorking = false;
			if(r.settles)
				settle(WorkingKit{*r.take}, Source::Memory);
			else
				observe(WorkingKit{*r.take}, Source::Memory);
		}
		if(stored && !m_memory.region)
			judgeWorkingKit(*stored);
	}

	// Edited or clean from memory against the stored slot, not from what the editor saw.
	void MdMachine::judgeWorkingKit(const ed::MdKit& _stored)
	{
		const auto& memory = m_memory.image;
		if(!memory || currentKit() != memory->position || _stored.position != memory->position)
			return;
		const bool clean = ed::mdSameKitSound(*memory, _stored);
		const auto want = clean ? mdDataLink::Session::WorkingKit::Clean : mdDataLink::Session::WorkingKit::Edited;
		if(m_session.state().workingKit != want)
			m_session.noteWorkingKitObserved(clean);
	}

	void MdMachine::setBaseChannel(const ed::MdGlobal& _g)
	{
		if(m_port.baseChannel)
			m_port.baseChannel(static_cast<uint8_t>(_g.baseChannel & 0x0f));
	}

	TelemetryEvents MdMachine::onTelemetry(const Telemetry& _t)
	{
		const auto e = diff(m_telemetry, _t);
		m_telemetry = _t;
		m_telemetrySeen = true;
		// The keys' fact: the device reports them pending, then none left.
		if(m_keys.sent && _t.panelPending > 0)
			m_keys.seenPending = true;
		else if(m_keys.sent && (m_keys.seenPending || _t.panelPending < 0))
			m_keys.sent = false;
		if(!e.any)
			return e;
		// The mutes the machine plays with (RAM), whoever set them: the page, CCs, the MUTE window.
		if(_t.mutes >= 0)
			for(size_t t = 0; t < 16; ++t)
				m_mutes[t] = (_t.mutes >> t) & 1;
		if(m_audibleQueue)
		{
			// Status and the RAM pattern byte both switch about two steps before the new pattern
			// is heard (P1, P2 smoke test); the playhead wrap is the audible switch.
			const auto t = now();
			const bool reported = (_t.valid && _t.pattern == *m_audibleQueue) || m_session.state().pattern == *m_audibleQueue;
			if(reported && m_switchReportedMs < 0)
				m_switchReportedMs = t;
			if(m_switchReportedMs >= 0 && (e.wrapped || !_t.playing || t - m_switchReportedMs > 2000))
			{
				m_audibleQueue.reset();
				m_switchReportedMs = -1;
			}
		}
		if(e.recordChanged)
		{
			m_knobs.reset();
			m_recLock.reset();
			// The last recorded notes: read the pattern back.
			if(!_t.recording && m_session.state().pattern)
				load({DocKind::Pattern, *m_session.state().pattern}, true);
		}
		// The sequencer switched on its own (chain, panel, program change): ask.
		if(e.patternChanged && m_wire.replied && m_session.state().pattern != _t.pattern && m_port.sendSysex)
		{
			m_port.sendSysex(ed::mdStatusRequest(ed::MdStatus::Pattern));
			m_port.sendSysex(ed::mdStatusRequest(ed::MdStatus::Kit));
		}
		pumpSequence(now());
		Value t = Value::object();
		t.set("type", "telemetry");
		t.set("step", _t.step);
		t.set("pattern", _t.pattern);
		t.set("playing", _t.playing);
		t.set("recording", _t.recording);
		t.set("valid", _t.valid);
		notice(std::move(t));
		return e;
	}

	void MdMachine::pumpRecording(const double _now, const Documents& _view)
	{
		if(m_recLock && _now - m_recLock->atMs > 3000)
			m_recLock.reset();
		if(!m_telemetry.recording)
			return;
		// What the firmware records shows up in the pattern: read it back now and then.
		const auto pattern = m_session.state().pattern;
		if(pattern && _now - m_recordPollMs > 400 && !busy())
		{
			m_recordPollMs = _now;
			load({DocKind::Pattern, *pattern}, true);
		}
		const auto* memory = m_memory.image && currentKit() == m_memory.image->position ? &*m_memory.image : nullptr;
		const auto step = m_knobs.next(_now, m_telemetry.knobPage, memory);
		if(!step)
			return;
		switch(step->kind)
		{
		case KnobStep::Kind::SelectTrack:
			if(m_port.sendSysex)
				m_port.sendSysex(ed::mdSetStatus(ed::MdStatus::Track, step->track));
			break;
		case KnobStep::Kind::PageKey:
			pressKey("page");
			break;
		case KnobStep::Kind::Turn:
			if(m_port.turnKnob && step->steps)
			{
				m_port.turnKnob(step->encoder, step->steps);
				m_keys.sent = true;
				m_keys.seenPending = false;
				m_keys.sentMs = _now;
				// Honest about where it lands: the track's next trig after the playing step.
				const auto p = pattern ? _view.patterns.find(*pattern) : _view.patterns.end();
				const auto at = p != _view.patterns.end() ? nextLockStep(p->second, step->track, m_telemetry.step) : std::nullopt;
				if(at && m_telemetry.knobPage >= 0)
					m_recLock = RecLock{step->track, static_cast<uint8_t>(m_telemetry.knobPage * 8 + step->encoder), *at, _now};
				else
					m_recLock.reset();
			}
			break;
		}
	}

	void MdMachine::pageReady()
	{
		if(m_profile.wire || m_probe == Probe::Running)
		{
			m_session.requestStatus();
			m_lastStatusMs = now();
		}
	}

	void MdMachine::tick(const double _now, const Documents& _view)
	{
		pumpSequence(_now);
		if(!m_profile.wire && m_probe != Probe::Running)
			return;
		releaseKeys();
		const auto statusEvery = m_wire.replied && m_audibleQueue ? g_statusQueuedMs : g_statusIdleMs;
		if(_now - m_lastStatusMs >= statusEvery && !keysOnTheirWay())
		{
			m_lastStatusMs = _now;
			if(m_wire.replied && m_audibleQueue && m_port.sendSysex)
				m_port.sendSysex(ed::mdStatusRequest(ed::MdStatus::Pattern));
			else
				m_session.requestStatus();
		}
		if(m_wire.replied)
			pumpLoads(_now);
		takeWorkingKit(&_view);
		pumpRecording(_now, _view);
		pumpPushes(_now);
	}

	// ---- the machine document ----

	Value MdMachine::state(const Documents&) const
	{
		auto doc = mdDataLink::Session::stateToJson(m_session.state());
		const auto lc = lifecycle();
		Value desk = Value::object();
		desk.set("firmware", deskCore::legacyFirmware(lc));
		desk.set("engine", m_profile.id);
		desk.set("link", deskCore::legacyLink(lc, m_profile.wire));
		desk.set("boot", deskCore::legacyBoot(lc));
		desk.set("tx", busy());
		desk.set("loading", static_cast<int>(m_loads.pending()));
		desk.set("roundTripMs", m_lastRoundTripMs);
		desk.set("queued", m_audibleQueue ? Value(static_cast<int>(*m_audibleQueue)) : Value());
		desk.set("playing", m_telemetry.playing);
		desk.set("telemetry", m_telemetry.valid);
		desk.set("recording", m_telemetry.recording);
		desk.set("gridEdit", m_telemetry.gridEdit);
		desk.set("knobPage", m_telemetry.knobPage);
		// "memory": the current kit document is read from the machine's memory. "tracked": it
		// is the stored slot plus the edits the editor saw.
		desk.set("kitSource", m_memory.image && currentKit() == m_memory.image->position ? "memory" : "tracked");
		Value mutes = Value::array();
		for(size_t t = 0; t < 16; ++t)
			if(m_mutes[t])
				mutes.push(static_cast<int>(t));
		desk.set("mutes", std::move(mutes));
		desk.set("mutesSource", m_telemetry.mutes >= 0 ? "memory" : "tracked");
		if(m_telemetry.chainKnown)
		{
			Value chain = Value::object();
			chain.set("active", m_telemetry.chain.active);
			chain.set("next", m_telemetry.chain.next);
			Value list = Value::array();
			for(const auto p : m_telemetry.chain.patterns)
				list.push(static_cast<int>(p));
			chain.set("patterns", std::move(list));
			desk.set("chain", std::move(chain));
		}
		else
			desk.set("chain", Value());
		desk.set("bankGroup", m_telemetry.bankGroup);
		if(m_recLock)
		{
			Value l = Value::object();
			l.set("track", static_cast<int>(m_recLock->track));
			l.set("param", static_cast<int>(m_recLock->param));
			l.set("step", static_cast<int>(m_recLock->step));
			desk.set("recLock", std::move(l));
		}
		else
			desk.set("recLock", Value());
		doc.set("desk", std::move(desk));
		return doc;
	}
}
