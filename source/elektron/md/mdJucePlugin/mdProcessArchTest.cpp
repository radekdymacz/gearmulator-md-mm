// Rosetta (mdProcessArch.h): the decision over every case the systems give (injected, so each runs on any machine),
// what the editor writes down (the start-up log's line, the performance report's session fields), the words of the
// notice, and this machine's own answer, which must agree with what the system says. Pure: no JUCE.
#include "mdProcessArch.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
	int g_failures = 0;
	void check(const bool _ok, const std::string& _what, const std::string& _text = {})
	{
		std::printf("  %s %s%s\n", _ok ? "ok  " : "FAIL", _what.c_str(), _text.empty() ? "" : (": " + _text).c_str());
		if(!_ok)
			++g_failures;
	}
	bool has(const std::string& _s, const char* _part) { return _s.find(_part) != std::string::npos; }

	using namespace mdJucePlugin::processArch;

	Facts facts(const char* _compiled, const int _translated = -1, const char* _brand = "", const char* _native = "")
	{
		Facts f;
		f.compiledArch = _compiled;
		f.procTranslated = _translated;
		f.cpuBrand = _brand;
		f.nativeMachine = _native;
		return f;
	}

	void expect(const std::string& _what, const Facts& _facts, const char* _process, const char* _machine,
		const bool _translated)
	{
		const auto a = classify(_facts);
		check(a.processArch == _process && a.machineArch == _machine && a.translated == _translated,
			_what, describe(a));
	}

	std::string field(const ProcessArch& _a, const char* _key)
	{
		for(const auto& f : sessionFields(_a))
			if(f.first == _key)
				return f.second;
		return "<missing>";
	}
}

