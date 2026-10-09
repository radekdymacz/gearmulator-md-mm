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
	class KeyWindowWatch;
	namespace pageBridge { class Pieces; class FileOutbox; }

	// A web page in the plug-in (P6: one host for both editors): bundling (stylesheets, scripts
	// and fonts inlined into one file, which WKWebView may read), the bridge (page -> C++ as
	// gmbridge:// navigations, C++ -> page as numbered gm.recv([...], seq) calls; on Linux as script files; on Windows WebView2's
	// postMessage and ExecuteScript instead), the outbox (messages wait until
	// the page said ready, then go out in batches), a temp file per instance, and the zoom that
	// fits the page's design width into the window. It knows nothing about documents. How a message travels
	// is mdPageBridge.h (the transport, pure).
	class WebPageHost final : private juce::FocusChangeListener
	{
	public:
		using Value = elektronData::json::Value;
		using Spec = PageSpec;

		WebPageHost(Spec _spec, std::function<std::string(const std::string&)> _resource,
			std::function<void(const Value&)> _onMessage);
		~WebPageHost() override;

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
		bool failed() const { return m_failure != nullptr; }	// B-022: the window shows why the page cannot start
		const juce::String& selfTest() const { return m_selfTest; }
		// A line of this instance's log (diagnostics builds only: a release build writes no file).
		void log(const juce::String& _line) const;
		// B-022: the start-up log every build keeps (the editor's version, the system, the web engine and its
		// version, loading, the bridge up, what failed), for a user to send: <data folder>/logs/editor-<page>.log, the
		// start before beside it. Set before load().
		void setStartupLog(const juce::File& _file);
		const juce::File& startupLog() const { return m_startupLog; }
		// A line for both logs.
		void note(const juce::String& _line) const;
		// The page has not said it is up in time: the window says what failed and what to do (the editor's timer).
		void checkStarted();
		// I-008: what a right-click on that message does (the editor's menu as a native menu: no page draws it).
		void setFallbackMenu(std::function<void()> _open) { m_fallbackMenu = std::move(_open); }
		// The page started again with batches already sent (Linux: it says a/0 when it starts, mdPageBridge.h): what it
		// had is gone, so the owner sends everything once more (the page's ready may have come before this, and what
		// it answered went out under the old page's numbers).
		void setOnRestart(std::function<void()> _restarted) { m_onRestart = std::move(_restarted); }

	private:
		void onBridge(const std::string& _url);
		void fail(const juce::String& _why);
		// B-018: the keyboard to the page: _always, or only when no other view of the window has it
		void focusPage(bool _always);
		void globalFocusChanged(juce::Component* _focused) override;
		void onAck(uint64_t _seq);
		// The batch files up to _upTo; true when none of them is left (one that cannot be deleted stays registered)
		bool deleteRecvFiles(uint64_t _upTo);
		// Linux: the outbox as script files beside the page, strictly in order (mdPageBridge.h FileOutbox).
		void flushFiles();
		std::string bundle() const;

		const Spec m_spec;
		std::function<std::string(const std::string&)> m_resource;
		std::function<void(const Value&)> m_onMessage;
		std::unique_ptr<PageWebView> m_web;
		std::unique_ptr<KeyWindowWatch> m_keyWatch;	// B-018: the window became the key window (macOS, mdWebFocus.h)
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
		std::unique_ptr<pageBridge::FileOutbox> m_files;	// Linux: the batches not written yet (a write failed), in order
		juce::String m_pageUrl;		// what load() went to: a page that lost batches is loaded again (FileOutbox resync)
		double m_nextResyncTry = 0;	// when to try that reload (again)
		std::function<void()> m_onRestart;	// setOnRestart
		double m_userZoom = 1.0;	// the user's page zoom (the editor's menu, Cmd - / Cmd + / Cmd 0)
		double m_cssZoom = 1.0;		// the CSS zoom sent (a zoom message), where the web view has no native page zoom
		bool m_keptDrawn = false;	// a background run (mdBackgroundRun.h): the page draws while covered
		mutable juce::File m_logFile;	// created on the first line
		juce::File m_startupLog;	// B-022 (setStartupLog)
		mutable std::vector<juce::String> m_early;	// lines noted before the start-up log was named
		double m_loadMs = 0;		// when load() asked for the page
		bool m_noStartTest = false;	// GEARMULATOR_MDMM_PAGE_TEST=nostart (load())
		std::unique_ptr<juce::Component> m_failure;	// B-022: what the window shows when the page cannot start
		std::function<void()> m_fallbackMenu;		// I-008 (setFallbackMenu)
		std::shared_ptr<int> m_alive = std::make_shared<int>(0);	// what runs later asks whether this host is still there
	};
}
