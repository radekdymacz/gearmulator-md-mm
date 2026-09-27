#include "mdDesk.h"

#include "elektronData/mdCommands.h"
#include "elektronData/mdJson.h"
#include "elektronData/mdMachines.h"
#include "elektronData/mdWorkingKit.h"

#include <algorithm>
#include <cstdio>

namespace mdDesk
{
	namespace ed = elektronData;
	using Value = ed::json::Value;

	namespace
	{
		constexpr double g_loadGapMs = 30;			// P1: about 30 ms per pattern in the background
		constexpr double g_loadTimeoutMs = 800;
		constexpr double g_pushTimeoutMs = 2000;
		constexpr double g_statusIdleMs = 1000;		// P1: poll gently, a burst costs ~1.5 ms of step jitter
		constexpr double g_statusQueuedMs = 250;
		constexpr double g_liveEditTxMs = 120;
		// The desk's own live edits reach the firmware's memory after the MIDI queue;
		// a memory image read before that would briefly undo them in the view.
		constexpr double g_workingKitHoldMs = 150;
		constexpr double g_keyQuietMs = 500;
		// HW MIDI: the machine counts as lost after this long without a reply (status is
		// asked for every second).
		constexpr double g_hwLostMs = 3500;

		// The dump a request brings back, for timeouts at DIN speed.
		size_t replyBytes(const DocKind _k)
		{
			switch(_k)
			{
			case DocKind::Pattern: return 5410;
			case DocKind::Kit: return 1233;
			case DocKind::Song: return 3100;
			case DocKind::Global: return 197;
			}
			return 0;
		}

		const char* kindName(const DocKind _k)
		{
			switch(_k)
			{
			case DocKind::Pattern: return "pattern";
			case DocKind::Kit: return "kit";
			case DocKind::Song: return "song";
			case DocKind::Global: return "global";
			}
			return "";
		}

		std::optional<DocKind> kindFromName(const std::string& _s)
		{
			for(const auto k : {DocKind::Pattern, DocKind::Kit, DocKind::Song, DocKind::Global})
				if(_s == kindName(k))
					return k;
			return {};
		}

		Value errorsToJson(const std::vector<std::string>& _errors)
		{
			Value a = Value::array();
			for(const auto& e : _errors)
				a.push(e);
			return a;
		}

		std::string opOf(const Value& _m)
		{
			const auto* op = _m.find("op");
			return op && op->isString() ? op->asString() : std::string();
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
			return v && v->isBool() && v->asBool();
		}
	}

	Desk::Desk(Port _port)
		: m_port(std::move(_port))
		, m_session([this](const Bytes& _b)
		{
			if(m_port.sendSysex)
				m_port.sendSysex(_b);
		})
	{
		m_session.onPattern = [this](const ed::MdPattern& _p) { onPattern(_p); };
		m_session.onKit = [this](const ed::MdKit& _k) { onKit(_k); };
		m_session.onSong = [this](const ed::MdSong& _s) { onSong(_s); };
		m_session.onGlobal = [this](const ed::MdGlobal& _g) { onGlobal(_g); };
		m_session.onState = [this](const mdDataLink::Session::State& _s) { onState(_s); };
	}

	Value Desk::machineCatalogue()
	{
		Value machines = Value::array();
		for(uint32_t model = 0; model < 256; ++model)
		{
			const auto name = ed::mdMachineName(model);
			if(name.empty())
				continue;
			Value m = Value::object();
			m.set("model", static_cast<int>(model));
			m.set("machine", name);
			m.set("family", ed::mdMachineFamily(model));
			Value params = Value::array();
			for(const auto* p : ed::mdMachineParamNames(model))
				params.push(p && *p ? Value(p) : Value());
			m.set("params", std::move(params));
			machines.push(std::move(m));
		}
		Value doc = Value::object();
		doc.set("schema", "md-desk/machines");
		doc.set("version", 1);
		doc.set("machines", std::move(machines));
		return doc;
	}

	// ---- page -> desk ----

