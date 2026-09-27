#include "mdStudioEditor.h"

#include "mdController.h"
#include "mdPluginProcessor.h"
#include "mdStudioLink.h"

#include "mdDesk/mdDesk.h"

#include "jucePluginLib/midiLearnTranslator.h"
#include "juceRmlUi/juceRmlComponent.h"

#include "juce_gui_extra/juce_gui_extra.h"

#include <cstring>
#include <functional>

namespace mdJucePlugin
{
	namespace json = elektronData::json;

	namespace
	{
		constexpr const char* g_bridgeCommand = "gmbridge://c/";
		constexpr const char* g_bridgeLog = "gmbridge://log/";
		constexpr const char* g_pageResource = "mdStudio.html";
		constexpr int g_headerHeight = 24;	// matches the RML header strip (dp at scale 1)
		constexpr int g_skinHeight = 924;	// mdStudio.rml body height

		// One line per bridge event, for measurements without a debugger.
		void log(const juce::String& _line)
		{
			juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gearmulator-mdStudio.log")
				.appendText(juce::Time::getCurrentTime().toString(false, true, true, true) + " " + _line + "\n");
		}

		double nowMs()
		{
			return juce::Time::getMillisecondCounterHiRes();
		}

		std::string opOf(const json::Value& _m)
		{
			const auto* op = _m.find("op");
			return op && op->isString() ? op->asString() : std::string();
		}

		int intOf(const json::Value& _m, const char* _key, const int _default = -1)
		{
			const auto* v = _m.find(_key);
			return v && v->isNumber() ? static_cast<int>(v->asNumber()) : _default;
		}
	}

#if JUCE_WEB_BROWSER
	// JUCE 7.0.10 has no native-function bridge (that is JUCE 8). Page -> C++
	// goes through navigations to gmbridge://..., cancelled here; the page makes
	// them in throw-away iframes so one never cancels another. C++ -> page uses
	// javascript: URLs (WKWebView evaluateJavaScript).
	class StudioWebView final : public juce::WebBrowserComponent
	{
	public:
		explicit StudioWebView(std::function<void(const std::string&)> _onBridge)
			: juce::WebBrowserComponent(juce::WebBrowserComponent::Options{}.withKeepPageLoadedWhenBrowserIsHidden())
			, m_onBridge(std::move(_onBridge))
		{
		}

		bool pageAboutToLoad(const juce::String& _url) override
		{
			if(!_url.startsWith("gmbridge://"))
				return true;
			m_onBridge(_url.toStdString());
			return false;
		}

	private:
		std::function<void(const std::string&)> m_onBridge;
	};
#else
	class StudioWebView final : public juce::Label
	{
	public:
		explicit StudioWebView(std::function<void(const std::string&)>)
			: juce::Label({}, "This build has JUCE_WEB_BROWSER=0; the Machinedrum Editor needs a web view.")
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
		if(auto* translator = getProcessor().getMidiLearnTranslator())
		{
			if(translator->isLearning())
				translator->cancelLearning();
			translator->onMappingLearned = nullptr;
		}
		m_web.reset();
		m_desk.reset();
		m_link.reset();
	}

