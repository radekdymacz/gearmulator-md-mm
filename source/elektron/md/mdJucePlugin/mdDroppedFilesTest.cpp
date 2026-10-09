// Files dropped on the window (mdDroppedFiles.h): what each is by its extension in any case, whether the window takes a
// drag, the drop message and the drag message as both editors' contracts declare them, the window's commands for a
// dropped file as the contracts take them (dropRom, dropSyx, dropSample), and the book of the last drop's files: each
// serves once, as the kind it was dropped as, until the next drop or the time without use. Pure.
#include "mdDroppedFiles.h"

#include "elektronData/jsonSchema.h"

#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	int g_failures = 0;

	void check(const bool _ok, const std::string& _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++g_failures;
	}

	namespace json = elektronData::json;
	namespace drop = mdJucePlugin::droppedFiles;

	std::string kinds(const std::vector<std::string>& _paths)
	{
		std::string out;
		for(const auto& it : drop::classify(_paths))
			out += (out.empty() ? "" : " ") + std::string(drop::kindName(it.kind));
		return out;
	}

	std::optional<json::Value> load(const char* _path)
	{
		std::ifstream in(_path);
		std::stringstream text;
		text << in.rdbuf();
		std::string error;
		return json::parse(text.str(), &error);
	}

	json::Value command(const std::string& _op, const int _drop, const int _n, const int _slot = -1)
	{
		auto c = json::Value::object();
		c.set("op", _op);
		c.set("id", 7);
		c.set("drop", _drop);
		c.set("n", _n);
		if(_slot >= 0)
			c.set("slot", _slot);
		return c;
	}
}

