#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace elektronData
{
	struct MdPattern;
	struct MdKit;
	struct MdSong;
	struct MdGlobal;

	// Machinedrum OS 1.63 hardware limits. Each returns human-readable problems,
	// empty when the value is something the firmware itself produces or accepts
	// unchanged. Every firmware dump in the P1 corpus validates clean.
	std::vector<std::string> validate(const MdPattern& _pattern);
	std::vector<std::string> validate(const MdKit& _kit);
	std::vector<std::string> validate(const MdSong& _song);
	std::vector<std::string> validate(const MdGlobal& _global);

	// Display units (manual) <-> firmware units, derived from the factory corpus
	// where every stored value is an exact image of an integer display value.
	// Swing: stored = round((percent - 50) * 16384 / 50), 50-80 %.
	int swingPercent(uint32_t _swingAmount);
	uint32_t swingAmountFromPercent(int _percent);
	// Accent: stored = round(display * 127 / 15), display 0-15.
	int accentDisplay(uint8_t _accentAmount);
	uint8_t accentAmountFromDisplay(int _display);
}
