#include "mdDeskSession.h"
#include "mdSessions.h"

#include "mdController.h"
#include "mdPluginProcessor.h"
#include "mmStudioLink.h"

#include "mmDesk/mmDesk.h"

#include "mdLib/mdautomation.h"

#include "juce_core/juce_core.h"

#include <array>
#include <deque>

namespace mdJucePlugin
{
	namespace json = elektronData::json;

	namespace
	{
		double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
	}

	// A Monomachine engine also plays the page's keyboard and joystick (channel messages).
	class MmEngine : public Engine<mmDesk::Desk>
	{
	public:
		virtual bool sendMidi(uint8_t _status, uint8_t _data1, uint8_t _data2) = 0;
	};

	// The emulated MM OS 1.32B (MmStudioLink): SysEx into the device, kit values through the parameter
	// layer, NRPN, panel keys timed in machine time, and the device's facts: telemetry, the working
	// kit from memory, the probe and, while it starts, its own LCD. What the device says is queued and
	// handed to the desk in step().
	class MmEmuEngine final : public MmEngine
	{
	public:
		explicit MmEmuEngine(DeskSession& _session)
			: m_link(_session.processor(), dynamic_cast<Controller&>(_session.processor().getController()))
		{
			m_link.onSysex = [this](const std::vector<uint8_t>& _m) { m_in.push_back(_m); };
		}

		mmDesk::DevicePort device() override
		{
			mmDesk::DevicePort p;
			p.sendSysex = [this](const std::vector<uint8_t>& _m) { m_link.sendSysex(_m); };
			p.sendParam = [this](const uint8_t _t, const uint8_t _p, const uint8_t _i, const uint8_t _v) { m_link.setParam(_t, _p, _i, _v); };
			p.sendNrpn = [this](const uint8_t _t, const uint8_t _p, const uint8_t _v) { m_link.sendNrpn(_t, _p, _v); };
			p.pressKeys = [this](const std::vector<mmDesk::Key>& _k) { return m_link.pressKeys(_k); };
			p.nowMs = [] { return nowMs(); };
			return p;
		}

		deskCore::LifeFacts::Probe probe() override { return m_link.probe(); }
		bool sendMidi(const uint8_t _s, const uint8_t _d1, const uint8_t _d2) override { return m_link.sendMidi(_s, _d1, _d2); }

		void step(mmDesk::Desk& _desk, const uint64_t _tick) override
		{
			while(!m_in.empty())
			{
				const auto m = std::move(m_in.front());
				m_in.pop_front();
				_desk.onDeviceSysex(m);
			}
			_desk.onTelemetry(m_link.readTelemetry());
			if(_tick % 4 != 0)
				return;
			_desk.setProbe(m_link.probe());
			m_lcd.step(_desk, _tick, [this](std::vector<uint8_t>& _bits) { return m_link.readLcd(_bits); });
			std::vector<uint8_t> region;
			if(m_link.readWorkingKit(region))
				_desk.onWorkingKit(region);
			// The working kit from memory follows host automation by itself.
			m_link.drainParameterChanges([](uint8_t, uint8_t, uint8_t, uint8_t) {});
		}

	private:
		MmStudioLink m_link;
		std::deque<std::vector<uint8_t>> m_in;
		LcdFeed m_lcd;
	};

	// A real Monomachine on the plug-in's MIDI in and out at DIN speed: SysEx, CCs and NRPN out (on the
	// machine's base channel, a fact the adapter gives), PLAY/STOP as MIDI Start/Stop; no panel, so
	// dumps need the machine parked on SYSEX RECV by the user (the capabilities say so).
	class MmWireEngine final : public MmEngine
	{
	public:
		explicit MmWireEngine(DeskSession& _session) : m_wire(midiWireOf(_session.processor())) {}

		mmDesk::DevicePort device() override
		{
			mmDesk::DevicePort p;
			p.sendSysex = [this](const std::vector<uint8_t>& _m) { m_wire.send(_m); };
			p.sendParam = [this](const uint8_t _t, const uint8_t _p, const uint8_t _i, const uint8_t _v)
			{
				if(const auto m = md::automation::encodeParameterChange(md::MachineModel::Monomachine, {_p, _t, _i, _v}, m_channel))
					m_wire.send({(*m)[0], (*m)[1], (*m)[2]});
			};
			p.sendNrpn = [this](const uint8_t _t, const uint8_t _p, const uint8_t _v)
			{
				const auto status = static_cast<uint8_t>(0xb0 | m_channel);
				m_wire.send({status, 99, static_cast<uint8_t>(_t & 0x7f)});
				m_wire.send({status, 98, static_cast<uint8_t>(_p & 0x7f)});
				m_wire.send({status, 6, static_cast<uint8_t>(_v & 0x7f)});
			};
			p.pressKeys = [this](const std::vector<mmDesk::Key>& _k)
			{
				std::vector<uint8_t> bytes;
				for(const auto k : _k)
				{
					const auto b = MidiWire::realtimeOf(k == mmDesk::Key::Play ? "play" : k == mmDesk::Key::Stop ? "stop" : "");
					if(!b)
						return false;
					bytes.push_back(b);
				}
				for(const auto b : bytes)
					m_wire.send({b});
				return true;
			};
			p.baseChannel = [this](const uint8_t _ch) { m_channel = _ch; };
			p.nowMs = [] { return nowMs(); };
			return p;
		}

