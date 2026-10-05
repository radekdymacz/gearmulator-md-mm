// MmMachine: pattern chaining as on the machine (MM-P8): BANK + TRIG keys, one run at a time, the latest
// waits (deskCore::Latest).
#include "mmDeskMachineParts.h"

#include "elektronData/mmCommands.h"

namespace mmDesk
{
	using namespace parts;

	bool MmMachine::pressBankTrigs(const std::vector<int>& _patterns)
	{
		if(_patterns.empty() || !m_port.pressBankTrigs || m_tel.bankGroup < 0)
			return false;
		if(m_recv.taking())	// a key pressed while the machine takes a dump is lost (pressKeys)
			return false;
		const int bank = _patterns.front() >> 4;
		// BANK GROUP first when the patterns are in the other half (A-D / E-H share the BANK keys)
		if((bank >= 4 ? 1 : 0) != m_tel.bankGroup && !pressKeys({Key::BankGroup}))
			return false;
		std::vector<uint8_t> trigs;
		for(const auto p : _patterns)
			trigs.push_back(static_cast<uint8_t>(p & 15));
		if(!m_port.pressBankTrigs(static_cast<uint8_t>(bank & 3), trigs))
			return false;
		// BANK, each TRIG, the TRIG rows let go, BANK let go
		const auto states = 2 + trigs.size() + 2;
		m_keysUntilMs = std::max(m_keysUntilMs, now()) + static_cast<double>(states) * g_keyStateMs + g_keysMarginMs;
		return true;
	}

	bool MmMachine::keysOnTheirWay() const
	{
		const auto r = m_recv.state();
		return now() < m_keysUntilMs || (r != RecvSession::State::Idle && r != RecvSession::State::Failed);
	}

	// MM-P8: chaining as on the machine (manual 1-46): hold BANK, press the TRIG keys (one bank, each
	// pattern once; DevicePort::pressBankTrigs). The chain is the firmware's; the page sees it in the
	// machine document (desk.chain, from RAM). The page sends the chain again at every pad it adds or
	// takes away: while the keys of the one before are still on their way the latest waits (one at a
	// time, the latest wins, pumpChain), so two key runs never interleave and BANK GROUP is pressed from
	// the half the machine is in after the run before.
	Outcome MmMachine::cmdChain(const Value& _m, const Documents&)
	{
		auto patterns = deskCore::chainPatterns(_m);
		auto errors = deskCore::validateChain(patterns, 16, 128);
		if(errors.empty() && !canChain())
			errors.emplace_back(g_noChains);
		if(!errors.empty())
			return {errors, {}, {}};
		if(!m_chain.offer(deskCore::ChainOf{patterns}, keysOnTheirWay()))
			return ok(now() >= m_keysUntilMs ? "Chain next: the machine is on its way to or from SYSEX RECV"
				: "Chain next: the machine is still taking the keys before it");
		return sendChain(patterns);
	}

	Outcome MmMachine::sendChain(const std::vector<int>& _patterns)
	{
		if(m_tel.bankGroup < 0)
			return refuse("The machine's BANK GROUP (A-D / E-H) is not known yet");
		// A chain is pattern mode's: in song mode the machine keeps the chain but plays the song (measured).
		// Pattern mode first (SET STATUS), as the Machinedrum's.
		if(m_songMode != 0)
		{
			m_port.sendSysex(ed::mmSetStatus(ed::MmStatus::SongMode, 0));
			m_port.sendSysex(ed::mmStatusRequest(ed::MmStatus::SongMode));
		}
		if(!pressBankTrigs(_patterns))
			return refuse("The panel did not take the keys");
		m_queuedPattern = -1;
		return ok(m_playing ? "Chained: the machine plays them in this order from the pattern end, and loops"
			: "Chained: PLAY starts at " + ed::mmPatternName(static_cast<uint8_t>(_patterns.front())) + ", then loops");
	}

	bool MmMachine::clearChain()
	{
		return m_curPattern >= 0 && pressBankTrigs({m_curPattern});
	}

	// The chain (or CLEAR) the page asked for while keys were on their way.
	void MmMachine::pumpChain()
	{
		const auto next = m_chain.takeWhen(!keysOnTheirWay());
		if(!next || !canChain())
			return;
		if(std::holds_alternative<deskCore::ChainClear>(*next))
			(void)clearChain();
		else
			(void)sendChain(std::get<deskCore::ChainOf>(*next).patterns);
	}

	// CLEAR is the machine's own way to end a chain: BANK + the TRIG key of the pattern that plays (a pick
	// of it; measured: "active" clears and it plays on). A SysEx LOAD PATTERN would not end it. After the
	// chain keys still on their way.
	Outcome MmMachine::cmdChainClear(const Value&, const Documents&)
	{
		if(!canChain())
			return refuse(g_noChains);
		if(m_curPattern < 0)
			return refuse("The current pattern is not known yet");
		const auto name = ed::mmPatternName(static_cast<uint8_t>(m_curPattern));
		if(!m_chain.offer(deskCore::ChainClear{}, keysOnTheirWay()))
			return ok("Chain cleared once the machine has taken the keys before it: " + name + " plays on");
		if(!clearChain())
			return refuse(m_tel.bankGroup < 0 ? "The machine's BANK GROUP (A-D / E-H) is not known yet" : "The panel did not take the keys");
		return ok("Chain cleared: " + name + " plays on");
	}
}