int main()
{
	std::printf("mdDroppedFilesTest\n");

	// ---- what each file is ----
	check(kinds({"/a/OS163.bin", "/a/rom.ZIP", "/a/kits.syx", "/a/K.SYX", "/a/kick.wav", "/a/snare.WAVE", "/a/hat.aif",
		"/a/tom.AIFF", "/a/c.aifc", "/a/notes.txt", "/a/noext", "/a/.syx", "C:\\x\\y.Wav"})
		== "rom rom sysex sysex sample sample sample sample sample unknown unknown unknown sample",
		"by the extension in any case: .bin .zip ROM, .syx SysEx, .wav .wave .aif .aiff .aifc sample; none, a dot file "
		"or another: unknown");
	const auto items = drop::classify({"/Users/me/Music/kick.wav", "/Users/me/x/Kits.syx"});
	check(items.size() == 2 && items[0].n == 0 && items[1].n == 1 && items[0].name == "kick.wav"
		&& items[1].name == "Kits.syx",
		"each item has its number in the drop and its file name, never its folder");
	check(drop::accepts({"/a/notes.txt", "/a/kick.wav"}) && !drop::accepts({"/a/notes.txt", "/a/b.pdf"})
		&& !drop::accepts({}),
		"a drag is taken when one file at least is a kind the window knows");

	// ---- the messages and commands against both contracts (what the plug-in writes: closed again) ----
	const auto message = drop::dropMessage(3, drop::classify({"/a/OS.bin", "/a/k.syx", "/a/s.wav", "/a/n.txt"}), 120.5,
		48.25);
	const auto* list = message.find("items");
	check(message.find("type")->asString() == "drop" && message.find("drop")->asNumber() == 3 && list
		&& list->asArray().size() == 4
		&& list->asArray()[3].find("kind")->asString() == "unknown" && list->asArray()[2].find("n")->asNumber() == 2
		&& message.find("x")->asNumber() == 120.5 && message.find("y")->asNumber() == 48.25,
		"the drop message: its number, each file's number, kind and name, and where (the page's CSS pixels)");
	for(const auto* path : {MDDESK_SCHEMA, MMDESK_SCHEMA})
	{
		const auto root = load(path);
		check(root.has_value(), std::string("the contract reads: ") + path);
		if(!root)
			continue;
		const json::Schema schema(json::Schema::closedForWriter(*root));
		const std::string which = std::string(path).find("mm-data") != std::string::npos ? "MM" : "MD";
		const auto problems = schema.validate(message, "message");
		check(problems.empty(), which + ": the drop message is on the contract"
			+ (problems.empty() ? "" : " (" + problems.front() + ")"));
		for(const bool on : {true, false})
			check(schema.validate(drop::dragMessage(on), "message").empty(), which + ": the dragFiles message ("
				+ (on ? "over" : "left") + ") is on the contract");
		check(schema.validate(command("dropRom", 3, 0), "command").empty()
			&& schema.validate(command("dropSyx", 3, 1), "command").empty()
			&& schema.validate(command("dropSample", 3, 2, 47), "command").empty(),
			which + ": dropRom {drop, n}, dropSyx {drop, n} and dropSample {drop, n, slot} are its commands");
		check(!schema.validate(command("dropSample", 3, 2), "command").empty()
			&& !schema.validate(command("dropSample", 3, 2, 48), "command").empty()
			&& !schema.validate(command("dropRom", 0, 0), "command").empty(),
			which + ": a sample needs a ROM slot 0-47, a drop's number starts at 1");
		auto pathInMessage = message;
		pathInMessage.set("path", "/a/k.syx");
		check(!schema.validate(pathInMessage, "message").empty(), which
			+ ": the drop message carries no path (closed for the writer)");
		auto withPath = command("dropSyx", 3, 1);
		withPath.set("path", "/etc/passwd");
		check(!schema.validate(withPath, "command").empty(), which
			+ ": a command naming a path is refused (the page names a file by its number)");
	}

	// ---- the book of the last drop ----
	drop::DropBook book;
	double now = 1000;
	const int first = book.add({"/a/OS.bin", "/a/k.syx", "/a/s1.wav", "/a/s2.wav", "/a/n.txt"}, now);
	check(first == 1 && book.current() == 1 && book.held() == 5, "a drop is numbered from 1 and holds its files");
	check(!book.take(first, 0, drop::Kind::Sample, now),
		"a file is used only as the kind it was dropped as (a .bin is no sample)");
	check(!book.take(first, 4, drop::Kind::Sysex, now) && !book.take(first, 4, drop::Kind::Sample, now),
		"an unknown file serves no command");
	const auto rom = book.take(first, 0, drop::Kind::Rom, now);
	check(rom && *rom == "/a/OS.bin", "dropRom's file: the ROM's path");
	check(!book.take(first, 0, drop::Kind::Rom, now), "a file serves once (a repeated command acts on nothing)");
	check(!book.take(first, 9, drop::Kind::Sample, now) && !book.take(first + 1, 2, drop::Kind::Sample, now),
		"a number the drop does not have, or another drop's: nothing");
	now += drop::DropBook::g_keepMs - 1;
	check(book.take(first, 2, drop::Kind::Sample, now) == std::optional<std::string>("/a/s1.wav"),
		"a sample a while later (each use keeps the drop)");
	now += drop::DropBook::g_keepMs - 1;
	check(book.take(first, 3, drop::Kind::Sample, now) == std::optional<std::string>("/a/s2.wav"),
		"the next one as long again after it");
	now += drop::DropBook::g_keepMs + 1;
	check(!book.take(first, 1, drop::Kind::Sysex, now), "after the time without use the drop is forgotten");
	const int second = book.add({"/b/new.syx"}, now);
	check(second == 2 && !book.take(first, 1, drop::Kind::Sysex, now)
		&& book.take(second, 0, drop::Kind::Sysex, now) == std::optional<std::string>("/b/new.syx"),
		"a new drop forgets the earlier one's files; its own serve");
	book.add({"/c/a.syx"}, now);
	book.clear();
	check(book.held() == 0 && !book.take(3, 0, drop::Kind::Sysex, now), "clear: nothing held");

	std::printf(g_failures ? "%d failure(s)\n" : "mdDroppedFilesTest: all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
