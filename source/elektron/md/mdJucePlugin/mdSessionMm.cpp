#include "mdDeskSession.h"
#include "mdSessions.h"

#include "mdController.h"
#include "mdPluginProcessor.h"
#include "mmStudioLink.h"

#include "mmDesk/mmDesk.h"

#include "deskCore/deskLcd.h"

#include "mdLib/mdautomation.h"

#include "juce_core/juce_core.h"

#include <array>

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
	// kit from memory, the probe and, while it starts, its own LCD.
	class MmEmuEngine final : public MmEngine
	{
	public:
		explicit MmEmuEngine(DeskSession& _session)
			: m_session(_session)
			, m_link(_session.processor(), dynamic_cast<Controller&>(_session.processor().getController()))
		{
			m_link.onSysex = [this](const std::vector<uint8_t>& _m)
			{
				if(m_desk)
					m_desk->onDeviceSysex(_m);
			};
		}

		mmDesk::MmMachine::Port device() override
		{
			mmDesk::MmMachine::Port p;
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
			_desk.onTelemetry(m_link.readTelemetry());
			if(_tick % 4 != 0)
				return;
			_desk.setProbe(m_link.probe());
			// The machine's own screen while it starts; about 15 times a second when it changes.
			if(m_session.attached() && !_desk.isInputReady() && _tick % 8 == 0)
			{
				std::vector<uint8_t> bits;
				if(m_link.readLcd(bits) && bits != m_lastLcd)
				{
					m_lastLcd = bits;
					m_session.toPage(deskCore::lcdMessage(bits));
				}
			}
			if(_desk.isInputReady())
				m_lastLcd.clear();
			std::vector<uint8_t> region;
			if(m_link.readWorkingKit(region))
				_desk.onWorkingKit(region);
			// The working kit from memory follows host automation by itself.
			m_link.drainParameterChanges([](uint8_t, uint8_t, uint8_t, uint8_t) {});
		}

	private:
		DeskSession& m_session;
		MmStudioLink m_link;
		std::vector<uint8_t> m_lastLcd;
	};

	// A real Monomachine on the plug-in's MIDI in and out at DIN speed: SysEx, CCs and NRPN out,
	// PLAY/STOP as MIDI Start/Stop; no panel, so dumps need the machine parked on SYSEX RECV by the
	// user (the capabilities say so).
	class MmWireEngine final : public MmEngine
	{
	public:
		explicit MmWireEngine(DeskSession& _session) : m_wire(_session.processor()) {}

		mmDesk::MmMachine::Port device() override
		{
			mmDesk::MmMachine::Port p;
			p.sendSysex = [this](const std::vector<uint8_t>& _m) { m_wire.send(_m); };
			p.sendParam = [this](const uint8_t _t, const uint8_t _p, const uint8_t _i, const uint8_t _v)
			{
				if(const auto m = md::automation::encodeParameterChange(md::MachineModel::Monomachine, {_p, _t, _i, _v}, baseChannel()))
					m_wire.send({(*m)[0], (*m)[1], (*m)[2]});
			};
			p.sendNrpn = [this](const uint8_t _t, const uint8_t _p, const uint8_t _v)
			{
				const auto status = static_cast<uint8_t>(0xb0 | baseChannel());
				m_wire.send({status, 99, static_cast<uint8_t>(_t & 0x7f)});
				m_wire.send({status, 98, static_cast<uint8_t>(_p & 0x7f)});
				m_wire.send({status, 6, static_cast<uint8_t>(_v & 0x7f)});
			};
			p.pressKeys = [this](const std::vector<mmDesk::Key>& _k)
			{
				for(const auto k : _k)
					if(k != mmDesk::Key::Play && k != mmDesk::Key::Stop)
						return false;
				for(const auto k : _k)
					m_wire.send({static_cast<uint8_t>(k == mmDesk::Key::Play ? 0xfa : 0xfc)});
				return true;
			};
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
			m_wire.pump([&_desk](const std::vector<uint8_t>& _m) { _desk.onDeviceSysex(_m); });
		}

	private:
		uint8_t baseChannel() const
		{
			const auto g = m_desk ? m_desk->global(static_cast<uint8_t>(std::max(0, m_desk->currentGlobal()))) : std::nullopt;
			return g ? static_cast<uint8_t>(g->baseChannel & 0x0f) : uint8_t(0);
		}

		MidiWire m_wire;
	};

	// The Monomachine Editor's session: the engine map, the learnable parameters and the page's MIDI.
	class MmSession final : public SessionOf<mmDesk::Desk>
	{
	public:
		explicit MmSession(AudioPluginAudioProcessor& _processor)
			: SessionOf<mmDesk::Desk>(_processor, engines(), learnModel(), [this](const mmDesk::Profile& _profile, const mmDesk::MmMachine::Port& _device)
			{
				mmDesk::Desk::Port p;
				p.sendSysex = _device.sendSysex;
				p.sendParam = _device.sendParam;
				p.sendNrpn = _device.sendNrpn;
				p.pressKeys = _device.pressKeys;
				p.nowMs = _device.nowMs;
				p.toPage = [this](const Value& _m) { toPage(_m); };
				return std::make_unique<mmDesk::Desk>(p, _profile);
			})
		{
		}

	private:
		static std::vector<Record> engines()
		{
			return {
				{mmDesk::emulatorProfile(), false, [](DeskSession& _s) { return std::make_unique<MmEmuEngine>(_s); }},
				{mmDesk::wireProfile(), true, [](DeskSession& _s) { return std::make_unique<MmWireEngine>(_s); }}};
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

		// {"op":"midi","b":[status, data1, data2]}: the page's keyboard and joystick, to the engine.
		void onMidi(const Value& _message) override
		{
			const auto* b = _message.find("b");
			std::array<int, 3> v{-1, 0, 0};
			if(b && b->isArray())
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
	};

	std::unique_ptr<DeskSession> makeMmSession(AudioPluginAudioProcessor& _processor)
	{
		return std::make_unique<MmSession>(_processor);
	}
}
