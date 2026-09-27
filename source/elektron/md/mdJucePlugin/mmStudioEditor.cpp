#include "mmStudioEditor.h"

#include "mdController.h"
#include "mdPluginProcessor.h"
#include "mmStudioLink.h"

#include "mdLib/mdautomation.h"

#include "jucePluginEditorLib/pluginEditorState.h"
#include "jucePluginLib/midiLearnTranslator.h"
#include "juceRmlUi/juceRmlComponent.h"

#include "juce_gui_extra/juce_gui_extra.h"

#include <cstring>
#include <functional>

namespace mdJucePlugin
{
	namespace json = elektronData::json;

#if JUCE_MAC
	bool setWebPageZoom(juce::Component& _web, double _zoom);	// mdStudioWebZoom.mm (shared with the MD)
#else
	inline bool setWebPageZoom(juce::Component&, double) { return false; }
#endif

	namespace
	{
		constexpr const char* g_bridgeCommand = "gmbridge://c/";
		constexpr const char* g_bridgeLog = "gmbridge://log/";
		constexpr const char* g_pageResource = "mmStudio.html";
		constexpr int g_headerHeight = 0;	// no RML header: the menu is native (standalone) or opened by the page
		constexpr int g_skinHeight = 924;	// mmStudio.rml body height

		void log(const juce::String& _line)
		{
			juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gearmulator-mmStudio.log")
				.appendText(juce::Time::getCurrentTime().toString(false, true, true, true) + " " + _line + "\n");
		}

		double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

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

		const char* engineName(const mmDesk::Desk::Engine _e)
		{
			static constexpr const char* names[] = {"missing", "unsupported", "loading", "booting", "ready"};
			return names[static_cast<int>(_e)];
		}
	}

#if JUCE_WEB_BROWSER
	// JUCE 7: page -> C++ through navigations to gmbridge://..., cancelled here;
	// C++ -> page through javascript: URLs (see the MD StudioEditor).
	class MmStudioWebView final : public juce::WebBrowserComponent
	{
	public:
		explicit MmStudioWebView(std::function<void(const std::string&)> _onBridge)
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
	class MmStudioWebView final : public juce::Label
	{
	public:
		explicit MmStudioWebView(std::function<void(const std::string&)>)
			: juce::Label({}, "This build has JUCE_WEB_BROWSER=0; the Monomachine Editor needs a web view.")
		{
		}
		void goToURL(const juce::String&) {}
	};
#endif

	MmStudioEditor::MmStudioEditor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin)
		: jucePluginEditorLib::Editor(_processor, _skin)
	{
	}

