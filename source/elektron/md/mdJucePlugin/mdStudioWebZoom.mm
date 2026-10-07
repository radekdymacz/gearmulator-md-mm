// The editor pages are laid out for 1440 CSS px (the mockup). A narrower window zooms the page out like the
// browser's zoom, so the layout stays whole instead of being cut, and the user's own zoom multiplies it
// (mdPageZoom.h): WKWebView.pageZoom (macOS 11+) on the view inside JUCE's WebBrowserComponent. P4, B-001.
// Where pageZoom is missing (macOS 10.15 and older) the caller zooms the page with CSS instead (mdWebPageHost.cpp).

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
}
