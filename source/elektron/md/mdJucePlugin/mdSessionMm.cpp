#include "mdDeskSession.h"
#include "mdSessions.h"

#include "mdController.h"
#include "mdPluginProcessor.h"
#include "mmStudioLink.h"

#include "mmDesk/mmDesk.h"
#include "mmDesk/mmDeskWirePort.h"

#include "deskWire/mmWire.h"

#include "elektronData/mmJson.h"
#include "elektronData/mmKit.h"
#include "elektronData/mmValidate.h"

#include <deque>

namespace mdJucePlugin
{
	namespace
	{
		double nowMs() { return sessionNowMs(); }

		// How often the emulator engine asks the device for the probe and the working kit.
		constexpr double g_probeMs = 32;
		constexpr double g_memoryMs = 32;
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
			if(due(_tick, g_probeMs))
				_desk.setProbe(m_link.probe());
			m_lcd.step(_desk, _tick, [this](std::vector<uint8_t>& _bits) { return m_link.readLcd(_bits); });
			std::vector<uint8_t> region;
			if(due(_tick, g_memoryMs) && m_link.readWorkingKit(region))
				_desk.onWorkingKit(region);
			// The working kit from memory follows host automation by itself.
			m_link.drainParameterChanges([](uint8_t, uint8_t, uint8_t, uint8_t) {});
		}

