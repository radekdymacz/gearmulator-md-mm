#pragma once

// Whether the editor runs translated: an Intel (x86_64) build running under Rosetta 2 on an Apple silicon Mac, or an
// x64 build in the emulation of a Windows on Arm PC. Testers on an M1 reported about twice the CPU of M2 users, and the
// likely cause is a DAW opened as an Intel app, which loads the Intel slice and runs the whole emulator translated.
//
// Three small pieces, none of them needing JUCE (so the unit test also builds as an x86_64 program and runs under
// Rosetta where that is installed):
//   - classify(): the decision, a pure function of the raw facts the system gave (tested with every case injected);
//   - detect() / current(): reads those facts from the system (sysctl on macOS, IsWow64Process2 on Windows);
//   - what the facts feed: the performance report's session fields, the start-up log's line, and the notice's words.
// The notice itself, its "Don't show again" and the once-per-session rule are mdRosettaNotice.h.

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mdJucePlugin::processArch
{
	// What the system says, nothing decided yet. A value that could not be read stays unknown.
	struct Facts
	{
		std::string compiledArch;		// the build that runs: "arm64", "x86_64", "x86" or "unknown" (a universal binary: the slice that loaded)
		int procTranslated = -1;		// macOS sysctl.proc_translated: 1 translated, 0 native, -1 unreadable (absent before macOS 11)
		std::string cpuBrand;			// macOS machdep.cpu.brand_string: "VirtualApple @ 2.50GHz" under Rosetta
		std::string nativeMachine;		// Windows IsWow64Process2's native machine: "arm64", "x86_64", "x86"; "" when unknown
	};

	// What the rest of the editor needs to know.
	struct ProcessArch
	{
		std::string processArch;		// the build that runs: "arm64", "x86_64", "x86", "unknown"
		std::string machineArch;		// the hardware under it: the same words
		bool translated = false;		// an x86 build running on an Arm machine (Rosetta 2, Windows on Arm emulation)
	};

	enum class Os { Mac, Windows, Other };

	// The decision. Translated when the system says so (macOS: proc_translated is 1; when that cannot be read, the
	// "VirtualApple" CPU Rosetta presents), or when an x86 build runs on an Arm machine (Windows: IsWow64Process2).
	// The machine is what the system named, else Arm for a translated process (only Apple silicon translates), else
	// the build itself.
	ProcessArch classify(const Facts& _facts);

	// This build's word for the architecture it was compiled for.
	const char* compiledArch();
	Os thisOs();

	// Reads the facts from the system (never throws; what cannot be read stays unknown), and what classify makes of them.
	Facts readFacts();
	ProcessArch detect();
	// detect() once per process (it cannot change while the process runs).
	const ProcessArch& current();

	// "process x86_64 on arm64, translated" / "process arm64 on arm64, native": one line for the start-up log.
	std::string describe(const ProcessArch& _arch);

	// The performance report's session fields: process_arch, machine_arch, translated ("true" / "false"). The same
	// type as synthLib::PerformanceReport::Context, which the processor builds and passes to start().
	using Fields = std::vector<std::pair<std::string, std::string>>;
	Fields sessionFields(const ProcessArch& _arch);
	// This process's, added to a session's fields (the processor's one call: doc/modern-ux/UPSTREAM.md).
	void addSessionFields(Fields& _fields);

	// What a user is told when the editor runs translated (title, text, buttons; the page shows them as a dialog).
	// The last button is the safe answer: a dialog closed any other way (Esc, a click outside) answers with it, so
	// "Don't show again" is the first and OK the last, and closing the dialog never silences it for good.
	struct Notice
	{
		std::string title;
		std::string text;
		std::vector<std::string> buttons;
		size_t dontShowAgain = 0;		// the index of "Don't show again" in buttons
	};
	// None when the editor does not run translated (or this system has no word for it).
	std::optional<Notice> translatedNotice(const ProcessArch& _arch, Os _os, bool _standalone);
}