	MmStudioEditor::~MmStudioEditor()
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
		// HW MIDI belongs to the open editor: without it the plug-in is the emulator again.
		dynamic_cast<AudioPluginAudioProcessor&>(getProcessor()).setExternalMidi(false);
	}

	void MmStudioEditor::create()
	{
		jucePluginEditorLib::Editor::create();

		auto& processor = dynamic_cast<AudioPluginAudioProcessor&>(getProcessor());
		m_link = std::make_unique<MmStudioLink>(processor, dynamic_cast<Controller&>(processor.getController()));

		// The desk's port: the emulated machine (MmStudioLink), or for HW MIDI (MM-P4) a real
		// Monomachine on the plug-in's MIDI in/out at DIN speed: SysEx and CCs out, SysEx in,
		// PLAY/STOP as MIDI Start/Stop, no panel keys, telemetry or memory.
		mmDesk::Desk::Port port;
		const auto baseChannel = [this]
		{
			const auto g = m_desk ? m_desk->currentGlobal() : -1;
			const auto& doc = g >= 0 ? m_desk->global(static_cast<uint8_t>(g)) : std::nullopt;
			return doc ? static_cast<uint8_t>(doc->baseChannel & 15) : uint8_t(0);
		};
		port.sendSysex = [this](const std::vector<uint8_t>& _m) { if(m_hw) m_hwPacer.push(_m); else m_link->sendSysex(_m); };
		port.sendParam = [this, baseChannel](const uint8_t _t, const uint8_t _p, const uint8_t _i, const uint8_t _v)
		{
			if(!m_hw)
			{
				m_link->setParam(_t, _p, _i, _v);
				return;
			}
			const md::automation::ParameterChange c{_p, _t, _i, _v};
			if(const auto cc = md::automation::encodeParameterChange(md::MachineModel::Monomachine, c, baseChannel()))
				m_hwPacer.push({(*cc)[0], (*cc)[1], (*cc)[2]});
		};
		port.sendNrpn = [this, baseChannel](const uint8_t _t, const uint8_t _p, const uint8_t _v)
		{
			if(!m_hw)
			{
				m_link->sendNrpn(_t, _p, _v);
				return;
			}
			const auto st = static_cast<uint8_t>(0xb0 | baseChannel());
			m_hwPacer.push({st, 99, _t});
			m_hwPacer.push({st, 98, _p});
			m_hwPacer.push({st, 6, static_cast<uint8_t>(_v & 0x7f)});
		};
		port.pressKeys = [this](const std::vector<mmDesk::Key>& _k)
		{
			if(!m_hw)
				return m_link->pressKeys(_k);
			if(_k.size() != 1 || (_k[0] != mmDesk::Key::Play && _k[0] != mmDesk::Key::Stop))
				return false;
			m_hwPacer.push({static_cast<uint8_t>(_k[0] == mmDesk::Key::Play ? 0xfa : 0xfc)});
			return true;
		};
		port.toPage = [this](const json::Value& _m) { m_outbox.push_back(_m); };
		port.nowMs = [] { return nowMs(); };
		m_desk = std::make_unique<mmDesk::Desk>(port);
		m_link->onSysex = [this](const std::vector<uint8_t>& _m)
		{
			if(m_hw)
				return;
			m_desk->onDeviceSysex(_m);
			flushPage();
		};

		m_web = std::make_unique<MmStudioWebView>([this](const std::string& _url) { onBridge(_url); });
		getRmlComponent()->addAndMakeVisible(*m_web);
		layoutWebView();

		const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gearmulator-mmStudio.html");
		file.replaceWithText(bundlePage());
		// GEARMULATOR_MMSTUDIO_SELFTEST=1: the page edits by itself and logs the round trips;
		// =mmcpu: fixed phases for scripts/mm-editor-cpu.sh.
		const auto selfTestMode = juce::SystemStats::getEnvironmentVariable("GEARMULATOR_MMSTUDIO_SELFTEST", {});
		const bool selfTest = selfTestMode == "1" || selfTestMode == "mmcpu";
		const auto url = selfTest ? juce::URL(file).withParameter("selftest", selfTestMode) : juce::URL(file);
		m_web->goToURL(url.toString(true));
		log("page loading, selftest=" + juce::String(selfTest ? 1 : 0) + ", " + juce::String(file.getSize()) + " bytes");
		startTimerHz(30);
	}

	std::string MmStudioEditor::resourceText(const std::string& _name) const
	{
		uint32_t size = 0;
		const auto* data = findResourceByFilename(_name, size);
		return data ? std::string(data, size) : std::string();
	}

	// WKWebView may only read the one file it loads: stylesheets, scripts and fonts are inlined.
	std::string MmStudioEditor::bundlePage() const
	{
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

	void MmStudioEditor::onBridge(const std::string& _url)
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

	void MmStudioEditor::onPageMessage(const json::Value& _message)
	{
		const auto op = opOf(_message);
		if(op == "ready")
		{
			m_pageReady = true;
			log("page ready");
		}
		if(handleEditorMessage(_message))
			return;
		m_desk->onPageMessage(_message);
		if(op == "ready")
		{
			publishLearn();
			publishLcd(true);
		}
	}

	bool MmStudioEditor::handleEditorMessage(const json::Value& _message)
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
		if(op == "engine")
		{
			// {"op":"engine","kind":"hw"|"emu"}: HW MIDI or the emulator.
			const auto* kind = _message.find("kind");
			const bool hw = kind && kind->isString() && kind->asString() == "hw";
			if(hw != m_hw)
			{
				m_hw = hw;
				m_hwPacer = {};
				dynamic_cast<AudioPluginAudioProcessor&>(getProcessor()).setExternalMidi(hw);
				m_desk->setHardwareLink(hw);
				json::Value ready = json::Value::object();
				ready.set("op", "ready");
				m_desk->onPageMessage(ready);
				log(juce::String("engine: ") + (hw ? "HW MIDI (the plug-in's MIDI in/out)" : "emulator"));
			}
			reply(true, hw ? "HW MIDI: the editor talks to a Monomachine on the plug-in's MIDI in and out" : "The emulated OS 1.32B again");
			return true;
		}
		if(op == "openMenu")
		{
			// The editor's menu (skins, scale, settings) where the page was right-clicked.
			if(auto* state = getProcessor().getEditorState())
				state->createPopupMenu().showMenuAsync(juce::PopupMenu::Options().withMousePosition());
			return true;
		}
		if(op == "revealRomFolder")
		{
			const juce::File folder(juce::String::fromUTF8(getProcessor().getPublicRomFolder().c_str()));
			folder.createDirectory();
			folder.revealToUser();
			reply(true, {});
			return true;
		}
		if(op == "midi")
		{
			// {"op":"midi","b":[status, data1, data2]}: the page's keyboard and joystick
			const auto* b = _message.find("b");
			std::array<int, 3> v{-1, 0, 0};
			if(b && b->isArray())
				for(size_t i = 0; i < 3 && i < b->asArray().size(); ++i)
					if(b->asArray()[i].isNumber())
						v[i] = static_cast<int>(b->asArray()[i].asNumber());
			const bool ok = v[0] >= 0x80 && v[0] < 0xf0 && v[1] >= 0 && v[1] < 128 && v[2] >= 0 && v[2] < 128
				&& m_link->sendMidi(static_cast<uint8_t>(v[0]), static_cast<uint8_t>(v[1]), static_cast<uint8_t>(v[2]));
			if(!ok)
				reply(false, "midi: a channel message, [status 0x80-0xef, data, data]");
			return true;
		}
		if(op == "recheckFirmware")
		{
			const auto e = m_link->engine();
			m_desk->setEngine(e);
			reply(e != mmDesk::Desk::Engine::Missing && e != mmDesk::Desk::Engine::Unsupported,
				e == mmDesk::Desk::Engine::Missing ? "No MM OS 1.32B ROM is running yet. After adding it, reopen the plug-in."
				: e == mmDesk::Desk::Engine::Unsupported ? "This ROM is not MM OS 1.32B." : "Firmware found");
			return true;
		}
		if(op.rfind("learn", 0) != 0)
			return false;
		if(!translator)
		{
			reply(false, "MIDI learn is not available in this build");
			return true;
		}
		const int t = intOf(_message, "t"), pg = intOf(_message, "pg"), i = intOf(_message, "i", 0);
		const bool target = t >= 0 && t <= 5 && pg >= 0 && pg <= 7 && i >= 0 && i <= 7
			&& *MmStudioLink::parameterName(static_cast<uint8_t>(pg), static_cast<uint8_t>(i));
		if(op == "learnStart")
		{
			if(!target)
			{
				reply(false, "learn: a synth track's DATA page value or level (MIDI page values are NRPN, not learnable)");
				return true;
			}
			translator->startLearning(MmStudioLink::parameterName(static_cast<uint8_t>(pg), static_cast<uint8_t>(i)));
			m_learnTrack = t;
			m_learnPage = pg;
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
				log("learned CC " + juce::String(mapping.controller) + " -> track " + juce::String(t + 1) + " " + juce::String(mapping.paramName));
				publishLearn();
				flushPage();
			};
			reply(true, {});
		}
		else if(op == "learnAdd")
		{
			const int cc = intOf(_message, "cc");
			if(cc < 0 || cc > 127 || !target)
			{
				reply(false, "learnAdd: expected cc 0-127 and a synth track's value");
				return true;
			}
			pluginLib::MidiLearnMapping mapping;
			mapping.type = pluginLib::MidiLearnMapping::Type::ControlChange;
			mapping.controller = static_cast<uint8_t>(cc);
			mapping.channel = pluginLib::MidiLearnMapping::AllChannels;
			mapping.part = static_cast<uint8_t>(t);
			mapping.paramName = MmStudioLink::parameterName(static_cast<uint8_t>(pg), static_cast<uint8_t>(i));
			auto preset = translator->getPreset();
			preset.addMapping(mapping);
			translator->setPreset(preset);
			getProcessor().saveDefaultMidiLearnPreset();
			reply(true, "CC " + std::to_string(cc) + " -> track " + std::to_string(t + 1));
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

	void MmStudioEditor::publishLearn()
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
				int page = -1, index = -1;
				for(uint8_t pg = 0; pg <= 7; ++pg)
					for(uint8_t i = 0; i < 8; ++i)
						if(*MmStudioLink::parameterName(pg, i) && m.paramName == MmStudioLink::parameterName(pg, i))
						{
							page = pg;
							index = i;
						}
				json::Value v = json::Value::object();
				v.set("index", static_cast<int>(n));
				v.set("t", m.part == pluginLib::MidiLearnMapping::AutoPart ? -1 : static_cast<int>(m.part));
				v.set("pg", page);
				v.set("i", index);
				v.set("name", m.paramName);
				v.set("cc", static_cast<int>(m.controller));
				v.set("ch", static_cast<int>(m.channel));
				v.set("invert", m.invert);
				mappings.push(std::move(v));
			}
		}
		doc.set("mappings", std::move(mappings));
		if(translator && translator->isLearning())
		{
			json::Value l = json::Value::object();
			l.set("t", m_learnTrack);
			l.set("pg", m_learnPage);
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

	// The firmware's own LCD for the page while the machine starts (and on request).
	void MmStudioEditor::publishLcd(const bool _force)
	{
		std::array<uint8_t, 1024> bits{};
		if(!m_link->readLcd(bits))
			return;
		if(!_force && m_lcdSent && bits == m_lastLcd)
			return;
		m_lastLcd = bits;
		m_lcdSent = true;
		static constexpr char hex[] = "0123456789abcdef";
		std::string h;
		h.reserve(2048);
		for(const auto b : bits)
		{
			h += hex[b >> 4];
			h += hex[b & 15];
		}
		json::Value m = json::Value::object();
		m.set("type", "lcd");
		m.set("bits", h);
		m.set("engine", engineName(m_desk->engine()));
		m_outbox.push_back(std::move(m));
	}

	void MmStudioEditor::flushPage()
	{
		if(!m_web || !m_pageReady || m_outbox.empty())
			return;
		json::Value batch(json::Value::Array(m_outbox.begin(), m_outbox.end()));
		m_outbox.clear();
		m_web->goToURL("javascript:window.gm&&gm.recv(" + json::write(batch) + ")");
	}

	void MmStudioEditor::timerCallback()
	{
		layoutWebView();
		if(!m_desk)
			return;
		++m_ticks;
		if(m_hw)
		{
			// HW MIDI: the wire at DIN speed out, the machine's SysEx in.
			auto& processor = dynamic_cast<AudioPluginAudioProcessor&>(getProcessor());
			for(auto& m : m_hwPacer.take(nowMs()))
			{
				synthLib::SMidiEvent e(synthLib::MidiEventSource::Editor);
				if(!m.empty() && m[0] == 0xf0)
					e.sysex.assign(m.begin(), m.end());
				else
				{
					e.a = m.size() > 0 ? m[0] : 0;
					e.b = m.size() > 1 ? m[1] : 0;
					e.c = m.size() > 2 ? m[2] : 0;
				}
				processor.sendExternalMidi(e);
				m_hwBytesOut += m.size();
			}
			if(m_ticks % 150 == 0)
				log("HW MIDI: " + juce::String(static_cast<int64_t>(m_hwBytesOut)) + " bytes out so far");
			std::vector<synthLib::SMidiEvent> in;
			processor.drainExternalMidiIn(in);
			for(const auto& e : in)
				m_desk->onDeviceSysex(std::vector<uint8_t>(e.sysex.begin(), e.sysex.end()));
			m_desk->tick();
			flushPage();
			return;
		}
		const auto t = m_link->readTelemetry();
		m_desk->onTelemetry(t);
		const auto e = m_link->engine();
		if(e != m_lastEngine)
		{
			log(juce::String("engine: ") + engineName(e));
			if(e == mmDesk::Desk::Engine::Ready)
				log("audio: " + juce::String(getProcessor().getSampleRate(), 0) + " Hz, block " + juce::String(getProcessor().getBlockSize()) + " frames");
			m_lastEngine = e;
			publishLcd(true);
		}
		m_desk->setEngine(e);
		// The machine's own screen while it starts; 15 times a second when it changes.
		if(e != mmDesk::Desk::Engine::Ready && (m_ticks & 1))
			publishLcd(false);
		std::vector<uint8_t> region;
		if(m_link->readWorkingKit(region))
			m_desk->onWorkingKit(region);
		m_link->drainParameterChanges([this](const uint8_t _t, const uint8_t _p, const uint8_t _i, const uint8_t _v)
		{
			m_desk->onHostParam(_t, _p, _i, _v);
		});
		m_desk->tick();
		if(m_ticks % 150 == 0)
			log("desk: engine " + juce::String(engineName(e)) + " pattern " + juce::String(m_desk->currentPattern()) + " kit "
				+ juce::String(m_desk->currentKit()) + " loaded " + juce::String(static_cast<int>(m_desk->loaded())) + " recv "
				+ m_desk->recv().stateName() + " round trip " + juce::String(m_desk->lastRoundTripMs(), 1) + " ms");
		flushPage();
	}

	void MmStudioEditor::layoutWebView() const
	{
		auto* root = getRmlComponent();
		if(!m_web || !root)
			return;
		const auto bounds = root->getLocalBounds();
		const int header = bounds.getHeight() * g_headerHeight / g_skinHeight;
		const auto target = bounds.withTrimmedTop(header);
		if(m_web->getBounds() != target)
			m_web->setBounds(target);
		// The page is laid out for 1440 px: below that it is zoomed out as a whole.
		if(target.getWidth() > 0)
			setWebPageZoom(*m_web, std::min(1.0, target.getWidth() / 1440.0));
	}
}
