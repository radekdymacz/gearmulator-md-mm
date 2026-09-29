#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

// DESIGN-edit-flow.md: what the session sends the machine, counted for the edit-flow driver
// (mdEditFlowDriver.h). Only a build with gearmulator_MDMM_EDITFLOW_DRIVER counts; the calls
// compile to nothing otherwise.
namespace mdJucePlugin::editFlow
{
#if MDMM_EDITFLOW_DRIVER
	struct Counters
	{
		std::array<std::atomic<uint32_t>, 128> sysexByCmd{};	// SysEx to the machine by command byte
		std::atomic<uint64_t> sysexBytes{0};
		std::atomic<uint32_t> panelPackets{0};					// panel key states and encoder steps
	};
	inline Counters& counters()
	{
		static Counters c;
		return c;
	}
	inline void sysexOut(const std::vector<uint8_t>& _m)
	{
		auto& c = counters();
		c.sysexBytes.fetch_add(_m.size(), std::memory_order_relaxed);
		if(_m.size() > 6)
			c.sysexByCmd[_m[6] & 0x7f].fetch_add(1, std::memory_order_relaxed);
	}
	inline void panelOut(const size_t _packets) { counters().panelPackets.fetch_add(static_cast<uint32_t>(_packets), std::memory_order_relaxed); }
#else
	inline void sysexOut(const std::vector<uint8_t>&) {}
	inline void panelOut(size_t) {}
#endif
}
