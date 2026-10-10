// MmMachine: GRID and LIVE RECORDING (MM-P4): the keys from the mode the machine is in, and the current
// pattern read back while it records (watchRecord, mmDeskWatch.h).
#include "mmDeskMachineParts.h"

namespace mmDesk
{
	using namespace parts;

	// MM-P4: GRID RECORDING (RECORD) and LIVE RECORDING (RECORD + PLAY), as the machine's keys from the
	// mode it is in (RAM). RECORD from LIVE goes to GRID (measured), so off from live is RECORD twice.
	Outcome MmMachine::cmdRecord(const Value& _m, const Documents&)
	{
		if(!machineState())
			return refuse(capabilities().reason("gridRecord"));
		if(m_tel.recording < 0)
			return refuse("The machine's recording mode is not known yet.");
		const auto* m = _m.find("mode");
		const auto mode = m && m->isString() ? m->asString() : std::string();
		const int want = mode == "live" ? 2 : mode == "grid" ? 1 : 0;
		const int cur = m_tel.recording;
		if(want == cur)
			return ok();
		// from -> to (0 off, 1 grid, 2 live): the keys
		static const std::vector<Key> R{Key::Record}, LR{Key::LiveRecord}, R_LR{Key::Record, Key::LiveRecord}, RR{Key::Record, Key::Record};
		const auto& keys = cur == 0 ? (want == 1 ? R : LR) : cur == 1 ? (want == 0 ? R : R_LR) : (want == 0 ? RR : R);
		return pressKeys(keys) ? ok() : refuse(g_panelBusy);
	}

	// The machine writes the current pattern while it records: read it back while it records and once when it
	// stops. True when the recording mode changed since the last step (the transport message says it).
	bool MmMachine::readWhileRecording(const double _now)
	{
		const auto r = watchRecord(m_recordReads, m_tel.recording, m_curPattern >= 0, _now, g_recordReadMs);
		m_recordReads = r.next;
		if(m_recLock && (r.changed || clock() - m_recLock->atMs > g_recLockMs))
			m_recLock.reset();
		if(r.read)
			request({Kind::Pattern, static_cast<uint8_t>(m_curPattern)}, true);
		return r.changed;
	}

	// Measured (mmDeskFirmwareTest reclock, OS 1.32B): while LIVE RECORDING plays, a synth track's value on its own MIDI
	// channel (a CC, pages SYN to LF3) becomes a lock of that value on the step that plays when it arrives, a step
	// without a trig included (it gets a trigless lock). The MIDI page's values go by NRPN, not measured: not named.
	void MmMachine::noteRecLock(const ed::MmKit& _before, const ed::MmKit& _after, const ChannelReach& _reach)
	{
		if(m_tel.recording != 2 || !m_playing || m_tel.step < 0 || m_tel.step > 63)
			return;
		for(uint8_t t = 0; t < 6; ++t)
		{
			if(!_reach.track[t] || _before.machines[t] != _after.machines[t])
				continue;
			for(uint8_t pg = 0; pg < 7; ++pg)
				for(uint8_t i = 0; i < 8; ++i)
					if(_before.tracks[t].pages[pg][i] != _after.tracks[t].pages[pg][i])
						m_recLock = RecLock{t, pg * 8 + i, m_tel.step, clock()};
		}
	}
}
