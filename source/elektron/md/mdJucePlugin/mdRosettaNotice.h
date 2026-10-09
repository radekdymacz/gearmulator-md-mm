#pragma once

// The notice for an editor that runs translated (mdProcessArch.h): the words, the "Don't show again" kept in the
// plug-in's config, and the rule of one notice a session. A window offers it to the page through the notice route
// (juceUiLib/messageRoute.h; PageEditor::offerRosettaNotice); the page shows it as its dialog and answers with the
// button pressed (NoticeBook).

#include "mdProcessArch.h"

#include "juceUiLib/messageRoute.h"

#include "juce_data_structures/juce_data_structures.h"

#include <atomic>
#include <optional>
#include <string>

namespace mdJucePlugin::rosettaNotice
{
	// The plug-in's config key (one per editor, with its other toggles): set when the user chose "Don't show again".
	constexpr const char* g_dismissedKey = "rosettaNoticeDismissed";

	// Upstream's own Rosetta warning ("<product> - Rosetta detected", jucePluginEditorLib/pluginEditor.cpp,
	// Editor::onDisclaimerFinished). The editor's constructor raises it through this same route every time a window is made,
	// with no way to dismiss it. This notice is the editors' word on it, so a window's sink drops that one
	// (PageEditor::create; no edit of upstream's file: doc/modern-ux/UPSTREAM.md). Matched by its title: if upstream
	// words it differently, both show, which is untidy and harmless.
	inline bool isUpstreamWarning(const std::string& _title)
	{
		return _title.find(" - Rosetta detected") != std::string::npos;
	}

	inline bool dismissed(juce::PropertiesFile& _config)
	{
		return _config.getBoolValue(g_dismissedKey, false);
	}

	inline void dismiss(juce::PropertiesFile& _config)
	{
		_config.setValue(g_dismissedKey, true);
		_config.saveIfNeeded();
	}

	// The notice to offer now, or none: the editor does not run translated, the user said "Don't show again", or
	// _offered says this session has had it (the caller's one flag: it is taken only when there is a notice to show).
	// Only "Don't show again" is kept; OK, and the dialog closed another way (the last button answers), keep nothing,
	// so the notice comes back with the next session. _config outlives the notice: it is the plug-in's own, and the
	// route drops a notice whose plug-in is gone (messageRoute::forget).
	inline std::optional<genericUI::messageRoute::Notice> make(const processArch::ProcessArch& _arch, const processArch::Os _os,
		const bool _standalone, juce::PropertiesFile& _config, std::atomic<bool>& _offered)
	{
		const auto notice = processArch::translatedNotice(_arch, _os, _standalone);
		if(!notice || dismissed(_config) || _offered.exchange(true))
			return std::nullopt;
		return genericUI::messageRoute::Notice{notice->title, notice->text, notice->buttons,
			[&_config, dontShowAgain = notice->dontShowAgain](const int _button)
			{
				if(_button >= 0 && static_cast<size_t>(_button) == dontShowAgain)
					dismiss(_config);
			}};
	}
}
