#include "mdDeskSession.h"
#include "mdSessions.h"

#include "mdController.h"
#include "mdPluginProcessor.h"
#include "mdStudioLink.h"

#include "mdDesk/mdDesk.h"

#include "mdLib/mdautomation.h"

#include "juce_core/juce_core.h"

#include <deque>

namespace mdJucePlugin
{
	namespace json = elektronData::json;
	using MdEngine = Engine<mdDesk::Desk>;

	namespace
	{
		double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

		// StudioLink's parameter index of a track's mute (after the 24 kit parameters and the level).
		constexpr uint8_t g_muteParam = 25;
	}

	// The emulated MD OS 1.63 (StudioLink): SysEx into the device, kit values through the parameter
	// layer, panel keys, and the device's facts: telemetry, the working kit from memory, the probe,
	// host parameter changes and, while it starts, its own LCD. What the device says is queued and
	// handed to the desk in step().
	class MdEmuEngine final : public MdEngine
	{
	public:
		explicit MdEmuEngine(DeskSession& _session)
			: m_link(_session.processor(), dynamic_cast<Controller&>(_session.processor().getController()))
		{
			m_link.onSysex = [this](const std::vector<uint8_t>& _m) { m_in.push_back(_m); };
		}

		mdDesk::DevicePort device() override
		{
			mdDesk::DevicePort p;
			p.sendSysex = [this](const std::vector<uint8_t>& _m) { m_link.sendSysex(_m); };
			p.sendKitParam = [this](const uint8_t _t, const uint8_t _i, const uint8_t _v) { m_link.setKitParam(_t, _i, _v); };
			p.sendMute = [this](const uint8_t _t, const bool _on) { m_link.setMute(_t, _on); };
			p.pressKey = [this](const std::string& _key) { return m_link.pressKey(_key); };
			p.turnKnob = [this](const uint8_t _e, const int _s) { return m_link.turnKnob(_e, _s); };
			p.nowMs = [] { return nowMs(); };
			return p;
		}

		deskCore::LifeFacts::Probe probe() override { return m_link.probe(); }

		void step(mdDesk::Desk& _desk, const uint64_t _tick) override
		{
			while(!m_in.empty())
			{
				const auto m = std::move(m_in.front());
				m_in.pop_front();
				_desk.onDeviceSysex(m);
			}
			if(_tick % 12 == 1)
				_desk.setProbe(m_link.probe());
			_desk.onTelemetry(m_link.readTelemetry());
			m_lcd.step(_desk, _tick, [this](std::vector<uint8_t>& _bits) { return m_link.readLcd(_bits); });
			std::vector<uint8_t> region;
			if(m_link.readWorkingKit(region))
				_desk.onWorkingKitMemory(region);
			m_link.drainParameterChanges([&_desk](const uint8_t _t, const uint8_t _i, const uint8_t _v)
			{
				if(_i == g_muteParam)
					_desk.onHostMute(_t, _v != 0);
				else
					_desk.onHostKitParam(_t, _i, _v);
			});
		}

	private:
		StudioLink m_link;
		std::deque<std::vector<uint8_t>> m_in;
		LcdFeed m_lcd;
	};

	// A real Machinedrum on the plug-in's MIDI in and out at DIN speed: SysEx and CCs out (on the
	// machine's base channel, a fact the adapter gives), PLAY/STOP as MIDI Start/Stop, SysEx in.
	// No panel, telemetry or memory.
	class MdWireEngine final : public MdEngine
	{
	public:
		explicit MdWireEngine(DeskSession& _session) : m_wire(midiWireOf(_session.processor())) {}

