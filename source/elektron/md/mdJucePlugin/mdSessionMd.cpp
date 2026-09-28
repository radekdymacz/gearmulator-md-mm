#include "mdDeskSession.h"
#include "mdSessions.h"

#include "mdController.h"
#include "mdPluginProcessor.h"
#include "mdStudioLink.h"

#include "mdDesk/mdDesk.h"
#include "mdDesk/mdDeskWirePort.h"

#include "deskWire/mdWire.h"

#include <deque>

namespace mdJucePlugin
{
	using MdEngine = Engine<mdDesk::Desk>;

	namespace
	{
		double nowMs() { return sessionNowMs(); }

		// How often the emulator engine asks the device whether the firmware runs.
		constexpr double g_probeMs = 96;

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
			if(due(_tick, g_probeMs))
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

	// A real Machinedrum on the plug-in's MIDI in and out at DIN speed (deskWire): SysEx and CCs out
	// on the machine's base channel (a fact the adapter gives), PLAY/STOP as MIDI Start/Stop, SysEx
	// in. The plug-in's MIDI is the wire's while this engine lives. No panel, telemetry or memory.
	class MdWireEngine final : public MdEngine
	{
	public:
		explicit MdWireEngine(DeskSession& _session) : m_wire(_session.processor()) {}

		mdDesk::DevicePort device() override
		{
			return mdDesk::wirePort(m_wire.wire(), m_channel, [] { return nowMs(); });
		}

		deskCore::LifeFacts::Probe probe() override { return deskCore::LifeFacts::Probe::Running; }

		// What came in, to the desk. No telemetry: the adapter's own is "none" from the start.
		void step(mdDesk::Desk& _desk, uint64_t) override
		{
			m_wire.wire().pump(nowMs(), [&_desk](const std::vector<uint8_t>& _m) { _desk.onDeviceSysex(_m); });
		}

	private:
		PluginWire m_wire;
		uint8_t m_channel = 0;
	};

	// The Machinedrum Editor's session: the engine map, the learnable parameters, its page and the
	// editor's setup kept with the project.
	class MdSession final : public SessionOf<mdDesk::Desk>
	{
	public:
		explicit MdSession(AudioPluginAudioProcessor& _processor)
			: SessionOf<mdDesk::Desk>(_processor, engines(), learnModel(), page(), mdDesk::deskSetupToJson({}))
		{
		}

	private:
		static std::vector<Record> engines()
		{
			return {
				{mdDesk::emulatorProfile(), [](DeskSession& _s) { return std::make_unique<MdEmuEngine>(_s); }, {}},
				{mdDesk::wireProfile(), [](DeskSession& _s) { return std::make_unique<MdWireEngine>(_s); }, midiOutAvailability}};
		}

		static PageSpec page()
		{
			return {"mdStudio.html", "gearmulator-mdStudio.log", "GEARMULATOR_MDSTUDIO_SELFTEST", {"1", "p4", "p5", "p6"}, 1440};
		}

		// Every kit parameter and the level of any of the 16 tracks.
		static MidiLearnCommands::Model learnModel()
		{
			MidiLearnCommands::Model m;
			m.pages = false;
			m.tracks = 16;
			for(uint8_t i = 0; i <= deskWire::md::g_levelIndex; ++i)
				if(const std::string name = StudioLink::parameterName(i); !name.empty())
					m.params.push_back({{-1, -1, i}, name});
			return m;
		}

		const char* missingText() const override { return "No MD OS 1.63 ROM is running yet. After adding it, reopen the plug-in."; }
	};

	std::unique_ptr<DeskSession> makeMdSession(AudioPluginAudioProcessor& _processor)
	{
		return std::make_unique<MdSession>(_processor);
	}
}
