#include "mdDeskSession.h"
#include "mdSessions.h"

#include "mdController.h"
#include "mdPluginProcessor.h"
#include "mdStudioLink.h"

#include "mdDesk/mdDesk.h"
#include "mdDesk/mdDeskWirePort.h"

#include "deskWire/mdWire.h"

#include "elektronData/mdJson.h"
#include "elektronData/mdValidate.h"

#include <deque>

namespace mdJucePlugin
{
	using MdEngine = Engine<mdDesk::Desk>;

	namespace
	{
		double nowMs() { return sessionNowMs(); }

		// How often the emulator engine asks the device whether the firmware runs.
		constexpr double g_probeMs = 96;
		// P9: how often it looks for a new UW sample bank (the device reads it only when its memory changed).
		constexpr double g_samplesMs = 250;

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
			p.sendNote = [this](const uint8_t _ch, const uint8_t _n, const uint8_t _v) { m_link.sendNote(_ch, _n, _v); };
			// The keyboard's held PTCH as the machine's CC, not the plug-in's parameter: no DAW sees it.
			p.sendHeldParam = [this](const uint8_t _t, const uint8_t _i, const uint8_t _v)
			{
				if(const auto cc = deskWire::md::kitParam(m_channel, _t, _i, _v))
					m_link.sendChannel(*cc);
			};
			p.baseChannel = [this](const uint8_t _ch) { m_channel = _ch; };
			p.pressKey = [this](const std::string& _key) { return m_link.pressKey(_key); };
			p.turnKnob = [this](const uint8_t _e, const int _s) { return m_link.turnKnob(_e, _s); };
			p.nowMs = [] { return nowMs(); };
			p.audition = [this](const elektronData::AuditionClip& _clip) { return m_link.audition(_clip); };
			p.auditionStatus = [this] { return m_link.auditionStatus(); };
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
			std::shared_ptr<const elektronData::MdSampleBank> bank;
			// A desk without a bank (none yet, or a reset forgot it) gets the device's current one again (B-012).
			if(_desk.pageSeen() && due(_tick, g_samplesMs) && m_link.readSampleBank(bank, !_desk.hasSamples()))
				_desk.onSampleBank(*bank);
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
		uint8_t m_channel = 0;	// the machine's base channel (the adapter's fact, baseChannel)
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
	// P7: the Machinedrum's SysEx import traits (mdSyxSession.h)
	template<> struct SyxTraits<mdDesk::Desk>
	{
		using Docs = elektronData::MdDocuments;
		static constexpr elektronData::SyxModel model = elektronData::SyxModel::Md;
		static constexpr const char* name = "Machinedrum";
		static const Docs& docs(const elektronData::SyxFile& _f) { return _f.md; }
		static Docs machine(const mdDesk::Desk& _d)
		{
			const auto& v = _d.documents();
			Docs o;
			o.patterns = v.patterns;
			o.kits = v.kits;
			o.songs = v.songs;
			if(v.global)
				o.globals[v.global->position] = *v.global;
			return o;
		}
		// The slots the machine is on (B-019: the preview marks the imported items that land in what plays).
		static SyxPlaying playing(const mdDesk::Desk& _d)
		{
			const auto& s = _d.linkState();
			const auto of = [](const std::optional<uint8_t>& _v) { return _v ? static_cast<int>(*_v) : -1; };
			return {of(s.pattern), of(s.kit), of(s.song), of(s.globalSlot)};
		}
	};

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
			return {"mdStudio.html", "gearmulator-mdStudio.log", "GEARMULATOR_MDSTUDIO_SELFTEST", {"1", "p4", "p5", "p6", "p7", "journey", "demo"}, 1440};
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

		// P9: a sample file the user chose for a UW ROM slot: read here (never through the page), then the
		// desk converts it and sends it as SDS; the page hears sampleLoad messages.
		void loadSampleFile(const uint8_t _slot, const juce::File& _file) override
		{
			juce::MemoryBlock mb;
			if(_file.getSize() > 512 * 1024 * 1024 || !_file.loadFileAsData(mb))
			{
				desk().loadSample(_slot, _file.getFileName().toStdString(), {});
				return;
			}
			const auto* d = static_cast<const uint8_t*>(mb.getData());
			desk().loadSample(_slot, _file.getFileName().toStdString(), std::vector<uint8_t>(d, d + mb.getSize()));
		}
	};

	std::unique_ptr<DeskSession> makeMdSession(AudioPluginAudioProcessor& _processor)
	{
		return std::make_unique<MdSession>(_processor);
	}
}
