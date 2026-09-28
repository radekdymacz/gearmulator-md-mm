#include "mdDeskSession.h"
#include "mdSessions.h"

#include "mdController.h"
#include "mdMidiLearnCommands.h"
#include "mdPluginProcessor.h"
#include "mmStudioLink.h"

#include "mdDesk/mdDeskPacer.h"
#include "mmDesk/mmDesk.h"

#include "mdLib/mdautomation.h"

#include "synthLib/midiTypes.h"

#include "juce_core/juce_core.h"

#include <array>
#include <map>

namespace mdJucePlugin
{
	namespace json = elektronData::json;

	namespace
	{
		double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
	}

	// The Monomachine Editor's session: mmDesk::Desk on the emulated machine (MmStudioLink) or on
	// a real Monomachine over the plug-in's MIDI in/out (the engine map's "hw").
	class MmSession final : public DeskSession
	{
	public:
		explicit MmSession(AudioPluginAudioProcessor& _processor)
			: DeskSession(_processor)
			, m_link(_processor, dynamic_cast<Controller&>(_processor.getController()))
			, m_desk(port(emuDevice()))
			, m_learn(_processor, learnModel(), [this](const Value& _m) { toPage(_m); })
		{
			m_link.onSysex = [this](const std::vector<uint8_t>& _m)
			{
				if(m_desk.engineId() != "hw")
					m_desk.onDeviceSysex(_m);
			};
			m_desk.setEngine(m_link.engine());
		}