	void Desk::onPageMessage(const Value& _message)
	{
		const auto op = opOf(_message);
		if(op == "ready")
		{
			m_pageReady = true;
			Value cat = Value::object();
			cat.set("type", "catalogue");
			cat.set("doc", machineCatalogue());
			publish(cat);
			for(const auto& [slot, p] : m_docs.patterns)
				m_dirty.insert({DocKind::Pattern, slot});
			for(const auto& [slot, k] : m_docs.kits)
				m_dirty.insert({DocKind::Kit, slot});
			for(const auto& [slot, s] : m_docs.songs)
				m_dirty.insert({DocKind::Song, slot});
			if(m_docs.global)
				m_dirty.insert({DocKind::Global, m_docs.global->position});
			m_machineDirty = true;
			publishSetup();
			publishModulators();
			if(m_firmware == Firmware::Present)
			{
				m_session.requestStatus();
				m_lastStatusMs = m_port.nowMs();
			}
			flush();
			return;
		}
		if(m_firmware != Firmware::Present)
		{
			result(_message, {m_firmware == Firmware::Loading || m_firmware == Firmware::Booting
				? "The machine is starting (device busy). Try again in a moment." : "No Machinedrum firmware is running"}, {});
			flush();
			return;
		}
		if(op == "load")
		{
			const auto* kind = _message.find("kind");
			const auto slot = intOf(_message, "slot");
			const auto k = kind && kind->isString() ? kindFromName(kind->asString()) : std::nullopt;
			if(k && slot && *slot >= 0 && *slot < 128)
				load({*k, static_cast<uint8_t>(*slot)}, true);
			return;
		}
		if(!isInputReady())
		{
			result(_message, {m_ready ? "The machine is still starting: its start-up animation ignores keys. The editor"
				" takes input when it is over." : "The machine is still starting (device busy). Try again in a moment."}, {});
			flush();
			return;
		}
		if(op == "select")
			handleSelect(_message);
		else if(op == "undo" || op == "redo")
			handleUndo(op == "redo", _message);
		else if(op == "saveKit" || op == "reloadKit")
		{
			const auto kit = currentKit();
			if(!kit)
				result(_message, {"The current kit is not known yet"}, {});
			else if(op == "saveKit")
			{
				m_session.saveKit(*kit);
				if(const auto it = m_docs.kits.find(*kit); it != m_docs.kits.end())
					m_storedKits[*kit] = it->second;
				result(_message, {}, "Saved kit " + std::to_string(*kit + 1) + " on the machine");
			}
			else
			{
				m_session.loadKit(*kit);
				m_docs.kits.erase(*kit);
				load({DocKind::Kit, *kit}, true);
				result(_message, {}, "Reloaded kit " + std::to_string(*kit + 1) + " from the machine");
			}
			m_machineDirty = true;
		}
		else if(op == "record" || op == "recTrig")
			handleRecord(_message);
		else if(op == "modSet")
			handleModulators(_message);
		else if(op == "chain" || op == "chainClear")
			handleChain(_message);
		else if(op == "knobs")
			handleKnobs(_message);
		else if(op == "kitLoad" || op == "kitSaveAs")
			handleKitSlot(_message);
		else if(op == "selectSong")
		{
			// P1: the Machinedrum ignores LOAD SONG while it plays.
			const auto s = intOf(_message, "s");
			if(!s || *s < 0 || *s > 31)
				result(_message, {"s: expected a song 0-31"}, {});
			else if(m_telemetry.playing)
				result(_message, {"Stop first: the Machinedrum loads another song only when stopped"}, {});
			else
			{
				m_session.loadSong(static_cast<uint8_t>(*s));
				load({DocKind::Song, static_cast<uint8_t>(*s)}, true);
				m_machineDirty = true;
				result(_message, {}, "Song " + std::to_string(*s + 1) + " loaded");
			}
		}
		else if(op == "sampleName")
		{
			// UW ROM slot names: the firmware takes 0x73 but never reports names.
			const auto slot = intOf(_message, "slot");
			const auto* name = _message.find("name");
			const auto bytes = slot && *slot >= 0 && *slot < 48 && name && name->isString()
				? ed::mdSetSampleName(static_cast<uint8_t>(*slot), name->asString()) : std::vector<uint8_t>{};
			if(bytes.empty())
				result(_message, {"A sample name is 1-4 letters (A-Z, 0-9, space, punctuation) for ROM slot 1-48"}, {});
			else
			{
				if(m_port.sendSysex)
					m_port.sendSysex(bytes);
				char label[8];
				std::snprintf(label, sizeof(label), "ROM-%02d", *slot + 1);
				result(_message, {}, std::string("Sent the name to ") + label + ". The Machinedrum shows it in SAMPLE MGR; "
					"it cannot report names back, so the editor does not read them.");
			}
		}
		else if(op == "play" || op == "stop")
		{
			const bool ok = pressKey(op);
			result(_message, ok ? std::vector<std::string>{} : std::vector<std::string>{"Transport keys need the local"
				" emulated machine"}, {});
		}
		else if(op == "reloadSong")
		{
			const auto song = m_session.state().song;
			if(!song)
				result(_message, {"The current song is not known yet"}, {});
			else
			{
				// P1: the playing song ignores dumps and LOAD SONG: stop, load, play.
				const bool wasPlaying = m_telemetry.playing;
				if(wasPlaying)
					pressKey("stop");
				schedule(wasPlaying ? 150 : 0, [this, s = *song] { m_session.loadSong(s); m_machineDirty = true; });
				if(wasPlaying)
					schedule(300, [this] { pressKey("play"); });
				result(_message, {}, "Song reloaded: stop, load, play");
			}
		}
		else if(op == "mute")
		{
			const auto t = intOf(_message, "t");
			if(!t || *t < 0 || *t > 15)
				result(_message, {"t: expected a track 0-15"}, {});
			else
			{
				const bool on = flagOf(_message, "on");
				m_mutes[size_t(*t)] = on;
				if(m_port.sendMute)
					m_port.sendMute(uint8_t(*t), on);
				m_machineDirty = true;
			}
		}
		else
			handleEdit(_message);
		flush();
	}

	void Desk::handleEdit(const Value& _message)
	{
		const auto op = opOf(_message);
		// The kit that plays is renamed live (0x55), like the LCD's kit name.
		const auto k = intOf(_message, "k");
		if(op == "kitRename" && k && currentKit() == *k)
		{
			Value m = Value::object();
			m.set("op", "kitName");
			m.set("k", *k);
			if(const auto* n = _message.find("name"))
				m.set("name", *n);
			if(const auto id = intOf(_message, "id"))
				m.set("id", *id);
			handleEdit(m);
			return;
		}
		auto edit = isLibraryCommand(op) ? applyLibrary(m_docs, _message, m_clipboard) : apply(m_docs, _message, m_clipboard);
		if(!edit.errors.empty())
		{
			result(_message, edit.errors, {});
			return;
		}
		// While live recording the firmware writes the playing pattern itself; a dump
		// from the desk would overwrite what it just recorded.
		if(m_telemetry.recording)
			for(const auto& c : edit.changes)
				if(c.ref().kind == DocKind::Pattern && m_session.state().pattern == c.ref().slot)
				{
					result(_message, {"Live recording owns this pattern: play the tracks, turn the knobs. Press REC "
						"to stop recording, then edit the grid."}, {});
					return;
				}
		if(!flagOf(_message, "force") && askFirst(_message, edit.changes))
			return;
		std::vector<std::string> errors;
		std::string note = edit.note;
		std::vector<Change> delivered;
		for(const auto& change : edit.changes)
		{
			const auto before = errors.size();
			deliver(change, errors, note);
			if(errors.size() == before)
				delivered.push_back(change);
		}
		const auto gesture = intOf(_message, "g");
		m_history.record(delivered, gesture && *gesture > 0 ? uint64_t(*gesture) : 0);
		m_machineDirty = true;
		result(_message, errors, note);
	}

