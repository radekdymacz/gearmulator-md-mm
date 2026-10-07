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
			if(_desk.pageSeen() && due(_tick, g_samplesMs) && m_link.readSampleBank(bank))
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
		static elektronData::json::Value json(const elektronData::SyxKind _k, const Docs& _d, const uint8_t _s)
		{
			namespace ed = elektronData;
			switch(_k)
			{
			case ed::SyxKind::Pattern: return ed::patternToJson(_d.patterns.at(_s));
			case ed::SyxKind::Kit: return ed::kitToJson(_d.kits.at(_s));
			case ed::SyxKind::Song: return ed::songToJson(_d.songs.at(_s));
			default: return ed::globalToJson(_d.globals.at(_s));
			}
		}
		// What OS 1.63 takes as it is: the documents of its own formats (the one it would store) that validate.
		static std::string fits(const elektronData::SyxKind _k, const Docs& _d, const uint8_t _s)
		{
			namespace ed = elektronData;
			const auto older = [](const int _v, const int _r, const int _wv, const int _wr)
			{
				return _v == _wv && _r == _wr ? std::string() : "format " + std::to_string(_v) + "/" + std::to_string(_r) + " (OS 1.63 stores " + std::to_string(_wv) + "/" + std::to_string(_wr) + ")";
			};
			switch(_k)
			{
			case ed::SyxKind::Pattern: { const auto& p = _d.patterns.at(_s); auto w = older(p.version, p.revision, 3, 1); if(w.empty() && !ed::validate(p).empty()) w = ed::validate(p).front(); return w; }
			case ed::SyxKind::Kit: { const auto& k = _d.kits.at(_s); auto w = older(k.version, k.revision, 4, 1); if(w.empty() && !ed::validate(k).empty()) w = ed::validate(k).front(); return w; }
			case ed::SyxKind::Song: { const auto& g = _d.songs.at(_s); return older(g.version, g.revision, 2, 2); }
			default: { const auto& g = _d.globals.at(_s); return older(g.version, g.revision, 6, 1); }
			}
		}
		// The editor edits the active GLOBAL slot only; the file's others are left out.
		static bool importable(const elektronData::SyxItem& _i, const mdDesk::Desk& _d)
		{
			return _i.kind != elektronData::SyxKind::Global || (_d.documents().global && _d.documents().global->position == _i.slot);
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
