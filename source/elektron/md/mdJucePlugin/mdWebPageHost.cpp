#include "mdWebPageHost.h"
#include "mdPageBridge.h"
#include "mdPageZoom.h"

#include "juce_gui_extra/juce_gui_extra.h"

#include <cmath>
#include <cstring>

namespace mdJucePlugin
{
	namespace json = elektronData::json;

#if JUCE_MAC
	int setWebPageZoom(juce::Component& _web, double _zoom);	// mdStudioWebZoom.mm: 1 done, 0 not yet, -1 no pageZoom
#else
	inline int setWebPageZoom(juce::Component&, double) { return -1; }
#endif

	namespace
	{
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

#if JUCE_WEB_BROWSER
	// JUCE 7.0.10 has no native-function bridge (that is JUCE 8). Page -> C++ goes through
	// navigations to gmbridge://..., cancelled here; the page makes them in throw-away iframes so
	// one never cancels another. C++ -> page uses javascript: URLs (WKWebView evaluateJavaScript).
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
			m_onLoadEvent("load error " + _error);
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
	{
		m_web = std::make_unique<PageWebView>([this](const std::string& _url) { onBridge(_url); },
			[this](const juce::String& _e) { log("web view: " + _e); },
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
		m_web.reset();
		// This instance's page file (P6: one per instance, so two open editors never share one).
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
		const auto url = m_selfTest.isNotEmpty() ? juce::URL(m_file).withParameter("selftest", m_selfTest) : juce::URL(m_file);
		m_web->goToURL(url.toString(true));
		log("page loading, selftest=" + juce::String(m_selfTest.isNotEmpty() ? 1 : 0) + ", "
			+ juce::String(m_file.getSize()) + " bytes");
	}

	void WebPageHost::onBridge(const std::string& _url)
	{
		namespace bridge = pageBridge;
		if(bridge::startsWith(_url, bridge::g_log))
		{
			log("page: " + juce::URL::removeEscapeChars(juce::String(_url.substr(std::strlen(bridge::g_log)))));
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
			log("page up");
		}
		for(const auto& message : batch->asArray())
			m_onMessage(message);
		flush();
	}

	void WebPageHost::flush()
	{
		if(!m_pageReady || m_outbox.empty())
			return;
		const auto scripts = pageBridge::recvScripts(m_outbox, m_recvSeq);
		m_outbox.clear();
		m_recvSeq += scripts.size();
		for(const auto& s : scripts)
			m_web->goToURL(juce::String::fromUTF8(s.c_str(), static_cast<int>(s.size())));
	}

	void WebPageHost::layout(const juce::Rectangle<int>& _bounds)
	{
		if(m_web->getBounds() != _bounds)
			m_web->setBounds(_bounds);
		if(_bounds.getWidth() <= 0)
			return;
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
