#pragma once

#include "jucePluginEditorLib/pluginEditorState.h"

#include "mdEditorMenu.h"

#include <vector>

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;

	class PluginEditorState : public jucePluginEditorLib::PluginEditorState
	{
	public:
		explicit PluginEditorState(AudioPluginAudioProcessor& _processor);

		// I-008: the editor's menu as data (mdEditorMenu.h): the page draws it; the menu bar and the native fallback
		// (no page up) show the same tree as a native menu (fillMenu).
		editorMenu::Menu menu();
		void fillMenu(juceRmlUi::Menu& _menu) override;

	private:
		jucePluginEditorLib::Editor* createEditor(const jucePluginEditorLib::Skin& _skin) override;
		void addSettingsEntry(juceRmlUi::Menu& _menu) override;
		editorMenu::Item windowSizeMenu();
		void addSysexEntries(std::vector<editorMenu::Item>& _items);
		static void toRml(const std::vector<editorMenu::Item>& _items, juceRmlUi::Menu& _menu);
	};
}
