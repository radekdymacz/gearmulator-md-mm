#pragma once

#include "mdLib/mdtypes.h"

#include <string_view>

namespace mdJucePlugin
{
	constexpr const char* defaultSkinName(const md::MachineModel _model)
	{
		return _model == md::MachineModel::Monomachine ? "Monomachine Editor" : "Machinedrum Editor";
	}

	constexpr const char* defaultSkinFile(const md::MachineModel _model)
	{
		return _model == md::MachineModel::Monomachine ? "mmStudio.rml" : "mdStudio.rml";
	}

	// The HTML studio editor (P0) instead of the panel editor.
	constexpr bool isStudioSkin(const std::string_view _displayName, const std::string_view _filename)
	{
		return _displayName == "mdStudio" || _filename == "mdStudio.rml";
	}

	// The Monomachine Editor page (MM-P2) instead of the panel editor.
	constexpr bool isMmStudioSkin(const std::string_view _displayName, const std::string_view _filename)
	{
		return _displayName == "mmStudio" || _filename == "mmStudio.rml";
	}

	// Only the product's own editor page: any other saved skin (the removed panels, another
	// product's page, a skin folder) falls back to it silently.
	constexpr bool isSkinCompatible(const md::MachineModel _model,
		const std::string_view _displayName, const std::string_view _filename)
	{
		return _model == md::MachineModel::Monomachine ? isMmStudioSkin(_displayName, _filename) || _displayName == "Monomachine Editor"
			: isStudioSkin(_displayName, _filename) || _displayName == "Machinedrum Editor";
	}
}
