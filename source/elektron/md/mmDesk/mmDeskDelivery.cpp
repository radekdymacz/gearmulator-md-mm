// MmMachine: how a document change reaches the machine (review, submit, the live kit edits, the dumps'
// push slots on SYSEX RECV or waiting for the person's, hwSend). Split from mmDeskMachine.cpp (review finding 9).
#include "mmDeskMachineParts.h"

#include "elektronData/mmCommands.h"
#include "elektronData/mmDump.h"

namespace mmDesk
{
	using namespace parts;
	using deskCore::Source;

	namespace
	{
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

	// ---- core -> machine ----

	MmMachine::Review MmMachine::review(const Value& _command, const std::vector<Change>& _changes, const Documents& _view)
	{
		for(const auto& c : _changes)
		{
			if(c.ref().kind != Kind::Global || static_cast<int>(c.ref().slot) != m_curGlobal)
				continue;
			const auto it = _view.globals.find(c.ref().slot);
			if(it != _view.globals.end() && std::get<ed::MmGlobal>(c.after).baseChannel != it->second.baseChannel)
				return refuse("The editor talks to the machine on the active global's base channel; change it on the machine.");
		}
		// The library's slot writes that lose something ask first (the page sends them again with force), as the
		// Machinedrum's: clearing a slot; writing over a kit that has a name, over the kit that plays (it is loaded
		// too, its unsaved edits go), or over a pattern with trigs.
		const auto op = deskCore::opOf(_command);
		const auto* row = MmModel::commands().find(op);
		if(!row || row->group != g_library)
			return ok();
		const bool clears = op == "kitClear" || op == "patClear";
		if(op == "kitRename" || op == "kitCopy" || op == "patCopy")
			return ok();	// a name, or the clipboard: nothing is lost
		const bool edited = kitState(_view) == deskCore::KitState::Edited;
		Outcome o;
		for(const auto& c : _changes)
		{
			const auto ref = c.ref();
			const bool plays = ref.kind == Kind::Kit && static_cast<int>(ref.slot) == m_curKit;
			const std::string playing = !plays ? std::string() : edited ? " It is the kit that plays: it is loaded too, and its unsaved edits are lost (the machine keeps them in its UNDO KIT)."
				: " It is the kit that plays: it is loaded too.";
			if(clears && ref.kind == Kind::Kit)
				o = deskCore::withAsk(std::move(o), ask("clearSlot", "Clear <b>" + kitLabel(_view, ref.slot) + "</b>? Every track becomes GND-SIN." + playing, "Clear kit"));
			else if(clears && ref.kind == Kind::Pattern)
				o = deskCore::withAsk(std::move(o), ask("clearSlot", "Clear <b>" + ed::mmPatternName(ref.slot) + "</b>? Its notes and locks are removed. It keeps its kit link.",
					"Clear pattern"));
			else if(ref.kind == Kind::Kit && (plays || !kitIsEmpty(std::get<ed::MmKit>(c.before))))
				o = deskCore::withAsk(std::move(o), ask("overwriteSlot", "Write over <b>" + kitLabel(_view, ref.slot) + "</b>? What it holds is replaced." + playing, "Overwrite"));
			else if(ref.kind == Kind::Pattern && patternHasTrigs(std::get<ed::MmPattern>(c.before)))
				o = deskCore::withAsk(std::move(o), ask("overwriteSlot", "Write over <b>" + ed::mmPatternName(ref.slot) + "</b>? Its notes and locks are replaced.",
					"Overwrite"));
		}
		return Review(o);
	}

