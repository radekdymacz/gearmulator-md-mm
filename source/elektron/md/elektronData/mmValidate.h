#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace elektronData
{
	struct MmPattern;
	struct MmKit;
	struct MmSong;
	struct MmGlobal;

	// Monomachine OS 1.32B limits. Each returns human-readable problems, empty
	// when the value is one the firmware itself produces. Every factory dump and
	// every programmed read-back in the MM-P1 corpus validates clean. The firmware
	// stores a dump sent on SYSEX RECV without checking it, so this is the guard.
	std::vector<std::string> validate(const MmPattern& _pattern);
	std::vector<std::string> validate(const MmKit& _kit);
	std::vector<std::string> validate(const MmSong& _song);
	std::vector<std::string> validate(const MmGlobal& _global);

	// Display units: swing 50-80 % = 50 + swingAmount; arp offset = step - 0x40.
	inline int mmSwingPercent(const uint8_t _amount) { return 50 + _amount; }
	constexpr uint8_t g_mmArpStepMuted = 0xff;
	constexpr uint8_t g_mmArpStepCentre = 0x40;
}
