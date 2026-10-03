#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mdDesk
{
	// Pattern chaining on MD OS 1.63 (P4, mdP4ProbeFirmwareTest chain2..chain5), as on the
	// machine (manual p.37): hold a BANK key and press the TRIG keys of the patterns to chain,
	// holding each one, in play order. One bank only, each pattern once, and the chain loops.
	// The firmware keeps it (md::ChainAndMutes); a SysEx LOAD PATTERN or a single TRIG with
	// BANK held clears it, and STOP then PLAY resumes at the cued pattern.
	//
	// Plain data in, plain data out: the chain as the machine reports it, the checks for a
	// chain the page asks for, and the panel keys that make it.
	struct Chain
	{
		bool active = false;
		int next = -1;						// the entry the firmware queues next, -1 unknown
		std::vector<uint8_t> patterns;		// 0-127, play order

		bool operator==(const Chain& _o) const { return active == _o.active && next == _o.next && patterns == _o.patterns; }
		bool operator!=(const Chain& _o) const { return !(*this == _o); }
	};

	// Problems with a chain request, as messages for the page; empty = it can be sent (deskCore::validateChain
	// with the Machinedrum's 8 banks of 16 patterns).
	std::vector<std::string> validateChain(const std::vector<int>& _patterns);

	// The panel keys (md::panelKeySequence names) that make the chain. _bankGroup is the
	// machine's BANK GROUP state: 0 A-D, 1 E-H, -1 unknown. A chain in the other half of the
	// banks starts with "bankGroup". Empty when the chain is invalid or the group is unknown
	// and matters (only A-D and E-H share the bank keys).
	std::vector<std::string> chainKeys(const std::vector<int>& _patterns, int _bankGroup);
}
