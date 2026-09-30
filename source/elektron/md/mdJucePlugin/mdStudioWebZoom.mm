// The Machinedrum Editor page is laid out for 1440 x 900 CSS px (the mockup). A smaller plug-in
// window (GUI scale below 100 %) zooms the page like the browser's zoom, so the layout stays whole
// instead of being cut: WKWebView.pageZoom (macOS 11+) on the view inside JUCE's
// WebBrowserComponent. P4.

#include "juce_gui_extra/juce_gui_extra.h"
#include <functional>

#if JUCE_MAC
#import <WebKit/WebKit.h>
#endif

namespace mdJucePlugin
{
	bool setWebPageZoom(juce::Component& _web, const double _zoom)
	{
#if JUCE_MAC && JUCE_WEB_BROWSER
		for(auto* c : _web.getChildren())
		{
			auto* nv = dynamic_cast<juce::NSViewComponent*>(c);
			if(!nv || !nv->getView())
				continue;
			id view = (__bridge id)nv->getView();
			if(![view isKindOfClass:[WKWebView class]] || ![view respondsToSelector:@selector(setPageZoom:)])
				return false;
			WKWebView* web = (WKWebView*)view;
			if(std::abs(web.pageZoom - _zoom) > 0.001)
				web.pageZoom = _zoom;
			return true;
		}
#else
		(void)_web;
		(void)_zoom;
#endif
		return false;
	}

	// P7: a file dropped on the window goes to the editor (a ROM .bin or .zip, a .syx), not to the web view,
	// which would open it as a page: the WKWebView stops taking drags, so AppKit hands them to JUCE's peer.
	// WKWebView registers again when its page loads, so this runs again from the editor's timer; it returns
	// how many views had registered types (0 = the window has them all).
	int passFileDropsToEditor(juce::Component& _web)
	{
		int taken = 0;
#if JUCE_MAC && JUCE_WEB_BROWSER
		for(auto* c : _web.getChildren())
		{
			auto* nv = dynamic_cast<juce::NSViewComponent*>(c);
			if(!nv || !nv->getView())
				continue;
			id view = (__bridge id)nv->getView();
			if(![view isKindOfClass:[NSView class]])
				continue;
			// depth first: the web view and everything inside it
			std::function<void(NSView*)> walk = [&](NSView* _v)
			{
				if([[_v registeredDraggedTypes] count] > 0)
				{
					++taken;
					[_v unregisterDraggedTypes];
				}
				for(NSView* sub in [_v subviews])
					walk(sub);
			};
			walk((NSView*)view);
		}
#else
		(void)_web;
#endif
		return taken;
	}

	// For the app log: every view of the window that takes file drags, and its class (diagnostics; the drop
	// works when only the window's own view is on the list).
	juce::String describeDragTargets(juce::Component& _web)
	{
		juce::String out;
#if JUCE_MAC && JUCE_WEB_BROWSER
		auto* peer = _web.getPeer();
		if(!peer)
			return "no window yet";
		NSView* root = (__bridge NSView*)peer->getNativeHandle();
		std::function<void(NSView*, int)> walk = [&](NSView* _v, int _depth)
		{
			const auto* types = [_v registeredDraggedTypes];
			if([types count] > 0)
				out << juce::String::fromUTF8([NSStringFromClass([_v class]) UTF8String]) << "(" << (int)[types count] << ") depth " << _depth << "; ";
			for(NSView* sub in [_v subviews])
				walk(sub, _depth + 1);
		};
		walk(root, 0);
#else
		(void)_web;
#endif
		return out.isEmpty() ? juce::String("none") : out;
	}
}