	void StudioEditor::create()
	{
		jucePluginEditorLib::Editor::create();

		auto& processor = dynamic_cast<AudioPluginAudioProcessor&>(getProcessor());
		m_link = std::make_unique<StudioLink>(processor, dynamic_cast<Controller&>(processor.getController()));

		mdDesk::Desk::Port port;
		port.sendSysex = [this](const std::vector<uint8_t>& _m) { m_link->sendSysex(_m); };
		port.sendKitParam = [this](const uint8_t _t, const uint8_t _i, const uint8_t _v) { m_link->setKitParam(_t, _i, _v); };
		port.sendMute = [this](const uint8_t _t, const bool _on) { m_link->setMute(_t, _on); };
		port.pressKey = [this](const std::string& _key) { return m_link->pressKey(_key); };
		port.turnKnob = [this](const uint8_t _e, const int _s) { return m_link->turnKnob(_e, _s); };
		port.toPage = [this](const json::Value& _m)
		{
			m_outbox.push_back(_m);
			if(m_lastCommandMs > 0)
			{
				// Instrumentation: command in -> first document out.
				const auto* type = _m.find("type");
				if(type && type->asString() == "doc")
				{
					log("page command -> document out " + juce::String(nowMs() - m_lastCommandMs, 1) + " ms");
					m_lastCommandMs = 0;
				}
			}
		};
		port.nowMs = [] { return nowMs(); };
		m_desk = std::make_unique<mdDesk::Desk>(port);
		m_desk->setFirmware(m_link->firmware());
		m_link->onSysex = [this](const std::vector<uint8_t>& _m)
		{
			m_desk->onDeviceSysex(_m);
			flushPage();
		};

		m_web = std::make_unique<StudioWebView>([this](const std::string& _url) { onBridge(_url); });
		getRmlComponent()->addAndMakeVisible(*m_web);
		layoutWebView();

		const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gearmulator-mdStudio.html");
		file.replaceWithText(bundlePage());
		// GEARMULATOR_MDSTUDIO_SELFTEST=1: the page edits a trig and a kit value by
		// itself and logs the round trips (the log file above).
		const bool selfTest = juce::SystemStats::getEnvironmentVariable("GEARMULATOR_MDSTUDIO_SELFTEST", {}) == "1";
		const auto url = selfTest ? juce::URL(file).withParameter("selftest", "1") : juce::URL(file);
		m_web->goToURL(url.toString(true));
		log("page loading, selftest=" + juce::String(selfTest ? 1 : 0) + ", " + juce::String(file.getSize()) + " bytes");
		startTimerHz(30);
	}

	std::string StudioEditor::resourceText(const std::string& _name) const
	{
		uint32_t size = 0;
		const auto* data = findResourceByFilename(_name, size);
		return data ? std::string(data, size) : std::string();
	}

	// WKWebView may only read the one file it loads, so stylesheets, scripts and
	// fonts are inlined. A font that is not bundled gets an empty source and the
	// page falls back to system fonts.
	std::string StudioEditor::bundlePage() const
	{
		// Replaces every "<open>NAME<close>" with _make(NAME).
		const auto replaceAll = [](std::string _text, const std::string& _open, const std::string& _close,
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
		};
		// url("fonts/NAME") -> a data URI; the type from the extension (ttf, woff2).
		const auto inlineFonts = [&](const std::string& _css)
		{
			return replaceAll(_css, "url(\"fonts/", "\")", [&](const std::string& _name)
			{
				uint32_t size = 0;
				const auto* data = findResourceByFilename(_name, size);
				if(!data)
					return std::string("url(\"data:,\")");
				const bool woff2 = _name.size() > 6 && _name.compare(_name.size() - 6, 6, ".woff2") == 0;
				return std::string("url(\"data:font/") + (woff2 ? "woff2" : "ttf") + ";base64,"
					+ juce::Base64::toBase64(data, size).toStdString() + "\")";
			});
		};
		auto html = resourceText(g_pageResource);
		html = replaceAll(html, "<link rel=\"stylesheet\" href=\"", "\">", [&](const std::string& _name)
		{
			return "<style>\n" + inlineFonts(resourceText(_name)) + "\n</style>";
		});
		return replaceAll(html, "<script src=\"", "\"></script>", [&](const std::string& _name)
		{
			return "<script>\n" + resourceText(_name) + "\n</script>";
		});
	}

