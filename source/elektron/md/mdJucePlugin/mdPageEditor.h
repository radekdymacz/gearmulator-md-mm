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
	class DropZone;

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
		// The page lays itself out in any window size (P7): the window resizes freely.
		bool keepsAspectRatio() const override { return false; }

		// P7: a firmware file (.bin, or a .zip with it) dropped anywhere on the window installs it
		// (DeskSession::installRom); the web view passes drops on to the editor's drop zone around it.
		static bool wantsFile(const juce::File& _file);

	private:
		void timerCallback() override;
		void onPageMessage(const elektronData::json::Value& _message);
		void chooseRom();
		void chooseSyx(bool _save);
		void openFile(const juce::File& _file);
		void layout() const;

		DeskSession* m_session = nullptr;
		std::unique_ptr<WebPageHost> m_page;
		std::unique_ptr<AudioMidiLink> m_audio;
		std::unique_ptr<Diagnostics> m_diagnostics;
		std::unique_ptr<juce::FileChooser> m_chooser;
		std::unique_ptr<DropZone> m_dropZone;
	};
}
