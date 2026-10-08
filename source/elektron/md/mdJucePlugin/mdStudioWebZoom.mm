// The editor pages are laid out for 1440 CSS px (the mockup). A narrower window zooms the page out like the
// browser's zoom, so the layout stays whole instead of being cut, and the user's own zoom multiplies it
// (mdPageZoom.h): WKWebView.pageZoom (macOS 11+) on the view inside JUCE's WebBrowserComponent. P4, B-001.
// Where pageZoom is missing (macOS 10.15 and older) the caller zooms the page with CSS instead (mdWebPageHost.cpp).

#include "mdWebFocus.h"

#include "juce_gui_extra/juce_gui_extra.h"
#include <functional>

#if JUCE_MAC
#import <WebKit/WebKit.h>
#endif

namespace mdJucePlugin
{
	// 1: the web view's pageZoom is _zoom; 0: no web view yet (try again); -1: this WebKit has no pageZoom.
	int setWebPageZoom(juce::Component& _web, const double _zoom)
	{
#if JUCE_MAC && JUCE_WEB_BROWSER
		for(auto* c : _web.getChildren())
		{
			auto* nv = dynamic_cast<juce::NSViewComponent*>(c);
			if(!nv || !nv->getView())
				continue;
			id view = (__bridge id)nv->getView();
			if(![view isKindOfClass:[WKWebView class]])
				continue;
			if(![view respondsToSelector:@selector(setPageZoom:)])
				return -1;
			WKWebView* web = (WKWebView*)view;
			if(std::abs(web.pageZoom - _zoom) > 0.001)
				web.pageZoom = _zoom;
			return 1;
		}
		return 0;
#else
		(void)_web;
		(void)_zoom;
		return -1;
#endif
	}

	// B-018 (mdWebFocus.h)
#if JUCE_MAC && JUCE_WEB_BROWSER
	namespace
	{
		NSView* webViewIn(juce::Component& _web)
		{
			for(auto* c : _web.getChildren())
			{
				auto* nv = dynamic_cast<juce::NSViewComponent*>(c);
				if(!nv || !nv->getView())
					continue;
				id view = (__bridge id)nv->getView();
				if([view isKindOfClass:[WKWebView class]])
					return (NSView*)view;
			}
			return nil;
		}
	}
#endif

	int focusWebView(juce::Component& _web, const bool _always)
	{
#if JUCE_MAC && JUCE_WEB_BROWSER
		if(NSView* web = webViewIn(_web))
		{
			NSWindow* window = [web window];
			if(window == nil)
				return 0;
			const id responder = [window firstResponder];
			const bool isView = responder != nil && [responder isKindOfClass:[NSView class]];
			if(isView && ((NSView*)responder == web || [(NSView*)responder isDescendantOf:web]))
				return 1;
			const bool juceHasIt = responder == nil || responder == window || (isView && [web isDescendantOf:(NSView*)responder]);
			if(!_always && !juceHasIt)
				return -1;
			return [window makeFirstResponder:web] ? 1 : 0;
		}
		return 0;
#else
		(void)_web;
		(void)_always;
		return 0;
#endif
	}

	struct KeyWindowWatch::Impl
	{
		std::function<void()> becameKey;
		NSWindow* window = nil;	// not retained: the observer goes before the window can
		id token = nil;

		void stop()
		{
			if(token != nil)
			{
				[[NSNotificationCenter defaultCenter] removeObserver:token];
				[token release];
			}
			token = nil;
			window = nil;
		}
	};

	KeyWindowWatch::KeyWindowWatch(std::function<void()> _becameKey) : m_impl(std::make_unique<Impl>())
	{
		m_impl->becameKey = std::move(_becameKey);
	}

	KeyWindowWatch::~KeyWindowWatch()
	{
		m_impl->stop();
	}

	void KeyWindowWatch::follow([[maybe_unused]] juce::Component& _web)
	{
#if JUCE_MAC && JUCE_WEB_BROWSER
		NSView* web = webViewIn(_web);
		NSWindow* window = web != nil ? [web window] : nil;
		if(window == m_impl->window)
			return;
		m_impl->stop();
		if(window == nil)
			return;
		m_impl->window = window;
		Impl* impl = m_impl.get();
		// NSWindowDidBecomeKeyNotification comes after -[NSWindow becomeKeyWindow], JUCE's grabFocus included
		m_impl->token = [[[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidBecomeKeyNotification object:window
			queue:nil usingBlock:^(NSNotification*) { if(impl->becameKey) impl->becameKey(); }] retain];
#endif
	}
}