int main()
{
	// macOS: sysctl.proc_translated and the CPU brand
	expect("an Apple silicon build on an M2: native", facts("arm64", 0, "Apple M2"), "arm64", "arm64", false);
	expect("an Intel build under Rosetta on an M1 (proc_translated 1): translated, the machine is Arm",
		facts("x86_64", 1, "VirtualApple @ 2.50GHz"), "x86_64", "arm64", true);
	expect("an Intel build on an Intel Mac: native", facts("x86_64", 0, "Intel(R) Core(TM) i7-9750H CPU @ 2.60GHz"),
		"x86_64", "x86_64", false);
	expect("the sysctl unreadable, the CPU is VirtualApple: translated", facts("x86_64", -1, "VirtualApple @ 2.50GHz"),
		"x86_64", "arm64", true);
	expect("the sysctl unreadable, an Intel CPU: native (unknown is not Rosetta)",
		facts("x86_64", -1, "Intel(R) Core(TM) i5"), "x86_64", "x86_64", false);
	expect("the sysctl unreadable, nothing else known: native", facts("x86_64"), "x86_64", "x86_64", false);
	expect("the sysctl says native, whatever the CPU is called: native (the system's own answer wins)",
		facts("x86_64", 0, "VirtualApple @ 2.50GHz"), "x86_64", "x86_64", false);
	expect("an Apple silicon build whose sysctl is unreadable: native", facts("arm64", -1, "Apple M1"), "arm64",
		"arm64", false);

	// Windows: IsWow64Process2's native machine
	expect("Windows: an x64 build on an Arm64 PC: translated", facts("x86_64", -1, "", "arm64"), "x86_64", "arm64",
		true);
	expect("Windows: an x64 build on an x64 PC: native", facts("x86_64", -1, "", "x86_64"), "x86_64", "x86_64", false);
	expect("Windows: an Arm64 build on an Arm64 PC: native", facts("arm64", -1, "", "arm64"), "arm64", "arm64", false);
	expect("Windows: a 32-bit build on an x64 PC (WOW64 is no emulation): native", facts("x86", -1, "", "x86_64"),
		"x86", "x86_64", false);
	expect("Windows: a 32-bit build on an Arm64 PC: translated", facts("x86", -1, "", "arm64"), "x86", "arm64", true);
	expect("Windows before 10 1709 (no IsWow64Process2): native", facts("x86_64"), "x86_64", "x86_64", false);

	// elsewhere, and a build that does not know its architecture
	expect("Linux: native", facts("x86_64"), "x86_64", "x86_64", false);
	expect("Linux on Arm: native", facts("arm64"), "arm64", "arm64", false);
	expect("an unknown architecture: native, named as such", facts("unknown"), "unknown", "unknown", false);
	expect("no architecture given: unknown, native", facts(""), "unknown", "unknown", false);

	// what is written down
	{
		const auto rosetta = classify(facts("x86_64", 1, "VirtualApple @ 2.50GHz"));
		const auto native = classify(facts("arm64", 0, "Apple M2"));
		check(describe(rosetta) == "process x86_64 on arm64, translated", "the start-up log's line, translated",
			describe(rosetta));
		check(describe(native) == "process arm64 on arm64, native", "the start-up log's line, native",
			describe(native));
		check(field(rosetta, "process_arch") == "x86_64" && field(rosetta, "machine_arch") == "arm64"
			&& field(rosetta, "translated") == "true",
			"the performance report's session fields, translated");
		check(field(native, "process_arch") == "arm64" && field(native, "machine_arch") == "arm64"
			&& field(native, "translated") == "false",
			"the performance report's session fields, native");
		check(sessionFields(native).size() == 3, "three session fields, no more");
		Fields context{{"cpu", "test CPU"}};
		addSessionFields(context);
		check(context.size() == 4 && context[0].first == "cpu" && context[1].first == "process_arch"
			&& context[3].first == "translated",
			"added to a session's fields: after what it has, in order");
	}

	// the notice
	{
		const ProcessArch rosetta{"x86_64", "arm64", true};
		const ProcessArch native{"arm64", "arm64", false};
		check(!translatedNotice(native, Os::Mac, false) && !translatedNotice(native, Os::Windows, false),
			"a native run has no notice");
		check(!translatedNotice(rosetta, Os::Other, false), "a system with no word for it has none either");

		const auto mac = translatedNotice(rosetta, Os::Mac, false);
		check(mac && mac->title == "Running under Rosetta", "macOS: the title");
		check(mac && mac->text == "This is the Intel version of the editor running translated on an Apple silicon Mac. "
			"It uses about twice the CPU. Open your DAW as an Apple silicon app (uncheck 'Open using Rosetta'), "
			"or use the Apple silicon standalone app.", "macOS plug-in: the text");
		check(mac && mac->buttons.size() == 2 && mac->buttons.back() == "OK"
			&& mac->buttons[mac->dontShowAgain] == "Don't show again"
			&& mac->dontShowAgain != mac->buttons.size() - 1,
			"the buttons: OK is the last (the safe answer when the dialog is closed any other way), Don't show again "
			"is another");

		const auto macApp = translatedNotice(rosetta, Os::Mac, true);
		check(macApp && macApp->title == mac->title && has(macApp->text, "Open this app as an Apple silicon app")
			&& has(macApp->text, "Get Info")
			&& !has(macApp->text, "standalone app") && has(macApp->text, "about twice the CPU"),
			"macOS standalone: it is the app that was opened with Rosetta, so it says how to undo that");

		const ProcessArch windowsArm{"x86_64", "arm64", true};
		const auto win = translatedNotice(windowsArm, Os::Windows, false);
		const auto winApp = translatedNotice(windowsArm, Os::Windows, true);
		check(win && has(win->title, "emulation") && has(win->text, "Windows on Arm") && has(win->text, "x64")
			&& has(win->text, "your DAW")
			&& !has(win->text, "Rosetta") && !has(win->text, "Apple"), "Windows: its own words, no Rosetta");
		check(winApp && has(winApp->text, "Windows on Arm") && has(winApp->text, "audio settings")
			&& !has(winApp->text, "your DAW"),
			"Windows standalone: the app's audio settings, not a DAW");
		check(win && win->buttons == mac->buttons && win->dontShowAgain == mac->dontShowAgain,
			"the same buttons on both systems");
		for(const auto* n : {&*mac, &*macApp, &*win, &*winApp})
			check(!n->title.empty() && !n->text.empty() && n->text.find('\n') == std::string::npos
				&& n->text.find('<') == std::string::npos,
				"a notice is plain text: " + n->title);
	}

	// this machine: whatever the system says, the answer agrees with it
	{
		const auto f = readFacts();
		const auto a = detect();
		const auto word = std::string(compiledArch());
		check(word == "arm64" || word == "x86_64" || word == "x86", "this build knows its architecture", word);
		check((word == "x86" ? 4u : 8u) == sizeof(void*), "and it matches the pointer size",
			std::to_string(sizeof(void*) * 8) + " bits");
		check(a.processArch == word && f.compiledArch == word, "the process architecture is the build's");
		check(a.translated == classify(f).translated && a.machineArch == classify(f).machineArch,
			"detect() is classify() of the facts read");
		check(&current() == &current() && current().translated == a.translated, "current() is read once");
		std::printf("  this run: %s (sysctl.proc_translated %d, CPU \"%s\", native machine \"%s\")\n",
			describe(a).c_str(),
			f.procTranslated, f.cpuBrand.c_str(), f.nativeMachine.c_str());
		if(thisOs() == Os::Mac)
		{
			check(f.procTranslated == 0 || f.procTranslated == 1, "macOS answers sysctl.proc_translated",
				std::to_string(f.procTranslated));
			check(f.procTranslated != 0 || (!a.translated && a.machineArch == word),
				"proc_translated 0: native, the machine is the build's");
			check(f.procTranslated != 1 || (a.translated && a.machineArch == "arm64" && word == "x86_64"),
				"proc_translated 1: translated on an Arm Mac");
		}
		else
			check(!a.translated || thisOs() == Os::Windows, "no system but macOS and Windows reports a translation");
	}

	// a diagnostics build can pretend (GEARMULATOR_MDMM_FAKE_ROSETTA=1), to see the notice on a Mac without Rosetta
	{
		const auto setFake = [](const char* _value)
		{
#ifdef _WIN32
			_putenv_s("GEARMULATOR_MDMM_FAKE_ROSETTA", _value);
#else
			setenv("GEARMULATOR_MDMM_FAKE_ROSETTA", _value, 1);
#endif
		};
		setFake("1");
		auto a = detect();
		check(a.translated && a.processArch == "x86_64" && a.machineArch == "arm64",
			"GEARMULATOR_MDMM_FAKE_ROSETTA=1: translated, as an Intel build on an Arm Mac", describe(a));
		setFake("0");
		a = detect();
		check(a.processArch == compiledArch(), "=0: the real answer again", describe(a));
		setFake("");
		a = detect();
		check(a.processArch == compiledArch(), "empty: the real answer", describe(a));
	}

	std::printf("mdProcessArchTest: %s\n", g_failures ? "FAIL" : "PASS");
	return g_failures ? 1 : 0;
}
