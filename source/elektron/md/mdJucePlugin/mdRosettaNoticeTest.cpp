// Rosetta (mdRosettaNotice.h): the notice a translated editor offers the page, through the same route and notice book
// as every plug-in notice (juceUiLib/messageRoute.h, mdNoticeBook.h). Checks the shape the page gets (title, text,
// buttons), the rule of one a session, that only "Don't show again" is kept (in a real config file, read back by the
// next session), and that OK, the dialog closed another way (the page answers with the last button) and a button the
// notice does not have keep nothing.
#include "mdNoticeBook.h"
#include "mdRosettaNotice.h"

#include <cstdio>
#include <memory>
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

	namespace route = genericUI::messageRoute;
	using namespace mdJucePlugin;

	std::unique_ptr<juce::PropertiesFile> openConfig(const juce::File& _file)
	{
		return std::make_unique<juce::PropertiesFile>(_file, juce::PropertiesFile::Options());
	}

	// A window: takes the notices of its instance into a notice book, as PageEditor does, and the page's answers.
	struct Window
	{
		explicit Window(const void* _owner)
			: owner(_owner), attachment(route::attach(_owner, [this](route::Notice _n)
			{
				shown.push_back({_n.title, _n.text, _n.buttons});
				ids.push_back(book.add(_n.buttons.size(), std::move(_n.answered)));
			})) {}
		// The page's answer to the last notice it was given.
		std::string press(const int _button) { return book.answer(ids.back(), _button); }

		struct Shown { std::string title, text; std::vector<std::string> buttons; };
		const void* owner;
		NoticeBook book;
		std::vector<Shown> shown;
		std::vector<int> ids;
		route::Attachment attachment;
	};

	bool offerTo(const void* _owner, std::optional<route::Notice> _notice)
	{
		if(!_notice)
			return false;
		const route::OwnerScope scope(_owner);
		return route::offer(std::move(*_notice));
	}
}

int main()
{
	using processArch::Os;
	const processArch::ProcessArch rosetta{"x86_64", "arm64", true};
	const processArch::ProcessArch native{"arm64", "arm64", false};

	const auto dir = juce::File::createTempFile("mdRosettaNoticeTest");
	dir.createDirectory();
	const auto file = dir.getChildFile("config.settings");
	auto config = openConfig(file);
	route::enable();
	int instance = 0;
	Window window(&instance);

	// upstream's own warning is recognised, ours and the others are not
	check(rosettaNotice::isUpstreamWarning("Machinedrum Editor - Rosetta detected") && rosettaNotice::isUpstreamWarning("Monomachine Editor - Rosetta detected"),
		"upstream's warning (\"<product> - Rosetta detected\") is told apart, to be replaced by this notice");
	check(!rosettaNotice::isUpstreamWarning("Running under Rosetta") && !rosettaNotice::isUpstreamWarning("Running in emulation")
		&& !rosettaNotice::isUpstreamWarning("Overwrite K05?") && !rosettaNotice::isUpstreamWarning(""), "...and none of this notice's titles, nor another notice, is taken for it");

	// nothing to say about a native run, and it does not use up the session's one notice
	{
		std::atomic<bool> offered{false};
		check(!rosettaNotice::make(native, Os::Mac, false, *config, offered) && !offered.load(), "a native run: no notice, the session's one is still there");
	}

	// a translated run: one notice, once a session
	std::atomic<bool> offered{false};
	{
		auto notice = rosettaNotice::make(rosetta, Os::Mac, false, *config, offered);
		check(notice && offered.load(), "an editor running under Rosetta: a notice, and the session's one is taken");
		check(!rosettaNotice::make(rosetta, Os::Mac, false, *config, offered), "a second window of the session: none");
		check(offerTo(&instance, std::move(notice)) && window.shown.size() == 1, "the route takes it to its instance's window");
		const auto& n = window.shown.back();
		check(n.title == "Running under Rosetta" && n.text.find("Open your DAW as an Apple silicon app") != std::string::npos,
			"the page gets the title and the text");
		check(n.buttons.size() == 2 && n.buttons[0] == "Don't show again" && n.buttons[1] == "OK", "and two buttons, OK last");
		check(window.book.waiting(window.ids.back()), "the notice waits for an answer in the window's book");

		// OK keeps nothing; neither does the page closing the dialog another way (it answers with the last button)
		check(window.press(static_cast<int>(n.buttons.size()) - 1).empty(), "OK, or the dialog closed (Esc, a click outside): answered");
		check(!config->containsKey(rosettaNotice::g_dismissedKey) && !rosettaNotice::dismissed(*config), "...and nothing is kept");
		check(window.press(0) == "notice " + std::to_string(window.ids.back()) + " is not waiting for an answer", "a notice answers once");
		check(!rosettaNotice::dismissed(*config), "...the second answer kept nothing either");
	}

	// the next session: the notice is back; a button it does not have keeps nothing; Don't show again is kept
	{
		std::atomic<bool> nextSession{false};
		check(offerTo(&instance, rosettaNotice::make(rosetta, Os::Mac, false, *config, nextSession)) && window.shown.size() == 2,
			"the next session (OK keeps nothing): the notice comes back");
		check(!window.press(2).empty() && !window.press(-1).empty() && !rosettaNotice::dismissed(*config) && window.book.waiting(window.ids.back()),
			"a button the notice does not have: refused, nothing kept, the notice still waits");
		check(window.press(0).empty(), "Don't show again: answered");
		check(rosettaNotice::dismissed(*config) && config->getBoolValue(rosettaNotice::g_dismissedKey, false), "...and kept in the config");
	}

	// and it stays kept: written to the file, read by the next session, which neither shows it nor uses its one up
	{
		config.reset();
		check(file.existsAsFile(), "the config file is written");
		config = openConfig(file);
		check(rosettaNotice::dismissed(*config), "read back from the file by the next session");
		std::atomic<bool> nextSession{false};
		check(!rosettaNotice::make(rosetta, Os::Mac, false, *config, nextSession) && !nextSession.load(),
			"Don't show again: no notice, and the session's one is still free");
	}

	// Windows on Arm: the same notice in its own words; the same key and rule
	{
		auto fresh = openConfig(dir.getChildFile("windows.settings"));
		std::atomic<bool> session{false};
		const size_t before = window.shown.size();
		check(offerTo(&instance, rosettaNotice::make(rosetta, Os::Windows, false, *fresh, session)) && window.shown.size() == before + 1
			&& window.shown.back().title == "Running in emulation" && window.shown.back().text.find("Windows on Arm") != std::string::npos
			&& window.shown.back().buttons.size() == 2, "Windows on Arm: the notice in the Windows words, the same buttons");
		check(window.press(0).empty() && rosettaNotice::dismissed(*fresh), "...and Don't show again is kept the same way");
	}

	// a notice of another instance goes to that instance's window only
	{
		int other = 0;
		Window otherWindow(&other);
		auto fresh = openConfig(dir.getChildFile("other.settings"));
		std::atomic<bool> session{false};
		const size_t mine = window.shown.size();
		check(offerTo(&other, rosettaNotice::make(rosetta, Os::Mac, true, *fresh, session)) && otherWindow.shown.size() == 1 && window.shown.size() == mine,
			"the notice goes to the window of the instance that offered it, not to another's");
		check(otherWindow.shown.back().text.find("Open this app as an Apple silicon app") != std::string::npos, "the standalone's words");
	}

	window.attachment.reset();
	dir.deleteRecursively();
	std::printf("mdRosettaNoticeTest: %s\n", g_failures ? "FAIL" : "PASS");
	return g_failures ? 1 : 0;
}
