// The Machinedrum Editor page is laid out for 1440 x 900 CSS px (the mockup). A smaller plug-in
// window (GUI scale below 100 %) zooms the page like the browser's zoom, so the layout stays whole
// instead of being cut: WKWebView.pageZoom (macOS 11+) on the view inside JUCE's
// WebBrowserComponent. P4.

#include "juce_gui_extra/juce_gui_extra.h"

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
	bool passFileDropsToEditor(juce::Component& _web)
	{
#if JUCE_MAC && JUCE_WEB_BROWSER
		for(auto* c : _web.getChildren())
		{
			auto* nv = dynamic_cast<juce::NSViewComponent*>(c);
			if(!nv || !nv->getView())
				continue;
			id view = (__bridge id)nv->getView();
			if(![view isKindOfClass:[NSView class]])
				return false;
			[(NSView*)view unregisterDraggedTypes];
			for(NSView* sub in [(NSView*)view subviews])
				[sub unregisterDraggedTypes];
			return true;
		}
#else
		(void)_web;
#endif
		return false;
	}
}
