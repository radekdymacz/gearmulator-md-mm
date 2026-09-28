#pragma once

#include "elektronData/json.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mdJucePlugin
{
	class PageWebView;

	// A web page in the plug-in (P6: one host for both editors): bundling (stylesheets, scripts
	// and fonts inlined into one file, which WKWebView may read), the bridge (page -> C++ as
	// gmbridge:// navigations, C++ -> page as gm.recv([...])), the outbox (messages wait until
	// the page said ready, then go out in batches), a temp file per instance, and the zoom that
	// fits the page's design width into the window. It knows nothing about documents.
	class WebPageHost
	{
	public:
		using Value = elektronData::json::Value;

		struct Spec
		{
			std::string page;					// the bundled page, e.g. "mdStudio.html"
			std::string log;					// the log file in the temp folder, e.g. "gearmulator-mdStudio.log"
			std::string selfTestVariable;		// GEARMULATOR_MDSTUDIO_SELFTEST
			std::vector<std::string> selfTests;	// the values (prefixes) that make the page test itself
			int designWidth = 1440;				// below it the page is zoomed out as a whole
		};

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
		void layout(const juce::Rectangle<int>& _bounds);
		bool pageReady() const { return m_pageReady; }
		const juce::String& selfTest() const { return m_selfTest; }
		void log(const juce::String& _line) const;

	private:
		void onBridge(const std::string& _url);
		std::string bundle() const;

		const Spec m_spec;
		std::function<std::string(const std::string&)> m_resource;
		std::function<void(const Value&)> m_onMessage;
		std::unique_ptr<PageWebView> m_web;
		std::vector<Value> m_outbox;
		juce::File m_file;
		juce::String m_selfTest;
		bool m_pageReady = false;
	};
}