	void StudioEditor::onBridge(const std::string& _url)
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
			onPageMessage(message);
		flushPage();
	}

	void StudioEditor::onPageMessage(const json::Value& _message)
	{
		const auto op = opOf(_message);
		if(op == "ready")
		{
			m_pageReady = true;
			log("page ready");
		}
		if(handleEditorMessage(_message))
			return;
		if(op != "ready" && op != "load")
			m_lastCommandMs = nowMs();
		m_desk->onPageMessage(_message);
		if(op == "ready")
			publishLearn();
	}

	// Messages for the plug-in rather than the machine: MIDI learn, the ROM folder.
	bool StudioEditor::handleEditorMessage(const json::Value& _message)
	{
		const auto op = opOf(_message);
		auto* translator = getProcessor().getMidiLearnTranslator();
		const auto reply = [&](const bool _ok, const std::string& _note)
		{
			json::Value r = json::Value::object();
			r.set("type", "result");
			r.set("op", op);
			r.set("id", intOf(_message, "id", 0));
			r.set("ok", _ok);
			json::Value errors = json::Value::array();
			if(!_ok)
				errors.push(_note);
			r.set("errors", std::move(errors));
			r.set("note", _ok ? _note : std::string());
			m_outbox.push_back(std::move(r));
		};
		if(op == "revealRomFolder")
		{
			const juce::File folder(juce::String::fromUTF8(getProcessor().getPublicRomFolder().c_str()));
			folder.createDirectory();
			folder.revealToUser();
			reply(true, {});
			return true;
		}
		if(op == "recheckFirmware")
		{
			const auto fw = m_link->firmware();
			m_desk->setFirmware(fw);
			reply(fw == mdDesk::Desk::Firmware::Present, fw == mdDesk::Desk::Firmware::Present ? "Firmware found"
				: "No MD OS 1.63 ROM is running yet. After adding it, reopen the plug-in.");
			return true;
		}
		if(op.rfind("learn", 0) != 0)
			return false;
		if(!translator)
		{
			reply(false, "MIDI learn is not available in this build");
			return true;
		}
		if(op == "learnStart")
		{
			const int t = intOf(_message, "t"), i = intOf(_message, "i");
			if(t < 0 || t > 15 || i < 0 || i > 24)
			{
				reply(false, "learn: expected a track and a parameter");
				return true;
			}
			translator->startLearning(StudioLink::parameterName(static_cast<uint8_t>(i)));
			m_learnTrack = t;
			m_learnIndex = i;
			translator->onMappingLearned = [this, translator, t](const pluginLib::MidiLearnMapping& _learned)
			{
				auto mapping = _learned;
				mapping.part = static_cast<uint8_t>(t);
				auto preset = translator->getPreset();
				preset.addMapping(mapping);
				translator->setPreset(preset);
				getProcessor().saveDefaultMidiLearnPreset();
				translator->onMappingLearned = nullptr;
				log("learned CC " + juce::String(mapping.controller) + " -> track " + juce::String(t + 1) + " "
					+ juce::String(mapping.paramName));
				publishLearn();
				flushPage();
			};
			reply(true, {});
		}
		else if(op == "learnCancel")
		{
			translator->cancelLearning();
			translator->onMappingLearned = nullptr;
			reply(true, {});
		}
		else if(op == "learnRemove" || op == "learnInvert")
		{
			auto preset = translator->getPreset();
			const auto index = intOf(_message, "index");
			if(index < 0 || static_cast<size_t>(index) >= preset.getMappings().size())
			{
				reply(false, "learn: no such mapping");
				return true;
			}
			if(op == "learnRemove")
				preset.removeMapping(static_cast<size_t>(index));
			else
				preset.getMappings()[static_cast<size_t>(index)].invert ^= true;
			translator->setPreset(preset);
			getProcessor().saveDefaultMidiLearnPreset();
			reply(true, {});
		}
		else
			reply(false, "unknown command " + op);
		publishLearn();
		return true;
	}

	void StudioEditor::publishLearn()
	{
		auto* translator = getProcessor().getMidiLearnTranslator();
		json::Value doc = json::Value::object();
		json::Value mappings = json::Value::array();
		if(translator)
		{
			const auto& list = translator->getPreset().getMappings();
			for(size_t n = 0; n < list.size(); ++n)
			{
				const auto& m = list[n];
				int index = -1;
				for(uint8_t i = 0; i <= 24; ++i)
					if(m.paramName == StudioLink::parameterName(i))
						index = i;
				json::Value v = json::Value::object();
				v.set("index", static_cast<int>(n));
				v.set("t", m.part == pluginLib::MidiLearnMapping::AutoPart ? -1 : static_cast<int>(m.part));
				v.set("i", index);
				v.set("name", m.paramName);
				v.set("cc", static_cast<int>(m.controller));
				v.set("ch", static_cast<int>(m.channel));
				v.set("mode", pluginLib::MidiLearnMapping::modeToString(m.mode));
				v.set("invert", m.invert);
				mappings.push(std::move(v));
			}
		}
		doc.set("mappings", std::move(mappings));
		if(translator && translator->isLearning())
		{
			json::Value l = json::Value::object();
			l.set("name", translator->getLearningParamName());
			l.set("t", m_learnTrack);
			l.set("i", m_learnIndex);
			doc.set("learning", std::move(l));
		}
		else
			doc.set("learning", json::Value());
		json::Value m = json::Value::object();
		m.set("type", "learn");
		m.set("doc", std::move(doc));
		m_outbox.push_back(std::move(m));
	}

	void StudioEditor::flushPage()
	{
		if(!m_web || !m_pageReady || m_outbox.empty())
			return;
		json::Value batch(json::Value::Array(m_outbox.begin(), m_outbox.end()));
		m_outbox.clear();
		m_web->goToURL("javascript:window.gm&&gm.recv(" + json::write(batch) + ")");
	}

	void StudioEditor::timerCallback()
	{
		layoutWebView();
		if(!m_desk)
			return;
		// The engine's state from the device, ten times a second; transitions logged.
		if(++m_ticks % 3 == 0)
		{
			const auto fw = m_link->firmware();
			if(fw != m_lastFirmware)
			{
				static constexpr const char* names[] = {"missing", "unsupported", "loading", "booting", "present"};
				log("engine: " + juce::String(names[static_cast<int>(fw)]));
				m_lastFirmware = fw;
			}
			m_desk->setFirmware(fw);
		}
		if(m_ticks % 15 == 0 && juce::SystemStats::getEnvironmentVariable("GEARMULATOR_MDSTUDIO_SELFTEST", {}) == "1")
		{
			const auto t = m_link->readTelemetry();
			log("telemetry: step " + juce::String(t.step) + " playing " + juce::String(t.playing ? 1 : 0) + " rec "
				+ juce::String(t.recording ? 1 : 0) + " grid " + juce::String(t.gridEdit ? 1 : 0) + " page " + juce::String(t.knobPage));
		}
		if(m_ticks % 150 == 0)
		{
			const auto& s = m_desk->session().state();
			const auto& d = m_desk->documents();
			log("desk: firmware " + juce::String(static_cast<int>(m_link->firmware())) + " ready " + juce::String(m_desk->isReady() ? 1 : 0)
				+ " pattern " + juce::String(s.pattern ? *s.pattern : -1) + " kit " + juce::String(s.kit ? *s.kit : -1)
				+ " docs p/k/s " + juce::String(static_cast<int>(d.patterns.size())) + "/" + juce::String(static_cast<int>(d.kits.size()))
				+ "/" + juce::String(static_cast<int>(d.songs.size())) + " round trip " + juce::String(m_desk->lastRoundTripMs(), 1) + " ms");
		}
		m_desk->onTelemetry(m_link->readTelemetry());
		std::vector<uint8_t> region;
		if(m_link->readWorkingKit(region))
			m_desk->onWorkingKitMemory(region);
		m_link->drainParameterChanges([this](const uint8_t _t, const uint8_t _i, const uint8_t _v)
		{
			if(_i == 25)
				m_desk->onHostMute(_t, _v != 0);
			else
				m_desk->onHostKitParam(_t, _i, _v);
		});
		m_desk->tick();
		flushPage();
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