	void Desk::deliver(const Change& _change, std::vector<std::string>& _errors, std::string& _note)
	{
		const auto ref = _change.ref();
		switch(ref.kind)
		{
		case DocKind::Pattern:
			m_docs.set(_change.after);
			deliverPattern(std::get<ed::MdPattern>(_change.after), _errors);
			break;
		case DocKind::Song:
			m_docs.set(_change.after);
			deliverSong(std::get<ed::MdSong>(_change.after), _errors);
			break;
		case DocKind::Kit:
		{
			const auto kit = currentKit();
			if(_change.slotWrite)
			{
				// The kit library: a stored-slot dump; into the kit that plays also LOAD KIT.
				const auto& k = std::get<ed::MdKit>(_change.after);
				const bool playing = kit && *kit == ref.slot;
				auto problems = m_session.pushKit(k, playing ? mdDataLink::Session::KitApply::StoreAndLoad
					: mdDataLink::Session::KitApply::Store);
				if(!problems.empty())
				{
					_errors.insert(_errors.end(), problems.begin(), problems.end());
					return;
				}
				m_storedKits[ref.slot] = k;
				m_docs.set(_change.after);
				if(playing)
					m_workingKit.reset();
				m_lastLiveEditMs = m_port.nowMs();
				m_dirty.insert(ref);
				break;
			}
			if(!kit || *kit != ref.slot)
			{
				_errors.push_back("Only the kit that plays can be edited live" + (kit ? " (kit "
					+ std::to_string(*kit + 1) + " plays)" : std::string()));
				return;
			}
			const auto current = m_docs.kits.find(ref.slot);
			const auto& from = current != m_docs.kits.end() ? current->second : std::get<ed::MdKit>(_change.before);
			const auto delivery = kitDelivery(from, std::get<ed::MdKit>(_change.after));
			bool live = false;
			for(const auto& e : delivery.edits)
			{
				if(e.kind == LiveEdit::Kind::Param && m_telemetry.recording && m_port.turnKnob)
				{
					// Recorded as a lock only when it comes from the knobs (P3).
					m_knobs.want(e.track, e.index, e.value);
					continue;
				}
				live = true;
				if(e.kind == LiveEdit::Kind::Param || e.kind == LiveEdit::Kind::Level)
				{
					if(m_port.sendKitParam)
						m_port.sendKitParam(e.track, e.kind == LiveEdit::Kind::Level ? 24 : e.index, e.value);
				}
				else if(const auto sysex = liveEditSysex(e); !sysex.empty() && m_port.sendSysex)
					m_port.sendSysex(sysex);
			}
			for(const auto& n : delivery.notLive)
				_note += (_note.empty() ? "" : ". ") + n + " stays as it is on the machine";
			m_docs.set(_change.after);
			if(!delivery.edits.empty())
				m_session.noteWorkingKitEdited();
			if(live)
				m_lastLiveEditMs = m_port.nowMs();
			m_dirty.insert(ref);
			break;
		}
		case DocKind::Global:
		{
			if(!m_docs.global)
				return;
			const auto delivery = globalDelivery(*m_docs.global, std::get<ed::MdGlobal>(_change.after));
			for(const auto& e : delivery.edits)
				if(const auto sysex = liveEditSysex(e); !sysex.empty() && m_port.sendSysex)
					m_port.sendSysex(sysex);
			if(!delivery.notLive.empty())
				m_session.pushGlobal(std::get<ed::MdGlobal>(_change.after));
			m_docs.set(_change.after);
			m_lastLiveEditMs = m_port.nowMs();
			m_dirty.insert(ref);
			// Read the slot back so the view shows what the firmware stored.
			schedule(60, [this, slot = ref.slot] { m_session.requestGlobal(slot); });
			break;
		}
		}
	}

	void Desk::deliverPattern(const ed::MdPattern& _p, std::vector<std::string>& _errors)
	{
		const DocRef ref{DocKind::Pattern, _p.position};
		m_dirty.insert(ref);
		auto& slot = m_patternPush[_p.position];
		if(!slot.want(_p))
			return;
		auto problems = m_session.pushPattern(_p);
		if(!problems.empty())
		{
			slot.abandon();
			_errors.insert(_errors.end(), problems.begin(), problems.end());
			return;
		}
		m_pushSentMs[ref] = m_port.nowMs();
	}

	void Desk::deliverSong(const ed::MdSong& _s, std::vector<std::string>& _errors)
	{
		const DocRef ref{DocKind::Song, _s.position};
		m_dirty.insert(ref);
		auto& slot = m_songPush[_s.position];
		if(!slot.want(_s))
			return;
		auto problems = m_session.pushSong(_s);
		if(!problems.empty())
		{
			slot.abandon();
			_errors.insert(_errors.end(), problems.begin(), problems.end());
			return;
		}
		m_pushSentMs[ref] = m_port.nowMs();
	}

	// REC works as on the machine: hold RECORD and press PLAY to live record (from
	// STOP; while playing the firmware does not enter it, so the desk stops first),
	// PLAY again to leave recording and keep playing. recTrig plays a track like its
	// TRIG key, which the firmware records.
	void Desk::handleRecord(const Value& _message)
	{
		const auto press = [&](const std::string& _key) { return pressKey(_key); };
		if(opOf(_message) == "recTrig")
		{
			const auto t = intOf(_message, "t");
			if(!t || *t < 0 || *t > 15)
				result(_message, {"t: expected a track 0-15"}, {});
			else if(!m_telemetry.recording)
				result(_message, {"Not recording: press REC first"}, {});
			else
				result(_message, press("trig" + std::to_string(*t + 1)) ? std::vector<std::string>{}
					: std::vector<std::string>{"TRIG keys need the local emulated machine"}, {});
			return;
		}
		if(!m_telemetry.valid)
		{
			result(_message, {"Live recording needs MD OS 1.63 telemetry"}, {});
			return;
		}
		if(m_telemetry.recording)
		{
			const bool ok = press("play");
			result(_message, ok ? std::vector<std::string>{} : std::vector<std::string>{"No panel here"},
				"Recording off, the pattern keeps playing");
			return;
		}
		if(m_telemetry.playing)
		{
			press("stop");
			m_recordAfterStopMs = m_port.nowMs();
			result(_message, {}, "Live recording from step 1 (the Machinedrum starts it from STOP)");
			return;
		}
		const bool ok = press("recordPlay");
		result(_message, ok ? std::vector<std::string>{} : std::vector<std::string>{"No panel here"},
			"Live recording: play the tracks, turn the knobs");
	}

	// Panel keys are lost while the firmware builds a dump (measured in the plug-in:
	// PLAY during the background song loads did nothing). Keep the line quiet
	// around every key press.
	bool Desk::pressKey(const std::string& _key)
	{
		if(!m_port.pressKey)
			return false;
		m_keyQuietUntilMs = m_port.nowMs() + g_keyQuietMs;
		return m_port.pressKey(_key);
	}

	// App-only LFO and random sources (mdDeskMod.h): the page sends the whole
	// setup; the desk runs it on the machine's steps.
	void Desk::handleModulators(const Value& _message)
	{
		const auto* doc = _message.find("doc");
		std::vector<std::string> errors;
		const auto setup = doc ? modSetupFromJson(*doc, errors) : std::nullopt;
		if(!doc)
			errors.emplace_back("doc: expected an md-desk/modulators document");
		if(setup)
		{
			m_mods.setSetup(*setup);
			m_setup.modulators = *setup;
			saveSetup();
		}
		result(_message, errors, {});
		publishModulators();
	}

	// Library edits that would silently lose something on the machine ask first (the page
	// sends them again with force): a kit written into the kit that plays while it holds
	// unsaved edits, or a pattern dump into the current pattern that links another kit
	// (the firmware then loads that kit, measured).
	bool Desk::askFirst(const Value& _message, const std::vector<Change>& _changes)
	{
		const bool edited = m_session.state().workingKit == mdDataLink::Session::WorkingKit::Edited;
		const auto kit = currentKit();
		for(const auto& c : _changes)
		{
			const auto ref = c.ref();
			std::string what;
			if(ref.kind == DocKind::Kit && c.slotWrite && kit && *kit == ref.slot && edited)
				what = "overwriteKit";
			else if(ref.kind == DocKind::Pattern && m_session.state().pattern == ref.slot && edited
				&& std::get<ed::MdPattern>(c.after).kit != std::get<ed::MdPattern>(c.before).kit)
				what = "relinkKit";
			if(what.empty())
				continue;
			Value ask = Value::object();
			ask.set("type", "ask");
			ask.set("ask", what);
			ask.set("command", _message);
			ask.set("kit", kit ? Value(static_cast<int>(*kit)) : Value());
			publish(ask);
			result(_message, {}, {});
			return true;
		}
		return false;
	}

