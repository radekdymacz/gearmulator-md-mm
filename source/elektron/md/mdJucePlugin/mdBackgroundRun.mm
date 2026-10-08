// A run in the background: mdBackgroundRun.h.

#include "mdBackgroundRun.h"

#include "juce_gui_extra/juce_gui_extra.h"

#import <AppKit/AppKit.h>
#if JUCE_WEB_BROWSER
#import <WebKit/WebKit.h>
#endif
#include <objc/message.h>

#ifndef MDMM_DIAGNOSTICS
#define MDMM_DIAGNOSTICS 0
#endif

namespace mdJucePlugin::backgroundRun
{
	bool requested()
	{
#if MDMM_DIAGNOSTICS
		static const bool on = juce::SystemStats::getEnvironmentVariable("GEARMULATOR_MDMM_BACKGROUND", {}) == "1";
		return on;
#else
		return false;
#endif
	}

	bool keepPageDrawn([[maybe_unused]] juce::Component& _web)
	{
#if JUCE_WEB_BROWSER
		if(!requested())
			return true;
		for(auto* c : _web.getChildren())
		{
			auto* nv = dynamic_cast<juce::NSViewComponent*>(c);
			if(!nv || !nv->getView())
				continue;
			id view = (__bridge id)nv->getView();
			if(![view isKindOfClass:[WKWebView class]])
				continue;
			// WKWebView's own switch (WKWebViewPrivate.h, macOS 10.12+); without it the page is drawn only while
			// some of its window shows.
			const SEL set = NSSelectorFromString(@"_setWindowOcclusionDetectionEnabled:");
			if([view respondsToSelector:set])
				reinterpret_cast<void (*)(id, SEL, BOOL)>(objc_msgSend)(view, set, NO);
			return true;
		}
		return false;
#else
		return true;
#endif
	}

	void enterBeforeLaunch()
	{
		if(!requested())
			return;
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
		static id activity = nil;
		if(activity == nil)
		{
			activity = [[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical
				reason:@"editor journeys in the background"];
			[activity retain];
		}
	}

	void sendWindowBack(juce::Component& _c)
	{
		if(!requested())
			return;
		auto* top = _c.getTopLevelComponent();
		if(top == nullptr)
			return;
		const auto place = [](juce::Component& _top)
		{
			if(const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
				_top.setBounds(display->userArea.getX(), display->userArea.getBottom() - 462, 720, 462);
		};
		place(*top);
		// after the launch (the run loop has finished it by then): a prohibited app's windows do not show. The
		// standalone's window may have put back its remembered bounds by then: placed again.
		juce::Timer::callAfterDelay(1000, [window = juce::Component::SafePointer<juce::Component>(top), place]
		{
			[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
			if(window == nullptr)
				return;
			place(*window);
			if(auto* peer = window->getPeer())
				[[(NSView*)peer->getNativeHandle() window] orderBack:nil];
		});
	}
}
