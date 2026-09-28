#include "mdDeskSession.h"
#include "mdSessions.h"

#include "mdController.h"
#include "mdMidiLearnCommands.h"
#include "mdPluginProcessor.h"
#include "mdStudioLink.h"

#include "mdDesk/mdDesk.h"
#include "mdDesk/mdDeskPacer.h"

#include "mdLib/mdautomation.h"

#include "synthLib/midiTypes.h"

#include "juce_core/juce_core.h"

#include <map>

namespace mdJucePlugin
{
	namespace json = elektronData::json;

	namespace
	{
		double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
	}

	// The Machinedrum Editor's session: mdDesk::Desk on the emulated machine (StudioLink) or on
	// a real Machinedrum over the plug-in's MIDI in/out (the engine map's "hw").
	class MdSession final : public DeskSession
	{
	public:
		explicit MdSession(AudioPluginAudioProcessor& _processor)
			: DeskSession(_processor)
			, m_link(_processor, dynamic_cast<Controller&>(_processor.getController()))
			, m_desk(port(emuDevice()))
			, m_learn(_processor, learnModel(), [this](const Value& _m) { toPage(_m); })
		{
			m_link.onSysex = [this](const std::vector<uint8_t>& _m)
			{
				if(!m_desk.isHardwareLink())
					m_desk.onDeviceSysex(_m);
			};
			loadSetup();
			m_desk.setFirmware(m_link.firmware());
		}

		~MdSession() override
		{
			m_processor.setExternalMidi(false);
		}

		void onPageMessage(const Value& _message) override
		{
			const auto op = deskCore::opOf(_message);
			if(MidiLearnCommands::isLearnCommand(op))
			{
				m_learn.handle(_message);
				return;
			}
			if(m_desk.onPageMessage(_message))
			{
				if(op == "ready")
					m_learn.publish();
				return;
			}
			if(op == "engine")
				setEngine(_message);
			else if(op == "recheckFirmware")
			{
				const auto fw = m_link.firmware();
				m_desk.setFirmware(fw);
				reply(_message, fw == mdDesk::Desk::Firmware::Present, fw == mdDesk::Desk::Firmware::Present ? "Firmware found"
					: "No MD OS 1.63 ROM is running yet. After adding it, reopen the plug-in.");
			}
			else if(op == "revealRomFolder")
			{
				const juce::File folder(juce::String::fromUTF8(m_processor.getPublicRomFolder().c_str()));
				folder.createDirectory();
				folder.revealToUser();
				reply(_message, true, {});
			}
			else
				reply(_message, false, "unknown command " + op);
		}

		void step() override
		{
			const auto s = ++m_steps;
			if(m_desk.isHardwareLink())
			{
				stepWire(s);
				return;
			}
			if(s % 12 == 0)
				m_desk.setFirmware(m_link.firmware());
			if(m_processor.getDeskSetupVersion() != m_setupVersion)
				loadSetup();
			m_desk.onTelemetry(m_link.readTelemetry());
			// While the machine starts, the page's LCD shows the firmware's own (start-up animation
			// included), about 15 times a second, until the desk takes input.
			if(attached() && !m_desk.isInputReady() && s % 8 == 0)
				publishLcd();
			std::vector<uint8_t> region;
			if(m_link.readWorkingKit(region))
				m_desk.onWorkingKitMemory(region);
			m_link.drainParameterChanges([this](const uint8_t _t, const uint8_t _i, const uint8_t _v)
			{
				if(_i == 25)
					m_desk.onHostMute(_t, _v != 0);
				else
					m_desk.onHostKitParam(_t, _i, _v);
			});
			if(m_active && s % 4 == 0)
				m_desk.tick();
		}

		std::string status() const override
		{
			const auto& st = m_desk.session().state();
			const auto& d = m_desk.documents();
			return "desk: engine " + m_desk.engine() + " " + deskCore::lifecycleName(m_desk.lifecycle()) + " pattern "
				+ std::to_string(st.pattern ? *st.pattern : -1) + " kit " + std::to_string(st.kit ? *st.kit : -1) + " docs p/k/s "
				+ std::to_string(d.patterns.size()) + "/" + std::to_string(d.kits.size()) + "/" + std::to_string(d.songs.size())
				+ " round trip " + std::to_string(static_cast<int>(m_desk.lastRoundTripMs())) + " ms";
		}

	protected:
		void onAttach() override
		{
			m_active = true;
			m_lastLcd.clear();
		}

		void onDetach() override
		{
			m_desk.detachPage();
		}

	private:
		// ---- the engine map: how each engine's device edge is made ----
		struct Engine
		{
			bool externalMidi = false;						// the plug-in's MIDI in/out go to a real machine
			mdDesk::MdMachine::Port (MdSession::*device)();
		};

		static const std::map<std::string, Engine>& engines()
		{
			static const std::map<std::string, Engine> map{
				{"emu", {false, &MdSession::emuDevice}},
				{"hw", {true, &MdSession::wireDevice}}};
			return map;
		}

		// The emulated MD: SysEx into the device, kit values through the parameter layer, panel keys.
		mdDesk::MdMachine::Port emuDevice()
		{
			mdDesk::MdMachine::Port p;
			p.sendSysex = [this](const std::vector<uint8_t>& _m) { m_link.sendSysex(_m); };
			p.sendKitParam = [this](const uint8_t _t, const uint8_t _i, const uint8_t _v) { m_link.setKitParam(_t, _i, _v); };
			p.sendMute = [this](const uint8_t _t, const bool _on) { m_link.setMute(_t, _on); };
			p.pressKey = [this](const std::string& _key) { return m_link.pressKey(_key); };
			p.turnKnob = [this](const uint8_t _e, const int _s) { return m_link.turnKnob(_e, _s); };
			return p;
		}