	// LOAD KIT and SAVE KIT n from the kit library. Measured (mdP4ProbeFirmwareTest library):
	// both make the slot the current kit and, in EXTENDED mode, relink the current pattern to
	// it; LOAD KIT replaces unsaved edits (the machine keeps them in its UNDO KIT).
	void Desk::handleKitSlot(const Value& _message)
	{
		const auto k = intOf(_message, "k");
		if(!k || *k < 0 || *k > 63)
		{
			result(_message, {"k: expected a kit 0-63"}, {});
			return;
		}
		const auto slot = static_cast<uint8_t>(*k);
		const bool edited = m_session.state().workingKit == mdDataLink::Session::WorkingKit::Edited;
		if(opOf(_message) == "kitLoad")
		{
			if(edited && !flagOf(_message, "force"))
			{
				Value ask = Value::object();
				ask.set("type", "ask");
				ask.set("ask", "loadKit");
				ask.set("command", _message);
				ask.set("kit", currentKit() ? Value(static_cast<int>(*currentKit())) : Value());
				publish(ask);
				result(_message, {}, {});
				return;
			}
			m_session.loadKit(slot);
		}
		else
		{
			m_session.saveKit(slot);
			if(const auto cur = currentKit(); cur)
				if(const auto it = m_docs.kits.find(*cur); it != m_docs.kits.end())
				{
					auto saved = it->second;
					saved.position = slot;
					m_storedKits[slot] = saved;
					m_docs.kits[slot] = saved;
					m_dirty.insert({DocKind::Kit, slot});
				}
		}
		// The machine switched kits and relinked the pattern: read them back.
		m_session.requestStatus();
		load({DocKind::Kit, slot}, true);
		if(const auto p = m_session.state().pattern)
			load({DocKind::Pattern, *p}, true);
		m_machineDirty = true;
		result(_message, {}, opOf(_message) == "kitLoad" ? "Loaded kit " + std::to_string(*k + 1)
			: "Saved as kit " + std::to_string(*k + 1) + ": it is now the current kit");
	}

	// The Control workspace's knob rows: which CC each of the eight rows is.
	void Desk::handleKnobs(const Value& _message)
	{
		std::vector<int> ccs;
		if(const auto* list = _message.find("ccs"); list && list->isArray())
			for(const auto& v : list->asArray())
				ccs.push_back(v.isNumber() ? static_cast<int>(v.asNumber()) : -1);
		const auto errors = validateKnobCcs(ccs);
		if(errors.empty())
		{
			for(size_t i = 0; i < 8; ++i)
				m_setup.knobCcs[i] = static_cast<uint8_t>(ccs[i]);
			saveSetup();
		}
		result(_message, errors, {});
		publishSetup();
	}

	std::vector<std::string> Desk::loadSetup(const Value& _setup)
	{
		std::vector<std::string> errors;
		const auto s = deskSetupFromJson(_setup, errors);
		if(!s)
			return errors;
		m_setup = *s;
		m_mods.setSetup(m_setup.modulators);
		publishSetup();
		publishModulators();
		return errors;
	}

	void Desk::publishSetup()
	{
		if(!m_pageReady)
			return;
		Value m = Value::object();
		m.set("type", "setup");
		m.set("doc", deskSetupToJson(m_setup));
		publish(m);
	}

	void Desk::saveSetup() const
	{
		if(m_port.saveSetup)
			m_port.saveSetup(deskSetupToJson(m_setup));
	}

	void Desk::runModulators(const double _now)
	{
		if(m_mods.setup().links.empty())
			return;
		const auto kit = currentKit();
		for(const auto& o : m_mods.step())
		{
			if(!kit || !m_port.sendKitParam || !m_ccBudget.take(_now))
			{
				m_mods.unsent(o);
				continue;
			}
			m_port.sendKitParam(o.track, o.param, o.value);
			onHostKitParam(o.track, o.param, o.value);
		}
		publishModulators();
	}

	void Desk::publishModulators()
	{
		if(!m_pageReady)
			return;
		Value m = Value::object();
		m.set("type", "mod");
		m.set("doc", modSetupToJson(m_mods.setup()));
		Value values = Value::array();
		for(const auto v : m_mods.values())
			values.push(v);
		m.set("values", std::move(values));
		m.set("ccPerSecond", m_ccBudget.lastSecond(m_port.nowMs()));
		m.set("ccLimit", g_modCcPerSecond);
		publish(m);
	}

