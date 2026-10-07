#pragma once

#include "jucePluginEditorLib/editorTraits.h"
#include "jucePluginEditorLib/pluginEditor.h"

#include "elektronData/json.h"
#include "juceUiLib/messageRoute.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace juceRmlUi
{
	class Menu;
}

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

	class PageEditor final : public jucePluginEditorLib::Editor, public jucePluginEditorLib::FreeSizeEditor,
		public jucePluginEditorLib::AudioMidiSettingsEditor, juce::Timer
	{
	public:
		PageEditor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin);
		~PageEditor() override;

		PageEditor(const PageEditor&) = delete;
		PageEditor& operator=(const PageEditor&) = delete;

		void create() override;
		std::pair<std::string, std::string> getDemoRestrictionText() const override { return {}; }
		// Audio > Audio/MIDI Settings... in the menu bar opens the page's own panel. The page lays
		// itself out in any window size (P7, FreeSizeEditor): the window resizes freely.
		bool openAudioMidiSettings() override;

		// B-001: the page's zoom in the editor's menu (the page's right-click menu, the standalone's menu bar):
		// smaller, larger, 100 % and the steps. Cmd - / Cmd + / Cmd 0 on the page do the same (pageZoom).
		void fillZoomMenu(juceRmlUi::Menu& _menu);

	private:
		// The page's zoom one step smaller (-1), larger (1) or back to 100 % (0), or _zoom itself (step 2),
		// remembered in the editor's config for every window (mdPageZoom.h).
		void setZoom(int _step, double _zoom = 1.0);
		void timerCallback() override;
		void onPageMessage(const elektronData::json::Value& _message);
		void chooseRom();
		void chooseSyx(bool _save);
		void chooseSample(uint8_t _slot);
		void layout() const;

		DeskSession* m_session = nullptr;
		std::unique_ptr<WebPageHost> m_page;
		std::unique_ptr<AudioMidiLink> m_audio;
		std::unique_ptr<Diagnostics> m_diagnostics;
		std::unique_ptr<juce::FileChooser> m_chooser;
		int m_noticeId = 0;
		std::shared_ptr<int> m_alive = std::make_shared<int>(0);	// callbacks that outlive the window check it
		std::map<int, std::function<void(int)>> m_notices;	// the plug-in's questions the page has not answered yet
		genericUI::messageRoute::Attachment m_noticeRoute;	// this window's sink for its instance's notices (messageRoute.h)
		// This window's instance as the notice route knows it (the AudioPluginAudioProcessor's address, the same
		// pointer the processor's own OwnerScope uses). Every entry point here runs in an OwnerScope of it, so a
		// notice it raises goes to this window, not the newest one.
		const void* m_noticeOwner = nullptr;
	};
}