	private:
		MmStudioLink m_link;
		std::deque<std::vector<uint8_t>> m_in;
		LcdFeed m_lcd;
	};

	// A real Monomachine on the plug-in's MIDI in and out at DIN speed (deskWire): SysEx, CCs and NRPN
	// out on the machine's base channel (a fact the adapter gives), PLAY/STOP as MIDI Start/Stop. The
	// plug-in's MIDI is the wire's while this engine lives. No panel, so dumps need the machine parked
	// on SYSEX RECV by the user (the capabilities say so).
	class MmWireEngine final : public MmEngine
	{
	public:
		explicit MmWireEngine(DeskSession& _session) : m_wire(_session.processor()) {}

		mmDesk::DevicePort device() override
		{
			return mmDesk::wirePort(m_wire.wire(), m_channel, [] { return nowMs(); });
		}

		deskCore::LifeFacts::Probe probe() override { return deskCore::LifeFacts::Probe::Running; }
		bool sendMidi(const uint8_t _s, const uint8_t _d1, const uint8_t _d2) override
		{
			m_wire.wire().send(deskWire::channelMessage(_s, _d1, _d2));
			return true;
		}

		void step(mmDesk::Desk& _desk, uint64_t) override
		{
			m_wire.wire().pump(nowMs(), [&_desk](const std::vector<uint8_t>& _m) { _desk.onDeviceSysex(_m); });
		}

	private:
		PluginWire m_wire;
		uint8_t m_channel = 0;
	};

	// The Monomachine Editor's session: the engine map, the learnable parameters, its page, the page's
	// MIDI and the app modulators kept with the project.
	// P7: the Monomachine's SysEx import traits (mdSyxSession.h)
	template<> struct SyxTraits<mmDesk::Desk>
	{
		using Docs = elektronData::MmDocuments;
		static constexpr elektronData::SyxModel model = elektronData::SyxModel::Mm;
		static constexpr const char* name = "Monomachine";
		static const Docs& docs(const elektronData::SyxFile& _f) { return _f.mm; }
		static Docs machine(const mmDesk::Desk& _d)
		{
			const auto& v = _d.documents();
			Docs o;
			o.patterns = v.patterns;
			o.kits = v.kits;
			o.songs = v.songs;
			o.globals = v.globals;
			return o;
		}
		static elektronData::json::Value json(const elektronData::SyxKind _k, const Docs& _d, const uint8_t _s)
		{
			namespace ed = elektronData;
			switch(_k)
			{
			case ed::SyxKind::Pattern: return ed::mmPatternToJson(_d.patterns.at(_s));
			case ed::SyxKind::Kit: return ed::mmKitToJson(_d.kits.at(_s));
			case ed::SyxKind::Song: return ed::mmSongToJson(_d.songs.at(_s));
			default: return ed::mmGlobalToJson(_d.globals.at(_s));
			}
		}
		static bool importable(const elektronData::SyxItem&, const mmDesk::Desk&) { return true; }
		// What OS 1.32B takes as it is: dumps of its own formats and sizes that validate (an older OS's kit
		// dump is shorter; the firmware ignores it).
		static std::string fits(const elektronData::SyxKind _k, const Docs& _d, const uint8_t _s)
		{
			namespace ed = elektronData;
			const auto older = [](const int _v, const int _r, const int _wv, const int _wr)
			{
				return _v == _wv && _r == _wr ? std::string() : "format " + std::to_string(_v) + "/" + std::to_string(_r) + " (OS 1.32B stores " + std::to_string(_wv) + "/" + std::to_string(_wr) + ")";
			};
			switch(_k)
			{
			// patterns of format 5/1 (an older OS) are stored as they are (measured on the firmware): only validation
			case ed::SyxKind::Pattern: { const auto& p = _d.patterns.at(_s); const auto v = ed::validate(p); return v.empty() ? std::string() : v.front(); }
			case ed::SyxKind::Kit:
			{
				static const auto size = ed::encodeMmKit(ed::MmKit{}).size();
				const auto& k = _d.kits.at(_s);
				auto w = older(k.version, k.revision, 2, 1);
				if(w.empty() && ed::encodeMmKit(k).size() != size) w = "an older OS's kit (" + std::to_string(ed::encodeMmKit(k).size()) + " bytes, OS 1.32B's are " + std::to_string(size) + ")";
				return w;
			}
			case ed::SyxKind::Song: { const auto& g = _d.songs.at(_s); return older(g.version, g.revision, 2, 1); }
			default: { const auto& g = _d.globals.at(_s); return older(g.version, g.revision, 3, 1); }
			}
		}
	};

	// The controller profile on the Monomachine (DESIGN-tr06.md): a voice plays its track's channel (base +
	// track) at its note; the knobs' edits of one round are one working-kit set, as the page's own edits
	// (the adapter sends what changed as CCs: a live edit, never a dump for a DATA page value or a level).
	template<> struct CtlTraits<mmDesk::Desk>
	{
		static constexpr deskController::Machine machine = deskController::Machine::Mm;

		static deskController::Route route(const mmDesk::Desk& _desk, const deskController::Setup& _setup)
		{
			const int slot = _desk.currentGlobal();
			const auto g = slot >= 0 ? _desk.global(static_cast<uint8_t>(slot)) : std::nullopt;
			return deskController::mmRoute(_setup, g ? &*g : nullptr);
		}

		static void apply(mmDesk::Desk& _desk, const std::vector<deskController::Edit>& _edits)
		{
			if(const auto& working = _desk.documents().working)
				if(const auto c = deskController::mmCommand(_edits, working->kit))
					_desk.onPageMessage(*c);
		}
	};

	class MmSession final : public SessionOf<mmDesk::Desk, MmEngine>
	{
	public:
		explicit MmSession(AudioPluginAudioProcessor& _processor)
			: SessionOf<mmDesk::Desk, MmEngine>(_processor, engines(), learnModel(), page(), std::nullopt)
		{
		}

	private:
		static std::vector<Record> engines()
		{
			return {
				{mmDesk::emulatorProfile(), [](DeskSession& _s) { return std::make_unique<MmEmuEngine>(_s); }, {}},
				{mmDesk::wireProfile(), [](DeskSession& _s) { return std::make_unique<MmWireEngine>(_s); }, midiOutAvailability}};
		}

		static PageSpec page()
		{
			return {"mmStudio.html", "gearmulator-mmStudio.log", "GEARMULATOR_MMSTUDIO_SELFTEST", {"1", "mmcpu", "p4", "p6", "p7"}, 1440};
		}

		// A synth track's DATA page values and level: the parameters that have a plug-in parameter.
		static MidiLearnCommands::Model learnModel()
		{
			MidiLearnCommands::Model m;
			m.pages = true;
			m.tracks = 6;
			m.refusal = "learn: a synth track's DATA page value or level (MIDI page values are NRPN, not learnable)";
			constexpr uint8_t dataPages = 7, levelPage = 7;
			for(uint8_t pg = 0; pg < dataPages; ++pg)
				for(uint8_t i = 0; i < 8; ++i)
					if(const std::string name = MmStudioLink::parameterName(pg, i); !name.empty())
						m.params.push_back({{-1, pg, i}, name});
			m.params.push_back({{-1, levelPage, 0}, MmStudioLink::parameterName(levelPage, 0)});
			return m;
		}

		// {"op":"midi","b":[status, data1, data2]}: the page's keyboard and joystick, to the engine. The
		// table checked b is three integers 0..0xef; which is the status and which are data is here.
		void onMidi(const Value& _message) override
		{
			const auto& b = _message.find("b")->asArray();
			const auto v = [&](const size_t _i) { return static_cast<int>(b[_i].asNumber()); };
			auto& engine = currentEngine();
			const bool ok = v(0) >= 0x80 && v(1) < 0x80 && v(2) < 0x80
				&& engine.sendMidi(static_cast<uint8_t>(v(0)), static_cast<uint8_t>(v(1)), static_cast<uint8_t>(v(2)));
			if(!ok)
				reply(_message, false, "midi: a channel message, [status 0x80-0xef, data, data]");
		}

		const char* missingText() const override { return "No MM OS 1.32B ROM is running yet. After adding it, reopen the plug-in."; }
	};

	std::unique_ptr<DeskSession> makeMmSession(AudioPluginAudioProcessor& _processor)
	{
		return std::make_unique<MmSession>(_processor);
	}
}
