#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

#include <functional>
#include <memory>
#include <string>

namespace mdJucePlugin
{
	// The editor page's web view on Windows (doc/release/WINDOWS.md, B-002): Microsoft Edge WebView2, driven
	// directly. JUCE 7.0.10's WebBrowserComponent defaults to the Internet Explorer control, and its WebView2
	// backend needs WebView2Loader.dll beside the host's executable (a VST3 cannot put it there) and reports
	// only top-level navigations, so the bridge's iframe navigations would never arrive. Here:
	//   - the loader is linked statically (WebView2LoaderStatic.lib): no DLL ships with the editors;
	//   - page -> plug-in: window.chrome.webview.postMessage(text) (deskBridge.js), in order, onMessage;
	//   - plug-in -> page: executeScript (the same gm.recv([...], seq) scripts as the Linux files, mdPageBridge.h);
	//   - the profile (cache, local storage) lives in a per-user folder that is writable (a VST3 host may run
	//     from a read-only folder, and WebView2's default is beside the host's executable).
	// The runtime itself comes with Windows 10 and 11 (the Evergreen WebView2 Runtime); without it the
	// component paints what to install. Only for Windows builds (mdmmWindowsWebView.cmake).
	class WebView2Page : public juce::Component
	{
	public:
		struct Callbacks
		{
			std::function<void(const std::string&)> onMessage;		// a postMessage text from the page
			std::function<bool(const juce::String&)> onNavigation;	// a top-level navigation: false cancels it
			std::function<void(const juce::String&)> onEvent;		// a line for the log (loads, errors)
		};

		WebView2Page(juce::File _userDataFolder, Callbacks _callbacks);
		~WebView2Page() override;

		void goToURL(const juce::String& _url);
		void executeScript(const juce::String& _script);
		// The page's zoom (1: its CSS pixels are the component's pixels).
		void setZoom(double _zoom);

		void paint(juce::Graphics& _g) override;
		void resized() override;
		void visibilityChanged() override;
		void parentHierarchyChanged() override;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebView2Page)
	};
}
