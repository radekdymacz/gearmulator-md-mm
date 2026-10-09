#pragma once

// I-008: the editor's menu (a right-click on the page's header, the standalone's menu bar) as data, one tree for every
// place that shows it. The window builds it (mdPluginEditorState.cpp) and sends it to the page as an editorMenu message
// (toJson): the page draws it in its own style (skins/shared/deskMenu.js) and answers with the entry chosen (menuPick),
// which the window runs. Where no page is up (it failed to start, no web view) and in the menu bar the same tree is a
// native menu (toRml, then jucePluginEditorLib::toPopupMenu). Pure: no JUCE; mdEditorMenuTest.

#include "elektronData/json.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace mdJucePlugin::editorMenu
{
	struct Item
	{
		enum class Kind
		{
			Action,		// runs its action
			Submenu,	// opens its items
			Separator,
			Heading,	// a section's name inside a menu (not chosen)
			Note		// a line of information (a version, a status), not chosen
		};

		Kind kind = Kind::Action;
		std::string id;			// stable, for the page's journeys and tests ("zoom-in", "open-log-folder")
		std::string label;
		std::string key;		// the shortcut's words, shown at the right ("Cmd +"); empty: none
		bool enabled = true;
		bool checked = false;
		std::function<void()> action;
		std::vector<Item> items;	// a submenu's
	};

	struct Menu
	{
		std::string title;		// the first line: which editor and its version, not an entry
		std::vector<Item> items;
	};

	inline Item action(std::string _id, std::string _label, std::function<void()> _action, std::string _key = {},
		const bool _checked = false, const bool _enabled = true)
	{
		Item i;
		i.id = std::move(_id);
		i.label = std::move(_label);
		i.action = std::move(_action);
		i.key = std::move(_key);
		i.checked = _checked;
		i.enabled = _enabled;
		return i;
	}

	inline Item submenu(std::string _id, std::string _label, std::vector<Item> _items)
	{
		Item i;
		i.kind = Item::Kind::Submenu;
		i.id = std::move(_id);
		i.label = std::move(_label);
		i.items = std::move(_items);
		return i;
	}

	inline Item separator()
	{
		Item i;
		i.kind = Item::Kind::Separator;
		return i;
	}

	inline Item heading(std::string _label)
	{
		Item i;
		i.kind = Item::Kind::Heading;
		i.label = std::move(_label);
		return i;
	}

	inline Item note(std::string _label)
	{
		Item i;
		i.kind = Item::Kind::Note;
		i.label = std::move(_label);
		return i;
	}

	// The first item with this id, anywhere in the tree; nullptr: none.
	inline const Item* find(const std::vector<Item>& _items, const std::string& _id)
	{
		for(const auto& i : _items)
		{
			if(i.id == _id)
				return &i;
			if(const auto* sub = find(i.items, _id))
				return sub;
		}
		return nullptr;
	}

	namespace detail
	{
		inline const char* kindName(const Item::Kind _k)
		{
			switch(_k)
			{
			case Item::Kind::Submenu: return "submenu";
			case Item::Kind::Separator: return "separator";
			case Item::Kind::Heading: return "heading";
			case Item::Kind::Note: return "note";
			case Item::Kind::Action: break;
			}
			return "action";
		}

		inline elektronData::json::Value items(const std::vector<Item>& _items, std::vector<std::function<void()>>& _actions)
		{
			using elektronData::json::Value;
			auto out = Value::array();
			for(const auto& i : _items)
			{
				auto v = Value::object();
				v.set("kind", kindName(i.kind));
				if(!i.id.empty())
					v.set("id", i.id);
				if(i.kind != Item::Kind::Separator)
					v.set("label", i.label);
				if(!i.key.empty())
					v.set("key", i.key);
				if(i.kind == Item::Kind::Action)
				{
					const bool can = i.enabled && i.action != nullptr;
					v.set("enabled", can);
					if(i.checked)
						v.set("checked", true);
					if(can)
					{
						v.set("n", static_cast<double>(_actions.size()));
						_actions.push_back(i.action);
					}
				}
				else if(i.kind == Item::Kind::Submenu)
				{
					v.set("enabled", i.enabled && !i.items.empty());
					v.set("items", items(i.items, _actions));
				}
				out.push(std::move(v));
			}
			return out;
		}
	}

	// The menu as the page's editorMenu message's title and items: each entry that can run has a number n, its index
	// in _actions (the page answers menuPick with it). _actions is cleared first.
	inline elektronData::json::Value toJson(const Menu& _menu, std::vector<std::function<void()>>& _actions)
	{
		_actions.clear();
		auto out = elektronData::json::Value::object();
		out.set("title", _menu.title);
		out.set("items", detail::items(_menu.items, _actions));
		return out;
	}
}
