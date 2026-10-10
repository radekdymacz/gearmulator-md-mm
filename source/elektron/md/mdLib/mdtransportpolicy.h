#pragma once

#include "mdtypes.h"

#include <cstddef>
#include <cstdint>

namespace md
{
	// Scheduling and host-port limits used to interleave the ColdFire and two
	// DSPs on one host thread. These values preserve the current runtime
	// behavior; keeping them together makes model differences and units explicit.
	struct TransportPolicy
	{
		double backgroundQuantumMicroseconds;
		uint64_t catchUpMaxDspCycles;
		size_t hostReceiveIrqMinWords;
		size_t hostReceiveQueueCapacityWords;
		size_t hostTransmitBackpressureThresholdWords;
		uint64_t hostTransmitBackpressureReleaseUcCycles;
		// The serial clock wakes the DSP at the exact cycle of its next slot instead of at half the remaining
		// cycles converted to instructions (dsp56k::EsxiClock::usesExactCycleDeadline). The first value is the
		// default emulation, the second the one with L3 opted in (md::Hardware::setExactEssiTiming, and only with
		// the speed-ups on): on the Machinedrum L3 (doc/modern-ux/RESEARCH-emulation-cpu.md 3.4) changes its
		// audio, so it is off unless asked for; the Monomachine has always run exact.
		bool exactEssiCycleDeadlines;
		bool exactEssiCycleDeadlinesL3;

		bool exactEssiCycleDeadlinesFor(const bool _l3) const
		{
			return _l3 ? exactEssiCycleDeadlinesL3 : exactEssiCycleDeadlines;
		}
	};

	constexpr TransportPolicy transportPolicy(const MachineModel _model)
	{
		return _model == MachineModel::Monomachine
			? TransportPolicy{30.0, 100'000, 1, 16, 4, 200'000, true, true}
			: TransportPolicy{125.0, 100'000, 3, 16, 4, 200'000, false, true};
	}
}
