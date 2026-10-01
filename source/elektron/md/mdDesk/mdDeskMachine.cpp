#include "mdDeskMachine.h"

#include "mdDeskChain.h"
#include "mdDeskLibrary.h"

#include "deskCore/deskKinds.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdGlobal.h"
#include "elektronData/mdJson.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mdValidate.h"
#include "elektronData/mdWorkingKit.h"

#include <cassert>
#include <algorithm>
#include <cstdio>
#include <utility>

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
		// Control All (DESIGN-edit-flow.md): FUNCTION is let go this long after the last turn; the page
		// key waits as long as the knob recorder's; without the panel, one CC per value per this.
		constexpr double g_tweakQuietMs = 150;
		constexpr double g_tweakPageGapMs = KnobRecorder::g_pageGapMs;
		constexpr double g_coalesceMs = 50;
		constexpr double g_tweakSelectMs = 100;		// SET STATUS track again if the status did not follow
		// The device sends one panel packet a block (DeskDevice, 128 at most queued): a turn waits while more
		// than this are on their way, and sends at most this many steps.
		constexpr int g_tweakMaxPending = 4;
		constexpr int g_tweakMaxSteps = 16;
		// The machine's gesture is a fact to check, not a promise: page keys or track selections that the
		// machine does not follow end it, and once it is over (or given up) memory must show every value
		// it wanted; what it does not show goes as CCs (the path without the panel).
		constexpr int g_tweakMaxPageKeys = 4;
		constexpr int g_tweakMaxSelects = 5;
		constexpr double g_tweakCheckMs = 300;

		// The dump a request brings back, for timeouts at DIN speed (the kind's record).
		size_t replyBytes(const DocKind _k)
		{
			const auto* s = deskCore::kindSpec<MdModel>(_k);
			return s ? s->replyBytes : 0;
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
		// A question before something is lost; the core publishes it and the page's answer is the
		// same command with force.
		Outcome ask(const char* _what, std::string _message, const char* _confirm, Value _details = Value::object())
		{
			assert(std::find(MdModel::asks().begin(), MdModel::asks().end(), _what) != MdModel::asks().end() && "a question the model does not declare");
			Outcome o;
			o.ask = deskCore::Ask{_what, std::move(_message), _confirm, std::move(_details)};
			return o;
		}

		std::string kitNameOf(const ed::MdKit& _k)
		{
			std::string n;
			for(const auto c : _k.name)
				if(c)
					n += static_cast<char>(c);
			return n.empty() ? "KIT " + std::to_string(_k.position + 1) : n;
		}

		// "K05 NAME": the kit that plays as it sounds, another slot as stored.
		std::string kitLabel(const Documents& _view, const uint8_t _slot)
		{
			char k[8];
			std::snprintf(k, sizeof(k), "K%02d", _slot + 1);
			const auto* w = _view.workingKitOf(_slot);
			const auto it = _view.kits.find(_slot);
			const auto* kit = w ? w : it != _view.kits.end() ? &it->second : nullptr;
			return std::string(k) + (kit ? " " + kitNameOf(*kit) : std::string());
		}

		bool emptyKit(const ed::MdKit& _k) { return isEmptyKit(_k); }
		bool anyTrig(const ed::MdPattern& _p) { return std::any_of(_p.trigs.begin(), _p.trigs.end(), [](const uint64_t _t) { return _t != 0; }); }

		// Why the panel-key features are not here (the capabilities' reasons and the refusals).
		constexpr const char* g_noLiveRecordKeys = "Live recording needs the machine's panel keys; this engine has none.";
		constexpr const char* g_noLiveRecordTelemetry = "Live recording needs the machine's sequencer telemetry (MD OS 1.63).";
		constexpr const char* g_noChains = "Chaining is made with the machine's keys: it needs the local emulated MD OS 1.63";


	}

	const Profile& emulatorProfile()
	{
		static const Profile p{"emu", "EMU OS 1.63", "Engine: the real Machinedrum OS 1.63 runs inside the app. Choose HW MIDI to "
			"edit a real Machinedrum instead.", false, false, true, true};
		return p;
	}

	const Profile& wireProfile()
	{
		static const Profile p{"hw", "HW MIDI", "Engine: a real Machinedrum on the plug-in's MIDI in and out, at MIDI speed (a "
			"pattern takes about 1.7 s each way). No live recording, chains or boot screen over MIDI; PLAY/STOP are MIDI "
			"Start/Stop.", true, true, false, false};
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
		// P9: while a sample goes out (SDS) nothing else may come between its packets; other SysEx waits.
		m_rawSysex = m_port.sendSysex;
		if(m_rawSysex)
			m_port.sendSysex = [this](const Bytes& _b)
			{
				if(m_sds.active())
					m_heldSysex.push_back(_b);
				else
					m_rawSysex(_b);
			};
		wireSession();
		m_wire = deskCore::WireFacts(m_port.nowMs ? m_port.nowMs() : 0);
	}

	void MdMachine::sendRaw(const Bytes& _message) const
	{
		if(m_rawSysex)
			m_rawSysex(_message);
	}

	// ---- P9: a sample to a ROM slot (SDS) ----

	std::string MdMachine::sendSample(const uint8_t _slot, const ed::MdSampleUpload& _upload)
	{
		if(!m_rawSysex)
			return "This engine cannot send SysEx to the machine.";
		if(m_sds.active())
			return "A sample is already on its way. Wait for it, or stop it.";
		if(m_telemetry.recording)
			return "The machine is recording. Stop it first.";
		auto dump = ed::mdSdsDump(_slot, _upload.samples, _upload.rate, _upload.name);
		if(!dump)
			return "The sample could not be made into SDS (ROM slot 1-48, 1 to 2 million samples).";
		m_sds.start(_slot, std::move(*dump), now(), [this](const Bytes& _b) { sendRaw(_b); });
		return {};
	}

	void MdMachine::cancelSample()
	{
		m_sds.cancel([this](const Bytes& _b) { sendRaw(_b); });
		pumpSample(now());
	}

	Outcome MdMachine::cmdSampleCancel(const Value&, const Documents&)
	{
		if(!m_sds.active())
			return refuse("No sample is on its way.");
		cancelSample();
		return ok("Stopped sending the sample.");
	}

	// The transfer's timeouts; once it is over, the SysEx held meanwhile goes out.
	void MdMachine::pumpSample(const double _now)
	{
		m_sds.pump(_now, [this](const Bytes& _b) { sendRaw(_b); });
		while(!m_sds.active() && !m_heldSysex.empty())
		{
			const auto b = std::move(m_heldSysex.front());
			m_heldSysex.pop_front();
			sendRaw(b);
		}
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
		m_working = deskCore::switched<ed::MdKit>();
		m_keys = {};
		m_lastKit.reset();
		m_lastPattern.reset();
		m_audibleQueue.reset();
		m_knobs.reset();
		m_recLock.reset();
		m_sequence.clear();
		m_intent.reset();
		m_tweak = {};
		m_coalesced.clear();
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
		static const std::string noKeys = "It needs the machine's panel keys; this engine has none.";
		c.set("transport", static_cast<bool>(m_port.pressKey), "This engine has no transport.");
		c.set("panelKeys", keys, noKeys);
		c.set("liveRecord", canLiveRecord(), panelKeys() ? g_noLiveRecordTelemetry : g_noLiveRecordKeys);
		c.set("chains", canChain(), g_noChains);
		c.set("lcd", m_profile.memory, "The machine's own LCD is on the machine.");
		c.set("workingKitMemory", m_profile.memory, "The working kit is the stored slot plus the edits the editor saw.");
		c.set("mutesFromMemory", m_profile.memory && m_telemetry.mutes >= 0, "The mutes are the ones the editor sent.");
		c.set("sampleNames", true);
		// P9: the waveforms come from the emulated machine's memory; a real Machinedrum ignores SDS dump
		// requests (measured on OS 1.63, P3), so its sample audio cannot be read.
		c.set("sampleAudio", m_profile.memory, "A real Machinedrum does not send its samples: it ignores SDS dump requests "
			"(measured on OS 1.63), so the editor cannot show their waveforms or play them.");
		c.set("sampleLoad", static_cast<bool>(m_rawSysex), "This engine cannot send SysEx to the machine.");
		c.set("modulators", telemetry, "The app modulators move with the machine's playhead; this engine reports none.");
		c.values.emplace_back("dumps", "direct");
		return c;
	}

	bool MdMachine::busy() const
	{
		if(m_sds.active())
			return true;
		for(const auto& [ref, push] : m_pushes)
			if(push.slot.busy())
				return true;
		// Control All on its way (FUNCTION held: no dump may be asked for meanwhile), or its CCs.
		if(m_tweak.active() || !m_coalesced.empty())
			return true;
		// A live edit the machine's memory does not show yet is on the wire too.
		return m_working.expect.expecting(now());
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
		m_keys.pressed(now());
		return true;
	}

	// ---- core -> machine ----

	Outcome MdMachine::review(const Value& _command, const std::vector<Change>& _changes, const Documents& _view)
	{
		// Control All: the intent the next submit() delivers (its change is the whole working kit).
		m_intent.reset();
		if(deskCore::opOf(_command) == "tweak")
		{
			const auto* g = _command.find("group");
			const std::string group = g && g->isString() ? g->asString() : "syn";
			const auto t = intOf(_command, "t");
			m_intent = TweakIntent{group == "fx" ? 1 : group == "rt" ? 2 : 0, static_cast<uint8_t>(intOf(_command, "knob").value_or(0)),
				intOf(_command, "d").value_or(0), t && *t >= 0 && *t < 16 ? std::optional<uint8_t>(static_cast<uint8_t>(*t)) : std::nullopt};
		}
		// While live recording the firmware writes the playing pattern itself; a dump from the
		// editor would overwrite what it just recorded.
		if(m_telemetry.recording)
			for(const auto& c : _changes)
				if(c.ref().kind == DocKind::Pattern && m_session.state().pattern == c.ref().slot)
					return refuse("Live recording owns this pattern: play the tracks, turn the knobs. Press REC to stop"
						" recording, then edit the grid.");
		// Slot writes that lose something ask first (the page sends them again with force): the
		// library clearing or writing over a slot that holds something, a kit written into the kit
		// that plays while it holds unsaved edits, a pattern dump into the current pattern that links
		// another kit (the firmware then loads that kit, measured).
		const bool edited = kitState(_view) == deskCore::KitState::Edited;
		const auto kit = currentKit();
		const auto* row = MdModel::commands().find(deskCore::opOf(_command));
		const bool library = row && row->group == g_library;
		const bool clears = library && (deskCore::opOf(_command) == "kitClear" || deskCore::opOf(_command) == "patClear");
		// Every question the changes raise, as one (the answer, force, answers them all).
		Outcome o;
		const auto add = [&o](Outcome _a) { o = deskCore::withAsk(std::move(o), _a); };
		for(const auto& c : _changes)
		{
			const auto ref = c.ref();
			// What the kit that plays loses; then what the stored slot loses.
			if(ref.kind == DocKind::Kit && kit && *kit == ref.slot && edited)
				add(ask("overwriteKit", "<b>" + kitLabel(_view, ref.slot) + "</b> is the kit that plays: it is loaded too, and its"
					" unsaved edits are lost (the machine keeps them in its UNDO KIT).", "Overwrite", kitDetails(kit)));
			if(ref.kind == DocKind::Pattern && m_session.state().pattern == ref.slot && edited
				&& std::get<ed::MdPattern>(c.after).kit != std::get<ed::MdPattern>(c.before).kit)
				add(ask("relinkKit", "The pattern that plays links another kit now: the machine loads it, and the unsaved edits"
					" of <b>" + (kit ? kitLabel(_view, *kit) : std::string("the kit")) + "</b> are lost.", "Go on", kitDetails(kit)));
			if(clears && ref.kind == DocKind::Kit)
				add(ask("clearSlot", "Clear <b>" + kitLabel(_view, ref.slot) + "</b>? Every track becomes GND-EMPTY.", "Clear kit"));
			else if(clears && ref.kind == DocKind::Pattern)
				add(ask("clearSlot", "Clear <b>" + ed::mdPatternName(ref.slot) + "</b>? Its notes and locks are removed.", "Clear pattern"));
			else if(library && ref.kind == DocKind::Kit && !emptyKit(std::get<ed::MdKit>(c.before)))
				add(ask("overwriteSlot", "Write over <b>" + kitLabel(_view, ref.slot) + "</b>? What it holds is replaced.", "Overwrite"));
			else if(library && ref.kind == DocKind::Pattern && anyTrig(std::get<ed::MdPattern>(c.before)))
				add(ask("overwriteSlot", "Write over <b>" + ed::mdPatternName(ref.slot) + "</b>? Its notes and locks are replaced.", "Overwrite"));
		}
		return o;
	}

	Value MdMachine::kitDetails(const std::optional<uint8_t> _kit)
	{
		Value d = Value::object();
		d.set("kit", _kit ? Value(static_cast<int>(*_kit)) : Value());
		return d;
	}

	deskCore::PushPolicy MdMachine::pushPolicy(const DocKind _kind) const
	{
		auto p = m_profile.push;
		// Over a wire a dump takes its time on it: no faster than that.
		if(m_profile.wire)
			p.minIntervalMs = std::max(p.minIntervalMs, deskCore::DinPacer::wireMs(replyBytes(_kind)));
		return p;
	}

	// Paced (DESIGN-edit-flow.md): the dump goes now or waits its turn (latest wins); the read-back is
	// asked for once the gesture is quiet (pumpPushes).
	Outcome MdMachine::pushDump(const Document& _doc, const Documents& _view)
	{
		const auto ref = refOf(_doc);
		auto problems = ref.kind == DocKind::Pattern ? ed::validate(std::get<ed::MdPattern>(_doc)) : ed::validate(std::get<ed::MdSong>(_doc));
		if(!problems.empty())
			return {problems, {}, {}};
		if(m_pushes[ref].slot.want(_doc, now(), pushPolicy(ref.kind)))
			sendDump(_doc, _view);
		return ok();
	}

	void MdMachine::sendDump(const Document& _doc, const Documents& _view)
	{
		if(refOf(_doc).kind != DocKind::Pattern)
		{
			m_session.pushSong(std::get<ed::MdSong>(_doc), false);
			return;
		}
		const auto& pattern = std::get<ed::MdPattern>(_doc);
		// A dump over the current pattern makes OS 1.63 load the kit it links from its slot, also when that
		// is the kit that plays (measured, mdDeskFirmwareTest sampler): the unsaved edits of the working kit
		// are gone. Taken before the dump, they go again as live edits right after it.
		const auto kit = currentKit();
		const bool reloads = m_session.state().pattern == pattern.position && kit && pattern.kit == *kit;
		const auto stored = reloads ? _view.kits.find(*kit) : _view.kits.end();
		const auto* held = stored != _view.kits.end() ? heldKit(_view) : nullptr;
		const auto working = held ? std::optional<ed::MdKit>(*held) : std::nullopt;
		m_session.pushPattern(pattern, false);
		if(working)
			restoreWorkingKit(stored->second, *working);
	}

	// The machine just loaded the kit that plays from its slot (_stored): what it held before (_working)
	// goes again as live edits, pending until memory shows it (the image of the slot is not taken meanwhile).
	void MdMachine::restoreWorkingKit(const ed::MdKit& _stored, const ed::MdKit& _working)
	{
		const auto delivery = kitDelivery(_stored, _working);
		if(delivery.edits.empty())
			return;
		for(const auto& e : delivery.edits)
		{
			if(e.kind == LiveEdit::Kind::Param || e.kind == LiveEdit::Kind::Level)
			{
				if(e.kind == LiveEdit::Kind::Param)
					m_coalesced.erase({e.track, e.index});
				if(m_port.sendKitParam)
					m_port.sendKitParam(e.track, e.kind == LiveEdit::Kind::Level ? 24 : e.index, e.value);
			}
			else if(const auto sysex = liveEditSysex(e); !sysex.empty() && m_port.sendSysex)
				m_port.sendSysex(sysex);
		}
		if(!m_profile.memory)
			return;
		// From the slot the machine now holds, not from before the edits still on their way.
		m_working.expect.clear();
		m_working.expect.sent(_stored, _working, now());
	}

	Outcome MdMachine::submit(const Change& _change, const Documents& _view)
	{
		const auto ref = _change.ref();
		switch(ref.kind)
		{
		case DocKind::Pattern:
		case DocKind::Song:
			return pushDump(_change.after, _view);
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
				m_working = deskCore::switched(m_working);
				m_working.seed = false;
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
			const auto intent = std::exchange(m_intent, std::nullopt);
			// Control All (manual p.37): the machine's own FUNCTION + DATA ENTRY gesture moves every track;
			// the edit is pending until memory shows it, like any live edit. No CCs.
			const auto lead = intent ? tweakLead(before, intent->track) : std::nullopt;
			if(intent && lead && tweakByPanel())
			{
				m_tweak.want(intent->page, intent->knob, intent->d, *lead, now());
				m_working.expect.sent(before, after, now());
				return ok();
			}
			// While live recording a parameter becomes DATA ENTRY turns: the firmware records knob
			// turns as locks, not CCs (P3). Which route is data: the machine's mode, then a pure split.
			const auto route = routeKitEdits(delivery.edits, m_telemetry.recording && static_cast<bool>(m_port.turnKnob));
			for(const auto& e : route.knobs)
				m_knobs.want(e.track, e.index, e.value);
			for(const auto& e : route.live)
			{
				if(e.kind == LiveEdit::Kind::Param && intent)
				{
					// Control All without the panel: coalesced, one CC per value per tick (pumpCoalesced).
					m_coalesced[{e.track, e.index}] = e.value;
				}
				else if(e.kind == LiveEdit::Kind::Param || e.kind == LiveEdit::Kind::Level)
				{
					// A plain edit goes at once; a coalesced value for it would come later and undo it.
					if(e.kind == LiveEdit::Kind::Param)
						m_coalesced.erase({e.track, e.index});
					if(m_port.sendKitParam)
						m_port.sendKitParam(e.track, e.kind == LiveEdit::Kind::Level ? 24 : e.index, e.value);
				}
				else if(const auto sysex = liveEditSysex(e); !sysex.empty() && m_port.sendSysex)
					m_port.sendSysex(sysex);
			}
			std::string note;
			for(const auto& n : delivery.notLive)
				note += (note.empty() ? "" : ". ") + n + " stays as it is on the machine";
			// The edit is pending until the machine's memory shows it (or it is too old to wait for);
			// without memory nothing reads it back: it is done as sent.
			if(m_profile.memory && !delivery.edits.empty())
				m_working.expect.sent(before, after, now());
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
			// The global's read-back is asked for with it (pushGlobal, requestGlobal): not paced.
			auto& push = m_pushes[ref];
			push.slot.abandon();
			push.slot.want(_change.after, now(), pushPolicy(ref.kind));
			push.slot.askedBack(now());
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
			{"sampleName", &MdMachine::cmdSampleName}, {"sampleCancel", &MdMachine::cmdSampleCancel}, {"play", &MdMachine::cmdPlay}, {"stop", &MdMachine::cmdStop},
			{"mute", &MdMachine::cmdMute}, {"followHost", &MdMachine::cmdFollowHost}};
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

	// What a machine command would lose asks first (the core sends it again with force): one
	// question function per command that has one.
	const std::map<std::string, MdMachine::Handler>& MdMachine::askers()
	{
		static const std::map<std::string, Handler> map{{"select", &MdMachine::askSelect}, {"kitLoad", &MdMachine::askKitLoad},
			{"reloadKit", &MdMachine::askReloadKit}, {"kitSaveAs", &MdMachine::askKitSaveAs}};
		return map;
	}

	std::vector<std::string> MdMachine::commandsAsking()
	{
		std::vector<std::string> ops;
		for(const auto& [op, h] : askers())
			ops.push_back(op);
		return ops;
	}

	Outcome MdMachine::askFor(const Value& _command, const Documents& _view)
	{
		const auto it = askers().find(deskCore::opOf(_command));
		return it == askers().end() ? ok() : (this->*(it->second))(_command, _view);
	}

	// Picking a pattern ends a chain, or drops the unsaved edits of the kit it does not link.
	Outcome MdMachine::askSelect(const Value& _command, const Documents& _view)
	{
		const auto slot = static_cast<uint8_t>(*intOf(_command, "p"));
		Outcome o;
		if(m_telemetry.chainKnown && m_telemetry.chain.active)
		{
			std::string list;
			for(const auto p : m_telemetry.chain.patterns)
				list += (list.empty() ? "" : " » ") + ed::mdPatternName(p);
			Value d = Value::object();
			d.set("p", slot);
			o = ask("breakChain", "Picking <b>" + ed::mdPatternName(slot) + "</b> ends the chain <b>" + list + "</b>, as on the machine.",
				"Pick it, end the chain", std::move(d));
		}
		// EXTENDED mode loads the kit a pattern links: another kit loses the edits.
		const auto& st = m_session.state();
		const auto link = st.patternKits.find(slot);
		if(kitState(_view) == deskCore::KitState::Edited && st.extendedMode == true && st.kit && link != st.patternKits.end()
			&& link->second != *st.kit)
		{
			const auto kit = *st.kit;
			const auto target = link->second;
			Value d = Value::object();
			d.set("p", slot);
			d.set("kit", static_cast<int>(kit));
			d.set("target", static_cast<int>(target));
			o = deskCore::withAsk(std::move(o), ask("discardKit", "<b>" + ed::mdPatternName(slot) + "</b> uses kit <b>" + kitLabel(_view, target)
				+ "</b>. Your edits to <b>" + kitLabel(_view, kit) + "</b> are not saved on the machine and will be lost.", "Switch and lose edits",
				std::move(d)));
		}
		return o;
	}

	// LOAD KIT drops the unsaved edits of the kit that plays.
	Outcome MdMachine::askKitLoad(const Value& _command, const Documents& _view)
	{
		if(kitState(_view) != deskCore::KitState::Edited)
			return ok();
		const auto kit = currentKit();
		return ask("loadKit", "Load <b>" + kitLabel(_view, static_cast<uint8_t>(*intOf(_command, "k"))) + "</b>? The unsaved edits of <b>"
			+ (kit ? kitLabel(_view, *kit) : std::string("the kit that plays")) + "</b> go to the machine's UNDO KIT.", "Load (edits to UNDO KIT)",
			kitDetails(kit));
	}

	Outcome MdMachine::askReloadKit(const Value&, const Documents& _view)
	{
		const auto kit = currentKit();
		if(!kit || kitState(_view) != deskCore::KitState::Edited)
			return ok();
		return ask("reloadKit", "Reload <b>" + kitLabel(_view, *kit) + "</b> from the machine? Your edits are lost.", "Reload (discard edits)",
			kitDetails(kit));
	}

	// SAVE KIT n over another slot that holds a kit.
	Outcome MdMachine::askKitSaveAs(const Value& _command, const Documents& _view)
	{
		const auto slot = static_cast<uint8_t>(*intOf(_command, "k"));
		const auto it = _view.kits.find(slot);
		if(slot == currentKit() || it == _view.kits.end() || emptyKit(it->second))
			return ok();
		return ask("overwriteSlot", "Overwrite <b>" + kitLabel(_view, slot) + "</b> with the kit that plays? The machine keeps the"
			" overwritten kit in its UNDO KIT, and " + kitLabel(_view, slot).substr(0, 3) + " becomes the current kit.", "Overwrite");
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

	// The kit that plays as the machine holds it: the live edits already sent (CCs and SysEx the
	// firmware has taken, memory just has not shown them yet), else memory's last image (with what
	// host automation moved since), else what the editor knows of it. Never knob turns still queued
	// (while recording they reach the machine one DATA ENTRY step at a time).
	const ed::MdKit* MdMachine::heldKit(const Documents& _view) const
	{
		const auto kit = currentKit();
		if(!kit)
			return nullptr;
		if(m_working.expect.expecting(now()) && !m_knobs.pending() && m_working.expect.to && m_working.expect.to->position == *kit)
			return &*m_working.expect.to;
		if(m_profile.memory && m_working.image && m_working.image->position == *kit)
			return &*m_working.image;
		return _view.workingKitOf(*kit);
	}

	Outcome MdMachine::cmdSaveKit(const Value&, const Documents& _view)
	{
		const auto kit = currentKit();
		if(!kit)
			return refuse("The current kit is not known yet");
		m_session.saveKit(*kit);
		// The stored slot now holds the working kit (as the machine holds it).
		if(const auto* w = heldKit(_view))
			observe(*w, Source::Tracked);
		return ok("Saved kit " + std::to_string(*kit + 1) + " on the machine");
	}

	Outcome MdMachine::cmdReloadKit(const Value&, const Documents& _view)
	{
		const auto kit = currentKit();
		if(!kit)
			return refuse("The current kit is not known yet");
		const auto state = kitState(_view);
		if(state == deskCore::KitState::Unknown)
			return refuse("The kit's saved slot is still being read from the machine. Try again in a moment.");
		if(state != deskCore::KitState::Edited)
			return refuse("The kit that plays matches its saved slot. Nothing to reload.");
		m_session.loadKit(*kit);
		// The working kit is the stored slot again: from memory, or from the slot's next dump.
		m_working = deskCore::switched(m_working);
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
		if(const auto* w = heldKit(_view))
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
			return refuse(panelKeys() ? g_noLiveRecordTelemetry : g_noLiveRecordKeys);
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
		// Whatever plays switches to the chain. A chain is pattern mode's: in SONG mode the firmware
		// plays it but stays in SONG mode (measured), so the song would be back after CLEAR, which says
		// the pattern plays on. Pattern mode first (SET STATUS, harmless when already there).
		if(m_port.sendSysex)
			m_port.sendSysex(ed::mdSetStatus(ed::MdStatus::SequencerMode, 0));
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

	// UW ROM slot names: the firmware takes 0x73 but never reports names over MIDI (the emulated machine's
	// are read from its memory with its samples, P9).
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
		return ok(std::string("Sent the name to ") + label + ". The Machinedrum shows it in SAMPLE MGR; a real one cannot report "
			"names back over MIDI.");
	}

	Outcome MdMachine::cmdPlay(const Value&, const Documents& _view)
	{
		if(!pressKey("play"))
			return refuse("Transport keys need the local emulated machine");
		// P7: a machine that follows the host's clock (in a DAW) plays with the host's transport
		const bool follows = _view.global && (_view.global->syncFlags & ed::mdGlobalBits::g_tempoInExternal);
		return ok(follows ? "The machine follows the host: it plays when the host's transport runs." : "");
	}

	Outcome MdMachine::cmdStop(const Value&, const Documents&)
	{
		return pressKey("stop") ? ok() : refuse("Transport keys need the local emulated machine");
	}

	// P7, in a DAW: the machine follows the host's MIDI clock and Start/Stop (MdModel::hostFollowing). The
	// global goes out as a dump (made active with 0x56, then read back): the machine's own setting, not an
	// edit, so no undo step.
	Outcome MdMachine::cmdFollowHost(const Value&, const Documents& _view)
	{
		const auto slot = m_session.state().globalSlot;
		if(!slot || !_view.global || _view.global->position != *slot)
			return ok();
		const auto g = MdModel::hostFollowing(*_view.global);
		if(!g)
			return ok();
		if(auto problems = m_session.pushGlobal(*g); !problems.empty())
			return {problems, {}, {}};
		if(m_port.sendSysex)
			m_port.sendSysex(ed::mdSetActiveGlobal(*slot));
		return ok("The machine follows the host's tempo and transport (GLOBAL " + std::to_string(*slot + 1) + ": TEMPO IN external)");
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

	void MdMachine::pumpPushes(const double _now, const Documents& _view)
	{
		for(auto& [ref, push] : m_pushes)
		{
			if(!push.slot.busy())
				continue;
			switch(push.slot.due(_now, pushPolicy(ref.kind)))
			{
			case PushSlot<Document>::Due::Send:
				sendDump(push.slot.takeNext(_now), _view);
				break;
			case PushSlot<Document>::Due::ReadBack:
				// The gesture is quiet: one read-back confirms the last dump.
				push.slot.askedBack(_now);
				request(ref);
				break;
			case PushSlot<Document>::Due::Nothing:
				break;
			}
			const double timeout = g_pushTimeoutMs + (m_profile.wire ? 2.5 * deskCore::DinPacer::wireMs(replyBytes(ref.kind)) : 0);
			if(!push.slot.timedOut(_now, timeout))
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
		if(m_sds.onReply(_message, now(), [this](const Bytes& _b) { sendRaw(_b); }))
		{
			pumpSample(now());
			return;
		}
		const auto status = ed::parseMdStatusResponse(_message);
		if(status && (m_profile.wire || m_probe == Probe::Running))
			m_wire.statusReply(now());
		m_session.onSysex(_message);
		// Control All: the machine said which track is selected (the one its gesture leads from).
		if(status && status->param == ed::MdStatus::Track)
			m_tweak.trackKnownMs = now();
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
		const auto askedMs = push.slot.asked() ? push.slot.askedMs() : push.slot.sentMs();
		switch(deskCore::readBack(push.slot, _doc))
		{
		case deskCore::ReadBackAction::Observe:
			observe(_doc, Source::Dump);
			break;
		case deskCore::ReadBackAction::Settle:
			m_lastRoundTripMs = now() - askedMs;
			settle(_doc, Source::Dump);
			break;
		case deskCore::ReadBackAction::Wait:
			break;
		}
	}

	void MdMachine::onPattern(const ed::MdPattern& _p)
	{
		const auto it = m_pushes.find({DocKind::Pattern, _p.position});
		const bool ownPush = it != m_pushes.end() && it->second.slot.busy();
		onDumpReadBack(_p);
		// The current pattern names the kit the Sound and Mix workspaces edit. The read-back of the
		// editor's own push re-reads it only when the pattern links another kit than the one that plays.
		if(m_session.state().pattern == _p.position)
			if(const auto kit = currentKit(); kit && (!ownPush || _p.kit != *kit))
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
		// Until memory shows it (or on a device without memory), the kit that plays starts as its slot.
		auto [next, seed] = deskCore::fromDump(std::move(m_working), _k);
		m_working = std::move(next);
		if(seed)
			observe(WorkingKit{*seed}, Source::Dump);
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
			m_lastRoundTripMs = now() - it->second.slot.askedMs();
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
			m_working = deskCore::switched(m_working);
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
		// Control All on the machine: FUNCTION + a knob steps every track, and the machine reports each step
		// (its CCs through the plug-in's parameters). Folded in, the steps would replace the gesture's values
		// (0.2.1: the Sound page's drags ended part way). Memory settles the gesture instead.
		if(_index < 24 && (m_tweak.guard & (1u << _index)))
			return;
		const auto* w = heldKit(_view);
		if(!w)
			return;
		auto k = *w;
		auto& slot = _index == 24 ? k.levels[_track] : k.params[_track][_index];
		if(slot == _value)
			return;
		slot = _value;
		// Something else moved the kit that plays: the machine holds it (memory will show it too),
		// so the next change builds on this one. The one value goes into each copy that describes
		// the kit (the edits expected, the last image); neither is replaced by the other.
		const auto set = [&](ed::MdKit& _k)
		{
			if(_k.position == k.position)
				(_index == 24 ? _k.levels[_track] : _k.params[_track][_index]) = _value;
		};
		if(m_working.expect.to)
			set(*m_working.expect.to);
		if(m_working.image)
			set(*m_working.image);
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
		m_working.region = _region;
		takeWorkingKit(&_view);
	}

	// The memory image, decoded (the dump format bytes from the stored slot), through the one
	// working-copy policy (deskCore::fromImage).
	void MdMachine::takeWorkingKit(const Documents* _view)
	{
		if(!m_working.region || m_probe != Probe::Running || !m_profile.memory)
			return;
		auto image = ed::mdWorkingKitFromMemory(*m_working.region);
		if(!image)
		{
			m_working.region.reset();
			return;
		}
		const auto kit = currentKit();
		const ed::MdKit* stored = nullptr;
		if(kit && _view)
			if(const auto it = _view->kits.find(*kit); it != _view->kits.end())
				stored = &it->second;
		if(stored)
		{
			image->version = stored->version;
			image->revision = stored->revision;
		}
		const auto* shown = _view && kit ? _view->workingKitOf(*kit) : nullptr;
		const std::optional<int> current = kit ? std::optional<int>(*kit) : std::nullopt;
		// Knob turns still on their way while recording keep the live edit pending over the image.
		// Control All on its way holds them too: the machine's gesture may wait (a dump request in flight, the
		// track selection), and an image of the kit before it must not win over the page's values meanwhile.
		auto r = deskCore::fromImage(std::move(m_working), *image, image->position, current, shown, now(), m_knobs.pending() || m_tweak.active(),
			reflects);
		m_working = std::move(r.next);
		if(r.askStatus && now() - m_kitStatusAskedMs > 200 && m_port.sendSysex)
		{
			m_kitStatusAskedMs = now();
			m_port.sendSysex(ed::mdStatusRequest(ed::MdStatus::Kit));
		}
		if(r.take)
		{
			if(r.settles)
				settle(WorkingKit{*r.take}, Source::Memory);
			else
				observe(WorkingKit{*r.take}, Source::Memory);
		}
	}

	// The kit that plays against its stored slot, as the page sees them (the working kit's pending
	// live edits count: they are what would be lost). One rule for both machines (deskCore).
	deskCore::KitState MdMachine::kitState(const Documents& _view) const
	{
		const auto kit = currentKit();
		const auto stored = kit ? _view.kits.find(*kit) : _view.kits.end();
		return deskCore::kitStateOf(kit ? _view.workingKitOf(*kit) : nullptr, stored == _view.kits.end() ? nullptr : &stored->second,
			[](const ed::MdKit& _a, const ed::MdKit& _b) { return ed::mdSameKitSound(_a, _b); });
	}

	void MdMachine::setBaseChannel(const ed::MdGlobal& _g)
	{
		if(m_port.baseChannel)
			m_port.baseChannel(static_cast<uint8_t>(_g.baseChannel & 0x0f));
	}

	TelemetryEvents MdMachine::onTelemetry(const Telemetry& _t)
	{
		const auto e = diff(m_telemetry, _t);
		// A chain the firmware now holds (made here or on the panel) is what plays next: whatever was
		// picked or playing gives way to it (a queued LOAD PATTERN, SONG mode; Session::noteChained).
		const bool chained = _t.chainKnown && _t.chain.active
			&& (!m_telemetry.chainKnown || !m_telemetry.chain.active || m_telemetry.chain.patterns != _t.chain.patterns);
		m_telemetry = _t;
		if(chained)
		{
			m_audibleQueue.reset();
			m_session.noteChained();
		}
		m_telemetrySeen = true;
		// The keys' fact: the device reports them pending, then none left.
		m_keys.onPending(_t.panelPending);
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
		t.set("step", _t.step);
		t.set("pattern", _t.pattern);
		t.set("playing", _t.playing);
		t.set("recording", _t.recording);
		t.set("valid", _t.valid);
		publishTelemetry(std::move(t));
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
		const auto* memory = m_working.image && currentKit() == m_working.image->position ? &*m_working.image : nullptr;
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
				m_keys.pressed(_now);
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
		// A sample on its way owns the wire: no status polls or loads meanwhile (their replies would come
		// late and their timeouts would run out).
		pumpSample(_now);
		if(m_sds.active())
			return;
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
		pumpTweak(_now);
		pumpCoalesced(_now);
		pumpPushes(_now, _view);
	}

	std::optional<uint8_t> MdMachine::tweakLead(const ed::MdKit& _kit, const std::optional<uint8_t> _preferred)
	{
		if(_preferred && controlAllLeads(_kit.models[*_preferred]))
			return _preferred;
		for(uint8_t t = 0; t < ed::MdKit::g_tracks; ++t)
			if(controlAllLeads(_kit.models[t]))
				return t;
		return std::nullopt;
	}

	bool MdMachine::tweakByPanel() const
	{
		return panelKeys() && m_port.turnKnob && m_telemetry.valid && m_telemetry.knobPage >= 0 && !m_telemetry.recording
			&& !m_telemetry.gridEdit;
	}

	// Control All on the machine (manual p.37): FUNCTION held, the knob turned by the net steps of each
	// tick, FUNCTION let go at quiet. The knob page is the group's first (the page key, with FUNCTION
	// up); keys wait while the firmware builds a dump (they would be lost).
	void MdMachine::pumpTweak(const double _now)
	{
		auto& w = m_tweak;
		const auto giveUp = [&]
		{
			if(w.held)
			{
				m_port.pressKey("release:function");
				m_keys.pressed(_now);
				w.held = false;
			}
			w.turns.clear();
			w.pageKeys = w.selects = 0;
			w.checkMs = _now;
		};
		checkTweak(_now);
		if(!w.turns.empty())
		{
			auto& turn = w.turns.front();
			if(turn.steps == 0)
			{
				w.turns.pop_front();
				return;
			}
			if(m_telemetry.knobPage != turn.page)
			{
				if(w.held)
				{
					m_port.pressKey("release:function");
					w.held = false;
					m_keys.pressed(_now);
				}
				if(!m_loads.loading() && _now - w.pageKeyMs >= g_tweakPageGapMs)
				{
					// Three presses go round the pages: a machine that does not follow is not on its pages.
					if(++w.pageKeys > g_tweakMaxPageKeys || !m_port.pressKey("page"))
					{
						giveUp();
						return;
					}
					w.pageKeyMs = _now;
					m_keys.pressed(_now);
				}
				return;
			}
			w.pageKeys = 0;
			if(!w.held)
			{
				if(m_loads.loading() || _now - w.pageKeyMs < g_tweakPageGapMs)
					return;
				// The firmware tweaks from its selected track: ask which it is (once a gesture: the panel
				// may have changed it), make it one that leads, and wait for the machine's status to say so.
				if(w.trackKnownMs < w.startMs)
				{
					if(_now - w.selectMs >= g_tweakSelectMs && m_port.sendSysex)
					{
						m_port.sendSysex(ed::mdStatusRequest(ed::MdStatus::Track));
						w.selectMs = _now;
					}
					return;
				}
				if(m_session.state().track != turn.lead)
				{
					if(_now - w.selectMs >= g_tweakSelectMs && m_port.sendSysex)
					{
						if(++w.selects > g_tweakMaxSelects)
						{
							giveUp();
							return;
						}
						m_port.sendSysex(ed::mdSetStatus(ed::MdStatus::Track, turn.lead));
						m_port.sendSysex(ed::mdStatusRequest(ed::MdStatus::Track));
						w.selectMs = _now;
					}
					return;
				}
				if(!m_port.pressKey("hold:function"))
				{
					w = {};
					return;
				}
				w.held = true;
			}
			// One packet a step, one a block on the device: turn when what went before is nearly through,
			// at most g_tweakMaxSteps at once; steps that come meanwhile add up (the latest total wins).
			if(m_telemetry.panelPending > g_tweakMaxPending)
				return;
			const int n = std::clamp(turn.steps, -g_tweakMaxSteps, g_tweakMaxSteps);
			m_port.turnKnob(turn.knob, n);
			m_keys.pressed(_now);
			w.lastMs = _now;
			// The edit is on its way from now: memory has until a second from here to show it.
			if(m_working.expect.from && m_working.expect.to)
				m_working.expect.sent(*m_working.expect.from, *m_working.expect.to, _now);
			turn.steps -= n;
			if(turn.steps == 0)
				w.turns.pop_front();
			return;
		}
		if(w.held && _now - w.lastMs >= g_tweakQuietMs)
		{
			m_port.pressKey("release:function");
			m_keys.pressed(_now);
			w.held = false;
			w.selects = 0;
			w.checkMs = _now + g_tweakCheckMs;
		}
	}

	// The gesture is over: every value it wanted that memory does not show (a knob page the machine did
	// not reach, a FUNCTION it lost, a track it did not select) goes as a CC, and the edit stays pending
	// until memory shows it.
	void MdMachine::checkTweak(const double _now)
	{
		auto& w = m_tweak;
		// The guard lasts until memory showed the gesture (or the edit is too old to wait for).
		if(!w.active() && w.checkMs < 0 && !m_working.expect.expecting(_now))
			w.guard = 0;
		if(w.checkMs < 0 || _now < w.checkMs || w.active())
			return;
		const auto indexes = std::exchange(w.indexes, 0u);
		w.checkMs = -1;
		auto& expect = m_working.expect;
		if(!expect.to || !expect.from)
			return;	// settled: memory showed it
		std::optional<ed::MdKit> memory;
		if(m_working.region)
			memory = ed::mdWorkingKitFromMemory(*m_working.region);
		if(!memory && m_working.image)
			memory = m_working.image;
		if(!memory || memory->position != expect.to->position)
			return;
		int sent = 0;
		for(uint8_t i = 0; i < 24; ++i)
		{
			if(!(indexes & (1u << i)))
				continue;
			for(uint8_t t = 0; t < ed::MdKit::g_tracks; ++t)
				if(memory->params[t][i] != expect.to->params[t][i])
				{
					m_coalesced[{t, i}] = expect.to->params[t][i];
					++sent;
				}
		}
		if(sent)
			expect.sent(*expect.from, *expect.to, _now);
	}

	void MdMachine::pumpCoalesced(const double _now)
	{
		if(m_coalesced.empty() || _now - m_coalescedMs < g_coalesceMs)
			return;
		m_coalescedMs = _now;
		if(m_port.sendKitParam)
			for(const auto& [key, v] : m_coalesced)
				m_port.sendKitParam(key.first, key.second, v);
		m_coalesced.clear();
	}

	// ---- the machine document ----

	Value MdMachine::status() const
	{
		const auto& st = m_session.state();
		Value v = Value::object();
		v.set("pattern", st.pattern ? Value(static_cast<int>(*st.pattern)) : Value());
		v.set("kit", st.kit ? Value(static_cast<int>(*st.kit)) : Value());
		v.set("known", static_cast<int>(knownCount()));
		v.set("loading", static_cast<int>(m_loads.pending()));
		v.set("roundTripMs", m_lastRoundTripMs);
		return v;
	}

	Value MdMachine::state(const Documents& _view) const
	{
		auto doc = mdDataLink::Session::stateToJson(m_session.state());
		if(auto* kit = doc.find("kit"))
			kit->put("working", deskCore::kitStateName(kitState(_view)));
		Value desk = Value::object();
		desk.set("tx", busy());
		desk.set("loading", static_cast<int>(m_loads.pending()));
		desk.set("roundTripMs", m_lastRoundTripMs);
		desk.set("queued", m_audibleQueue ? Value(static_cast<int>(*m_audibleQueue)) : Value());
		desk.set("telemetry", m_telemetry.valid);
		desk.set("gridEdit", m_telemetry.gridEdit);
		desk.set("knobPage", m_telemetry.knobPage);
		Value mutes = Value::array();
		for(size_t t = 0; t < m_mutes.size(); ++t)
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
