#include "mdEditorPages.h"

#include "mdPageEditor.h"
#include "mdPluginProcessor.h"

#include "juce_events/juce_events.h"

#include <string_view>

namespace mdJucePlugin
{
	namespace
	{
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

		bool isMonomachine(const jucePluginEditorLib::Processor& _processor)
		{
			const auto* p = dynamic_cast<const AudioPluginAudioProcessor*>(&_processor);
			return p && p->getModel() == md::MachineModel::Monomachine;
		}

#if MDMM_DIAGNOSTICS
		// GEARMULATOR_MDSTUDIO_SELFTEST=p5skin: switch skins live through the same call the
		// Editor > Skins menu makes, and log the editor each time (P5). Diagnostics builds only.
		void startSkinSelfTest(jucePluginEditorLib::PluginEditorState& _state)
		{
			if(juce::SystemStats::getEnvironmentVariable("GEARMULATOR_MDSTUDIO_SELFTEST", {}) != "p5skin")
				return;
			auto* state = &_state;
			const auto logLine = [](const juce::String& _l)
			{
				juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gearmulator-mdStudio.log")
					.appendText(juce::Time::getCurrentTime().toString(false, true, true, true) + " P5: " + _l + "\n");
			};
			const auto report = [state, logLine](const juce::String& _what)
			{
				auto* e = state->getEditor();
				auto* ui = state->getUiRoot();
				logLine(_what + ": current skin \"" + juce::String(state->getCurrentSkin().displayName) + "\" (" + juce::String(state->getCurrentSkin().filename)
					+ "), editor " + (dynamic_cast<PageEditor*>(e) ? "Machinedrum Editor page" : e ? "panel" : "none")
					+ (ui ? ", " + juce::String(ui->getWidth()) + " x " + juce::String(ui->getHeight()) : juce::String()));
			};
			juce::String names;
			for(const auto& skin : state->getIncludedSkins())
				names << "\"" << skin.displayName << "\" ";
			logLine("Editor > Skins lists: " + names);
			// The editor page re-made twice, as a settings change or a host reopening it does.
			juce::Timer::callAfterDelay(20000, [state, report] { report("at start"); state->loadSkin(state->getIncludedSkins()[0]);
				juce::Timer::callAfterDelay(6000, [state, report] { report("re-made once"); state->loadSkin(state->getIncludedSkins()[0]);
					juce::Timer::callAfterDelay(8000, [report] { report("re-made twice"); }); }); });
		}
#endif
	}

	std::vector<jucePluginEditorLib::Skin> editorPageSkins(const md::MachineModel _model)
	{
		if(_model == md::MachineModel::Monomachine)
			return {{"Monomachine Editor", "mmStudio.rml", "", {"mmStudio.rml", "mmStudio.html"}}};
		return {{"Machinedrum Editor", "mdStudio.rml", "", {"mdStudio.rml", "mdStudio.html"}}};
	}

	void keepEditorPage(jucePluginEditorLib::PluginEditorState& _state)
	{
#if MDMM_DIAGNOSTICS
		startSkinSelfTest(_state);
#endif
		const auto& page = _state.getIncludedSkins().front();
		const auto configured = _state.readSkinFromConfig();
		if(configured.displayName != page.displayName || configured.filename != page.filename || !configured.folder.empty())
			_state.writeSkinToConfig(page);
	}

	jucePluginEditorLib::Editor* createEditorPage(jucePluginEditorLib::PluginEditorState& _state,
		jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin)
	{
		const auto& page = _state.getIncludedSkins().front();
		const bool own = isMonomachine(_processor) ? isMmStudioSkin(_skin.displayName, _skin.filename) : isStudioSkin(_skin.displayName, _skin.filename);
		return new PageEditor(_processor, own ? _skin : page);
	}
}
