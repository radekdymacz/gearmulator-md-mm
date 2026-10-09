#pragma once

#include "jucePluginEditorLib/editorTraits.h"
#include "jucePluginEditorLib/pluginEditor.h"

#include "elektronData/json.h"
#include "juceUiLib/messageRoute.h"
#include "mdEditorMenu.h"
#include "mdNoticeBook.h"
#include "mdUpdater.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

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

		// B-001: the page's zoom in the editor's menu (mdEditorMenu.h, the page's right-click menu, the standalone's
		// menu bar): smaller, larger, 100 % and the steps, then _windowSize (the window's size, upstream's GUI Scale).
		// Cmd - / Cmd + / Cmd 0 on the page do the same (pageZoom).
		editorMenu::Item zoomMenu(editorMenu::Item _windowSize);
		// I-005: Updates (Check for Updates Now, Check Daily) in the same menus (DESIGN-updates.md).
		editorMenu::Item updateMenu();

	private:
		// The page's zoom one step smaller (-1), larger (1) or back to 100 % (0), or _zoom itself (step 2),
		// remembered in the editor's config for every window (mdPageZoom.h).
		void setZoom(int _step, double _zoom = 1.0);
		// I-008: the editor's menu: sent to the page to draw (editorMenu), or native when no page is up; then the
		// entry the page chose (menuPick) of the menu numbered _menu.
		void openMenu();
		void pickMenu(int _menu, size_t _n);
		void timerCallback() override;
		void onPageMessage(const elektronData::json::Value& _message);
		void chooseRom();
		void chooseSyx(bool _save);
		void chooseSample(uint8_t _slot);
		void layout() const;
		// The update banner (DESIGN-updates.md 4): the Updater's state as a non-modal notice, sent when it changes.
		void showUpdateBanner();
		// The notice for an editor that runs translated (Rosetta), once the page is up: its dialog, never twice in a
		// process, and never again once the user chose "Don't show again" (mdRosettaNotice.h).
		void offerRosettaNotice();

		DeskSession* m_session = nullptr;
		std::unique_ptr<WebPageHost> m_page;
		std::unique_ptr<AudioMidiLink> m_audio;
		std::unique_ptr<Diagnostics> m_diagnostics;
		std::unique_ptr<juce::FileChooser> m_chooser;
		std::shared_ptr<int> m_alive = std::make_shared<int>(0);	// callbacks that outlive the window check it
		NoticeBook m_notices;	// the plug-in's questions and the banner the page has not answered yet (their numbers)
		genericUI::messageRoute::Attachment m_noticeRoute;	// this window's sink for its instance's notices (messageRoute.h)
		// This window's instance as the notice route knows it (the AudioPluginAudioProcessor's address, the same
		// pointer the processor's own OwnerScope uses). Every entry point here runs in an OwnerScope of it, so a
		// notice it raises goes to this window, not the newest one.
		const void* m_noticeOwner = nullptr;
		juce::SharedResourcePointer<updates::Updater> m_updater;	// one a process, shared by every window
		int m_updateToken = 0;
		int m_bannerId = 0;				// the notice number of the banner shown, 0: none
		std::string m_bannerShown;		// what it says (sent again only when that changes)
		juce::int64 m_nextUpdatePoll = 0;
		bool m_rosettaOffered = false;	// offerRosettaNotice ran for this window
		int m_menuSerial = 0;							// the editor's menu last sent to the page (editorMenu's menu)
		std::vector<std::function<void()>> m_menuActions;	// its entries' actions, by their n
	};
}
