#pragma once

#include "mdMidiLearnCommands.h"

#include "deskCore/deskCapabilities.h"
#include "deskCore/deskCommands.h"
#include "deskCore/deskLifecycle.h"
#include "deskCore/deskPacer.h"

#include "elektronData/json.h"

#include "juce_events/juce_events.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;
	pluginLib::Processor& pluginProcessorOf(AudioPluginAudioProcessor& _processor);

	// The editor's session (P6), owned by the processor: the desk (core, Machine adapter, the
	// editor's setup), the engine that feeds it (the device edge and its facts: the emulator, or a
	// machine over the plug-in's MIDI) and the plug-in's own commands (MIDI learn, the engine map,
	// the ROM folder). It lives as long as the plug-in instance, so documents, undo, the clipboard,
	// pushes in flight, the engine choice and the app modulators survive the editor window. A page
	// view attaches to it and detaches when the window closes. Message thread only.
	class DeskSession : juce::Timer
	{
	public:
		using Value = elektronData::json::Value;
		using ToPage = std::function<void(const Value&)>;

		explicit DeskSession(AudioPluginAudioProcessor& _processor);
		~DeskSession() override;

		DeskSession(const DeskSession&) = delete;
		DeskSession& operator=(const DeskSession&) = delete;

		// The Machinedrum or Monomachine session for the processor's model.
		static std::unique_ptr<DeskSession> create(AudioPluginAudioProcessor& _processor);

		void attach(ToPage _toPage);
		void detach();
		bool attached() const { return static_cast<bool>(m_toPage); }

		// A page message: the desk's commands and the plug-in's (the command table's owners).
		virtual void onPageMessage(const Value& _message) = 0;
		// What the plug-in does for this message (the table's host column): the view acts on Audio
		// and Menu itself, the session on the rest.
		virtual deskCore::HostOp hostOp(const Value& _message) const = 0;
		// One step of the session: the engine's facts in, the desk's work, the page's messages out.
		// The timer calls it; tests call it directly.
		virtual void step() = 0;
		// One line of state for the diagnostics log.
		virtual std::string status() const = 0;

		void toPage(const Value& _message) const;
		AudioPluginAudioProcessor& processor() const { return m_processor; }

	protected:
		void reply(const Value& _message, bool _ok, const std::string& _note) const;
		void setExternalMidi(bool _on) const;
		void revealRomFolder(const Value& _message) const;
		virtual void onAttach() {}
		virtual void onDetach() {}

		AudioPluginAudioProcessor& m_processor;

	private:
		void timerCallback() override { step(); }

		ToPage m_toPage;
	};

	// A machine at the end of the plug-in's MIDI in and out (HW MIDI, P4), for either model: what
	// goes out is paced at DIN speed, SysEx that comes in goes to the desk. The engines over it only
	// say how their machine's messages are encoded.
	class MidiWire
	{
	public:
		explicit MidiWire(AudioPluginAudioProcessor& _processor);

		void send(std::vector<uint8_t> _message) { m_pacer.push(std::move(_message)); }
		// Out at DIN speed, then the machine's SysEx to _in.
		void pump(const std::function<void(const std::vector<uint8_t>&)>& _in);

	private:
		AudioPluginAudioProcessor& m_processor;
		deskCore::DinPacer m_pacer;
	};

	// An engine of the engine map, for a model (P6): the device edge its desk's adapter talks to and
	// the facts it feeds the desk. A third engine is one class implementing this plus one record.
	template<typename DeskT>
	class Engine
	{
	public:
		using DevicePort = typename DeskT::DevicePort;
		virtual ~Engine() = default;

		virtual DevicePort device() = 0;
		// Feed the desk what the device says (sysex, telemetry, memory, probes). _tick counts the
		// session's 8 ms steps.
		virtual void step(DeskT& _desk, uint64_t _tick) = 0;
		// What the device says about the firmware now (Running for a wire: its replies decide).
		virtual deskCore::LifeFacts::Probe probe() = 0;

		// The desk it feeds, from the moment it is made (replies to the desk's first requests arrive
		// before the first step).
		void bindDesk(DeskT& _desk) { m_desk = &_desk; }

	protected:
		DeskT* m_desk = nullptr;
	};

	// One engine of the engine map: its profile (id, label and what its wire allows), whether the
	// plug-in's MIDI in/out go to it, and how to make it.
	template<typename DeskT>
	struct EngineRecord
	{
		using Profile = typename DeskT::Profile;
		Profile profile;
		bool externalMidi = false;
		std::function<std::unique_ptr<Engine<DeskT>>(DeskSession&)> make;
	};

	// The session for one model (P6: one class for both editors). The engine map, the host commands
	// and the step are here; a model adds its desk, its engines, its learnable parameters and what
	// is its own (the MD's setup, the MM's page MIDI).
	template<typename DeskT>
	class SessionOf : public DeskSession
	{
	public:
		using Record = EngineRecord<DeskT>;

		SessionOf(AudioPluginAudioProcessor& _processor, std::vector<Record> _engines, MidiLearnCommands::Model _learn,
			const std::function<std::unique_ptr<DeskT>(const typename DeskT::Profile&, const typename DeskT::DevicePort&)>& _makeDesk)
			: DeskSession(_processor)
			, m_engines(std::move(_engines))
			, m_learn(pluginProcessorOf(_processor), std::move(_learn), [this](const Value& _m) { toPage(_m); })
		{
			m_engine = m_engines.front().make(*this);
			m_desk = _makeDesk(m_engines.front().profile, m_engine->device());
			m_engine->bindDesk(*m_desk);
			m_desk->setEngines(choices());
		}

		~SessionOf() override { setExternalMidi(false); }

		deskCore::HostOp hostOp(const Value& _message) const override { return DeskT::hostOp(_message); }

		void onPageMessage(const Value& _message) override
		{
			const auto op = deskCore::opOf(_message);
			if(m_desk->onPageMessage(_message))
			{
				if(op == "ready")
					onReadyExtra();
				return;
			}
			switch(DeskT::hostOp(_message))
			{
			case deskCore::HostOp::Engine: setEngine(_message); break;
			case deskCore::HostOp::RecheckFirmware: recheckFirmware(_message); break;
			case deskCore::HostOp::RevealRomFolder: revealRomFolder(_message); break;
			case deskCore::HostOp::Learn: m_learn.handle(_message); break;
			case deskCore::HostOp::Midi: onMidi(_message); break;
			default: reply(_message, false, "unknown command " + op); break;
			}
		}

		void step() override
		{
			const auto t = ++m_tick;
			m_engine->step(*m_desk, t);
			stepExtra(t);
			if(m_active && t % 4 == 0)
				m_desk->tick();
		}

		std::string status() const override
		{
			return "desk: engine " + m_desk->engine() + " " + deskCore::lifecycleName(m_desk->lifecycle()) + " " + statusExtra();
		}

		DeskT& desk() { return *m_desk; }
		const DeskT& desk() const { return *m_desk; }
		const Record& record() const { return *m_record; }

	protected:
		virtual void onReadyExtra() { m_learn.publish(); }
		virtual void stepExtra(uint64_t) {}
		virtual std::string statusExtra() const { return {}; }
		virtual void onMidi(const Value& _message) { reply(_message, false, "not here"); }
		virtual const char* missingText() const = 0;
		Engine<DeskT>& currentEngine() { return *m_engine; }
		void onAttach() override { m_active = true; }
		void onDetach() override { m_desk->detachPage(); }

	private:
		std::vector<deskCore::EngineChoice> choices() const
		{
			std::vector<deskCore::EngineChoice> out;
			for(const auto& r : m_engines)
				out.push_back({r.profile.id, r.profile.label, true, {}});
			return out;
		}

		void setEngine(const Value& _message)
		{
			const auto* kind = _message.find("kind");
			const auto id = kind && kind->isString() ? kind->asString() : std::string();
			const Record* record = nullptr;
			for(const auto& r : m_engines)
				if(r.profile.id == id)
					record = &r;
			if(!record)
			{
				reply(_message, false, "engine: not in the engine map");
				return;
			}
			if(record != m_record)
			{
				m_record = record;
				m_engine.reset();
				setExternalMidi(record->externalMidi);
				m_engine = record->make(*this);
				m_engine->bindDesk(*m_desk);
				m_desk->setEngine(record->profile, m_engine->device());
				m_desk->setEngines(choices());
			}
			reply(_message, true, "Engine: " + record->profile.label);
		}

		void recheckFirmware(const Value& _message)
		{
			using P = deskCore::LifeFacts::Probe;
			const auto p = m_engine->probe();
			m_desk->setProbe(p);
			reply(_message, p != P::Missing && p != P::Unsupported, p == P::Missing ? missingText()
				: p == P::Unsupported ? "This ROM is not the firmware the editor knows." : "Firmware found");
		}

		std::vector<Record> m_engines;
		const Record* m_record = &m_engines.front();
		MidiLearnCommands m_learn;
		std::unique_ptr<Engine<DeskT>> m_engine;
		std::unique_ptr<DeskT> m_desk;
		uint64_t m_tick = 0;
		bool m_active = false;	// a page attached once: the desk polls and loads from then on
	};
}
