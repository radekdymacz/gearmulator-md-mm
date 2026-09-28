#pragma once

#include "jucePluginEditorLib/pluginEditor.h"

#include "elektronData/json.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <memory>
#include <string>

namespace mdJucePlugin
{
	class AudioMidiLink;
	class DeskSession;
	class Diagnostics;
	class WebPageHost;

	// The Machinedrum or Monomachine Editor window (P6: one view class for both): the product's
	// page (skins/mdStudio or skins/mmStudio) in a web view, attached to the processor's
	// DeskSession. The view owns only what belongs to the window: the page host, the editor's
	// menu, the standalone's AUDIO / MIDI panel and the diagnostics. Documents, undo, the engine
	// choice and the modulators are the session's, so they outlive the window.
	class PageEditor final : public jucePluginEditorLib::Editor, juce::Timer
	{
	public:
		PageEditor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin);
		~PageEditor() override;

		PageEditor(const PageEditor&) = delete;
		PageEditor& operator=(const PageEditor&) = delete;

		void create() override;
		std::pair<std::string, std::string> getDemoRestrictionText() const override { return {}; }
		// Audio > Audio/MIDI Settings... in the menu bar opens the page's own panel.
		bool openAudioMidiSettings() override;

	private:
		void timerCallback() override;
		void onPageMessage(const elektronData::json::Value& _message);
		void layout() const;

		DeskSession* m_session = nullptr;
		std::unique_ptr<WebPageHost> m_page;
		std::unique_ptr<AudioMidiLink> m_audio;
		std::unique_ptr<Diagnostics> m_diagnostics;
	};
}
