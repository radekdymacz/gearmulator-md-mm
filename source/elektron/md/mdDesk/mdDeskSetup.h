#pragma once

#include "deskCore/deskMod.h"

#include "elektronData/json.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mdDesk
{
	// The app modulators are deskCore's (shared with the Monomachine); the MD's links address the
	// 16 tracks' 24 kit parameters.
	using deskCore::ModSetup;
	using deskCore::ModSource;
	using deskCore::ModLink;
	using deskCore::ModEngine;
	using deskCore::g_modCcPerSecond;
	using deskCore::modSetupFromJson;
	using deskCore::modSetupToJson;
	constexpr deskCore::ModLimits g_mdModLimits{"md-desk/modulators", 15, 23};

	// The editor's own setup that belongs to the project, not to the machine (P4): the app
	// modulators (md-desk/modulators) and the CC numbers of the Control workspace's eight
	// controller knob rows. As the "md-desk/setup" document it is stored with the plug-in
	// state (the host keeps the text, it never looks inside) and restored with the project.
	struct DeskSetup
	{
		static constexpr std::array<uint8_t, 8> g_defaultKnobCcs{21, 22, 23, 24, 25, 26, 27, 28};

		ModSetup modulators;
		std::array<uint8_t, 8> knobCcs = g_defaultKnobCcs;

		bool operator==(const DeskSetup& _o) const;
		bool operator!=(const DeskSetup& _o) const { return !(*this == _o); }
	};

	// Errors carry JSON paths. Knob CCs must be 0-127 and distinct.
	std::optional<DeskSetup> deskSetupFromJson(const elektronData::json::Value& _doc, std::vector<std::string>& _errors);
	elektronData::json::Value deskSetupToJson(const DeskSetup& _setup);
	std::vector<std::string> validateKnobCcs(const std::vector<int>& _ccs);
}
