#pragma once

// Keys pressed the way the operating system delivers them (diagnostics builds, macOS): the user journeys' u.osKey
// (skins/shared/deskJourney.js) asks for them through the page's log ("oskeys <spec>", WebPageHost). Each key is a
// real NSEvent, routed as NSApplication routes a key press: a key equivalent (one with ⌘ or ⌃) first to the window's
// views (performKeyEquivalent: JUCE's peer, the components, then the web view, where JUCE's
// WebViewKeyEquivalentResponder turns ⌘C / ⌘X / ⌘V into the copy: / cut: / paste: commands), then to the menu bar,
// then as a keyDown to the first responder; any other key straight to the first responder. So a key the window,
// JUCE or the menu bar keeps from the page is kept here too, which the page's synthetic keys (u.key) never show.
// It is sent to the web view's own window, never through the system's event tap, so it needs neither the focus nor
// the window in front (the journeys run in the background).
//
// _spec: tokens separated by spaces, each "focus" or a key with modifiers: "cmd+c", "cmd+shift+z", "shift+/", "?",
// "escape", "delete", "a". "focus" makes the web view the window's first responder, as a click on the page does
// (AppKit ignores a synthesized click on a window that is not key, and the journeys' window never is).

#include "juce_gui_basics/juce_gui_basics.h"

#include <functional>

namespace mdJucePlugin::osKeys
{
	// Sends _spec's keys to the window of the web view inside _web; a line for each to _log.
	void send(juce::Component& _web, const juce::String& _spec, const std::function<void(const juce::String&)>& _log);
}
