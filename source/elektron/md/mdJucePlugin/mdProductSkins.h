#pragma once

#include <vector>

#include "jucePluginEditorLib/skin.h"
#include "mdLib/mdtypes.h"

namespace mdJucePlugin
{
	// The editor page is the only UI (P5, Radek: "we don't want the previous one at all"): the
	// panel skins (mdDefault, mmSfx60) are gone. A saved config that names one falls back to it.
	inline std::vector<jucePluginEditorLib::Skin> productSkins(const md::MachineModel _model)
	{
		if(_model == md::MachineModel::Monomachine)
			return {{"Monomachine Editor", "mmStudio.rml", "", {"mmStudio.rml", "mmStudio.html"}}};
		return {{"Machinedrum Editor", "mdStudio.rml", "", {"mdStudio.rml", "mdStudio.html"}}};
	}
}
