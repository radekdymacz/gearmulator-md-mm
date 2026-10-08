#include "mdDeskMachine.h"

#include "mdDeskChain.h"
#include "mdDeskKeys.h"
#include "mdDeskLibrary.h"

#include "deskCore/deskKinds.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdGlobal.h"
#include "elektronData/mdJson.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mdValidate.h"

#include <algorithm>
#include <cstdlib>
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

		// Control All: a "tweak" command's intent (MdModel::Intent), nothing for any other command.
		MdModel::Intent tweakIntentOf(const Value& _command)
		{
			if(deskCore::opOf(_command) != "tweak")
				return std::nullopt;
			const auto* g = _command.find("group");
			const std::string group = g && g->isString() ? g->asString() : "syn";
			const auto t = intOf(_command, "t");
			return TweakIntent{group == "fx" ? 1 : group == "rt" ? 2 : 0, static_cast<uint8_t>(intOf(_command, "knob").value_or(0)),
				intOf(_command, "d").value_or(0), t && *t >= 0 && *t < 16 ? std::optional<uint8_t>(static_cast<uint8_t>(*t)) : std::nullopt};
		}

		// A kit value's place (the held layer's accessor, deskCore::HeldOverrides).
		struct KitValue
		{
			template<typename Kit>
			auto& operator()(Kit& _k, const uint8_t _t, const uint8_t _i) const { return _k.params[_t][_i]; }
		};
		constexpr KitValue kitValue{};

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
			const auto n = kitNameText(_k);
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

	// B-014: the editor sends to the emulated machine no faster than a MIDI cable would (3125 B/s: a pattern
	// dump in 1.73 s, so a lock drag pushes at most one dump every 1.73 s instead of five a second), and asks
	// nothing while the firmware applies a dump (its read is paced at 125 KB/s by md::Hardware, B-010; 250 ms
	// to apply it). Each dump keeps the emulated 68k busy, which costs the audio thread about 1.5 times its
	// idle work for 50-70 ms: on a slower Mac five of them a second made late audio buffers.
	// GEARMULATOR_MDMM_EDIT_RATE=<bytes a second> raises the rate (0: no pacing, as 0.3.3).
	StreamPolicy emulatorStream()
	{
		StreamPolicy s;
		s.bytesPerSecond = deskCore::DinPacer::g_bytesPerSecond;
		s.ingestBytesPerSecond = 125000;
		s.settleMs = 250;
		// values at cable speed too, 64 CCs at once (a track's sound), and a value SysEx (tempo...) 10 times a second
		s.valueBytesPerSecond = deskCore::DinPacer::g_bytesPerSecond;
		s.valueBurstBytes = 192;
		s.latestIntervalMs = 100;
		if(const char* rate = std::getenv("GEARMULATOR_MDMM_EDIT_RATE"); rate && *rate)
		{
			s.bytesPerSecond = s.valueBytesPerSecond = std::max(0.0, std::atof(rate));
			if(s.bytesPerSecond <= 0)
				s.latestIntervalMs = 0;
		}
		return s;
	}

	const Profile& emulatorProfile()
	{
		static const Profile p = []
		{
			Profile e{"emu", "EMU OS 1.63", "Engine: the real Machinedrum OS 1.63 runs inside the app. Choose HW MIDI to "
				"edit a real Machinedrum instead.", false, false, true, true};
			e.stream = emulatorStream();
			return e;
		}();
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
		, m_out(m_port.sendSysex, m_profile.stream)
		, m_session([this](const Bytes& _b) { sendSysex(_b); })
	{
		// B-014: CCs and notes go through the stream too. Kit values and mutes are values (the value budget, the
		// newest value of a parameter wins, behind any SysEx before them: a machine change, then its values);
		// notes and a key's held value have priority (they pass waiting values, never SysEx).
		if(m_port.sendKitParam)
			m_port.sendKitParam = [this, send = std::move(m_port.sendKitParam)](const uint8_t _t, const uint8_t _i, const uint8_t _v)
			{
				m_out.value(_t * 32 + _i, 3, [send, _t, _i, _v] { send(_t, _i, _v); }, now());
			};
		if(m_port.sendMute)
			m_port.sendMute = [this, send = std::move(m_port.sendMute)](const uint8_t _t, const bool _on)
			{
				m_out.value(1000 + _t, 3, [send, _t, _on] { send(_t, _on); }, now());
			};
		if(m_port.sendHeldParam)
			m_port.sendHeldParam = [this, send = std::move(m_port.sendHeldParam)](const uint8_t _t, const uint8_t _i, const uint8_t _v)
			{
				m_out.priority([send, _t, _i, _v] { send(_t, _i, _v); }, now());
			};
		if(m_port.sendNote)
			m_port.sendNote = [this, send = std::move(m_port.sendNote)](const uint8_t _c, const uint8_t _n, const uint8_t _v)
			{
				m_out.priority([send, _c, _n, _v] { send(_c, _n, _v); }, now());
			};
		wireSession();
		m_wire = deskCore::WireFacts(m_port.nowMs ? m_port.nowMs() : 0);
	}

	// P9: while a sample goes out (SDS) nothing else may come between its packets; other SysEx waits.
	void MdMachine::sendSysex(const Bytes& _message)
	{
		m_out.send(_message, m_sds.active(), now());
	}

	// B-019: an imported file's message as it is, in the stream like the editor's own (a dump at the machine's read
	// speed while it stands, at cable speed while it plays; a request after the dumps before it are applied).
	std::string MdMachine::sendAsIs(const Bytes& _message, const bool)
	{
		if(!canSendSysex())
			return "This engine cannot send SysEx to the machine.";
		sendSysex(_message);
		return {};
	}

	MdMachine::AsIs MdMachine::asIs() const
	{
		AsIs a;
		a.queued = m_out.waiting() + m_out.held();
		a.busy = m_out.sending(now()) || m_out.held() > 0;
		return a;
	}

	// B-014: a live edit's SysEx: one that sets a value goes in the stream's latest lane (the newest per value, at
	// most 10 a second); a machine change is no value (its CCs follow it) and keeps its order.
	void MdMachine::sendLiveSysex(const LiveEdit& _e, const Bytes& _message)
	{
		if(_e.kind == LiveEdit::Kind::Machine)
		{
			sendSysex(_message);
			return;
		}
		m_out.sendLatest((static_cast<int>(_e.kind) << 16) | (_e.track << 8) | _e.index, _message, m_sds.active(), now());
	}

	// ---- P9: a sample to a ROM slot (SDS) ----

	std::string MdMachine::sendSample(const uint8_t _slot, const ed::MdSampleUpload& _upload)
	{
		if(!m_out.open())
			return "This engine cannot send SysEx to the machine.";
		if(m_sds.active())
			return "A sample is already on its way. Wait for it, or stop it.";
		if(m_telemetry.recording)
			return "The machine is recording. Stop it first.";
		auto dump = ed::mdSdsDump(_slot, _upload.samples, _upload.rate, _upload.name);
		if(!dump)
			return "The sample could not be made into SDS (ROM slot 1-48, 1 to 2 million samples).";
		m_sds.start(_slot, std::move(*dump), now(), [this](const Bytes& _b) { m_out.sample(_b); });
		return {};
	}

	void MdMachine::cancelSample()
	{
		m_sds.cancel([this](const Bytes& _b) { m_out.sample(_b); });
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
		m_sds.pump(_now, [this](const Bytes& _b) { m_out.sample(_b); });
		m_out.release(m_sds.active(), _now);
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
		m_out.clear();
		m_reloadsPending = 0;
		m_working = deskCore::switched<ed::MdKit>();
		m_keys = {};
		m_chain.drop();
		m_lastKit.reset();
		m_lastPattern.reset();
		m_audibleQueue.reset();
		m_knobs.reset();
		m_recLock.reset();
		m_sequence.clear();
		m_tweak = {};
		m_notes = {};
		m_coalesced.clear();
		m_telemetrySeen = false;
		m_session = mdDataLink::Session([this](const Bytes& _b) { sendSysex(_b); });
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
		c.set("sampleLoad", m_out.open(), "This engine cannot send SysEx to the machine.");
		c.set("modulators", telemetry, "The app modulators move with the machine's playhead; this engine reports none.");
		c.values.emplace_back("dumps", "direct");
		return c;
	}

	bool MdMachine::busy() const
	{
		if(m_sds.active())
			return true;
		if(m_pushes.anyBusy())
			return true;
		// B-014: SysEx waiting its turn in the stream, or a dump the machine still reads or applies (the TX LED)
		if(m_out.sending(now()))
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

	MdMachine::Review MdMachine::review(const Value& _command, const std::vector<Change>& _changes, const Documents& _view)
	{
		// Control All: the intent submit() delivers its change with (the change is the whole working kit).
		const auto intent = tweakIntentOf(_command);
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
		return {o, intent};
	}

	Value MdMachine::kitDetails(const std::optional<uint8_t> _kit)
	{
		Value d = Value::object();
		d.set("kit", _kit ? Value(static_cast<int>(*_kit)) : Value());
		return d;
	}

	deskCore::PushPolicy MdMachine::pushPolicy(const DocKind _kind) const
	{
		auto policy = deskCore::wirePolicy(m_profile.push, m_profile.wire, replyBytes(_kind));
		// B-014: no faster than the stream carries the dump (latest wins meanwhile)
		policy.minIntervalMs = std::max(policy.minIntervalMs, m_out.policy().wireMs(replyBytes(_kind)));
		return policy;
	}

	// Paced (DESIGN-edit-flow.md): the dump goes now or waits its turn (latest wins); the read-back is
	// asked for once the gesture is quiet (pumpPushes).
	Outcome MdMachine::pushDump(const Document& _doc, const Documents& _view)
	{
		const auto ref = refOf(_doc);
		auto problems = ref.kind == DocKind::Pattern ? ed::validate(std::get<ed::MdPattern>(_doc)) : ed::validate(std::get<ed::MdSong>(_doc));
		if(!problems.empty())
			return {problems, {}, {}};
		if(m_pushes.want(ref, _doc, now(), pushPolicy(ref.kind)))
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
		// after the dump (it may wait its turn in the stream, B-014), once the machine has read and applied it
		// (B-025: the stream's after-work waits for that). Until then memory shows the kit before the reload or
		// the stored slot, neither the working kit: no image is taken meanwhile.
		if(working)
		{
			++m_reloadsPending;
			m_reloadQueuedMs = now();
			m_out.after([this, storedKit = stored->second, w = *working]
			{
				m_reloadsPending = std::max(0, m_reloadsPending - 1);
				restoreWorkingKit(storedKit, w);
			}, now());
		}
	}

	// The machine just loaded the kit that plays from its slot (_stored): what it held before (_working)
	// goes again as live edits, pending until memory shows it (the image of the slot is not taken meanwhile).
	void MdMachine::restoreWorkingKit(const ed::MdKit& _stored, const ed::MdKit& _working)
	{
		const auto delivery = kitDelivery(_stored, _working);
		if(delivery.edits.empty())
			return;
		// The values go again after the dump (sendLive: the plug-in's parameters already hold them).
		for(const auto& e : delivery.edits)
		{
			if(e.kind == LiveEdit::Kind::Param || e.kind == LiveEdit::Kind::Level)
			{
				if(e.kind == LiveEdit::Kind::Param)
					m_coalesced.erase({e.track, e.index});
				sendLive(e.track, e.kind == LiveEdit::Kind::Level ? 24 : e.index, e.value);
			}
			else if(const auto sysex = liveEditSysex(e); !sysex.empty() && canSendSysex())
				sendLiveSysex(e, sysex);
		}
		if(!m_profile.memory)
			return;
		// From the slot the machine now holds, not from before the edits still on their way.
		m_working.expect.clear();
		m_working.expect.sent(_stored, _working, now());
	}

	Outcome MdMachine::submit(const Change& _change, const Intent& _intent, const Documents& _view)
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
			const auto& intent = _intent;
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
					sendLive(e.track, e.kind == LiveEdit::Kind::Level ? 24 : e.index, e.value);
				}
				else if(const auto sysex = liveEditSysex(e); !sysex.empty() && canSendSysex())
					sendLiveSysex(e, sysex);
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
				if(const auto sysex = liveEditSysex(e); !sysex.empty() && canSendSysex())
					sendLiveSysex(e, sysex);
			if(!delivery.notLive.empty())
			{
				// A global dump is stored at once but applied only when its slot is made active
				// (P5, measured): 0x56 right after it. pushGlobal asks for the slot back.
				if(auto problems = m_session.pushGlobal(after); !problems.empty())
					return {problems, {}, {}};
				if(canSendSysex() && (!m_session.state().globalSlot || *m_session.state().globalSlot == ref.slot))
					sendSysex(ed::mdSetActiveGlobal(ref.slot));
			}
			// A dump's read-back is asked for with it (pushGlobal). Live edits only (the tempo, routing): read back once
			// the gesture is quiet (pumpPushes), as a pattern's: B-014, a tempo drag asked for the global at every value,
			// and those requests held its tempo values in the stream (one a 100 ms, the newest wins).
			auto& push = m_pushes[ref];
			push.slot.abandon();
			push.slot.want(_change.after, now(), pushPolicy(ref.kind));
			if(!delivery.notLive.empty())
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
			{"record", &MdMachine::cmdRecord}, {"recTrig", &MdMachine::cmdRecTrig}, {"noteOn", &MdMachine::cmdNoteOn}, {"noteOff", &MdMachine::cmdNoteOff}, {"chain", &MdMachine::cmdChain},
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
		auto o = ask("loadKit", "<b>" + (kit ? kitLabel(_view, *kit) : std::string("The kit that plays")) + "</b> has unsaved changes. Load <b>"
			+ kitLabel(_view, static_cast<uint8_t>(*intOf(_command, "k"))) + "</b>? Without saving, the machine keeps the edits in its UNDO KIT.",
			"Load without saving", kitDetails(kit));
		// Save and load: SAVE KIT to the current slot, then LOAD KIT (the wire keeps their order).
		if(kit)
		{
			Value save = Value::object();
			save.set("op", "saveKit");
			o.ask->alternatives.push_back({"Save and load", {std::move(save)}});
		}
		return o;
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
		// A pick ends a chain: one still waiting for its keys is not made.
		m_chain.drop();
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
		// A held key's value must not be saved with the kit: the machine holds the document's again.
		restore(m_working.held.releaseAll(), _view);
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
		restore(m_working.held.releaseAll(), _view);
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

	// The page's keyboard (P10), the note intent (deskCore/deskNotes.h, mdDeskKeys.h): noteOn plays track t
	// as its MAP EDITOR note on the base channel (the manual's default map while no global is known), so
	// the firmware trigs it at the velocity. On the sample machines (ROM, RAM-P) pitch is a PTCH the
	// machine holds while the key is down: the working copy's held layer (m_working.held), never an edit
	// (no undo step, no expectation; memory images are masked with it). Letting go puts back the
	// document's value. Other machines play at their own pitch; GND-EMPTY and the recorders are refused.
	// While live recording a key is the track's TRIG key (as recTrig): the firmware records a plain trig.
	// One note sounds per track: a later key replaces it, and only its own noteOff lets it go.
	Outcome MdMachine::cmdNoteOn(const Value& _m, const Documents& _view)
	{
		if(!m_port.sendNote)
			return refuse("This engine cannot play notes");
		const auto on = deskCore::noteOnOf(_m);
		const auto t = on.track;
		const auto kit = currentKit();
		const auto* document = kit ? _view.workingKitOf(*kit) : nullptr;
		if(!document)
			document = heldKit(_view);
		const auto model = document ? document->models[t] : 0u;
		const auto machine = ed::mdMachineName(model);
		const auto kind = document ? keys::kindOf(model) : keys::Kind::Trig;
		if(kind == keys::Kind::None)
			return refuse(keys::notPlayed(machine));
		if(m_telemetry.recording)
		{
			if(!pressKey("trig" + std::to_string(t + 1)))
				return refuse("TRIG keys need the local emulated machine");
			return ok("Recording: a key records a plain trig on the track, at the kit's pitch.");
		}
		const auto note = keys::trackNote(_view.global ? &*_view.global : nullptr, t);
		if(!note)
			return refuse("Track " + std::to_string(t + 1) + " has no MIDI note in the MAP EDITOR (GLOBAL settings)");
		releaseNote(t, _view);
		const auto channel = static_cast<uint8_t>(_view.global ? _view.global->baseChannel & 0x0f : 0);
		if(kind == keys::Kind::Pitch && document)
		{
			const auto tuned = document->params[t][keys::g_ptchIndex];
			const auto held = keys::heldPtch(tuned, on.pitch);
			if(held != tuned)
			{
				sendHeld(t, keys::g_ptchIndex, held);
				m_working.held.hold({t, keys::g_ptchIndex, held, tuned});
			}
		}
		m_port.sendNote(channel, *note, on.velocity);
		m_notes[t] = SoundingNote{channel, *note, on.pitch};
		return ok(kind == keys::Kind::Trig && document ? keys::ownPitch(machine) : std::string());
	}

	Outcome MdMachine::cmdNoteOff(const Value& _m, const Documents& _view)
	{
		const auto off = deskCore::noteOffOf(_m);
		const auto& n = m_notes[off.track];
		if(n && off.releases(n->pitch))
			releaseNote(off.track, _view);
		return ok();
	}

	void MdMachine::releaseNote(const uint8_t _t, const Documents& _view)
	{
		if(auto& n = m_notes[_t])
		{
			if(m_port.sendNote)
				m_port.sendNote(n->channel, n->note, 0);
			n.reset();
		}
		restore(m_working.held.release(_t), _view);
	}

	// What held keys put on the machine goes back to the document's values (an edit made meanwhile is
	// in the document; host automation too), else to the values the keys replaced.
	void MdMachine::restore(std::vector<deskCore::HeldOverride> _held, const Documents& _view)
	{
		const auto kit = currentKit();
		const auto* document = kit ? _view.workingKitOf(*kit) : nullptr;
		for(const auto& o : _held)
			sendHeld(o.track, o.index, deskCore::HeldOverrides::restoreValue(o, document, kitValue));
	}

	// A kit value of the kit that plays, to the machine. Through the plug-in's parameter (a DAW sees the move and can
	// record it), and as the machine's CC past it: the parameter holds the kit as it was loaded or last set from
	// here, not what the machine changed by itself (a Control All gesture on its panel, the initial values of a new
	// machine, a kit the machine loaded from its slot), and a parameter set to the value it holds sends nothing. So
	// the undo of a Control All, a value set right after a machine change, and the unsaved edits sent again after
	// a dump were lost on the machine (heard, mdDeskFirmwareTest keepedits). Unset sendHeldParam: the parameter
	// alone (the wire engines send their CCs as such).
	void MdMachine::sendLive(const uint8_t _t, const uint8_t _index, const uint8_t _value)
	{
		if(m_port.sendKitParam)
			m_port.sendKitParam(_t, _index, _value);
		if(m_port.sendHeldParam)
			m_port.sendHeldParam(_t, _index, _value);
	}

	void MdMachine::sendHeld(const uint8_t _t, const uint8_t _index, const uint8_t _value)
	{
		if(m_port.sendHeldParam)
			m_port.sendHeldParam(_t, _index, _value);
		else if(m_port.sendKitParam)
			m_port.sendKitParam(_t, _index, _value);
	}

	ed::MdKit MdMachine::unheld(ed::MdKit _image, const Documents* _view) const
	{
		if(!m_working.held.any())
			return _image;
		const auto* document = _view ? _view->workingKitOf(_image.position) : nullptr;
		return m_working.held.masked(std::move(_image), document, kitValue);
	}

	// Chaining as on the machine: hold BANK, press the TRIG keys (mdDeskChain.h). The chain is
	// the firmware's; the page sees it through the telemetry. The page sends the chain again at
	// every pad it adds or takes away: while the keys of the one before are still on their way the
	// latest waits (one at a time, the latest wins, pumpChain), so two key runs never interleave
	// and BANK GROUP is pressed from the group the machine is in after the run before.
	Outcome MdMachine::cmdChain(const Value& _m, const Documents&)
	{
		auto patterns = deskCore::chainPatterns(_m);
		auto errors = validateChain(patterns);
		if(errors.empty() && !canChain())
			errors.emplace_back(g_noChains);
		if(!errors.empty())
			return {errors, {}, {}};
		if(!m_chain.offer(deskCore::ChainOf{patterns}, keysOnTheirWay()))
			return ok("Chain next: the machine is still taking the keys before it");
		return sendChain(patterns);
	}

	Outcome MdMachine::sendChain(const std::vector<int>& _patterns)
	{
		const auto keys = chainKeys(_patterns, m_telemetry.bankGroup);
		if(keys.empty())
			return refuse("The machine's BANK GROUP (A-D / E-H) is not known yet");
		// Whatever plays switches to the chain. A chain is pattern mode's: in SONG mode the firmware
		// plays it but stays in SONG mode (measured), so the song would be back after CLEAR, which says
		// the pattern plays on. Pattern mode first (SET STATUS, harmless when already there).
		if(canSendSysex())
			sendSysex(ed::mdSetStatus(ed::MdStatus::SequencerMode, 0));
		bool pressed = true;
		for(const auto& k : keys)
			pressed = pressed && pressKey(k);
		m_audibleQueue.reset();
		if(!pressed)
			return refuse("The panel did not take the keys");
		return ok(m_telemetry.playing ? "Chained: the machine plays them in this order from the pattern end, and loops"
			: "Chained: PLAY starts at " + ed::mdPatternName(static_cast<uint8_t>(_patterns.front())) + ", then loops");
	}

	bool MdMachine::clearChain()
	{
		const auto current = m_session.state().pattern;
		if(!current)
			return false;
		m_session.selectPattern(*current);
		return true;
	}

	// The chain (or CLEAR) the page asked for while keys were on their way.
	void MdMachine::pumpChain()
	{
		const auto next = m_chain.takeWhen(!keysOnTheirWay());
		if(!next)
			return;
		if(std::holds_alternative<deskCore::ChainClear>(*next))
			(void)clearChain();
		else if(canChain())
			(void)sendChain(std::get<deskCore::ChainOf>(*next).patterns);
	}

	// CLEAR is LOAD PATTERN of the current pattern, which is what ends a chain on the machine. After
	// the chain keys still on their way (a LOAD PATTERN before them would be undone by them).
	Outcome MdMachine::cmdChainClear(const Value&, const Documents&)
	{
		const auto current = m_session.state().pattern;
		if(!current)
			return refuse("The current pattern is not known yet");
		if(!m_chain.offer(deskCore::ChainClear{}, keysOnTheirWay()))
			return ok("Chain cleared once the machine has taken the keys before it: " + ed::mdPatternName(*current) + " plays on");
		(void)clearChain();
		return ok("Chain cleared: " + ed::mdPatternName(*current) + " plays on");
	}

	// The machine's active GLOBAL slot (0x56), then its settings are read.
	Outcome MdMachine::cmdGlobalSlot(const Value& _m, const Documents&)
	{
		const auto slot = static_cast<uint8_t>(*intOf(_m, "slot"));
		if(canSendSysex())
			sendSysex(ed::mdSetActiveGlobal(slot));
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
		if(canSendSysex())
			sendSysex(bytes);
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
		if(canSendSysex())
			sendSysex(ed::mdSetActiveGlobal(*slot));
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
		const double timeout = g_loadTimeoutMs + (m_profile.wire && inFlight ? deskCore::DinPacer::wireMs(replyBytes(inFlight->kind)) * 1.5 : 0)
			+ streamTimeoutMs();
		// Loads wait while an edit is on the wire (they would delay its read-back) and around
		// panel key presses.
		const bool mayStart = !busy() && !keysOnTheirWay();
		const auto step = m_loads.next(_now, timeout, mayStart, g_loadPolicy);
		if(step.gaveUp)
			gaveUpLoad(*step.gaveUp);
		if(step.send)
			request(*step.send);
	}

	// A read with no reply after every resend and round (a slow wire, a cable pulled): the queue gave
	// it up. A document the machine plays now is reported once (one given up in the background is
	// reported when it plays and is given up again); a status reply asks for it again once its backoff
	// is over (onSysex). Another one stays unknown until it is wanted again.
	void MdMachine::gaveUpLoad(const DocRef& _ref)
	{
		gaveUpReading(_ref, now());
		if(!current(_ref) || !tellUnread(_ref))
			return;
		fail(_ref, std::string("The machine did not answer the request for ") + kindName(_ref.kind) + " "
			+ (_ref.kind == DocKind::Pattern ? ed::mdPatternName(_ref.slot) : std::to_string(_ref.slot + 1))
			+ ": it cannot be edited until it is read. The editor asks again.");
	}

	bool MdMachine::current(const DocRef& _ref) const
	{
		const auto& st = m_session.state();
		const auto is = [&](const auto& _slot) { return _slot && static_cast<int>(*_slot) == static_cast<int>(_ref.slot); };
		switch(_ref.kind)
		{
		case DocKind::Pattern: return is(st.pattern);
		case DocKind::Kit: return is(st.kit);
		case DocKind::Song: return is(st.song);
		case DocKind::Global: return is(st.globalSlot);
		case DocKind::WorkingKit: return false;
		}
		return false;
	}

	void MdMachine::pumpPushes(const double _now, const Documents& _view)
	{
		const auto policyOf = [this](const DocRef& _ref) { return pushPolicy(_ref.kind); };
		const auto timeoutOf = [this](const DocRef& _ref)
		{
			return g_pushTimeoutMs + (m_profile.wire ? 2.5 * deskCore::DinPacer::wireMs(replyBytes(_ref.kind)) : 0)
				+ 1.5 * m_out.policy().wireMs(replyBytes(_ref.kind)) + streamTimeoutMs();
		};
		using K = Pushes::Effect::Kind;
		if(m_out.sending(_now))
			m_pushes.restartAsked(_now);
		for(const auto& e : m_pushes.pump(_now, policyOf, timeoutOf, deskCore::g_maxReadBacks))
		{
			switch(e.kind)
			{
			case K::Send:
				sendDump(*e.value, _view);
				break;
			case K::AskBack:
				// The gesture is quiet: one read-back confirms the last dump.
				request(e.ref);
				break;
			case K::TimedOut:
				fail(e.ref, std::string("Push failed: the machine did not read back ") + kindName(e.ref.kind)
					+ " " + (e.ref.kind == DocKind::Pattern ? ed::mdPatternName(e.ref.slot) : std::to_string(e.ref.slot + 1))
					+ ". Showing what it holds.");
				load(e.ref, true);
				break;
			}
		}
	}

	// ---- device -> machine ----

	void MdMachine::onSysex(const Bytes& _message)
	{
		m_wire.heard(now());
		if(m_sds.onReply(_message, now(), [this](const Bytes& _b) { m_out.sample(_b); }))
		{
			pumpSample(now());
			return;
		}
		const auto status = ed::parseMdStatusResponse(_message);
		if(status && (m_profile.wire || m_probe == Probe::Running))
			m_wire.statusReply(now());
		m_session.onSysex(_message);
		// The machine answers: a current document whose read was given up (no reply) is asked for again.
		if(status && (status->param == ed::MdStatus::Pattern || status->param == ed::MdStatus::Kit))
		{
			const auto& st = m_session.state();
			for(const auto& ref : {st.pattern ? std::optional<DocRef>(DocRef{DocKind::Pattern, *st.pattern}) : std::nullopt,
					st.kit ? std::optional<DocRef>(DocRef{DocKind::Kit, *st.kit}) : std::nullopt})
				if(ref && mayAskAgain(*ref, now()) && !m_loads.contains(*ref))
					load(*ref, true);
		}
		// Control All: the machine said which track is selected (the one its gesture leads from).
		if(status && status->param == ed::MdStatus::Track)
			m_tweak.trackKnownMs = now();
	}

	void MdMachine::onDumpReadBack(const Document& _doc)
	{
		const auto ref = refOf(_doc);
		m_loads.arrived(ref);
		auto* found = m_pushes.find(ref);
		if(!found)
		{
			observe(_doc, Source::Dump);
			return;
		}
		auto& push = *found;
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
		const bool ownPush = m_pushes.busy({DocKind::Pattern, _p.position});
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
		if(auto* push = m_pushes.find(ref); push && push->slot.busy())
		{
			m_lastRoundTripMs = now() - push->slot.askedMs();
			push->slot.abandon();
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
		// The value a held key holds, reported back (the plug-in's parameter when the engine has no
		// sendHeldParam): the machine's for a moment, not a change of the kit.
		if(m_working.held.echo(_track, _index, _value))
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
		if(reloadHolds())	// B-025: the region waits for the restore after the reload
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
		// A held key's value is the machine's for a moment, not the kit's.
		*image = unheld(std::move(*image), _view);
		const auto* shown = _view && kit ? _view->workingKitOf(*kit) : nullptr;
		const std::optional<int> current = kit ? std::optional<int>(*kit) : std::nullopt;
		// Knob turns still on their way while recording keep the live edit pending over the image.
		// Control All on its way holds them too: the machine's gesture may wait (a dump request in flight, the
		// track selection), and an image of the kit before it must not win over the page's values meanwhile.
		auto r = deskCore::fromImage(std::move(m_working), *image, image->position, current, shown, now(), m_knobs.pending() || m_tweak.active(),
			reflects);
		m_working = std::move(r.next);
		if(r.askStatus && now() - m_kitStatusAskedMs > 200 && canSendSysex())
		{
			m_kitStatusAskedMs = now();
			sendSysex(ed::mdStatusRequest(ed::MdStatus::Kit));
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
			[](const ed::MdKit& _a, const ed::MdKit& _b)
		{
			// Names as text. LOAD KIT of a slot whose name bytes are no text (never written) names the
			// working kit NEW KIT (measured, mdDeskFirmwareTest p4): that is the slot as it loads, not an edit.
			const auto nameOf = [](const ed::MdKit& _k) { const auto t = kitNameText(_k); return t.empty() && _k.name[0] ? std::string("NEW KIT") : t; };
			if(nameOf(_a) != nameOf(_b))
				return false;
			auto b = _b;
			b.name = _a.name;
			return ed::mdSameKitSound(_a, b);
		});
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
		if(e.patternChanged && m_wire.replied && m_session.state().pattern != _t.pattern && canSendSysex())
		{
			sendSysex(ed::mdStatusRequest(ed::MdStatus::Pattern));
			sendSysex(ed::mdStatusRequest(ed::MdStatus::Kit));
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
			if(canSendSysex())
				sendSysex(ed::mdSetStatus(ed::MdStatus::Track, step->track));
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

	// B-014: what a request may wait in the stream on top of its reply (a dump before it being applied).
	double MdMachine::streamTimeoutMs() const
	{
		const auto& st = m_out.policy();
		return st.bytesPerSecond > 0 ? st.ingestMs(ed::MdPattern::g_extendedDumpSize) + st.settleMs : 0.0;
	}

	void MdMachine::tick(const double _now, const Documents& _view)
	{
		// 0.3.4: cable speed only while the sequencer plays (a machine without telemetry counts as playing)
		m_out.setPlaying(!m_telemetry.valid || m_telemetry.playing, _now);
		m_out.pump(_now);
		// A sample on its way owns the wire: no status polls or loads meanwhile (their replies would come
		// late and their timeouts would run out).
		pumpSample(_now);
		if(m_sds.active())
			return;
		pumpSequence(_now);
		if(!m_profile.wire && m_probe != Probe::Running)
			return;
		releaseKeys();
		pumpChain();
		const auto statusEvery = m_wire.replied && m_audibleQueue ? g_statusQueuedMs : g_statusIdleMs;
		if(_now - m_lastStatusMs >= statusEvery && !keysOnTheirWay())
		{
			m_lastStatusMs = _now;
			if(m_wire.replied && m_audibleQueue && canSendSysex())
				sendSysex(ed::mdStatusRequest(ed::MdStatus::Pattern));
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
					if(_now - w.selectMs >= g_tweakSelectMs && canSendSysex())
					{
						sendSysex(ed::mdStatusRequest(ed::MdStatus::Track));
						w.selectMs = _now;
					}
					return;
				}
				if(m_session.state().track != turn.lead)
				{
					if(_now - w.selectMs >= g_tweakSelectMs && canSendSysex())
					{
						if(++w.selects > g_tweakMaxSelects)
						{
							giveUp();
							return;
						}
						sendSysex(ed::mdSetStatus(ed::MdStatus::Track, turn.lead));
						sendSysex(ed::mdStatusRequest(ed::MdStatus::Track));
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
		memory = m_working.held.masked(std::move(*memory), &*expect.to, kitValue);
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
