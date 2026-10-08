#pragma once

// A run in the background (diagnostics builds, macOS): the user journeys and self-tests played on a person's Mac
// without getting in their way (scripts/mdmm-journeys.sh --background; doc/modern-ux/FOUNDATION.md, "User
// journeys"). Asked for with GEARMULATOR_MDMM_BACKGROUND=1 in the environment; anywhere else every call does
// nothing.

#include "juce_gui_basics/juce_gui_basics.h"

namespace mdJucePlugin::backgroundRun
{
	// GEARMULATOR_MDMM_BACKGROUND=1 in a diagnostics build on macOS.
	bool requested();

	// The page keeps drawing while its window is covered by other windows: WebKit's window occlusion detection is
	// off for the web view inside _web (a covered page is otherwise hidden to WebKit: no animation frames, slow
	// timers). Once per web view; false while there is no web view yet.
	bool keepPageDrawn(juce::Component& _web);

	// An application (the standalone) before it shows a window: it cannot be activated, so finishing its launch does
	// not bring it to the front (a process started from a shell is activated then), and App Nap does not slow it.
	void enterBeforeLaunch();

	// The window holding _c goes small (half the page's design size), to the bottom-left corner of the main screen,
	// and once the launch is over the application becomes an accessory app (no Dock icon, never activated by itself;
	// its windows show, unlike a prohibited app's) with that window behind every other window: on screen for WebKit.
	void sendWindowBack(juce::Component& _c);
}
