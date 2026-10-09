#pragma once

// B-018: the keyboard for the editor's page on macOS (mdStudioWebZoom.mm). JUCE gives its own view the keyboard
// whenever the window becomes the key window (NSViewComponentPeer::becomeKeyWindow: makeFirstResponder: on the peer's
// view); a key that view does not use goes up the responder chain to AppKit's beep, so a key pressed before a click in
// the page (at start, or after switching back to the editor) never reached it. The page's host hands the keyboard back
// to the web view: when the page is up, when the window becomes the key window, and when JUCE's focus lands on the
// window around the page (WebPageHost).

#include "juce_gui_basics/juce_gui_basics.h"

#include <functional>
#include <memory>

namespace mdJucePlugin
{
	// Makes the web view inside _web the first responder of its window. _always: JUCE has just taken the keyboard for
	// its own view; else only when no one in the window has it, or one of the web view's containers (JUCE's views)
	// does, never another control of the window (a host's). 1: the page has the keyboard; 0: not (no web view in a
	// window yet); -1: another view of the window keeps it. Elsewhere than macOS: 0.
	int focusWebView(juce::Component& _web, bool _always);

	// Calls back each time the window that holds the web view inside a component becomes the key window, after JUCE's
	// own handling of that. follow() attaches to the component's window now (again when it moved to another one).
	class KeyWindowWatch
	{
	public:
		explicit KeyWindowWatch(std::function<void()> _becameKey);
		~KeyWindowWatch();
		KeyWindowWatch(const KeyWindowWatch&) = delete;
		KeyWindowWatch& operator=(const KeyWindowWatch&) = delete;

		void follow(juce::Component& _web);

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}
