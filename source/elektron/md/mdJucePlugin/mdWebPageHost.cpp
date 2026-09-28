#include "mdWebPageHost.h"

#include "juce_gui_extra/juce_gui_extra.h"

#include <cstring>

namespace mdJucePlugin
{
	namespace json = elektronData::json;

#if JUCE_MAC
	bool setWebPageZoom(juce::Component& _web, double _zoom);	// mdStudioWebZoom.mm
#else
	inline bool setWebPageZoom(juce::Component&, double) { return false; }
#endif

	namespace
	{
		constexpr const char* g_bridgeCommand = "gmbridge://c/";
		constexpr const char* g_bridgeLog = "gmbridge://log/";

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
		PageWebView(std::function<void(const std::string&)> _onBridge, std::function<void(const juce::String&)> _onLoadEvent)
			: juce::WebBrowserComponent(juce::WebBrowserComponent::Options{}.withKeepPageLoadedWhenBrowserIsHidden())
			, m_onBridge(std::move(_onBridge)), m_onLoadEvent(std::move(_onLoadEvent))
		{
		}

		bool pageAboutToLoad(const juce::String& _url) override
		{
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
			m_onLoadEvent("load error " + _error);
			return false;
		}

	private:
		std::function<void(const std::string&)> m_onBridge;
		std::function<void(const juce::String&)> m_onLoadEvent;
	};
#else
	class PageWebView final : public juce::Label
	{
	public:
		PageWebView(std::function<void(const std::string&)>, std::function<void(const juce::String&)>)
			: juce::Label({}, "This build has JUCE_WEB_BROWSER=0; the editor needs a web view.")
		{
		}
		void goToURL(const juce::String&) {}
	};
#endif

	WebPageHost::WebPageHost(Spec _spec, std::function<std::string(const std::string&)> _resource,
		std::function<void(const Value&)> _onMessage)
		: m_spec(std::move(_spec)), m_resource(std::move(_resource)), m_onMessage(std::move(_onMessage))
	{
		m_web = std::make_unique<PageWebView>([this](const std::string& _url) { onBridge(_url); },
			[this](const juce::String& _e) { log("web view: " + _e); });
	}

	WebPageHost::~WebPageHost()
	{
		m_web.reset();
		// This instance's page file (P6: one per instance, so two open editors never share one).
		if(m_file != juce::File())
			m_file.deleteFile();
	}

	juce::Component& WebPageHost::component()
	{
		return *m_web;
	}

	void WebPageHost::log(const juce::String& _line) const
	{
		juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(juce::String(m_spec.log))
			.appendText(juce::Time::getCurrentTime().toString(false, true, true, true) + " " + _line + "\n");
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
		const auto kind = juce::SystemStats::getEnvironmentVariable(juce::String(m_spec.selfTestVariable), {});
		for(const auto& t : m_spec.selfTests)
			if(kind.isNotEmpty() && kind.startsWith(juce::String(t)))
				m_selfTest = kind;
		const auto url = m_selfTest.isNotEmpty() ? juce::URL(m_file).withParameter("selftest", m_selfTest) : juce::URL(m_file);
		m_web->goToURL(url.toString(true));
		log("page loading, selftest=" + juce::String(m_selfTest.isNotEmpty() ? 1 : 0) + ", "
			+ juce::String(m_file.getSize()) + " bytes");
	}

	void WebPageHost::onBridge(const std::string& _url)
	{
		if(_url.rfind(g_bridgeLog, 0) == 0)
		{
			log("page: " + juce::URL::removeEscapeChars(juce::String(_url.substr(std::strlen(g_bridgeLog)))));
			return;
		}
		if(_url.rfind(g_bridgeCommand, 0) != 0)
			return;
		const auto text = juce::URL::removeEscapeChars(juce::String(_url.substr(std::strlen(g_bridgeCommand))));
		std::string error;
		const auto batch = json::parse(text.toStdString(), &error);
		if(!batch || !batch->isArray())
		{
			log("bad page message: " + juce::String(error));
			return;
		}
		for(const auto& message : batch->asArray())
		{
			const auto* op = message.find("op");
			if(op && op->isString() && op->asString() == "ready")
			{
				m_pageReady = true;
				log("page ready");
			}
			m_onMessage(message);
		}
		flush();
	}

	void WebPageHost::flush()
	{
		if(!m_pageReady || m_outbox.empty())
			return;
		json::Value batch(json::Value::Array(m_outbox.begin(), m_outbox.end()));
		m_outbox.clear();
		m_web->goToURL("javascript:window.gm&&gm.recv(" + json::write(batch) + ")");
	}

	void WebPageHost::layout(const juce::Rectangle<int>& _bounds)
	{
		if(m_web->getBounds() != _bounds)
			m_web->setBounds(_bounds);
		if(_bounds.getWidth() > 0)
			setWebPageZoom(*m_web, std::min(1.0, _bounds.getWidth() / static_cast<double>(m_spec.designWidth)));
	}
}
