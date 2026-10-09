#include "mdWebPageHost.h"
#include "mdPageBridge.h"
#include "mdWebFocus.h"
#include "mdPageZoom.h"

#include "juce_gui_extra/juce_gui_extra.h"

#if JUCE_WINDOWS && MDMM_WEBVIEW2
#include "mdWebView2Page.h"
#endif
#if JUCE_MAC
#include "mdBackgroundRun.h"
#endif
#if JUCE_MAC && MDMM_DIAGNOSTICS
#include "mdOsKeys.h"
#endif

#include "mdmmVersion.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace mdJucePlugin
{
	namespace json = elektronData::json;

#if JUCE_MAC
	int setWebPageZoom(juce::Component& _web, double _zoom);	// mdStudioWebZoom.mm: 1 done, 0 not yet, -1 no pageZoom
#elif JUCE_WINDOWS && MDMM_WEBVIEW2
	// 1 done, -1 not a WebView2 page (the CSS zoom then)
	inline int setWebPageZoom(juce::Component& _web, const double _zoom)
	{
		auto* web = dynamic_cast<WebView2Page*>(&_web);
		if(web == nullptr)
			return -1;
		web->setZoom(_zoom);
		return 1;
	}
#else
	inline int setWebPageZoom(juce::Component&, double) { return -1; }
#endif

#if !JUCE_MAC
	// B-018's macOS parts (mdStudioWebZoom.mm); WebView2 takes the keyboard through WebView2Page::focusPage.
	int focusWebView(juce::Component&, bool) { return 0; }
	struct KeyWindowWatch::Impl {};
	KeyWindowWatch::KeyWindowWatch(std::function<void()>) {}
	KeyWindowWatch::~KeyWindowWatch() = default;
	void KeyWindowWatch::follow(juce::Component&) {}
#endif

	namespace
	{
		// B-022: a web view's event that means the page cannot start (WebView2Page's onFailed, a load error)
		const juce::String g_failed = "FAILED: ";
		// The page says it is up (its first message) within this, or the window says what failed
		constexpr double g_pageUpTimeoutMs = 30000.0;

		// Replaces every "<open>NAME<close>" with _make(NAME).
		std::string replaceAll(const std::string& _text, const std::string& _open, const std::string& _close,
			const std::function<std::string(const std::string&)>& _make)
		{
			std::string out;
			size_t pos = 0;
			for(;;)
			{
				const auto a = _text.find(_open, pos);
				const auto b = a == std::string::npos ? a : _text.find(_close, a + _open.size());
				if(b == std::string::npos)
					break;
				out += _text.substr(pos, a - pos);
				out += _make(_text.substr(a + _open.size(), b - a - _open.size()));
				pos = b + _close.size();
			}
			return out + _text.substr(pos);
		}
	}

#if JUCE_WINDOWS && MDMM_WEBVIEW2
	// Windows: WebView2 driven directly (mdWebView2Page.h). Page -> C++ as postMessage texts (the same
	// gmbridge://... strings, deskBridge.js), C++ -> page as executed scripts. A top-level navigation to a
	// gmbridge:// URL still reaches the bridge, and a file dragged onto the page is not opened.
	class PageWebView final : public WebView2Page
	{
	public:
		PageWebView(std::function<void(const std::string&)> _onBridge, std::function<void(const juce::String&)> _onLoadEvent,
			std::function<bool(const juce::String&)> _onFileUrl)
			: WebView2Page(dataFolder(), callbacks(std::move(_onBridge), std::move(_onLoadEvent), std::move(_onFileUrl)))
		{
		}

	private:
		// Per user and writable: %LOCALAPPDATA%\Gearmulator\EditorWebView2 (a VST3 host may run from a read-only folder).
		static juce::File dataFolder()
		{
			return juce::File::getSpecialLocation(juce::File::windowsLocalAppData).getChildFile("Gearmulator").getChildFile("EditorWebView2");
		}

		static Callbacks callbacks(std::function<void(const std::string&)> _onBridge, std::function<void(const juce::String&)> _onLoadEvent,
			std::function<bool(const juce::String&)> _onFileUrl)
		{
			Callbacks c;
			c.onMessage = _onBridge;
			c.onEvent = _onLoadEvent;
			c.onFailed = [_onLoadEvent](const juce::String& _why) { _onLoadEvent(g_failed + _why); };
			c.onNavigation = [_onBridge, _onLoadEvent, _onFileUrl](const juce::String& _url)
			{
				if(_onFileUrl && _onFileUrl(_url))
					return false;
				if(_url.startsWith("gmbridge://"))
				{
					_onBridge(_url.toStdString());
					return false;
				}
				_onLoadEvent("about to load " + _url.substring(0, 60));
				return true;
			};
			return c;
		}
	};
#elif JUCE_WEB_BROWSER
	// JUCE 7.0.10 has no native-function bridge (that is JUCE 8). Page -> C++ goes through
	// navigations to gmbridge://..., cancelled here; the page makes them in throw-away iframes so
	// one never cancels another. C++ -> page uses javascript: URLs (WKWebView evaluateJavaScript); on Linux script
	// files (mdPageBridge.h).
	class PageWebView final : public juce::WebBrowserComponent
	{
	public:
		PageWebView(std::function<void(const std::string&)> _onBridge, std::function<void(const juce::String&)> _onLoadEvent,
			std::function<bool(const juce::String&)> _onFileUrl)
			: juce::WebBrowserComponent(juce::WebBrowserComponent::Options{}.withKeepPageLoadedWhenBrowserIsHidden())
			, m_onBridge(std::move(_onBridge)), m_onLoadEvent(std::move(_onLoadEvent)), m_onFileUrl(std::move(_onFileUrl))
		{
		}

		bool pageAboutToLoad(const juce::String& _url) override
		{
			if(m_onFileUrl && m_onFileUrl(_url))
				return false;
			if(!_url.startsWith("gmbridge://"))
			{
				if(!_url.startsWith("javascript:"))
					m_onLoadEvent("about to load " + _url.substring(0, 60));
				return true;
			}
			m_onBridge(_url.toStdString());
			return false;
		}

		void pageFinishedLoading(const juce::String& _url) override
		{
			if(!_url.startsWith("javascript:"))
				m_onLoadEvent("finished loading " + _url.substring(0, 60));
		}

		bool pageLoadHadNetworkError(const juce::String& _error) override
		{
			// What a user of any build needs to report a blank editor.
			juce::Logger::writeToLog("Gearmulator editor page: load error " + _error);
			m_onLoadEvent(g_failed + "the page did not load (" + _error + ")");
			return false;
		}

	private:
		std::function<void(const std::string&)> m_onBridge;
		std::function<void(const juce::String&)> m_onLoadEvent;
		std::function<bool(const juce::String&)> m_onFileUrl;
	};
#else
	class PageWebView final : public juce::Label
	{
	public:
		PageWebView(std::function<void(const std::string&)>, std::function<void(const juce::String&)>, std::function<bool(const juce::String&)>)
			: juce::Label({}, "This build has JUCE_WEB_BROWSER=0; the editor needs a web view.")
		{
		}
		void goToURL(const juce::String&) {}
	};
#endif

	WebPageHost::WebPageHost(Spec _spec, std::function<std::string(const std::string&)> _resource,
		std::function<void(const Value&)> _onMessage)
		: m_spec(std::move(_spec)), m_resource(std::move(_resource)), m_onMessage(std::move(_onMessage))
		, m_pieces(std::make_unique<pageBridge::Pieces>())
#if JUCE_LINUX || JUCE_BSD
		, m_fileRecv(true)	// webkit2gtk: javascript: URLs crash its web process next to the bridge iframes
#else
		, m_fileRecv(false)
#endif
#if JUCE_WINDOWS && MDMM_WEBVIEW2
		, m_scriptRecv(true)	// WebView2: ExecuteScript (mdWebView2Page.h)
#else
		, m_scriptRecv(false)
#endif
	{
		juce::Desktop::getInstance().addFocusChangeListener(this);
		m_keyWatch = std::make_unique<KeyWindowWatch>([this]
		{
			if(m_pageReady)
				focusPage(true);	// after JUCE took the keyboard for its own view (B-018)
		});
		m_web = std::make_unique<PageWebView>([this](const std::string& _url) { onBridge(_url); },
			[this, alive = std::weak_ptr<int>(m_alive)](const juce::String& _e)
			{
				note("web view: " + _e);
				if(_e.startsWith(g_failed))	// later: the web view may say so while it is being made
					juce::MessageManager::callAsync([this, alive, why = _e.substring(g_failed.length())]
					{
						if(!alive.expired())
							fail(why);
					});
			},
			[this](const juce::String& _url)
			{
				// A file the web view is asked to open (one dragged onto it) is not this page: cancelled quietly, the
				// page's own handler says where to click.
				if(!_url.startsWithIgnoreCase("file:"))
					return false;
				const auto file = juce::URL(_url).getLocalFile();
				if(file == juce::File() || file == m_file)
					return false;
				log("a file was dragged onto the page: not opened (" + file.getFileName() + ")");
				return true;
			});
	}

	WebPageHost::~WebPageHost()
	{
		juce::Desktop::getInstance().removeFocusChangeListener(this);
		m_keyWatch.reset();
		m_web.reset();
		// This instance's page file (P6: one per instance, so two open editors never share one), and the
		// batches the page has not read.
		deleteRecvFiles(std::numeric_limits<uint64_t>::max());
		if(m_file != juce::File())
			m_file.deleteFile();
		// And its log, unless a self-test ran: its runner reads the log after the app has gone.
		if(m_logFile != juce::File() && m_selfTest.isEmpty())
			m_logFile.deleteFile();
	}

	juce::Component& WebPageHost::component()
	{
		return *m_web;
	}

	// This instance's log (P6: one per instance, like its page file; two editors never share one).
	// Only diagnostics builds keep one.
	void WebPageHost::log([[maybe_unused]] const juce::String& _line) const
	{
#if MDMM_DIAGNOSTICS
		if(m_logFile == juce::File())
		{
			const juce::String name(m_spec.log);
			m_logFile = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name.upToLastOccurrenceOf(".", false, false)
				+ "-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt()) + ".log");
		}
		m_logFile.appendText(juce::Time::getCurrentTime().toString(false, true, true, true) + " " + _line + "\n");
#endif
	}

	// WKWebView may only read the one file it loads, so stylesheets, scripts and fonts are inlined.
	// A font that is not bundled gets an empty source and the page falls back to system fonts.
	std::string WebPageHost::bundle() const
	{
		const auto inlineFonts = [&](const std::string& _css)
		{
			return replaceAll(_css, "url(\"fonts/", "\")", [&](const std::string& _name)
			{
				const auto data = m_resource(_name);
				if(data.empty())
					return std::string("url(\"data:,\")");
				const bool woff2 = _name.size() > 6 && _name.compare(_name.size() - 6, 6, ".woff2") == 0;
				return std::string("url(\"data:font/") + (woff2 ? "woff2" : "ttf") + ";base64,"
					+ juce::Base64::toBase64(data.data(), data.size()).toStdString() + "\")";
			});
		};
		auto html = m_resource(m_spec.page);
		html = replaceAll(html, "<link rel=\"stylesheet\" href=\"", "\">", [&](const std::string& _name)
		{
			return "<style>\n" + inlineFonts(m_resource(_name)) + "\n</style>";
		});
		return replaceAll(html, "<script src=\"", "\"></script>", [&](const std::string& _name)
		{
			return "<script>\n" + m_resource(_name) + "\n</script>";
		});
	}

	void WebPageHost::load()
	{
		const auto name = juce::File::createLegalFileName(juce::String(m_spec.page).upToLastOccurrenceOf(".", false, false)
			+ "-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()) + ".html");
		m_file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gearmulator-" + name);
		m_file.replaceWithText(bundle());
#if MDMM_DIAGNOSTICS
		// A self-test is asked for in the environment; its script is only in diagnostics builds.
		const auto kind = juce::SystemStats::getEnvironmentVariable(juce::String(m_spec.selfTestVariable), {});
		for(const auto& t : m_spec.selfTests)
			if(kind.isNotEmpty() && kind.startsWith(juce::String(t)))
				m_selfTest = kind;
#endif
		auto url = m_selfTest.isNotEmpty() ? juce::URL(m_file).withParameter("selftest", m_selfTest) : juce::URL(m_file);
#if MDMM_DIAGNOSTICS
		// B-019: a .syx for the import journeys (mdPageEditor's chooseSyx opens it without a chooser); the page only
		// learns that there is one
		if(juce::File(juce::SystemStats::getEnvironmentVariable("GEARMULATOR_MDMM_SYX_FILE", {})).existsAsFile())
			url = url.withParameter("syxfile", "1");
#endif
		if(m_fileRecv)
			url = url.withParameter(pageBridge::g_fileRecvQuery, "file");
		// 0.3.4: the page shows which version it is (skins/shared/deskAbout.js)
		url = url.withParameter("version", mdmm::editorVersion());
		// The start tests' key probe (skins/shared/deskKeys.js): the page shows which keys reached it, for the
		// accessibility API to read. Any build, so the shipped one is what the start tests press keys into.
		if(juce::SystemStats::getEnvironmentVariable("GEARMULATOR_MDMM_KEYPROBE", {}) == "1")
			url = url.withParameter("keyprobe", "1");
		m_loadMs = juce::Time::getMillisecondCounterHiRes();
		// B-022: GEARMULATOR_MDMM_PAGE_TEST=nostart loads no page, so the window's failure message shows (in 3 s): the
		// start tests check it on every system, any build
		m_noStartTest = juce::SystemStats::getEnvironmentVariable("GEARMULATOR_MDMM_PAGE_TEST", {}) == "nostart";
		if(m_noStartTest)
		{
			note("page not loaded (test: GEARMULATOR_MDMM_PAGE_TEST=nostart)");
			return;
		}
		note("page loading: " + m_file.getFullPathName() + " (" + juce::String(m_file.getSize()) + " bytes)");
		m_web->goToURL(url.toString(true));
		log("page loading, selftest=" + juce::String(m_selfTest.isNotEmpty() ? 1 : 0) + ", keyprobe="
			+ juce::String(url.getParameterNames().contains("keyprobe") ? 1 : 0) + ", " + juce::String(m_file.getSize()) + " bytes");
	}

	void WebPageHost::onBridge(const std::string& _url)
	{
		namespace bridge = pageBridge;
		if(bridge::startsWith(_url, bridge::g_log))
		{
			const auto line = juce::URL::removeEscapeChars(juce::String(_url.substr(std::strlen(bridge::g_log))));
			log("page: " + line);
#if JUCE_MAC && MDMM_DIAGNOSTICS
			// A journey's keys through the operating system's way in (mdOsKeys.h), after this navigation's callback.
			if(line.startsWith("oskeys "))
				juce::MessageManager::callAsync([this, alive = std::weak_ptr<int>(m_alive), spec = line.fromFirstOccurrenceOf(" ", false, false)]
				{
					if(!alive.expired())
						osKeys::send(*m_web, spec, [this](const juce::String& _l) { log(_l); });
				});
#endif
			return;
		}
		if(const auto seq = bridge::ackOf(_url))
		{
			onAck(*seq);
			return;
		}
		std::string escaped;
		if(bridge::startsWith(_url, bridge::g_command))
			escaped = _url.substr(std::strlen(bridge::g_command));
		else if(const auto joined = m_pieces->add(_url))
			escaped = *joined;
		else
			return;	// a piece of a batch still coming, or not the bridge's
		const auto text = juce::URL::removeEscapeChars(juce::String::fromUTF8(escaped.c_str(), static_cast<int>(escaped.size())));
		std::string error;
		const auto batch = json::parse(text.toStdString(), &error);
		if(!batch || !batch->isArray())
		{
			log("bad page message: " + juce::String(error));
			return;
		}
		// The page speaks: its script is up and takes messages (whatever it said first).
		if(!m_pageReady)
		{
			m_pageReady = true;
			if(m_failure)
			{
				// B-022: what said it failed was not the last word (a slow start): the page after all
				note("the page started after all: the failure message goes");
				m_failure.reset();
				m_web->setVisible(true);
			}
			note("page up: the bridge works (" + juce::String(static_cast<int>(juce::Time::getMillisecondCounterHiRes() - m_loadMs)) + " ms after loading)");
			focusPage(false);
		}
		for(const auto& message : batch->asArray())
			m_onMessage(message);
		flush();
	}

	// B-018: the page takes the keyboard when it is up, unless something else in the window has it (a host's own
	// control): a key pressed before any click reaches the page instead of JUCE's view (macOS: the beep).
	void WebPageHost::focusPage(const bool _always)
	{
#if JUCE_MAC
		const auto r = focusWebView(*m_web, _always);
		log(r > 0 ? "keyboard focus: the page" : r < 0 ? "keyboard focus: kept by another view of the window" : "keyboard focus: no window yet");
#elif JUCE_WINDOWS && MDMM_WEBVIEW2
		// WebView2's window is a child of the editor's: moved into it when the editor's window has the focus
		auto* top = m_web->getTopLevelComponent();
		if(!_always && !(top && top->getPeer() && top->getPeer()->isFocused()))
			return;
		if(auto* web = dynamic_cast<WebView2Page*>(m_web.get()))
		{
			web->focusPage();
			log("keyboard focus: the page");
		}
#else
		(void)_always;
#endif
	}

	// B-018: JUCE gives the window's own view the keyboard whenever the window is activated (macOS: becomeKeyWindow;
	// Windows: WM_SETFOCUS), and its focus to the window component or one around the page. Handed on to the page.
	void WebPageHost::globalFocusChanged(juce::Component* _focused)
	{
		if(_focused == nullptr || !m_pageReady || (_focused != m_web.get() && !_focused->isParentOf(m_web.get())))
			return;
		focusPage(true);
	}

	void WebPageHost::flush()
	{
		if(!m_pageReady || m_outbox.empty())
			return;
		const auto firstSeq = m_recvSeq;
		const auto scripts = pageBridge::recvScripts(m_outbox, firstSeq, pageBridge::g_maxRecvBytes,
			m_fileRecv || m_scriptRecv ? std::string() : std::string(pageBridge::g_javascriptUrl));
		m_outbox.clear();
		m_recvSeq += scripts.size();
		for(size_t i = 0; i < scripts.size(); ++i)
		{
			const auto& s = scripts[i];
#if JUCE_WINDOWS && MDMM_WEBVIEW2
			if(m_scriptRecv)
			{
				m_web->executeScript(juce::String::fromUTF8(s.c_str(), static_cast<int>(s.size())));
				continue;
			}
#endif
			if(!m_fileRecv)
			{
				m_web->goToURL(juce::String::fromUTF8(s.c_str(), static_cast<int>(s.size())));
				continue;
			}
			// Written whole and then renamed (replaceWithText), so the page never reads half a batch.
			const auto seq = firstSeq + i;
			auto file = m_file.getSiblingFile(pageBridge::recvFileName(m_file.getFileName().toStdString(), seq));
			if(!file.replaceWithData(s.data(), s.size()))
				log("could not write " + file.getFileName());
			m_recvFiles[seq] = std::move(file);
		}
	}

	// The page has read every batch up to _seq; 0: it has (re)started and reads from the first batch.
	void WebPageHost::onAck(const uint64_t _seq)
	{
		if(_seq != 0)
		{
			deleteRecvFiles(_seq);
			return;
		}
		if(m_recvSeq == 1)
			return;
		// A page that loaded again: what it has not read is for the old one; number from 1 for this one.
		log("page started again: batches numbered from 1");
		deleteRecvFiles(std::numeric_limits<uint64_t>::max());
		m_recvSeq = 1;
	}

	void WebPageHost::deleteRecvFiles(const uint64_t _upTo)
	{
		while(!m_recvFiles.empty() && m_recvFiles.begin()->first <= _upTo)
		{
			m_recvFiles.begin()->second.deleteFile();
			m_recvFiles.erase(m_recvFiles.begin());
		}
	}

	// ---- B-022: the start-up log and the window's word when the page cannot start ----

	void WebPageHost::setStartupLog(const juce::File& _file)
	{
		m_startupLog = _file;
		if(m_startupLog == juce::File() || !m_startupLog.getParentDirectory().createDirectory().wasOk())
		{
			m_startupLog = juce::File();
			return;
		}
		// this window's start, the one before kept beside it (the last two starts are what a report needs)
		const auto previous = m_startupLog.getSiblingFile(m_startupLog.getFileNameWithoutExtension() + "-previous.log");
		if(m_startupLog.existsAsFile())
			m_startupLog.moveFileTo(previous);
		note(juce::String("Gearmulator ") + m_spec.page + " " + juce::String(mdmm::editorVersion()) + ", "
			+ juce::SystemStats::getOperatingSystemName() + (juce::SystemStats::isOperatingSystem64Bit() ? " 64-bit" : "")
			+ ", " + (juce::JUCEApplicationBase::isStandaloneApp() ? "standalone" : "plug-in in " + juce::File::getSpecialLocation(juce::File::hostApplicationPath).getFileName())
			+ ", CPU " + juce::SystemStats::getCpuModel());
		// what the web view said while it was being made, before this file was named
		for(const auto& line : std::exchange(m_early, {}))
			m_startupLog.appendText(line);
	}

	// A line every build keeps (the start-up log), and the diagnostics log's too.
	void WebPageHost::note(const juce::String& _line) const
	{
		log(_line);
		const auto line = juce::Time::getCurrentTime().toString(true, true, true, true) + " " + _line + "\n";
		if(m_startupLog != juce::File())
			m_startupLog.appendText(line);
		else if(m_early.size() < 64)
			m_early.push_back(line);
	}

	// The page did not say it is up in time: the window says so (from the editor's timer).
	void WebPageHost::checkStarted()
	{
		const double timeout = m_noStartTest ? 3000.0 : g_pageUpTimeoutMs;
		if(m_pageReady || m_failure || m_loadMs <= 0 || juce::Time::getMillisecondCounterHiRes() - m_loadMs < timeout)
			return;
		fail("the page did not start within " + juce::String(static_cast<int>(timeout / 1000)) + " s (it loaded, or is still loading, but its script never answered)");
	}

	namespace
	{
		// What the window shows instead of the page when it cannot start (B-022): what failed, what to do, the log.
		class FailureView final : public juce::Component
		{
		public:
			FailureView(const juce::String& _why, const juce::File& _log, std::function<void()> _menu) : m_menu(std::move(_menu))
			{
				setOpaque(true);
				juce::String text = "The editor page could not start.\n\n" + _why + "\n\n";
#if JUCE_WINDOWS
				text += "What to do: install or update the Microsoft Edge WebView2 Runtime (the Evergreen Runtime, version "
					+ juce::String(g_minimumRuntimeText) + " or newer; Windows Update or the button below), then open the editor again.\n\n";
#elif JUCE_LINUX || JUCE_BSD
				text += "What to do: install webkit2gtk (libwebkit2gtk-4.1 or 4.0), then open the editor again.\n\n";
#else
				text += "What to do: open the editor again; if it stays like this, send the log.\n\n";
#endif
				text += "If it still does not work, please send the log: " + (_log != juce::File() ? _log.getFullPathName() : juce::String("(none could be written)"));
				m_text.setText(text, juce::dontSendNotification);
				m_text.setJustificationType(juce::Justification::topLeft);
				m_text.setColour(juce::Label::textColourId, juce::Colours::white);
				m_text.setFont(juce::Font(16.0f));
				m_text.setInterceptsMouseClicks(false, false);	// a right-click on the text is the view's (the menu)
				addAndMakeVisible(m_text);
				m_open.setButtonText("Open the log folder");
				m_open.onClick = [_log] { if(_log != juce::File()) _log.getParentDirectory().revealToUser(); };
				addAndMakeVisible(m_open);
#if JUCE_WINDOWS
				m_get.setButtonText("Get the WebView2 Runtime");
				m_get.onClick = [] { juce::URL("https://go.microsoft.com/fwlink/p/?LinkId=2124703").launchInDefaultBrowser(); };
				addAndMakeVisible(m_get);
#endif
			}

			void paint(juce::Graphics& _g) override { _g.fillAll(juce::Colour(0xff15171a)); }

			// I-008: the editor's menu, native (the page that draws it is not up)
			void mouseDown(const juce::MouseEvent& _e) override
			{
				if(_e.mods.isPopupMenu() && m_menu)
					m_menu();
			}

			void resized() override
			{
				auto r = getLocalBounds().reduced(32);
				auto buttons = r.removeFromBottom(36);
				m_open.setBounds(buttons.removeFromLeft(220));
				buttons.removeFromLeft(12);
				m_get.setBounds(buttons.removeFromLeft(240));
				m_text.setBounds(r.removeFromTop(std::min(r.getHeight(), 320)));
			}

		private:
			static constexpr const char* g_minimumRuntimeText =
#if JUCE_WINDOWS && MDMM_WEBVIEW2
				WebView2Page::g_minimumRuntime;
#else
				"86";
#endif
			juce::Label m_text;
			juce::TextButton m_open, m_get;
			std::function<void()> m_menu;
		};
	}

	void WebPageHost::fail(const juce::String& _why)
	{
		if(m_failure)
			return;
		note("FAILED: " + _why + " - the window says so; log: " + m_startupLog.getFullPathName());
		m_failure = std::make_unique<FailureView>(_why, m_startupLog, m_fallbackMenu);
		m_failure->setTitle("The editor page could not start");
		m_failure->setDescription(_why);
		if(auto* parent = m_web->getParentComponent())
		{
			parent->addAndMakeVisible(*m_failure);
			m_failure->setBounds(m_web->getBounds());
		}
		m_web->setVisible(false);	// WebView2's own window would cover the message
	}

	void WebPageHost::layout(const juce::Rectangle<int>& _bounds)
	{
		if(m_failure)
			m_failure->setBounds(_bounds);
		if(m_web->getBounds() != _bounds)
			m_web->setBounds(_bounds);
		if(_bounds.getWidth() <= 0)
			return;
#if JUCE_MAC
		if(!m_keptDrawn)
			m_keptDrawn = backgroundRun::keepPageDrawn(*m_web);
		m_keyWatch->follow(*m_web);	// B-018: the window it is in now
#endif
		const double zoom = pageZoom::effective(_bounds.getWidth(), _bounds.getHeight(), m_userZoom,
			{m_spec.designWidth, m_spec.designHeight, m_spec.minHeight});
		if(setWebPageZoom(*m_web, zoom) >= 0)
			return;
		// No native page zoom (macOS 10.15 and older, the other platforms' web views): the page's own CSS zoom,
		// which lays it out the same way. Sent when it changes and again when the page (re)loads.
		if(!m_pageReady || std::abs(zoom - m_cssZoom) < 0.001)
			return;
		m_cssZoom = zoom;
		m_web->goToURL("javascript:document.documentElement.style.zoom='" + juce::String(zoom, 4) + "';void 0");
	}

	void WebPageHost::setUserZoom(const double _zoom)
	{
		m_userZoom = pageZoom::clampUser(_zoom);
		log("page zoom " + juce::String(m_userZoom, 2));
	}
}
