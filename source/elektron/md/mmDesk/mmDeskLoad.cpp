// MmMachine: reading the machine: requests and the load queue, what the device says (SysEx, status,
// telemetry, the working kit's memory image) into observed documents. Split from mmDeskMachine.cpp (review finding 9).
#include "mmDeskMachineParts.h"

#include "elektronData/mmCommands.h"
#include "elektronData/mmDump.h"
#include "elektronData/mmMachines.h"

#include "deskCore/deskPacer.h"

namespace mmDesk
{
	using namespace parts;
	using deskCore::Source;

	// ---- loading ----

	void MmMachine::request(const Ref& _r, const bool _urgent)
	{
		m_loads.want(_r, _urgent);
	}

	void MmMachine::requestStatus()
	{
		for(const auto p : {ed::MmStatus::Pattern, ed::MmStatus::Kit, ed::MmStatus::Song, ed::MmStatus::Global, ed::MmStatus::SongMode,
			ed::MmStatus::Poly})
			m_port.sendSysex(ed::mmStatusRequest(p));
	}

	void MmMachine::pumpLoads(const double _now)
	{
		const auto& inFlight = m_loads.loading();
		const double timeout = g_loadTimeoutMs + (m_profile.wire && inFlight ? 1.5 * deskCore::DinPacer::wireMs(replyBytes(inFlight->kind)) : 0);
		const auto step = m_loads.next(_now, timeout, true, g_loadPolicy);
		if(step.gaveUp)
			gaveUpLoad(*step.gaveUp);
		if(!step.send)
			return;
		const auto& r = *step.send;
		m_port.sendSysex(r.kind == Kind::Pattern ? ed::mmPatternRequest(r.slot) : r.kind == Kind::Kit ? ed::mmKitRequest(r.slot)
			: r.kind == Kind::Song ? ed::mmSongRequest(r.slot) : ed::mmGlobalRequest(r.slot));
	}

	// A read with no reply after every resend and round (a slow wire, a cable pulled): the queue gave
	// it up. It counts as done but failed in the machine document's loading (so the progress ends); a
	// document the machine plays now is reported once (one given up in the background is reported when
	// it plays and is given up again), and a status reply asks for it again once its backoff is over
	// (onStatus). Another one stays unknown until it is wanted again.
	void MmMachine::gaveUpLoad(const Ref& _r)
	{
		gaveUpReading(_r, now());
		if(!current(_r) || !tellUnread(_r))
			return;
		fail(_r, std::string("The machine did not answer the request for ") + kindName(_r.kind) + " " + std::to_string(_r.slot + 1)
			+ ": it cannot be edited until it is read. The editor asks again.");
	}

	bool MmMachine::current(const Ref& _r) const
	{
		const auto is = [&](const int _cur) { return _cur >= 0 && deskCore::slotIn<MmModel>(_r.kind, _cur) == _r.slot; };
		switch(_r.kind)
		{
		case Kind::Pattern: return is(m_curPattern);
		case Kind::Kit: return is(m_curKit);
		case Kind::Song: return is(m_curSong);
		case Kind::Global: return is(m_curGlobal);
		case Kind::WorkingKit: return false;
		}
		return false;
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
		case ed::g_mmSongDump: onDump({Kind::Song, deskCore::slotIn<MmModel>(Kind::Song, slot)}, _m); break;
		case ed::g_mmGlobalDump: onDump({Kind::Global, deskCore::slotIn<MmModel>(Kind::Global, slot)}, _m); break;
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
		auto* found = m_pushes.find(_r);
		if(found && found->slot.busy() && !found->parked)
		{
			auto& push = *found;
			const auto askedMs = push.slot.asked() ? push.slot.askedMs() : push.slot.sentMs();
			action = deskCore::readBack(push.slot, _sysex);
			if(action == deskCore::ReadBackAction::Wait)
				return;	// an older reply: the read-back at quiet confirms, or its timeout fails the push
			m_lastRoundTripMs = now() - askedMs;
		}
		if(action == deskCore::ReadBackAction::Settle)
		{
			settle(*doc, Source::Dump);
			// A global is stored at once but applied only when its slot is made active, and not while the
			// machine is on SYSEX RECV (P7, measured with MIDI SYNC's CLOCK IN, mmDeskFirmwareTest
			// hostclock): the active slot's push ends with 0x56 once the panel is back on its main screen.
			if(_r.kind == Kind::Global && activeSlot() >= 0 && static_cast<int>(_r.slot) == (activeSlot() & 7))
			{
				m_activateGlobal = static_cast<int>(_r.slot);
				m_reactivateGlobal = manualDumps();	// the person may still be on SYSEX RECV: again before PLAY
			}
		}
		else
			observe(*doc, Source::Dump);
		// Until memory shows it (or on a device without memory), the kit that plays starts as its slot.
		if(_r.kind == Kind::Kit && static_cast<int>(_r.slot) == m_curKit)
		{
			auto [next, seed] = deskCore::fromDump(std::move(m_working), ed::mmKitAsLoaded(std::get<ed::MmKit>(*doc)));
			m_working = std::move(next);
			if(seed)
				observe(WorkingKit{*seed}, Source::Dump);
		}
	}

