// I-008: the editor's menu as data (mdEditorMenu.h): the page's editorMenu message carries every entry of the tree,
// in order, with its kind; each entry that can run has a number n that picks its action (menuPick), and none other
// has one; the title is not an entry. Pure.
#include "mdEditorMenu.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
	int g_failures = 0;

	void check(const bool _ok, const char* _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what);
		if(!_ok)
			++g_failures;
	}

	namespace json = elektronData::json;
	namespace menu = mdJucePlugin::editorMenu;

	const json::Value* at(const json::Value& _items, const size_t _i)
	{
		return _i < _items.asArray().size() ? &_items.asArray()[_i] : nullptr;
	}

	std::string str(const json::Value* _v, const char* _key)
	{
		const auto* f = _v ? _v->find(_key) : nullptr;
		return f ? f->asString() : std::string();
	}
}

int main()
{
	std::printf("mdEditorMenuTest\n");
	std::string ran;
	menu::Menu m;
	m.title = "Machinedrum Editor 0.3.5";
	m.items.push_back(menu::submenu("zoom", "Zoom", {
		menu::action("zoom-in", "Zoom In", [&] { ran += "in;"; }, "Cmd +"),
		menu::action("zoom-out", "Zoom Out", [&] { ran += "out;"; }, "Cmd -", false, false),
		menu::separator(),
		menu::action("zoom-100", "100 %", [&] { ran += "100;"; }, {}, true),
		menu::submenu("window-size", "Window Size", {menu::action("window-100", "100 %", [&] { ran += "w100;"; })})}));
	m.items.push_back(menu::separator());
	m.items.push_back(menu::action("open-log-folder", "Open Log Folder", [&] { ran += "log;"; }));
	m.items.push_back(menu::submenu("developer", "Developer", {menu::heading("RAM recording"), menu::note("idle"),
		menu::action("none", "No action", {})}));

	std::vector<std::function<void()>> actions{[] {}};
	const auto j = menu::toJson(m, actions);
	check(str(&j, "title") == "Machinedrum Editor 0.3.5", "the title is the message's title");
	const auto& items = *j.find("items");
	check(items.asArray().size() == 4, "the top level's four entries, the title not among them");
	check(actions.size() == 4, "four entries can run (Zoom In, 100 %, the window's 100 %, Open Log Folder): the old actions cleared");

	const auto* zoom = at(items, 0);
	check(str(zoom, "kind") == "submenu" && str(zoom, "id") == "zoom" && zoom->find("items"), "Zoom is a submenu with its entries");
	const auto& zi = *zoom->find("items");
	check(str(at(zi, 0), "key") == "Cmd +" && at(zi, 0)->find("n") && at(zi, 0)->find("enabled")->asBool(), "Zoom In: its key and a number");
	check(!at(zi, 1)->find("n") && !at(zi, 1)->find("enabled")->asBool(), "Zoom Out disabled: no number, so the page cannot run it");
	check(str(at(zi, 2), "kind") == "separator" && !at(zi, 2)->find("label"), "a separator");
	check(at(zi, 3)->find("checked") && at(zi, 3)->find("checked")->asBool(), "the step shown is checked");
	check(str(at(zi, 4), "kind") == "submenu" && at(zi, 4)->find("items")->asArray().size() == 1, "a submenu in a submenu");

	// every number runs its own entry's action, in the tree's order
	for(const auto* id : {"zoom-in", "zoom-100", "window-100", "open-log-folder"})
	{
		const json::Value* found = nullptr;
		const std::function<void(const json::Value&)> walk = [&](const json::Value& _list)
		{
			for(const auto& v : _list.asArray())
			{
				if(str(&v, "id") == id)
					found = &v;
				if(const auto* sub = v.find("items"))
					walk(*sub);
			}
		};
		walk(items);
		if(found && found->find("n"))
			actions[static_cast<size_t>(found->find("n")->asNumber())]();
	}
	check(ran == "in;100;w100;log;", ("each number runs its entry: " + ran).c_str());

	const auto& dev = *at(items, 3)->find("items");
	check(str(at(dev, 0), "kind") == "heading" && str(at(dev, 1), "kind") == "note" && !at(dev, 1)->find("n"), "a heading and a note are not chosen");
	check(!at(dev, 2)->find("n"), "an entry without an action has no number");
	check(menu::find(m.items, "window-100") && menu::find(m.items, "window-100")->label == "100 %", "find: an entry anywhere in the tree");
	check(!menu::find(m.items, "nothing"), "find: none");

	std::printf(g_failures ? "mdEditorMenuTest: FAIL (%d)\n" : "mdEditorMenuTest: PASS\n", g_failures);
	return g_failures ? 1 : 0;
}
