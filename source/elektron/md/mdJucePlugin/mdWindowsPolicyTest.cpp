// The Windows editors' decisions as plain values, tested on every system:
//   - B-029, B-022: what the WebView2 controller does when the page's native window changes (mdWebView2Window.h);
//   - B-037: what the AUDIO / MIDI panel says when a MIDI port did not open (mdMidiPortRefusal.h).
#include "mdMidiPortRefusal.h"
#include "mdWebView2Window.h"

#include <cstdio>
#include <string>

namespace
{
	int g_failures = 0;

	void check(const bool _ok, const char* _what)
	{
		std::printf("  %s %s\n", _ok ? "ok  " : "FAIL", _what);
		if(!_ok)
			++g_failures;
	}

	const char* name(const mdJucePlugin::webView2Window::Step _s)
	{
		using S = mdJucePlugin::webView2Window::Step;
		switch(_s)
		{
		case S::Nothing: return "Nothing";
		case S::Create: return "Create";
		case S::Reparent: return "Reparent";
		case S::Remake: return "Remake";
		case S::Close: return "Close";
		}
		return "?";
	}

	void step(const mdJucePlugin::webView2Window::Facts& _f, const mdJucePlugin::webView2Window::Step _want, const char* _what)
	{
		const auto got = mdJucePlugin::webView2Window::next(_f);
		check(got == _want, _what);
		if(got != _want)
			std::printf("       got %s, want %s\n", name(got), name(_want));
	}
}

int main()
{
	using namespace mdJucePlugin::webView2Window;
	using S = Step;

	std::printf("WebView2 controller and the page's window (B-029, B-022)\n");
	// The first window: made once the page has one, not while one is being made (its completion decides).
	step({false, false, false, false, false}, S::Nothing, "no controller, no window: wait for a window");
	step({false, false, true, false, false}, S::Create, "no controller, a window: make one in it");
	step({false, true, true, false, false}, S::Nothing, "one being made: its completion decides (E_ABORT makes it again)");
	step({true, false, true, true, true}, S::Nothing, "in its own window: as it is");

	// B-029: a VST3 host closes the editor (JUCE's removed() deletes the peer: its window and WebView2's in it are
	// destroyed) while the processor keeps the page; the host opens it again in a new window.
	step({true, false, false, false, false}, S::Close, "the editor closed: its window was destroyed, the controller is closed");
	step({false, false, false, false, false}, S::Nothing, "closed, no window: nothing until the editor opens again");
	step({false, false, true, false, false}, S::Create, "opened again: a new controller in the new window (the page loads again)");
	// What 0.3.5 did instead: the old controller moved into the new window, whose WebView2 window was gone
	step({true, false, true, false, false}, S::Remake, "a new window and the controller's was destroyed: never moved, made again");

	// B-022: the standalone makes its window again while it starts (its native title bar, setResizable): JUCE tells the
	// page before the old window goes (no window), then again in the new one, after the old one was destroyed.
	step({true, false, false, false, true}, S::Nothing, "the window about to be made again: still there, wait");
	step({true, false, true, false, false}, S::Remake, "in the new window, the old one destroyed: made again");

	// A page moved into another window while its own one lives on: moved, the page keeps running.
	step({true, false, true, false, true}, S::Reparent, "another window, its own still there: moved into the new one");
	step({true, false, false, false, true}, S::Nothing, "taken out of a window that lives on: stays there, hidden");
	// A window handle seen again although its window was destroyed (Windows reuses handles): never trusted
	step({true, false, true, true, false}, S::Remake, "the same handle but its window was destroyed: made again");

	std::printf("MIDI ports that did not open (B-037)\n");
	{
		using namespace mdJucePlugin::midiPortRefusal;
		check(text(Kind::Input, "UM-ONE", true, true, true).empty(), "an input asked for and open: nothing to say");
		check(text(Kind::Input, "UM-ONE", false, false, true).empty(), "an input switched off: nothing to say");
		check(text(Kind::Output, "UM-ONE", false, false, true).empty(), "no output chosen (NONE): nothing to say");
		const auto in = text(Kind::Input, "UM-ONE", true, false, true);
		check(in.find("The MIDI input UM-ONE could not be opened") == 0, "Windows, an input refused: names it");
		check(in.find("one program at a time") != std::string::npos, "Windows: says why (WinMM: one program per port)");
		const auto out = text(Kind::Output, "UM-ONE", true, false, true);
		check(out.find("The MIDI output UM-ONE could not be opened") == 0, "Windows, an output refused: names it");
		const auto mac = text(Kind::Output, "IAC Bus 1", true, false, false);
		check(mac.find("could not be opened") != std::string::npos && mac.find("one program") == std::string::npos,
			"another system: refused, without Windows' reason");
		check(text(Kind::Input, "", true, false, true).find("The MIDI input port could not be opened") == 0, "a port without a name");
	}

	if(g_failures)
	{
		std::printf("%d failure(s)\n", g_failures);
		return 1;
	}
	std::printf("all passed\n");
	return 0;
}
