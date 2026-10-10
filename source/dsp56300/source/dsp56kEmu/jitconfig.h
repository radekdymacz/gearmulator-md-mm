#pragma once

#include <cstdint>
#include <optional>
#include <functional>
#include <vector>

#include "types.h"

namespace dsp56k
{
	struct JitConfig
	{
		bool aguSupportBitreverse = false;
		bool aguSupportMultipleWrapModulo = true;
		bool cacheSingleOpBlocks = true;
		bool linkJitBlocks = true;
		bool splitOpsByNops = false;
		bool dynamicPeripheralAddressing = false;

		uint32_t maxInstructionsPerBlock = 0;
		bool memoryWritesCallCpp = false;

		// 16 bit compatibility mode for AGU operations are not supported by default, set to true if needed
		bool support16BitSCMode = false;

		// maximum number of iterations of a do loop before the Jit block is exited (and later re-entered), giving a time slice for interrupts/peripherals
		uint32_t maxDoIterations = 0;

		// Emit the idle fast-forward call at the head of DO loop bodies that hold only NOPs (needs maxDoIterations).
		// It skips whole turns at run time when the DSP allows it (DSP::fastForwardNopLoop, DSP::setIdleFastForward);
		// with that switch off the call returns at once and the loop runs as it always did.
		bool nopLoopFastForward = false;

		// Emit the idle fast-forward call at the end of a block that polls a DMA register and branches back to itself,
		// and whose turns repeat the same state (JitBlock::isIdlePollLoop, DSP::fastForwardPollLoop). Switched as above.
		bool pollLoopFastForward = false;

		// The same for poll loops that span several blocks (JitBlock::findPollCycle, DSP::fastForwardPollCycle): the
		// blocks of the loop pass a marker of the straight turn on, and the head calls the fast-forward when it came
		// round. Below P:$100 blocks are at most two words, so such a loop is always cut into several. Needs
		// linkJitBlocks off. Switched as above.
		bool pollCycleFastForward = false;

		// X peripheral addresses (as on the bus: $ffff80-$ffffff) whose reads a poll loop may hold besides the DMA
		// registers: reads that, repeated, return the same value and change nothing more than the first one did, until
		// a peripheral run of this DSP or the end of the run (the host's run loop returns). The host knows which of its
		// pins qualify; a pin whose value follows the DSP's own counters (a clock derived from them) does not.
		std::vector<TWord> pollLoopPureReads;

		// needs to be true if there is code that executes code in interrupt regions as regular jumps
		bool dynamicFastInterrupts = false;

		// asmjit can validate the generate code, usually not needed
		bool asmjitDiagnostics = false;

		// enable JIT optimizer (dead code elimination + constant folding)
		bool enableOptimizer = true;

		// x86-64 only: Will issue int3() = breakpoint interrupt if a memory address is detected that points to peripherals but DPA is disabled
		bool debugDynamicPeripheralAddressing = false;

		// retrieves a JitConfig for a specific PC. If null, the global default config is used
		std::function<std::optional<JitConfig>(TWord)> getBlockConfig;
	};
}