	Outcome MmMachine::submit(const Change& _change, const Intent&, const Documents&)
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
			// A stored slot: its dump on SYSEX RECV; into the kit that plays it is loaded too (LOAD KIT, as the
			// Machinedrum's library does), so what plays is what the slot holds.
		{
			const bool sent = pushDump(ref, ed::encodeMmKit(std::get<ed::MmKit>(_change.after)));
			if(static_cast<int>(ref.slot) == m_curKit)
			{
				loadKitAfter(ref, sent);
				return ok("Written to the kit slot and loaded.");
			}
			return ok();
		}
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
			const Ref slot{Kind::Kit, k.position};
			loadKitAfter(slot, pushDump(slot, ed::encodeMmKit(k)));
			_notes.push_back("Written to the kit slot and loaded (no live SysEx for this setting).");
		}
	}

	deskCore::PushPolicy MmMachine::pushPolicy(const Kind _kind) const
	{
		return deskCore::wirePolicy(m_profile.push, m_profile.wire, replyBytes(_kind));
	}

	// Paced (DESIGN-edit-flow.md): the dump goes now or waits its turn (latest wins); the read-back is
	// asked for once the gesture is quiet (pumpPushes).
	bool MmMachine::pushDump(const Ref& _ref, Bytes _dump)
	{
		auto& p = m_pushes[_ref];
		if(manualDumps() && p.parked)
		{
			// Still waiting for the person's SYSEX RECV: the newer dump takes its place (latest wins), in its
			// place in the queue.
			for(auto& w : m_manual)
				if(w.ref && *w.ref == _ref)
					w.bytes = _dump;
			p.slot.abandon();
			p.slot.want(_dump, now(), pushPolicy(_ref.kind));
			return true;
		}
		// A dump still queued on SYSEX RECV (parked) goes first; the new one waits its turn (Held). Else
		// paced; latest wins.
		if(!m_pushes.want(_ref, _dump, now(), pushPolicy(_ref.kind)))
			return false;
		sendDump(_ref, std::move(_dump));
		return true;
	}

	void MmMachine::loadKitAfter(const Ref& _ref, const bool _sent)
	{
		if(_sent)
			afterDumps(ed::mmLoadKit(_ref.slot));
		else
			m_loadAfter.insert(_ref);
	}

	void MmMachine::sendDump(const Ref& _ref, Bytes _dump)
	{
		m_pushes[_ref].parked = true;
		if(manualDumps())
		{
			// HW MIDI: the machine takes a dump only on SYSEX RECV, which only the person can open: it waits
			// (recv.waiting, SEND n) until the page says the machine is there (hwSend).
			m_manual.push_back({std::move(_dump), _ref});
			return;
		}
		const auto tag = m_nextRecvTag++;
		m_recvRefs[tag] = _ref;
		m_recv.want(std::move(_dump), tag);
	}

	void MmMachine::pumpPushes(const double _now)
	{
		const auto policyOf = [this](const Ref& _ref) { return pushPolicy(_ref.kind); };
		const double timeout = m_profile.wire ? g_wireReadBackTimeoutMs : g_readBackTimeoutMs;
		const auto timeoutOf = [timeout](const Ref&) { return timeout; };
		using K = Pushes::Effect::Kind;
		for(auto& e : m_pushes.pump(_now, policyOf, timeoutOf))
		{
			switch(e.kind)
			{
			case K::Send:
				sendDump(e.ref, std::move(*e.value));
				// a write into the kit that plays waited behind the dump before it: its LOAD KIT follows it (measured:
				// two kit edits in quick succession lost the second, LOAD KIT ran before its dump)
				if(m_loadAfter.erase(e.ref) && static_cast<int>(e.ref.slot) == m_curKit)
					afterDumps(ed::mmLoadKit(e.ref.slot));
				break;
			case K::AskBack:
				// The gesture is quiet: one read-back confirms the last dump.
				request(e.ref, true);
				break;
			case K::TimedOut:
				// A read-back that never came: the push was given up.
				fail(e.ref, std::string("The machine did not read back the ") + kindName(e.ref.kind) + " that was sent. Showing what it holds.");
				request(e.ref, true);
				break;
			}
		}
	}

	void MmMachine::afterDumps(Bytes _message)
	{
		if(manualDumps())
			m_manual.push_back({std::move(_message), std::nullopt});
		else
			m_recv.want(std::move(_message));
	}


	// MM-P4, HW MIDI: the person says the machine is on SYSEX RECV. What waited goes out, in order; the
	// dumps are read back.
	Outcome MmMachine::cmdHwSend(const Value&, const Documents&)
	{
		if(!manualDumps())
			return refuse("The editor opens SYSEX RECV on this engine by itself.");
		if(m_manual.empty())
			return ok("Nothing waits for SYSEX RECV.");
		const auto n = m_manual.size();
		for(auto& w : m_manual)
		{
			m_port.sendSysex(w.bytes);
			if(!w.ref)
				continue;
			if(auto* push = m_pushes.find(*w.ref); push && push->parked)
			{
				push->parked = false;
				push->slot.askedBack(now());
				request(*w.ref, true);
			}
		}
		m_manual.clear();
		return ok(std::to_string(n) + (n == 1 ? " message" : " messages") + " sent. Press EXIT on the Monomachine when the editor has read"
			" them back (the pattern field is empty again).");
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
			// On the wire now; its read-back waits for the gesture's quiet (pumpPushes).
			if(auto* push = m_pushes.find(ref); push && push->parked)
				push->parked = false;
		}
	}
}