	void Desk::pumpRecording(const double _now)
	{
		// REC pressed while playing: live recording starts once the machine stopped.
		if(m_recordAfterStopMs >= 0)
		{
			if(!m_telemetry.playing)
				pressKey("recordPlay");
			if(!m_telemetry.playing || _now - m_recordAfterStopMs > 1500)
				m_recordAfterStopMs = -1;
		}
		if(m_recLock && _now - m_recLock->atMs > 3000)
		{
			m_recLock.reset();
			m_machineDirty = true;
		}
		if(!m_telemetry.recording)
			return;
		// What the firmware records shows up in the pattern: read it back now and then.
		const auto pattern = m_session.state().pattern;
		if(pattern && _now - m_recordPollMs > 400 && !isBusy())
		{
			m_recordPollMs = _now;
			load({DocKind::Pattern, *pattern}, true);
		}
		const auto* memory = m_workingKit && currentKit() == m_workingKit->position ? &*m_workingKit : nullptr;
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
				m_keyQuietUntilMs = _now + g_keyQuietMs;
				m_port.turnKnob(step->encoder, step->steps);
				// Honest about where it lands: the track's next trig after the playing step.
				const auto p = m_session.state().pattern ? m_docs.patterns.find(*m_session.state().pattern) : m_docs.patterns.end();
				const auto at = p != m_docs.patterns.end() ? nextLockStep(p->second, step->track, m_telemetry.step) : std::nullopt;
				if(at && m_telemetry.knobPage >= 0)
					m_recLock = RecLock{step->track, static_cast<uint8_t>(m_telemetry.knobPage * 8 + step->encoder), *at, _now};
				else
					m_recLock.reset();
				m_machineDirty = true;
			}
			break;
		}
	}

	void Desk::handleUndo(const bool _redo, const Value& _message)
	{
		auto changes = _redo ? m_history.redo() : m_history.undo();
		if(!changes)
		{
			result(_message, {_redo ? "Nothing to redo" : "Nothing to undo"}, {});
			return;
		}
		std::vector<std::string> errors;
		std::string note = _redo ? "Redo" : "Undo";
		for(const auto& c : *changes)
			deliver(c, errors, note);
		m_machineDirty = true;
		result(_message, errors, note);
	}

	void Desk::handleSelect(const Value& _message)
	{
		const auto p = intOf(_message, "p");
		if(!p || *p < 0 || *p > 127)
		{
			result(_message, {"p: expected a pattern 0-127"}, {});
			return;
		}
		const auto slot = static_cast<uint8_t>(*p);
		if(!flagOf(_message, "force") && !flagOf(_message, "chainOk") && m_telemetry.chainKnown && m_telemetry.chain.active)
		{
			Value ask = Value::object();
			ask.set("type", "ask");
			ask.set("ask", "breakChain");
			ask.set("p", *p);
			publish(ask);
			result(_message, {}, {});
			return;
		}
		if(!flagOf(_message, "force") && m_session.selectWouldDiscardKitEdits(slot))
		{
			const auto& links = m_session.state().patternKits;
			Value ask = Value::object();
			ask.set("type", "ask");
			ask.set("ask", "discardKit");
			ask.set("p", *p);
			ask.set("kit", static_cast<int>(*m_session.state().kit));
			ask.set("target", static_cast<int>(links.at(slot)));
			publish(ask);
			result(_message, {}, {});
			return;
		}
		if(flagOf(_message, "now") && m_telemetry.playing)
		{
			// Switch now: STOP, LOAD PATTERN, PLAY (measured: 367 ms, plays the new pattern).
			pressKey("stop");
			schedule(80, [this, slot] { m_session.selectPattern(slot); m_machineDirty = true; });
			schedule(220, [this] { pressKey("play"); });
			m_audibleQueue.reset();
			load({DocKind::Pattern, slot}, true);
			result(_message, {}, "Switched now: STOP, LOAD PATTERN, PLAY");
			return;
		}
		m_session.selectPattern(slot);
		// While the sequencer plays the switch waits for the end of the pattern.
		m_switchReportedMs = -1;
		if(m_telemetry.playing && m_session.state().pattern != slot)
			m_audibleQueue = slot;
		else
			m_audibleQueue.reset();
		load({DocKind::Pattern, slot}, true);
		m_machineDirty = true;
		result(_message, {}, {});
	}

	// Chaining as on the machine: hold BANK, press the TRIG keys (mdDeskChain.h). The chain is
	// the firmware's; the page sees it through the telemetry. CLEAR is LOAD PATTERN of the
	// current pattern, which is what ends a chain on the machine.
	void Desk::handleChain(const Value& _message)
	{
		if(opOf(_message) == "chainClear")
		{
			const auto current = m_session.state().pattern;
			if(!current)
			{
				result(_message, {"The current pattern is not known yet"}, {});
				return;
			}
			m_session.selectPattern(*current);
			m_machineDirty = true;
			result(_message, {}, "Chain cleared: " + ed::mdPatternName(*current) + " plays on");
			return;
		}
		std::vector<int> patterns;
		if(const auto* list = _message.find("patterns"); list && list->isArray())
			for(const auto& v : list->asArray())
				patterns.push_back(v.isNumber() ? static_cast<int>(v.asNumber()) : -1);
		auto errors = validateChain(patterns);
		if(errors.empty() && (!m_telemetry.valid || !m_port.pressKey))
			errors.emplace_back("Chaining is made with the machine's keys: it needs the local emulated MD OS 1.63");
		const auto keys = errors.empty() ? chainKeys(patterns, m_telemetry.bankGroup) : std::vector<std::string>{};
		if(errors.empty() && keys.empty())
			errors.emplace_back("The machine's BANK GROUP (A-D / E-H) is not known yet");
		if(!errors.empty())
		{
			result(_message, errors, {});
			return;
		}
		bool ok = true;
		for(const auto& k : keys)
			ok = ok && pressKey(k);
		m_audibleQueue.reset();
		m_machineDirty = true;
		result(_message, ok ? std::vector<std::string>{} : std::vector<std::string>{"The panel did not take the keys"},
			m_telemetry.playing ? "Chained: the machine plays them in this order from the pattern end, and loops"
			: "Chained: PLAY starts at " + ed::mdPatternName(static_cast<uint8_t>(patterns.front())) + ", then loops");
	}

	// ---- device -> desk ----

	// HW MIDI: nothing answered for a while, or never since the link was chosen.
	bool Desk::linkLost() const
	{
		const auto now = m_port.nowMs();
		return m_hw && (m_ready ? now - m_lastReplyMs > g_hwLostMs : now - m_hwSinceMs > g_hwLostMs + 1500);
	}

	void Desk::setHardwareLink(const bool _hardware)
	{
		if(m_hw == _hardware)
			return;
		m_hw = _hardware;
		m_hwSinceMs = m_port.nowMs ? m_port.nowMs() : 0;
		m_machineDirty = true;
	}

	void Desk::onDeviceSysex(const Bytes& _message)
	{
		m_lastReplyMs = m_port.nowMs();
		m_session.onSysex(_message);
		flush();
	}

	void Desk::onPattern(const ed::MdPattern& _p)
	{
		const DocRef ref{DocKind::Pattern, _p.position};
		if(m_loading == ref)
			m_loading.reset();
		auto& slot = m_patternPush[_p.position];
		const auto now = m_port.nowMs();
		switch(slot.onReadBack(_p))
		{
		case PushSlot<ed::MdPattern>::ReadBack::NotWaiting:
			m_docs.set(_p);
			break;
		case PushSlot<ed::MdPattern>::ReadBack::Confirmed:
			m_lastRoundTripMs = now - m_pushSentMs[ref];
			m_pushSentMs.erase(ref);
			m_docs.set(_p);
			break;
		case PushSlot<ed::MdPattern>::ReadBack::ConfirmedSendNext:
			m_lastRoundTripMs = now - m_pushSentMs[ref];
			m_session.pushPattern(*slot.inFlight());
			m_pushSentMs[ref] = now;
			break;
		case PushSlot<ed::MdPattern>::ReadBack::Other:
			return;
		}
		m_dirty.insert(ref);
		m_machineDirty = true;
		// The current pattern names the kit the Sound and Mix workspaces edit.
		if(m_session.state().pattern == _p.position)
			if(const auto kit = currentKit())
				load({DocKind::Kit, *kit}, true);
	}

	void Desk::onKit(const ed::MdKit& _k)
	{
		const DocRef ref{DocKind::Kit, _k.position};
		if(m_loading == ref)
			m_loading.reset();
		m_storedKits[_k.position] = _k;
		const bool working = currentKit() == _k.position;
		const bool edited = m_session.state().workingKit == mdDataLink::Session::WorkingKit::Edited;
		const bool fromMemory = working && m_workingKit && m_workingKit->position == _k.position;
		// A dump is the stored slot. The playing kit comes from memory when the
		// firmware offers it; otherwise its unsaved edits live only in our working
		// copy, which the dump must not overwrite.
		if(fromMemory)
			judgeWorkingKit();
		else if(!(working && edited && m_docs.kits.count(_k.position)))
			m_docs.set(_k);
		m_dirty.insert(ref);
		m_machineDirty = true;
	}

	void Desk::onSong(const ed::MdSong& _s)
	{
		const DocRef ref{DocKind::Song, _s.position};
		if(m_loading == ref)
			m_loading.reset();
		auto& slot = m_songPush[_s.position];
		const auto now = m_port.nowMs();
		switch(slot.onReadBack(_s))
		{
		case PushSlot<ed::MdSong>::ReadBack::NotWaiting:
			m_docs.set(_s);
			break;
		case PushSlot<ed::MdSong>::ReadBack::Confirmed:
			m_lastRoundTripMs = now - m_pushSentMs[ref];
			m_pushSentMs.erase(ref);
			m_docs.set(_s);
			break;
		case PushSlot<ed::MdSong>::ReadBack::ConfirmedSendNext:
			m_lastRoundTripMs = now - m_pushSentMs[ref];
			m_session.pushSong(*slot.inFlight());
			m_pushSentMs[ref] = now;
			break;
		case PushSlot<ed::MdSong>::ReadBack::Other:
			return;
		}
		m_dirty.insert(ref);
		m_machineDirty = true;
	}

	void Desk::onGlobal(const ed::MdGlobal& _g)
	{
		const DocRef ref{DocKind::Global, _g.position};
		if(m_loading == ref)
			m_loading.reset();
		if(!m_session.state().globalSlot || *m_session.state().globalSlot == _g.position)
		{
			m_docs.global = _g;
			m_dirty.insert(ref);
		}
	}

	void Desk::onState(const mdDataLink::Session::State& _s)
	{
		m_machineDirty = true;
		if(!m_ready && m_firmware == Firmware::Present)
			m_ready = true;
		if(_s.kit && _s.kit != m_lastKit)
		{
			// Another kit plays now: its working copy is its stored slot.
			if(m_lastKit)
			{
				if(const auto it = m_storedKits.find(*m_lastKit); it != m_storedKits.end())
					m_docs.kits[*m_lastKit] = it->second;
				m_dirty.insert({DocKind::Kit, *m_lastKit});
			}
			m_docs.kits.erase(*_s.kit);
			load({DocKind::Kit, *_s.kit}, true);
			m_lastKit = _s.kit;
			if(m_workingKit && m_workingKit->position != *_s.kit)
				m_workingKit.reset();
		}
		if(_s.pattern && _s.pattern != m_lastPattern)
		{
			m_lastPattern = _s.pattern;
			if(!m_docs.patterns.count(*_s.pattern))
				load({DocKind::Pattern, *_s.pattern}, true);
			if(!m_telemetry.valid && m_audibleQueue == _s.pattern)
				m_audibleQueue.reset();
		}
		if(_s.globalSlot && (!m_docs.global || m_docs.global->position != *_s.globalSlot))
			load({DocKind::Global, *_s.globalSlot}, true);
		if(_s.song && !m_docs.songs.count(*_s.song))
			load({DocKind::Song, *_s.song}, true);
		if(!m_backgroundQueued && _s.pattern && _s.kit)
		{
			// Everything else in the background, so the song palette and the
			// kit-link warnings know every pattern.
			m_backgroundQueued = true;
			// Over DIN MIDI a pattern takes 1.7 s: the small kits first there (the library).
			if(m_hw)
				for(unsigned k = 0; k < 64; ++k)
					load({DocKind::Kit, static_cast<uint8_t>(k)}, false);
			for(unsigned p = 0; p < 128; ++p)
				load({DocKind::Pattern, static_cast<uint8_t>(p)}, false);
			for(unsigned s = 0; s < 32; ++s)
				load({DocKind::Song, static_cast<uint8_t>(s)}, false);
			// The kit library shows every slot's name (P4).
			for(unsigned k = 0; !m_hw && k < 64; ++k)
				load({DocKind::Kit, static_cast<uint8_t>(k)}, false);
		}
	}

	void Desk::onHostKitParam(const uint8_t _track, const uint8_t _index, const uint8_t _value)
	{
		const auto kit = currentKit();
		if(!kit || _track > 15 || _index > 24)
			return;
		const auto it = m_docs.kits.find(*kit);
		if(it == m_docs.kits.end())
			return;
		auto& slot = _index == 24 ? it->second.levels[_track] : it->second.params[_track][_index];
		if(slot == _value)
			return;
		slot = _value;
		m_session.noteWorkingKitEdited();
		m_dirty.insert({DocKind::Kit, *kit});
	}

	void Desk::onHostMute(const uint8_t _track, const bool _muted)
	{
		if(_track > 15 || m_mutes[_track] == _muted)
			return;
		m_mutes[_track] = _muted;
		m_machineDirty = true;
	}

	void Desk::onWorkingKitMemory(const Bytes& _region)
	{
		m_workingRegion = _region;
		applyWorkingKit();
		flush();
	}

	void Desk::applyWorkingKit()
	{
		if(!m_workingRegion || m_firmware != Firmware::Present)
			return;
		const auto now = m_port.nowMs();
		if(now - m_lastLiveEditMs < g_workingKitHoldMs)
			return;
		auto kit = ed::mdWorkingKitFromMemory(*m_workingRegion);
		if(!kit)
		{
			m_workingRegion.reset();
			return;
		}
		// Memory names the current kit. Status is polled; until it agrees, ask once
		// and keep the image.
		if(currentKit() != kit->position)
		{
			if(now - m_kitStatusAskedMs > 200 && m_port.sendSysex)
			{
				m_kitStatusAskedMs = now;
				m_port.sendSysex(ed::mdStatusRequest(ed::MdStatus::Kit));
			}
			return;
		}
		m_workingRegion.reset();
		const auto stored = m_storedKits.find(kit->position);
		if(stored != m_storedKits.end())
		{
			kit->version = stored->second.version;
			kit->revision = stored->second.revision;
		}
		m_workingKit = *kit;
		// Knob moves still on their way stay in the view.
		for(const auto& [key, value] : m_knobs.targets())
			kit->params[key.first][key.second] = value;
		const auto doc = m_docs.kits.find(kit->position);
		if(doc == m_docs.kits.end() || doc->second != *kit)
		{
			m_docs.kits[kit->position] = *kit;
			m_dirty.insert({DocKind::Kit, kit->position});
		}
		judgeWorkingKit();
		m_machineDirty = true;
	}

	// Edited or clean from memory against the stored slot, not from what the desk saw.
	void Desk::judgeWorkingKit()
	{
		if(!m_workingKit || currentKit() != m_workingKit->position)
			return;
		const auto stored = m_storedKits.find(m_workingKit->position);
		if(stored == m_storedKits.end())
			return;
		const bool clean = ed::mdSameKitSound(*m_workingKit, stored->second);
		const auto want = clean ? mdDataLink::Session::WorkingKit::Clean : mdDataLink::Session::WorkingKit::Edited;
		if(m_session.state().workingKit != want)
			m_session.noteWorkingKitObserved(clean);
	}

	void Desk::onTelemetry(const Telemetry& _t)
	{
		const bool changed = _t.step != m_telemetry.step || _t.pattern != m_telemetry.pattern
			|| _t.playing != m_telemetry.playing || _t.valid != m_telemetry.valid
			|| _t.recording != m_telemetry.recording || _t.gridEdit != m_telemetry.gridEdit
			|| _t.knobPage != m_telemetry.knobPage || _t.bootAnimation != m_telemetry.bootAnimation
			|| _t.mutes != m_telemetry.mutes || _t.chainKnown != m_telemetry.chainKnown || _t.chain != m_telemetry.chain
			|| _t.bankGroup != m_telemetry.bankGroup;
		const bool machineChanged = _t.bootAnimation != m_telemetry.bootAnimation || _t.mutes != m_telemetry.mutes
			|| _t.chainKnown != m_telemetry.chainKnown || _t.chain != m_telemetry.chain || _t.bankGroup != m_telemetry.bankGroup;
		const bool recordingChanged = _t.recording != m_telemetry.recording;
		const bool patternChanged = _t.valid && _t.pattern != m_telemetry.pattern;
		const bool wasPlaying = m_telemetry.playing;
		const int stepBefore = m_telemetry.step;
		const bool wrapped = _t.valid && m_telemetry.step >= 0 && _t.step >= 0 && _t.step < m_telemetry.step;
		m_telemetry = _t;
		if(!m_telemetrySeen)
		{
			m_telemetrySeen = true;
			m_machineDirty = true;
		}
		if(!changed)
			return;
		if(machineChanged)
			m_machineDirty = true;
		// The mutes the machine plays with (RAM), whoever set them: the page, CCs, the MUTE window.
		if(_t.mutes >= 0)
			for(size_t t = 0; t < 16; ++t)
				m_mutes[t] = (_t.mutes >> t) & 1;
		if(m_audibleQueue)
		{
			// Status and the RAM pattern byte both switch about two steps before the
			// new pattern is heard (P1, P2 smoke test); the playhead wrap is the
			// audible switch.
			const auto now = m_port.nowMs();
			const bool reported = (_t.valid && _t.pattern == *m_audibleQueue)
				|| m_session.state().pattern == *m_audibleQueue;
			if(reported && m_switchReportedMs < 0)
				m_switchReportedMs = now;
			if(m_switchReportedMs >= 0 && (wrapped || !_t.playing || now - m_switchReportedMs > 2000))
			{
				m_audibleQueue.reset();
				m_switchReportedMs = -1;
				m_machineDirty = true;
			}
		}
		if(wasPlaying != _t.playing)
		{
			m_machineDirty = true;
			if(!_t.playing)
				m_mods.reset();
		}
		// App modulators move on the machine's own steps.
		if(_t.valid && _t.playing && _t.step >= 0 && _t.step != stepBefore)
			runModulators(m_port.nowMs());
		if(recordingChanged)
		{
			m_knobs.reset();
			m_recLock.reset();
			m_machineDirty = true;
			// The last recorded notes: read the pattern back.
			if(!_t.recording && m_session.state().pattern)
				load({DocKind::Pattern, *m_session.state().pattern}, true);
		}
		// The sequencer switched on its own (chain, panel, program change): ask.
		if(patternChanged && m_ready && m_session.state().pattern != _t.pattern && m_port.sendSysex)
		{
			m_port.sendSysex(ed::mdStatusRequest(ed::MdStatus::Pattern));
			m_port.sendSysex(ed::mdStatusRequest(ed::MdStatus::Kit));
		}
		if(!m_pageReady)
			return;
		Value t = Value::object();
		t.set("type", "telemetry");
		t.set("step", _t.step);
		t.set("pattern", _t.pattern);
		t.set("playing", _t.playing);
		t.set("recording", _t.recording);
		t.set("valid", _t.valid);
		publish(t);
	}

	void Desk::setFirmware(const Firmware _firmware)
	{
		if(m_firmware == _firmware)
			return;
		m_firmware = _firmware;
		if(_firmware != Firmware::Present)
			m_ready = false;
		m_machineDirty = true;
		flush();
	}

	// ---- loading ----

	void Desk::load(const DocRef& _ref, const bool _urgent)
	{
		if(m_queued.count(_ref) || m_loading == _ref)
		{
			if(_urgent)
			{
				const auto it = std::find(m_loadQueue.begin(), m_loadQueue.end(), _ref);
				if(it != m_loadQueue.end())
				{
					m_loadQueue.erase(it);
					m_loadQueue.push_front(_ref);
				}
			}
			return;
		}
		if(!_urgent && m_docs.get(_ref))
			return;
		m_queued.insert(_ref);
		if(_urgent)
			m_loadQueue.push_front(_ref);
		else
			m_loadQueue.push_back(_ref);
	}

	void Desk::request(const DocRef& _ref)
	{
		switch(_ref.kind)
		{
		case DocKind::Pattern: m_session.requestPattern(_ref.slot); break;
		case DocKind::Kit: m_session.requestKit(_ref.slot); break;
		case DocKind::Song: m_session.requestSong(_ref.slot); break;
		case DocKind::Global: m_session.requestGlobal(_ref.slot); break;
		}
	}

	void Desk::pumpLoads(const double _now)
	{
		if(m_loading)
		{
			if(_now - m_loadSentMs < g_loadTimeoutMs + (m_hw ? DinPacer::wireMs(replyBytes(m_loading->kind)) * 1.5 : 0))
				return;
			// No answer: once more, then give up on it.
			if(m_loadRetries++ < 1)
			{
				request(*m_loading);
				m_loadSentMs = _now;
				return;
			}
			m_loading.reset();
		}
		if(m_loadQueue.empty() || _now - m_lastRequestMs < g_loadGapMs)
			return;
		// Loads wait while an edit is on the wire (they would delay its read-back) and
		// around panel key presses.
		if(isBusy() || _now < m_keyQuietUntilMs)
			return;
		const auto next = m_loadQueue.front();
		m_loadQueue.pop_front();
		m_queued.erase(next);
		m_loading = next;
		m_loadRetries = 0;
		m_loadSentMs = _now;
		m_lastRequestMs = _now;
		request(next);
	}

	std::optional<uint8_t> Desk::currentKit() const
	{
		return m_session.state().kit;
	}

	bool Desk::isInputReady() const
	{
		// Without telemetry (another firmware) the first status reply is all there is.
		return m_ready && m_firmware == Firmware::Present && m_telemetrySeen
			&& (!m_telemetry.valid || m_telemetry.bootAnimation == 0);
	}

	bool Desk::isBusy() const
	{
		for(const auto& [slot, push] : m_patternPush)
			if(push.busy())
				return true;
		for(const auto& [slot, push] : m_songPush)
			if(push.busy())
				return true;
		return m_port.nowMs() - m_lastLiveEditMs < g_liveEditTxMs;
	}

	void Desk::schedule(const double _delayMs, std::function<void()> _action)
	{
		if(_delayMs <= 0)
		{
			_action();
			return;
		}
		m_scheduled.emplace_back(m_port.nowMs() + _delayMs, std::move(_action));
	}

	void Desk::tick()
	{
		const auto now = m_port.nowMs();
		auto due = std::move(m_scheduled);
		m_scheduled.clear();
		for(auto& [at, action] : due)
		{
			if(at <= now)
				action();
			else
				m_scheduled.emplace_back(at, std::move(action));
		}
		if(m_firmware != Firmware::Present)
		{
			flush();
			return;
		}
		const auto statusEvery = m_ready && m_audibleQueue ? g_statusQueuedMs : g_statusIdleMs;
		if(now - m_lastStatusMs >= statusEvery && now >= m_keyQuietUntilMs)
		{
			m_lastStatusMs = now;
			if(m_ready && m_audibleQueue && m_port.sendSysex)
				m_port.sendSysex(ed::mdStatusRequest(ed::MdStatus::Pattern));
			else
				m_session.requestStatus();
		}
		if(m_hw && linkLost() != m_linkLost)
		{
			m_linkLost = !m_linkLost;
			m_machineDirty = true;
		}
		if(m_ready)
			pumpLoads(now);
		applyWorkingKit();
		pumpRecording(now);

		for(auto it = m_pushSentMs.begin(); it != m_pushSentMs.end();)
		{
			if(now - it->second < g_pushTimeoutMs + (m_hw ? 2.5 * DinPacer::wireMs(replyBytes(it->first.kind)) : 0))
			{
				++it;
				continue;
			}
			const auto ref = it->first;
			it = m_pushSentMs.erase(it);
			if(ref.kind == DocKind::Pattern)
				m_patternPush[ref.slot].abandon();
			else
				m_songPush[ref.slot].abandon();
			Value e = Value::object();
			e.set("type", "error");
			e.set("message", std::string("Push failed: the machine did not read back ") + kindName(ref.kind) + " "
				+ (ref.kind == DocKind::Pattern ? ed::mdPatternName(ref.slot) : std::to_string(ref.slot + 1))
				+ ". Showing what it holds.");
			publish(e);
			load(ref, true);
		}
		flush();
	}

	// ---- desk -> page ----

	void Desk::publish(const Value& _message) const
	{
		if(m_port.toPage)
			m_port.toPage(_message);
	}

	void Desk::result(const Value& _message, const std::vector<std::string>& _errors, const std::string& _note)
	{
		Value r = Value::object();
		r.set("type", "result");
		r.set("op", opOf(_message));
		if(const auto id = intOf(_message, "id"))
			r.set("id", *id);
		r.set("ok", _errors.empty());
		r.set("errors", errorsToJson(_errors));
		r.set("note", _note);
		publish(r);
	}

	void Desk::publishDoc(const DocRef& _ref)
	{
		const auto doc = m_docs.get(_ref);
		if(!doc)
			return;
		Value m = Value::object();
		m.set("type", "doc");
		m.set("kind", kindName(_ref.kind));
		bool pending = false;
		if(_ref.kind == DocKind::Pattern)
		{
			const auto it = m_patternPush.find(_ref.slot);
			pending = it != m_patternPush.end() && it->second.busy();
		}
		else if(_ref.kind == DocKind::Song)
		{
			const auto it = m_songPush.find(_ref.slot);
			pending = it != m_songPush.end() && it->second.busy();
		}
		m.set("pending", pending);
		m.set("doc", std::visit([](const auto& _v) -> Value
		{
			using T = std::decay_t<decltype(_v)>;
			if constexpr(std::is_same_v<T, ed::MdPattern>)
				return ed::patternToJson(_v);
			else if constexpr(std::is_same_v<T, ed::MdKit>)
				return ed::kitToJson(_v);
			else if constexpr(std::is_same_v<T, ed::MdSong>)
				return ed::songToJson(_v);
			else
				return ed::globalToJson(_v);
		}, *doc));
		publish(m);
	}

	void Desk::publishMachine()
	{
		auto doc = mdDataLink::Session::stateToJson(m_session.state());
		Value desk = Value::object();
		desk.set("firmware", m_firmware == Firmware::Missing ? "missing"
			: m_firmware == Firmware::Unsupported ? "unsupported" : m_firmware == Firmware::Loading ? "loading"
			: isInputReady() ? "ready" : "booting");
		// "animation": the firmware answers MIDI but its start-up animation still ignores keys.
		desk.set("engine", m_hw ? "hw" : "emu");
		desk.set("link", !m_hw ? "local" : linkLost() ? "lost" : !m_ready ? "connect" : "ready");
		desk.set("boot", m_firmware != Firmware::Present ? "off" : !m_ready ? "starting"
			: isInputReady() ? "ready" : "animation");
		desk.set("tx", isBusy());
		desk.set("loading", static_cast<int>(m_loadQueue.size() + (m_loading ? 1 : 0)));
		desk.set("roundTripMs", m_lastRoundTripMs);
		desk.set("undo", m_history.canUndo());
		desk.set("redo", m_history.canRedo());
		desk.set("undoCount", static_cast<int>(m_history.size()));
		desk.set("redoCount", static_cast<int>(m_history.redoSize()));
		desk.set("queued", m_audibleQueue ? Value(static_cast<int>(*m_audibleQueue)) : Value());
		desk.set("playing", m_telemetry.playing);
		desk.set("telemetry", m_telemetry.valid);
		desk.set("recording", m_telemetry.recording);
		desk.set("gridEdit", m_telemetry.gridEdit);
		desk.set("knobPage", m_telemetry.knobPage);
		// "memory": the current kit document is read from the machine's memory.
		// "tracked": it is the stored slot plus the edits the desk saw.
		desk.set("kitSource", m_workingKit && currentKit() == m_workingKit->position ? "memory" : "tracked");
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
		Value m = Value::object();
		m.set("type", "machine");
		m.set("doc", std::move(doc));
		publish(m);
	}

	void Desk::flush()
	{
		if(!m_pageReady)
			return;
		const bool tx = isBusy();
		for(const auto& ref : m_dirty)
			publishDoc(ref);
		m_dirty.clear();
		if(m_machineDirty || tx != m_lastTx)
		{
			m_machineDirty = false;
			m_lastTx = tx;
			publishMachine();
		}
	}
}
