#pragma once

#include "jucePluginEditorLib/pluginEditorState.h"
#include "jucePluginEditorLib/skin.h"

#include "mdLib/mdtypes.h"

#include <vector>

namespace jucePluginEditorLib
{
	class Editor;
	class Processor;
}

namespace mdJucePlugin
{
	// The editor page is the only UI (P5, Radek: "we don't want the previous one at all"). The
	// plug-in's editor state calls these three from its hooks (doc/modern-ux/UPSTREAM.md); upstream's
	// panel skins (mdDefault, mmSfx60) stay in the tree, out of the build and the product.

	// The product's page, the only skin built into the plug-in.
	std::vector<jucePluginEditorLib::Skin> editorPageSkins(md::MachineModel _model);

	// Before the editor state picks its skin: a saved config that names any other skin (the panel
	// skins, another product's page, a skin folder) names the product's page instead, so it is what
	// loads, silently. Diagnostics builds also start the P5 skin self-test here.
	void keepEditorPage(jucePluginEditorLib::PluginEditorState& _state);

	// Whatever skin is asked for, the product's page is made.
	jucePluginEditorLib::Editor* createEditorPage(jucePluginEditorLib::PluginEditorState& _state,
		jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin);
}