		// A real Machinedrum at DIN speed: SysEx and CCs out, PLAY/STOP as MIDI Start/Stop.
		mdDesk::MdMachine::Port wireDevice()
		{
			const auto cc = [this](const md::automation::ParameterChange& _c)
			{
				const auto& g = m_desk.documents().global;
				if(const auto m = md::automation::encodeParameterChange(md::MachineModel::Machinedrum, _c, g ? g->baseChannel : uint8_t(0)))
					m_pacer.push({(*m)[0], (*m)[1], (*m)[2]});
			};
			mdDesk::MdMachine::Port p;
			p.sendSysex = [this](const std::vector<uint8_t>& _m) { m_pacer.push(_m); };
			p.sendKitParam = [cc](const uint8_t _t, const uint8_t _i, const uint8_t _v)
			{
				cc({static_cast<uint8_t>(_i == 24 ? md::automation::machinedrum::Level : _i / 8), _t,
					static_cast<uint8_t>(_i == 24 ? 0 : _i % 8), _v});
			};
			p.sendMute = [cc](const uint8_t _t, const bool _on)
			{
				cc({md::automation::machinedrum::Mute, _t, 0, static_cast<uint8_t>(_on ? 1 : 0)});
			};
			p.pressKey = [this](const std::string& _key)
			{
				if(_key != "play" && _key != "stop")
					return false;
				m_pacer.push({static_cast<uint8_t>(_key == "play" ? 0xfa : 0xfc)});
				return true;
			};
			return p;
		}

		mdDesk::Desk::Port port(const mdDesk::MdMachine::Port& _device)
		{
			mdDesk::Desk::Port p;
			p.sendSysex = _device.sendSysex;
			p.sendKitParam = _device.sendKitParam;
			p.sendMute = _device.sendMute;
			p.pressKey = _device.pressKey;
			p.turnKnob = _device.turnKnob;
			p.toPage = [this](const Value& _m) { toPage(_m); };
			p.saveSetup = [this](const Value& _setup)
			{
				m_processor.setDeskSetup(json::write(_setup));
				m_setupVersion = m_processor.getDeskSetupVersion();
			};
			p.nowMs = [] { return nowMs(); };
			return p;
		}

		void setEngine(const Value& _message)
		{
			const auto* kind = _message.find("kind");
			const auto id = kind && kind->isString() ? kind->asString() : std::string();
			const auto it = engines().find(id);
			if(it == engines().end())
			{
				reply(_message, false, "engine: expected emu or hw");
				return;
			}
			if(id != m_desk.engine())
			{
				m_processor.setExternalMidi(it->second.externalMidi);
				m_pacer = {};
				m_desk.setEngine(id, port((this->*(it->second.device))()));
				m_desk.setFirmware(it->second.externalMidi ? mdDesk::Desk::Firmware::Present : m_link.firmware());
			}
			reply(_message, true, it->second.externalMidi ? "HW MIDI: the editor talks to a Machinedrum on the plug-in's MIDI in and out"
				: "The emulated OS 1.63 again");
		}

		// HW MIDI: the wire at DIN speed out, the machine's SysEx in.
		void stepWire(const uint64_t _s)
		{
			for(auto& m : m_pacer.take(nowMs()))
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
				m_processor.sendExternalMidi(e);
			}
			std::vector<synthLib::SMidiEvent> in;
			m_processor.drainExternalMidiIn(in);
			for(const auto& e : in)
				m_desk.onDeviceSysex(std::vector<uint8_t>(e.sysex.begin(), e.sysex.end()));
			m_desk.onTelemetry(mdDesk::Telemetry{});
			if(m_active && _s % 4 == 0)
				m_desk.tick();
		}

		// The editor's setup from the project (the processor's MDSK chunk), or the default one.
		void loadSetup()
		{
			m_setupVersion = m_processor.getDeskSetupVersion();
			const auto text = m_processor.getDeskSetup();
			const auto doc = text.empty() ? std::optional<json::Value>(mdDesk::deskSetupToJson({})) : json::parse(text);
			if(doc)
				m_desk.loadSetup(*doc);
		}

		void publishLcd()
		{
			std::vector<uint8_t> bits;
			if(!m_link.readLcd(bits) || bits == m_lastLcd)
				return;
			m_lastLcd = bits;
			json::Value l = json::Value::object();
			l.set("type", "lcd");
			l.set("bits", juce::Base64::toBase64(bits.data(), bits.size()).toStdString());
			toPage(l);
		}

		static MidiLearnCommands::Model learnModel()
		{
			MidiLearnCommands::Model m;
			m.pages = false;
			m.tracks = 16;
			m.parameter = [](const MidiLearnCommands::Target& _t)
			{
				return _t.i >= 0 && _t.i <= 24 ? std::string(StudioLink::parameterName(static_cast<uint8_t>(_t.i))) : std::string();
			};
			m.targetOf = [](const std::string& _name) -> std::optional<MidiLearnCommands::Target>
			{
				for(uint8_t i = 0; i <= 24; ++i)
					if(_name == StudioLink::parameterName(i))
						return MidiLearnCommands::Target{-1, -1, i};
				return {};
			};
			return m;
		}

		StudioLink m_link;
		mdDesk::Desk m_desk;
		MidiLearnCommands m_learn;
		mdDesk::DinPacer m_pacer;
		uint32_t m_setupVersion = 0;
		std::vector<uint8_t> m_lastLcd;
		bool m_active = false;	// a page attached once: the desk polls and loads from then on
	};

	std::unique_ptr<DeskSession> makeMdSession(AudioPluginAudioProcessor& _processor)
	{
		return std::make_unique<MdSession>(_processor);
	}
}