		deskCore::LifeFacts::Probe probe() override { return deskCore::LifeFacts::Probe::Running; }
		bool sendMidi(const uint8_t _s, const uint8_t _d1, const uint8_t _d2) override
		{
			m_wire.send({_s, _d1, _d2});
			return true;
		}

		void step(mmDesk::Desk& _desk, uint64_t) override
		{
			m_wire.pump(nowMs(), [&_desk](const std::vector<uint8_t>& _m) { _desk.onDeviceSysex(_m); });
		}

	private:
		MidiWire m_wire;
		uint8_t m_channel = 0;
	};

	// The Monomachine Editor's session: the engine map, the learnable parameters, its page, the page's
	// MIDI and the app modulators kept with the project.
	class MmSession final : public SessionOf<mmDesk::Desk>
	{
	public:
		explicit MmSession(AudioPluginAudioProcessor& _processor)
			: SessionOf<mmDesk::Desk>(_processor, engines(), learnModel(), page(), [this](std::unique_ptr<mmDesk::MmAdapter> _adapter)
			{
				mmDesk::Desk::Port p;
				p.device.nowMs = [] { return nowMs(); };
				p.toPage = [this](const Value& _m) { toPage(_m); };
				p.saveSetup = [this](const Value& _setup)
				{
					m_processor.setDeskSetup(json::write(_setup));
					m_setupVersion = m_processor.getDeskSetupVersion();
				};
				return std::make_unique<mmDesk::Desk>(std::move(_adapter), p);
			})
		{
			loadSetup();
		}

	private:
		static std::vector<Record> engines()
		{
			return {
				{mmDesk::emulatorProfile(), [](DeskSession& _s) { return std::make_unique<MmEmuEngine>(_s); }},
				{mmDesk::wireProfile(), [](DeskSession& _s) { return std::make_unique<MmWireEngine>(_s); }}};
		}

		static WebPageHost::Spec page()
		{
			return {"mmStudio.html", "gearmulator-mmStudio.log", "GEARMULATOR_MMSTUDIO_SELFTEST", {"1", "mmcpu", "p6"}, 1440};
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

		// A project restore brings its modulators: the desk takes them.
		void stepExtra(uint64_t) override
		{
			if(m_processor.getDeskSetupVersion() != m_setupVersion)
				loadSetup();
		}

		// The app modulators from the project (the processor's setup chunk), if any.
		void loadSetup()
		{
			m_setupVersion = m_processor.getDeskSetupVersion();
			const auto text = m_processor.getDeskSetup();
			if(text.empty())
				return;
			if(const auto doc = json::parse(text))
				desk().loadSetup(*doc);
		}

		// {"op":"midi","b":[status, data1, data2]}: the page's keyboard and joystick, to the engine (the
		// table checked b is a list; its values are checked here).
		void onMidi(const Value& _message) override
		{
			const auto* b = _message.find("b");
			std::array<int, 3> v{-1, 0, 0};
			for(size_t i = 0; i < 3 && i < b->asArray().size(); ++i)
				if(b->asArray()[i].isNumber())
					v[i] = static_cast<int>(b->asArray()[i].asNumber());
			auto& engine = static_cast<MmEngine&>(currentEngine());
			const bool ok = v[0] >= 0x80 && v[0] < 0xf0 && v[1] >= 0 && v[1] < 128 && v[2] >= 0 && v[2] < 128
				&& engine.sendMidi(static_cast<uint8_t>(v[0]), static_cast<uint8_t>(v[1]), static_cast<uint8_t>(v[2]));
			if(!ok)
				reply(_message, false, "midi: a channel message, [status 0x80-0xef, data, data]");
		}

		std::string statusExtra() const override
		{
			return "pattern " + std::to_string(desk().currentPattern()) + " kit " + std::to_string(desk().currentKit()) + " loaded "
				+ std::to_string(desk().loaded()) + " recv " + desk().recv().stateName() + " round trip "
				+ std::to_string(static_cast<int>(desk().lastRoundTripMs())) + " ms";
		}

		const char* missingText() const override { return "No MM OS 1.32B ROM is running yet. After adding it, reopen the plug-in."; }

		uint32_t m_setupVersion = 0;
	};

	std::unique_ptr<DeskSession> makeMmSession(AudioPluginAudioProcessor& _processor)
	{
		return std::make_unique<MmSession>(_processor);
	}
}
