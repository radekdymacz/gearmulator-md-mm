#pragma once

#include <cstdint>

namespace elektronData
{
	// The screen the Monomachine OS 1.32B firmware shows, as a fact (P6). The device layer
	// (md::MmTelemetry::screenOf) classifies the firmware's screen handler words; everything above
	// it sees only these kinds. One list for both.
	enum class MmScreen : uint8_t
	{
		Unknown,	// no firmware screen yet (0, or not an OS 1.32B handler)
		Boot,		// the start-up animation
		Main,
		Global,
		GlobalEdit,	// the GLOBAL EDIT menus, SYSEX RECV among them
		Other		// another OS 1.32B screen
	};
}