	void MmMachine::kitSwitched(const int _from, const int _to)
	{
		if(_from == _to)
			return;
		m_loadAfter.clear();	// a LOAD KIT still waiting was for the kit that played
		// The kit that played is gone; the new one comes from memory, or from its slot's dump.
		if(_from >= 0)
			forget({Kind::WorkingKit, 0});
		m_working = deskCore::switched(m_working);
		if(_to >= 0)
			request({Kind::Kit, static_cast<uint8_t>(_to)}, true);
	}

	void MmMachine::setBaseChannel(const ed::MmGlobal& _g)
	{
		// B-051: a new base channel is the machine's once the global is made active (after SYSEX RECV), and the
		// emulator's parameter layer learns it by its own poll: no channel messages until both have it
		if(m_baseChannel >= 0 && m_baseChannel != _g.baseChannel)
			m_channelsSettleUntilMs = clock() + g_channelsSettleMs;
		m_baseChannel = _g.baseChannel;
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
			else if(mayAskAgain({Kind::Pattern, _value}, now()) && !m_loads.contains({Kind::Pattern, _value}))
				request({Kind::Pattern, _value}, true);	// its read was given up (no reply): ask again
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
			else if(mayAskAgain({Kind::Kit, _value}, now()) && !m_loads.contains({Kind::Kit, _value}))
				request({Kind::Kit, _value}, true);	// its read was given up (no reply): ask again
			break;
		case ed::MmStatus::Song:
			if(m_curSong != _value)
			{
				m_curSong = _value;
				m_songReloadNeeded = false;	// another song: what was edited is not what plays
				if(!known({Kind::Song, deskCore::slotIn<MmModel>(Kind::Song, _value)}))
					request({Kind::Song, deskCore::slotIn<MmModel>(Kind::Song, _value)}, true);
			}
			break;
		case ed::MmStatus::Global:
			if(m_chosenGlobal >= 0 && (_value == m_chosenGlobal || (m_chosenSentMs >= 0 && clock() - m_chosenSentMs > g_chosenGlobalMs)))
				m_chosenGlobal = -1;	// the machine shows the choice (or never took it)
			if(m_curGlobal != _value)
			{
				m_curGlobal = _value;
				if(!known({Kind::Global, deskCore::slotIn<MmModel>(Kind::Global, _value)}))
					request({Kind::Global, deskCore::slotIn<MmModel>(Kind::Global, _value)}, true);
			}
			break;
		case ed::MmStatus::SongMode:
			m_songMode = _value;
			break;
		case ed::MmStatus::Poly:
			m_poly = _value;
			m_expectPoly = m_expectPoly.observed(polyInMemory(), clock(), m_profile.settleMs);
			return;
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

	bool MmMachine::onTelemetry(const Telemetry& _t)
	{
		// MM-P8: a chain the firmware now holds (made here or on the panel) is what plays next: a queued
		// pick gives way to it, and the machine's pattern and mode are asked again.
		const bool newChain = _t.chainKnown && _t.chain.active && !_t.chain.patterns.empty()
			&& (!m_tel.chainKnown || !m_tel.chain.active || m_tel.chain.patterns != _t.chain.patterns);
		m_tel = _t;
		// what memory now shows settles the fields the editor set (or gives them up: memory wins)
		for(size_t i = 0; i < m_expectMute.size(); ++i)
			m_expectMute[i] = m_expectMute[i].observed(muteInMemory(static_cast<int>(i)), clock(), m_profile.settleMs);
		m_expectTempo = m_expectTempo.observed(tempoInMemory(), clock(), m_profile.settleMs);
		if(newChain)
		{
			m_queuedPattern = -1;
			if(ready())
			{
				requestStatus();
				m_lastStatusMs = now();
			}
		}
		// Playing = the RAM flag, or the step byte advancing (watchStep). Derived; m_tel stays as read.
		const double t = now();
		const auto seen = watchStep(m_steps, _t, t);
		m_steps = seen.next;
		const bool stepped = seen.stepped;
		m_playing = seen.playing;
		if(!m_playing && m_queuedPattern >= 0)
			m_lastStatusMs = -1e9;	// stopped: LOAD PATTERN switches at once; status will say
		pumpSequence(t);
		return stepped;
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
		if(!reach(_view).track[_track])
			return;	// B-051: no channel of its own; an app modulator is too fast for kit dumps
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
		if(reloadHolds())	// B-027: the region waits for the restore after a pattern dump's kit reload
			return;
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
		auto r = deskCore::fromImage(std::move(m_working), *k, kitNumber, current, _view.workingKitOf(m_curKit), _now, false, reflects);
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
}
