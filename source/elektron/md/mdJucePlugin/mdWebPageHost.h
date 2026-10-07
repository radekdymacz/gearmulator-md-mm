#pragma once

#include "mdPageSpec.h"

#include "elektronData/json.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mdJucePlugin
{
	class PageWebView;
	namespace pageBridge { class Pieces; }

	// A web page in the plug-in (P6: one host for both editors): bundling (stylesheets, scripts
	// and fonts inlined into one file, which WKWebView may read), the bridge (page -> C++ as
	// gmbridge:// navigations, C++ -> page as numbered gm.recv([...], seq) calls; on Linux as script files; on Windows WebView2's
	// postMessage and ExecuteScript instead), the outbox (messages wait until
	// the page said ready, then go out in batches), a temp file per instance, and the zoom that
	// fits the page's design width into the window. It knows nothing about documents. How a message travels
	// is mdPageBridge.h (the transport, pure).
	class WebPageHost
	{
	public:
		using Value = elektronData::json::Value;
		using Spec = PageSpec;

		WebPageHost(Spec _spec, std::function<std::string(const std::string&)> _resource,
			std::function<void(const Value&)> _onMessage);
		~WebPageHost();

		WebPageHost(const WebPageHost&) = delete;
		WebPageHost& operator=(const WebPageHost&) = delete;

		juce::Component& component();
		// Writes this instance's page file and loads it (with ?selftest= when asked for).
		void load();
		void send(Value _message) { m_outbox.push_back(std::move(_message)); }
		void flush();
		// The web view over _bounds, the page zoomed to fit its design size times the user's zoom (mdPageZoom.h).
		void layout(const juce::Rectangle<int>& _bounds);
		void setUserZoom(double _zoom);
		double userZoom() const { return m_userZoom; }
		bool pageReady() const { return m_pageReady; }
		const juce::String& selfTest() const { return m_selfTest; }
		// A line of this instance's log (diagnostics builds only: a release build writes no file).
		void log(const juce::String& _line) const;

	private:
		void onBridge(const std::string& _url);
		void onAck(uint64_t _seq);
		void deleteRecvFiles(uint64_t _upTo);
		std::string bundle() const;

		const Spec m_spec;
		std::function<std::string(const std::string&)> m_resource;
		std::function<void(const Value&)> m_onMessage;
		std::unique_ptr<PageWebView> m_web;
		std::unique_ptr<pageBridge::Pieces> m_pieces;	// the page's long batches, joined (mdPageBridge.h)
		std::vector<Value> m_outbox;
		uint64_t m_recvSeq = 1;	// the next gm.recv batch's number (the page drops one it has had, mdPageBridge.h)
		juce::File m_file;
		juce::String m_selfTest;
		bool m_pageReady = false;
		// Linux: plug-in -> page as script files beside the page file (mdPageBridge.h), not javascript: URLs.
		const bool m_fileRecv;
		// Windows (WebView2, mdWebView2Page.h): plug-in -> page as executed scripts, not javascript: URLs.
		const bool m_scriptRecv;
		std::map<uint64_t, juce::File> m_recvFiles;	// written and not yet read by the page, by batch number
		double m_userZoom = 1.0;	// the user's page zoom (the editor's menu, Cmd - / Cmd + / Cmd 0)
		double m_cssZoom = 1.0;		// the CSS zoom sent, where the web view has no native page zoom
		mutable juce::File m_logFile;	// created on the first line
	};
}
