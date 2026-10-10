#pragma once

#include <cstdint>

#include "types.h"

namespace dsp56k
{
	// An idle poll loop that spans several JIT blocks (JitBlock::findPollCycle): the words from head to end, the
	// closing branch back to head last, run as one straight turn when no exit branch is taken. Each of its blocks is
	// made with it and checks its id at run time. Below P:$100 blocks
	// are at most two words, so a loop of a few instructions there is always cut into several blocks.
	struct JitPollCycle
	{
		TWord head = 0;					// first word, the target of the closing branch
		TWord end = 0;					// one past the closing branch
		uint32_t id = 0;				// hash of head and words, never 0: the marker's upper half (DSP::m_pollCycleMarker)
		uint32_t instructions = 0;		// per straight turn
		uint32_t cycles = 0;			// per straight turn, as the blocks count them

		uint64_t marker(const TWord _pc) const { return (static_cast<uint64_t>(id) << 32) | _pc; }
	};
}
