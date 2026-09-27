#include "mdStudioEditor.h"

#include "mdController.h"
#include "mdPluginProcessor.h"
#include "mdStudioLink.h"

#include "juceRmlUi/juceRmlComponent.h"

#include "juce_gui_extra/juce_gui_extra.h"

#include <cstdio>
#include <sstream>

namespace mdJucePlugin
{
	namespace
	{
		constexpr const char* g_bridgeScheme = "gmbridge://";
		constexpr const char* g_pageResource = "mdStudio.html";
		constexpr int g_headerHeight = 28;	// matches the RML header strip (dp at scale 1)
		constexpr int g_skinHeight = 570;

		// P0 instrumentation: one line per bridge event, for measurements without a debugger.
		void log(const juce::String& _line)
		{
			juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gearmulator-mdStudio.log")
				.appendText(juce::Time::getCurrentTime().toString(false, true, true, true) + " " + _line + "\n");
		}

		// View mapping: the page gets a plain JSON document, never SysEx.
		std::string toJson(const elektronData::MdPattern& _p, const double _roundTripMs)
		{
			std::ostringstream s;
			s << "{\"position\":" << int(_p.position) << ",\"length\":" << int(_p.length)
				<< ",\"lockRows\":" << elektronData::usedLockRows(_p) << ",\"roundTripMs\":" << _roundTripMs
				<< ",\"tracks\":[";
			for(size_t t = 0; t < elektronData::MdPattern::g_tracks; ++t)
			{
				s << (t ? "," : "") << "{\"trigs\":\"";
				for(size_t step = 0; step < _p.length && step < elektronData::MdPattern::g_maxSteps; ++step)
				{
					bool locked = false;
					for(size_t param = 0; param < 32 && !locked; ++param)
						locked = elektronData::lockValue(_p, t, param, step).has_value();
					const bool trig = elektronData::hasTrig(_p, t, step);
					s << (trig ? (locked ? 'L' : 'x') : '.');
				}
				s << "\"}";
			}
			s << "]}";
			return s.str();
		}
	}

#if JUCE_WEB_BROWSER
	// JUCE 7.0.10 has no native-function bridge (that is JUCE 8). Page -> C++
	// goes through navigations to gmbridge://..., which are cancelled here;
	// C++ -> page uses javascript: URLs (WKWebView evaluateJavaScript).
	class StudioWebView final : public juce::WebBrowserComponent
	{
	public:
		explicit StudioWebView(std::function<void(const std::string&)> _onCommand)
			: juce::WebBrowserComponent(juce::WebBrowserComponent::Options{}.withKeepPageLoadedWhenBrowserIsHidden())
			, m_onCommand(std::move(_onCommand))
		{
		}

		bool pageAboutToLoad(const juce::String& _url) override
		{
			if(!_url.startsWith(g_bridgeScheme))
				return true;
			m_onCommand(_url.fromFirstOccurrenceOf(g_bridgeScheme, false, false).toStdString());
			return false;
		}

	private:
		std::function<void(const std::string&)> m_onCommand;
	};
#else
	class StudioWebView final : public juce::Label
	{
	public:
		explicit StudioWebView(std::function<void(const std::string&)>)
			: juce::Label({}, "This build has JUCE_WEB_BROWSER=0; the studio editor needs a web view.")
		{
		}
		void goToURL(const juce::String&) {}
	};
#endif

