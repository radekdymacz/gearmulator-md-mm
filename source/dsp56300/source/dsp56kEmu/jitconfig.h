#pragma once

#include <cstdint>
#include <optional>
#include <functional>

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