		mdDesk::DevicePort device() override
		{
			const auto cc = [this](const md::automation::ParameterChange& _c)
			{
				if(const auto m = md::automation::encodeParameterChange(md::MachineModel::Machinedrum, _c, m_channel))
					m_wire.send({(*m)[0], (*m)[1], (*m)[2]});
			};
			mdDesk::DevicePort p;
			p.sendSysex = [this](const std::vector<uint8_t>& _m) { m_wire.send(_m); };
			p.sendKitParam = [cc](const uint8_t _t, const uint8_t _i, const uint8_t _v)
			{
				constexpr uint8_t levelIndex = 24;
				cc({static_cast<uint8_t>(_i == levelIndex ? md::automation::machinedrum::Level : _i / 8), _t,
					static_cast<uint8_t>(_i == levelIndex ? 0 : _i % 8), _v});
			};
			p.sendMute = [cc](const uint8_t _t, const bool _on) { cc({md::automation::machinedrum::Mute, _t, 0, static_cast<uint8_t>(_on ? 1 : 0)}); };
			p.pressKey = [this](const std::string& _key)
			{
				const auto b = MidiWire::realtimeOf(_key);
				if(b)
					m_wire.send({b});
				return b != 0;
			};
			p.baseChannel = [this](const uint8_t _ch) { m_channel = _ch; };
			p.nowMs = [] { return nowMs(); };
			return p;
		}

		deskCore::LifeFacts::Probe probe() override { return deskCore::LifeFacts::Probe::Running; }

		void step(mdDesk::Desk& _desk, uint64_t) override
		{
			m_wire.pump(nowMs(), [&_desk](const std::vector<uint8_t>& _m) { _desk.onDeviceSysex(_m); });
			_desk.onTelemetry(mdDesk::Telemetry{});
		}

	private:
		MidiWire m_wire;
		uint8_t m_channel = 0;
	};

	// The Machinedrum Editor's session: the engine map, the learnable parameters, its page and the
	// editor's setup kept with the project.
	class MdSession final : public SessionOf<mdDesk::Desk>
	{
	public:
		explicit MdSession(AudioPluginAudioProcessor& _processor)
			: SessionOf<mdDesk::Desk>(_processor, engines(), learnModel(), page(), [this](std::unique_ptr<mdDesk::MdAdapter> _adapter)
			{
				mdDesk::Desk::Port p;
				p.device.nowMs = [] { return nowMs(); };
				p.toPage = [this](const Value& _m) { toPage(_m); };
				p.saveSetup = [this](const Value& _setup)
				{
					m_processor.setDeskSetup(json::write(_setup));
					m_setupVersion = m_processor.getDeskSetupVersion();
				};
				return std::make_unique<mdDesk::Desk>(std::move(_adapter), p);
			})
		{
			loadSetup();
		}

	private:
		static std::vector<Record> engines()
		{
			return {
				{mdDesk::emulatorProfile(), [](DeskSession& _s) { return std::make_unique<MdEmuEngine>(_s); }},
				{mdDesk::wireProfile(), [](DeskSession& _s) { return std::make_unique<MdWireEngine>(_s); }}};
		}

		static WebPageHost::Spec page()
		{
			return {"mdStudio.html", "gearmulator-mdStudio.log", "GEARMULATOR_MDSTUDIO_SELFTEST", {"1", "p4", "p5", "p6"}, 1440};
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

		// A project restore brings its setup: the desk takes it.
		void stepExtra(uint64_t) override
		{
			if(m_processor.getDeskSetupVersion() != m_setupVersion)
				loadSetup();
		}

		// The editor's setup from the project (the processor's MDSK chunk), or the default one.
		void loadSetup()
		{
			m_setupVersion = m_processor.getDeskSetupVersion();
			const auto text = m_processor.getDeskSetup();
			const auto doc = text.empty() ? std::optional<json::Value>(mdDesk::deskSetupToJson({})) : json::parse(text);
			if(doc)
				desk().loadSetup(*doc);
		}

		std::string statusExtra() const override
		{
			const auto& st = desk().linkState();
			const auto& d = desk().documents();
			return "pattern " + std::to_string(st.pattern ? *st.pattern : -1) + " kit " + std::to_string(st.kit ? *st.kit : -1)
				+ " docs p/k/s " + std::to_string(d.patterns.size()) + "/" + std::to_string(d.kits.size()) + "/"
				+ std::to_string(d.songs.size()) + " round trip " + std::to_string(static_cast<int>(desk().lastRoundTripMs())) + " ms";
		}

		const char* missingText() const override { return "No MD OS 1.63 ROM is running yet. After adding it, reopen the plug-in."; }

		uint32_t m_setupVersion = 0;
	};

	std::unique_ptr<DeskSession> makeMdSession(AudioPluginAudioProcessor& _processor)
	{
		return std::make_unique<MdSession>(_processor);
	}
}
