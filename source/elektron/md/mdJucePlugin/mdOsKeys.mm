// Keys pressed the way the operating system delivers them: mdOsKeys.h.

#include "mdOsKeys.h"

#include "juce_gui_extra/juce_gui_extra.h"

#import <AppKit/AppKit.h>
#if JUCE_WEB_BROWSER
#import <WebKit/WebKit.h>
#endif

#include <map>

namespace mdJucePlugin::osKeys
{
	namespace
	{
		// The ANSI keyboard's virtual key codes (HIToolbox Events.h) for what a spec names.
		struct Key
		{
			unsigned short code;
			NSString* chars;		// what the key types without Shift
			NSString* shifted;		// with Shift
		};

		const std::map<juce::String, Key>& keys()
		{
			static const std::map<juce::String, Key> k = {
				{"a", {0, @"a", @"A"}}, {"s", {1, @"s", @"S"}}, {"d", {2, @"d", @"D"}}, {"f", {3, @"f", @"F"}},
				{"h", {4, @"h", @"H"}}, {"g", {5, @"g", @"G"}}, {"z", {6, @"z", @"Z"}}, {"x", {7, @"x", @"X"}},
				{"c", {8, @"c", @"C"}}, {"v", {9, @"v", @"V"}}, {"b", {11, @"b", @"B"}}, {"q", {12, @"q", @"Q"}},
				{"w", {13, @"w", @"W"}}, {"e", {14, @"e", @"E"}}, {"r", {15, @"r", @"R"}}, {"y", {16, @"y", @"Y"}},
				{"t", {17, @"t", @"T"}}, {"o", {31, @"o", @"O"}}, {"u", {32, @"u", @"U"}}, {"i", {34, @"i", @"I"}},
				{"p", {35, @"p", @"P"}}, {"l", {37, @"l", @"L"}}, {"j", {38, @"j", @"J"}}, {"k", {40, @"k", @"K"}},
				{"n", {45, @"n", @"N"}}, {"m", {46, @"m", @"M"}}, {"/", {44, @"/", @"?"}}, {"space", {49, @" ", @" "}},
				{"escape", {53, @"\x1b", @"\x1b"}}, {"delete", {51, @"\x7f", @"\x7f"}}, {"return", {36, @"\r", @"\r"}},
				{"tab", {48, @"\t", @"\t"}}};
			return k;
		}

		WKWebView* webViewIn(juce::Component& _web)
		{
#if JUCE_WEB_BROWSER
			for(auto* c : _web.getChildren())
				if(auto* nv = dynamic_cast<juce::NSViewComponent*>(c); nv && nv->getView())
					if(id view = (__bridge id)nv->getView(); [view isKindOfClass:[WKWebView class]])
						return (WKWebView*)view;
#endif
			return nil;
		}

		NSEvent* keyEvent(const NSEventType _type, const NSEventModifierFlags _flags, const Key& _key, NSWindow* _window)
		{
			NSString* chars = (_flags & NSEventModifierFlagShift) ? _key.shifted : _key.chars;
			return [NSEvent keyEventWithType:_type location:NSZeroPoint modifierFlags:_flags
				timestamp:[[NSProcessInfo processInfo] systemUptime] windowNumber:[_window windowNumber] context:nil
				characters:chars charactersIgnoringModifiers:chars isARepeat:NO keyCode:_key.code];
		}
	}

	void send(juce::Component& _web, const juce::String& _spec, const std::function<void(const juce::String&)>& _log)
	{
		WKWebView* web = webViewIn(_web);
		NSWindow* window = [web window];
		if(window == nil)
		{
			_log("oskeys: no web view in a window");
			return;
		}
		for(const auto& token : juce::StringArray::fromTokens(_spec, " ", ""))
		{
			if(token.isEmpty())
				continue;
			if(token == "activate")
			{
				// What happens when the window becomes the key window: JUCE's peer grabs the focus, its view the first
				// responder (NSViewComponentPeer::becomeKeyWindow), then AppKit posts NSWindowDidBecomeKeyNotification
				// (B-018). Not the window's activation itself: the journeys' window stays in the background.
				if(auto* peer = _web.getPeer())
					peer->grabFocus();
				[[NSNotificationCenter defaultCenter] postNotificationName:NSWindowDidBecomeKeyNotification object:window];
				const id r = [window firstResponder];
				_log(juce::String("oskeys activate: JUCE took the focus; first responder: ") + (r ? [NSStringFromClass([r class]) UTF8String] : "none"));
				continue;
			}
			if(token == "focus")
			{
				const bool done = [window makeFirstResponder:web];
				_log(juce::String("oskeys focus: the web view is ") + (done && [window firstResponder] == web ? "the first responder" : "NOT the first responder"));
				continue;
			}
			NSEventModifierFlags flags = 0;
			auto parts = juce::StringArray::fromTokens(token.toLowerCase(), "+", "");
			auto name = parts[parts.size() - 1];
			for(int i = 0; i < parts.size() - 1; ++i)
			{
				const auto& m = parts[i];
				flags |= m == "cmd" ? NSEventModifierFlagCommand : m == "ctrl" ? NSEventModifierFlagControl
					: m == "alt" ? NSEventModifierFlagOption : m == "shift" ? NSEventModifierFlagShift : 0;
			}
			if(name == "?")
			{
				name = "/";
				flags |= NSEventModifierFlagShift;
			}
			const auto it = keys().find(name);
			if(it == keys().end())
			{
				_log("oskeys " + token + ": not a key this knows");
				continue;
			}
			NSEvent* down = keyEvent(NSEventTypeKeyDown, flags, it->second, window);
			NSEvent* up = keyEvent(NSEventTypeKeyUp, flags, it->second, window);
			// NSApplication's way for a key press in the key window (-[NSApplication sendEvent:]): a key equivalent goes
			// down the window's views, then to the menu bar; what nobody took is a keyDown for the first responder.
			const char* took = "the first responder";
			if((flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0)
			{
				if([window performKeyEquivalent:down])
					took = "a view's key equivalent (the window's views)";
				else if([[NSApp mainMenu] performKeyEquivalent:down])
					took = "the menu bar";
				else
					[window sendEvent:down];
			}
			else
			{
				// A plain key goes to the first responder; one that is not the page's web view (or inside it) does not
				// use it and AppKit beeps (B-018): not sent, said in the log, so the journey fails without a sound.
				const id r = [window firstResponder];
				const bool page = r != nil && [r isKindOfClass:[NSView class]] && ((NSView*)r == (NSView*)web || [(NSView*)r isDescendantOf:web]);
				if(!page)
				{
					_log("oskeys " + token + ": NOT sent, the first responder is not the page ("
						+ juce::String(r ? [NSStringFromClass([r class]) UTF8String] : "none") + "): AppKit would beep");
					continue;
				}
				[window sendEvent:down];
			}
			[window sendEvent:up];
			const id responder = [window firstResponder];
			_log("oskeys " + token + ": taken by " + took + " (first responder: "
				+ juce::String([NSStringFromClass([responder class]) UTF8String]) + ")");
		}
	}
}
