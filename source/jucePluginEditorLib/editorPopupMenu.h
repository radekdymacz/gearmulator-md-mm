#pragma once

#include <functional>
#include <memory>
#include <string>

#include "pluginEditorState.h"

#include "juceRmlUi/rmlMenu.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jucePluginEditorLib
{
	// A juceRmlUi::Menu as a native menu (the standalone's menu bar, a context menu that is not in
	// RmlUi, such as a web view page's). Actions run after the menu closed, on the message thread.
	inline juce::PopupMenu toPopupMenu(const juceRmlUi::Menu& _menu)
	{
		juce::PopupMenu menu;
		_menu.forEachEntry([&menu](const std::string& _name, const bool _checked, const bool _separator, const bool _enabled,
			const std::function<void()>& _action, const std::shared_ptr<juceRmlUi::Menu>& _submenu)
		{
			if(_separator)
			{
				menu.addSeparator();
			}
			else if(_submenu)
			{
				menu.addSubMenu(juce::String::fromUTF8(_name.c_str()), toPopupMenu(*_submenu), _enabled);
			}
			else
			{
				juce::PopupMenu::Item item(juce::String::fromUTF8(_name.c_str()));
				item.setEnabled(_enabled && _action != nullptr);
				item.setTicked(_checked);
				if(_action)
					item.setAction([action = _action] { action(); });
				menu.addItem(std::move(item));
			}
		});
		return menu;
	}

	// The editor's menu (GUI scale, regions, the plug-in's own entries, Settings) as a native menu.
	inline juce::PopupMenu createPopupMenu(PluginEditorState& _state)
	{
		juceRmlUi::Menu menu;
		_state.fillMenu(menu);
		return toPopupMenu(menu);
	}
}
