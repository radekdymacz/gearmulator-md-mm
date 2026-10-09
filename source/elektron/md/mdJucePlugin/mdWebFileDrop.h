#pragma once

// Files dragged from the Finder onto the editor's page go to JUCE's window, not to the web view (macOS; mdWebFileDrop.mm,
// FOUNDATION.md "Files dropped on the window"). The views are passed as void* (an NSView*), so C++ can call this.

namespace mdJucePlugin::webFileDrop
{
	// The page's web view (a WKWebView, the view inside JUCE's WebBrowserComponent) hands a drag of files from outside the
	// page to the first view above it that takes file drags (JUCE's). 1: it does (now or already); -1: it cannot (not a
	// WKWebView, or one whose class key-value observing made), the web view keeps such drags.
	int install(void* _webView);

	// The same for any view (install's work, and mdWebFileDropTest's): its class gets a subclass made at run time whose
	// dragging methods route each drag. 1: done or already; -1: cannot.
	int extend(void* _view);
}