		~MmSession() override
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
				{
					m_learn.publish();
					publishLcd(true);
				}
				return;
			}
			// The plug-in's commands (the table's Owner::Host), by op.
			using Handler = void (MmSession::*)(const Value&);
			static const std::map<std::string, Handler> handlers{
				{"engine", &MmSession::setEngine},
				{"midi", &MmSession::midiFromPage},
				{"recheckFirmware", &MmSession::recheckFirmware},
				{"revealRomFolder", &MmSession::revealRomFolder}};
			if(const auto it = handlers.find(op); it != handlers.end())
				(this->*(it->second))(_message);
			else
				reply(_message, false, "unknown command " + op);
		}

		// {"op":"midi","b":[status, data1, data2]}: the page's keyboard and joystick.
		void midiFromPage(const Value& _message)
		{
			const auto* b = _message.find("b");
			std::array<int, 3> v{-1, 0, 0};
			if(b && b->isArray())
				for(size_t i = 0; i < 3 && i < b->asArray().size(); ++i)
					if(b->asArray()[i].isNumber())
						v[i] = static_cast<int>(b->asArray()[i].asNumber());
			const bool ok = v[0] >= 0x80 && v[0] < 0xf0 && v[1] >= 0 && v[1] < 128 && v[2] >= 0 && v[2] < 128
				&& sendMidi(static_cast<uint8_t>(v[0]), static_cast<uint8_t>(v[1]), static_cast<uint8_t>(v[2]));
			if(!ok)
				reply(_message, false, "midi: a channel message, [status 0x80-0xef, data, data]");
		}

		void recheckFirmware(const Value& _message)
		{
			using E = mmDesk::Desk::Engine;
			const auto e = m_link.engine();
			m_desk.setEngine(e);
			reply(_message, e != E::Missing && e != E::Unsupported, e == E::Missing
				? "No MM OS 1.32B ROM is running yet. After adding it, reopen the plug-in."
				: e == E::Unsupported ? "This ROM is not MM OS 1.32B." : "Firmware found");
		}

		void revealRomFolder(const Value& _message)
		{
			const juce::File folder(juce::String::fromUTF8(m_processor.getPublicRomFolder().c_str()));
			folder.createDirectory();
			folder.revealToUser();
			reply(_message, true, {});
		}

		void step() override
		{
			const auto s = ++m_steps;
			if(m_desk.engineId() == "hw")
			{
				stepWire(s);
				return;
			}
			m_desk.onTelemetry(m_link.readTelemetry());
			if(s % 4 != 0)
				return;
			const auto e = m_link.engine();
			if(e != m_lastEngine)
			{
				m_lastEngine = e;
				publishLcd(true);
			}
			m_desk.setEngine(e);
			// The machine's own screen while it starts; about 15 times a second when it changes.
			if(e != mmDesk::Desk::Engine::Ready && s % 8 == 0)
				publishLcd(false);
			std::vector<uint8_t> region;
			if(m_link.readWorkingKit(region))
				m_desk.onWorkingKit(region);
			// The working kit from memory follows host automation by itself.
			m_link.drainParameterChanges([](uint8_t, uint8_t, uint8_t, uint8_t) {});
			if(m_active)
				m_desk.tick();
		}

		std::string status() const override
		{
			return "desk: engine " + m_desk.engineId() + " " + deskCore::lifecycleName(m_desk.machine().lifecycle()) + " pattern "
				+ std::to_string(m_desk.currentPattern()) + " kit " + std::to_string(m_desk.currentKit()) + " loaded "
				+ std::to_string(m_desk.loaded()) + " recv " + m_desk.recv().stateName() + " round trip "
				+ std::to_string(static_cast<int>(m_desk.lastRoundTripMs())) + " ms";
		}

	protected:
		void onAttach() override
		{
			m_active = true;
			m_lcdSent = false;
		}

		void onDetach() override
		{
			m_desk.detachPage();
		}

	private:
		struct Engine
		{
			bool externalMidi = false;
			mmDesk::MmMachine::Port (MmSession::*device)();
		};

		static const std::map<std::string, Engine>& engines()
		{
			static const std::map<std::string, Engine> map{
				{"emu", {false, &MmSession::emuDevice}},
				{"hw", {true, &MmSession::wireDevice}}};
			return map;
		}

		mmDesk::MmMachine::Port emuDevice()
		{
			mmDesk::MmMachine::Port p;
			p.sendSysex = [this](const std::vector<uint8_t>& _m) { m_link.sendSysex(_m); };
			p.sendParam = [this](const uint8_t _t, const uint8_t _p, const uint8_t _i, const uint8_t _v) { m_link.setParam(_t, _p, _i, _v); };
			p.sendNrpn = [this](const uint8_t _t, const uint8_t _p, const uint8_t _v) { m_link.sendNrpn(_t, _p, _v); };
			p.pressKeys = [this](const std::vector<mmDesk::Key>& _k) { return m_link.pressKeys(_k); };
			return p;
		}

		uint8_t baseChannel() const
		{
			const auto g = m_desk.global(static_cast<uint8_t>(std::max(0, m_desk.currentGlobal())));
			return g ? static_cast<uint8_t>(g->baseChannel & 0x0f) : uint8_t(0);
		}

		// A real Monomachine at DIN speed: SysEx, CCs and NRPN out, PLAY/STOP as MIDI Start/Stop; no
		// panel, so dumps need the machine parked on SYSEX RECV by the user (capabilities say so).
		mmDesk::MmMachine::Port wireDevice()
		{
			mmDesk::MmMachine::Port p;
			p.sendSysex = [this](const std::vector<uint8_t>& _m) { m_pacer.push(_m); };
			p.sendParam = [this](const uint8_t _t, const uint8_t _p, const uint8_t _i, const uint8_t _v)
			{
				if(const auto m = md::automation::encodeParameterChange(md::MachineModel::Monomachine, {_p, _t, _i, _v}, baseChannel()))
					m_pacer.push({(*m)[0], (*m)[1], (*m)[2]});
			};
			p.sendNrpn = [this](const uint8_t _t, const uint8_t _p, const uint8_t _v)
			{
				const auto status = static_cast<uint8_t>(0xb0 | baseChannel());
				m_pacer.push({status, 99, static_cast<uint8_t>(_t & 0x7f)});
				m_pacer.push({status, 98, static_cast<uint8_t>(_p & 0x7f)});
				m_pacer.push({status, 6, static_cast<uint8_t>(_v & 0x7f)});
			};
			p.pressKeys = [this](const std::vector<mmDesk::Key>& _k)
			{
				for(const auto k : _k)
					if(k != mmDesk::Key::Play && k != mmDesk::Key::Stop)
						return false;
				for(const auto k : _k)
					m_pacer.push({static_cast<uint8_t>(k == mmDesk::Key::Play ? 0xfa : 0xfc)});
				return true;
			};
			return p;
		}

		mmDesk::Desk::Port port(const mmDesk::MmMachine::Port& _device)
		{
			mmDesk::Desk::Port p;
			p.sendSysex = _device.sendSysex;
			p.sendParam = _device.sendParam;
			p.sendNrpn = _device.sendNrpn;
			p.pressKeys = _device.pressKeys;
			p.toPage = [this](const Value& _m) { toPage(_m); };
			p.nowMs = [] { return nowMs(); };
			return p;
		}

		bool sendMidi(const uint8_t _status, const uint8_t _d1, const uint8_t _d2)
		{
			if(m_desk.engineId() != "hw")
				return m_link.sendMidi(_status, _d1, _d2);
			m_pacer.push({_status, _d1, _d2});
			return true;
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
			if(id != m_desk.engineId())
			{
				m_processor.setExternalMidi(it->second.externalMidi);
				m_pacer = {};
				m_desk.setEngineId(id, port((this->*(it->second.device))()));
			}
			reply(_message, true, it->second.externalMidi ? "HW MIDI: the editor talks to a Monomachine on the plug-in's MIDI in and "
				"out. Put it on GLOBAL › FILE › SYSEX RECV to send patterns, songs and globals." : "The emulated OS 1.32B again");
		}

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
			if(m_active && _s % 4 == 0)
				m_desk.tick();
		}

		// The firmware's own LCD for the page while the machine starts (and on request).
		void publishLcd(const bool _force)
		{
			if(!attached())
				return;
			std::array<uint8_t, 1024> bits{};
			if(!m_link.readLcd(bits))
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
			m.set("engine", deskCore::legacyFirmware(m_desk.machine().lifecycle()));
			toPage(m);
		}

		static MidiLearnCommands::Model learnModel()
		{
			MidiLearnCommands::Model m;
			m.pages = true;
			m.tracks = 6;
			m.refusal = "learn: a synth track's DATA page value or level (MIDI page values are NRPN, not learnable)";
			m.parameter = [](const MidiLearnCommands::Target& _t)
			{
				if(_t.pg < 0 || _t.pg > 7 || _t.i < 0 || _t.i > 7)
					return std::string();
				return std::string(MmStudioLink::parameterName(static_cast<uint8_t>(_t.pg), static_cast<uint8_t>(_t.i)));
			};
			m.targetOf = [](const std::string& _name) -> std::optional<MidiLearnCommands::Target>
			{
				for(uint8_t pg = 0; pg <= 7; ++pg)
					for(uint8_t i = 0; i < 8; ++i)
						if(*MmStudioLink::parameterName(pg, i) && _name == MmStudioLink::parameterName(pg, i))
							return MidiLearnCommands::Target{-1, pg, i};
				return {};
			};
			return m;
		}

		MmStudioLink m_link;
		mmDesk::Desk m_desk;
		MidiLearnCommands m_learn;
		mdDesk::DinPacer m_pacer;
		mmDesk::Desk::Engine m_lastEngine = mmDesk::Desk::Engine::Missing;
		std::array<uint8_t, 1024> m_lastLcd{};
		bool m_lcdSent = false;
		bool m_active = false;
	};

	std::unique_ptr<DeskSession> makeMmSession(AudioPluginAudioProcessor& _processor)
	{
		return std::make_unique<MmSession>(_processor);
	}
}