	StudioEditor::StudioEditor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin)
		: jucePluginEditorLib::Editor(_processor, _skin)
	{
	}

	StudioEditor::~StudioEditor()
	{
		stopTimer();
		m_web.reset();
		m_link.reset();
	}

	void StudioEditor::create()
	{
		jucePluginEditorLib::Editor::create();

		auto& processor = dynamic_cast<AudioPluginAudioProcessor&>(getProcessor());
		m_link = std::make_unique<StudioLink>(processor, dynamic_cast<Controller&>(processor.getController()));
		m_link->onPattern = [this](const elektronData::MdPattern& _p) { onPattern(_p); };

		m_web = std::make_unique<StudioWebView>([this](const std::string& _c) { onPageCommand(_c); });
		getRmlComponent()->addAndMakeVisible(*m_web);
		layoutWebView();

		uint32_t size = 0;
		if(const auto* html = findResourceByFilename(g_pageResource, size))
		{
			const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
				.getChildFile("gearmulator-mdStudio.html");
			file.replaceWithData(html, size);
			// GEARMULATOR_MDSTUDIO_SELFTEST=1: the page clicks a few cells by itself,
			// so the bridge can be exercised without UI automation.
			const bool selfTest = juce::SystemStats::getEnvironmentVariable("GEARMULATOR_MDSTUDIO_SELFTEST", {}) == "1";
			const auto url = selfTest ? juce::URL(file).withParameter("selftest", "1") : juce::URL(file);
			m_web->goToURL(url.toString(true));
			log("page loading, selftest=" + juce::String(selfTest ? 1 : 0));
		}
		startTimerHz(30);
	}

	void StudioEditor::onPageCommand(const std::string& _command)
	{
		log("page -> C++: " + juce::String(_command));
		if(_command.rfind("ready", 0) == 0 || _command.rfind("refresh", 0) == 0)
		{
			m_pageReady = true;
			m_link->requestCurrentPattern();
			return;
		}
		int track = -1;
		int step = -1;
		if(std::sscanf(_command.c_str(), "toggle/%d/%d", &track, &step) != 2 || !m_pattern)
			return;
		if(track < 0 || track >= int(elektronData::MdPattern::g_tracks) || step < 0 || step >= m_pattern->length)
			return;
		const auto on = !elektronData::hasTrig(*m_pattern, size_t(track), size_t(step));
		m_editStartedMs = juce::Time::getMillisecondCounterHiRes();
		m_link->sendPattern(elektronData::withTrig(*m_pattern, size_t(track), size_t(step), on));
	}

	void StudioEditor::onPattern(const elektronData::MdPattern& _pattern)
	{
		double roundTrip = -1;
		if(m_editStartedMs > 0)
		{
			roundTrip = juce::Time::getMillisecondCounterHiRes() - m_editStartedMs;
			m_editStartedMs = 0;
		}
		log("firmware -> page: pattern " + juce::String(_pattern.position) + " length " + juce::String(_pattern.length)
			+ " T1 trigs " + juce::String::toHexString(static_cast<juce::int64>(_pattern.trigs[0]))
			+ (roundTrip >= 0 ? " click->read-back " + juce::String(roundTrip, 1) + " ms" : juce::String()));
		m_pattern = _pattern;
		callPage("gm.pattern(" + toJson(_pattern, roundTrip) + ")");
	}

	void StudioEditor::timerCallback()
	{
		layoutWebView();
		if(!m_link)
			return;
		// The firmware ignores requests while it boots; ask again until it answers.
		if(!m_pattern && m_pageReady && ++m_retryTicks % 60 == 0)
			m_link->requestCurrentPattern();
		const auto step = m_link->readPlayhead();
		const int value = step ? int(*step) : -1;
		if(value == m_lastPlayhead)
			return;
		m_lastPlayhead = value;
		if(value == 0)
			log("playhead wrapped to step 1");
		callPage("gm.playhead(" + std::to_string(value) + ")");
	}

	void StudioEditor::callPage(const std::string& _script) const
	{
		if(m_web)
			m_web->goToURL("javascript:window.gm&&" + _script);
	}

	void StudioEditor::layoutWebView() const
	{
		auto* root = getRmlComponent();
		if(!m_web || !root)
			return;
		const auto bounds = root->getLocalBounds();
		const int header = bounds.getHeight() * g_headerHeight / g_skinHeight;
		const auto target = bounds.withTrimmedTop(header);
		if(m_web->getBounds() != target)
			m_web->setBounds(target);
	}
}
