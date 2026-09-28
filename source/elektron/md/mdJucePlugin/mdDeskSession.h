#pragma once

#include "mdMidiLearnCommands.h"
#include "mdWebPageHost.h"

#include "deskCore/deskCapabilities.h"
#include "deskCore/deskCommands.h"
#include "deskCore/deskLcd.h"
#include "deskCore/deskLifecycle.h"
#include "deskCore/deskPacer.h"

#include "deskHost/deskHost.h"

#include "elektronData/json.h"

#include "juce_events/juce_events.h"

#include <deque>
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
	// machine over the plug-in's MIDI) and the plug-in's own commands (deskHost's table: MIDI learn,
	// the engine map, the ROM folder, the page's MIDI). It lives as long as the plug-in instance, so
	// documents, undo, the clipboard, pushes in flight, the engine choice and the app modulators
	// survive the editor window. A page view attaches to it and detaches when the window closes.
	// Message thread only.
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

		// A page message: the desk's commands (the model's table) and the plug-in's (deskHost's).
		virtual void onPageMessage(const Value& _message) = 0;
		// One step of the session: the engine's facts in, the desk's work, the page's messages out.
		// The timer calls it; tests call it directly.
		virtual void step() = 0;
		// One line of state for the diagnostics log.
		virtual std::string status() const = 0;
		// The page the editor window shows for this session (data: the file, its log, its self-tests).
		virtual const WebPageHost::Spec& pageSpec() const = 0;

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

	// A machine at the end of a MIDI wire (HW MIDI, P4), for either model: what goes out is paced at
	// DIN speed, what comes in is handed over whole. Pure: the plug-in's MIDI out and in are
	// functions, so a test drives it without a processor.
	class MidiWire
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Out = std::function<void(const Bytes&)>;
		using In = std::function<std::vector<Bytes>()>;

		MidiWire(Out _out, In _in) : m_out(std::move(_out)), m_in(std::move(_in)) {}

		void send(Bytes _message) { m_pacer.push(std::move(_message)); }
		// Out at DIN speed (at _nowMs), then what the machine sent to _in.
		void pump(double _nowMs, const std::function<void(const Bytes&)>& _in);

		// The panel keys a wire has: PLAY and STOP as MIDI Start and Stop (0 when there is none).
		static uint8_t realtimeOf(const std::string& _key);

	private:
		Out m_out;
		In m_in;
		deskCore::DinPacer m_pacer;
	};

	// The plug-in's MIDI out and in as a wire's functions.
	MidiWire midiWireOf(AudioPluginAudioProcessor& _processor);

	// An engine of the engine map, for a model (P6): the device edge its desk's adapter talks to and
	// the facts it feeds the desk. Engines queue what the device says and hand it over in step():
	// nothing reaches the desk outside the session's step. A third engine is one class implementing
	// this plus one record; an engine with its own protocol also gives its own adapter.
	template<typename DeskT>
	class Engine
	{
	public:
		using DevicePort = typename DeskT::DevicePort;
		using Adapter = typename DeskT::Adapter;
		using Profile = typename DeskT::Profile;
		virtual ~Engine() = default;

		virtual DevicePort device() = 0;
		// The adapter for this engine: the model's own over device() unless the engine speaks another
		// protocol.
		virtual std::unique_ptr<Adapter> adapter(const Profile& _profile) { return DeskT::defaultAdapter(_profile, device()); }
		// Feed the desk what the device said since the last step (sysex, telemetry, memory, probes,
		// its screen). _tick counts the session's 8 ms steps.
		virtual void step(DeskT& _desk, uint64_t _tick) = 0;
		// What the device says about the firmware now (Running for a wire: its replies decide).
		virtual deskCore::LifeFacts::Probe probe() = 0;
	};

	// One engine of the engine map: its profile (id, label, what it offers) and how to make it. The
	// plug-in's MIDI in and out go to an engine on a wire (profile.wire).
	template<typename DeskT>
	struct EngineRecord
	{
		using Profile = typename DeskT::Profile;
		Profile profile;
		std::function<std::unique_ptr<Engine<DeskT>>(DeskSession&)> make;
	};

	// The session for one model (P6: one class for both editors). The engine map, the plug-in's
	// commands and the step are here; a model adds its desk, its engines, its learnable parameters
	// and what is its own (the setup it keeps, the page's MIDI).
	template<typename DeskT>
	class SessionOf : public DeskSession
	{
	public:
		using Record = EngineRecord<DeskT>;
		using Action = deskHost::Action;

		SessionOf(AudioPluginAudioProcessor& _processor, std::vector<Record> _engines, MidiLearnCommands::Model _learn,
			WebPageHost::Spec _page, const std::function<std::unique_ptr<DeskT>(std::unique_ptr<typename DeskT::Adapter>)>& _makeDesk)
			: DeskSession(_processor)
			, m_engines(std::move(_engines))
			, m_learn(pluginProcessorOf(_processor), std::move(_learn), [this](const Value& _m) { toPage(_m); })
			, m_page(std::move(_page))
		{
			m_engine = m_record->make(*this);
			m_desk = _makeDesk(m_engine->adapter(m_record->profile));
			m_desk->setEngines(choices());
		}

		~SessionOf() override { setExternalMidi(false); }

		void onPageMessage(const Value& _message) override
		{
			const auto ready = m_desk->readyCount();
			if(m_desk->onPageMessage(_message))
			{
				if(m_desk->readyCount() != ready)
					m_learn.publish();
				return;
			}
			const auto* row = deskHost::commands().find(deskCore::opOf(_message));
			if(!row)
			{
				reply(_message, false, "unknown command " + deskCore::opOf(_message));
				return;
			}
			if(const auto errors = deskHost::Table::check(*row, _message); !errors.empty())
			{
				reply(_message, false, errors.front());
				return;
			}
			switch(row->handler)
			{
			case Action::Engine: setEngine(_message); break;
			case Action::RecheckFirmware: recheckFirmware(_message); break;
			case Action::RevealRomFolder: revealRomFolder(_message); break;
			case Action::Midi: onMidi(_message); break;
			case Action::LearnStart:
			case Action::LearnAdd:
			case Action::LearnSetCc:
			case Action::LearnCancel:
			case Action::LearnRemove:
			case Action::LearnInvert: m_learn.handle(row->handler, _message); break;
			case Action::Menu:
			case Action::AudioPublish:
			case Action::AudioSet:
			case Action::AudioMeter: reply(_message, false, "the editor window acts on " + deskCore::opOf(_message)); break;
			}
		}

		void step() override
		{
			const auto t = ++m_tick;
			m_engine->step(*m_desk, t);
			stepExtra(t);
			if(m_desk->pageSeen() && t % 4 == 0)
				m_desk->tick();
		}

		std::string status() const override
		{
			return "desk: engine " + m_record->profile.id + " " + deskCore::lifecycleName(m_desk->lifecycle()) + " " + statusExtra();
		}

		const WebPageHost::Spec& pageSpec() const override { return m_page; }

		DeskT& desk() { return *m_desk; }
		const DeskT& desk() const { return *m_desk; }
		const Record& record() const { return *m_record; }

	protected:
		virtual void stepExtra(uint64_t) {}
		virtual std::string statusExtra() const { return {}; }
		virtual void onMidi(const Value& _message) { reply(_message, false, "midi: this editor's page has no keyboard"); }
		virtual const char* missingText() const = 0;
		Engine<DeskT>& currentEngine() { return *m_engine; }
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
				// The new engine first, then the desk takes its adapter (the old adapter goes with it),
				// then the old engine: nothing ever points into an engine that is gone.
				auto engine = record->make(*this);
				m_record = record;
				setExternalMidi(record->profile.wire);
				m_desk->setEngine(engine->adapter(record->profile));
				m_engine = std::move(engine);
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
		WebPageHost::Spec m_page;
		std::unique_ptr<Engine<DeskT>> m_engine;
		std::unique_ptr<DeskT> m_desk;
		uint64_t m_tick = 0;
	};

	// The machine's own screen while it starts, as the page's LCD (both models): at most about 15
	// times a second, only when it changed, through the desk (so it goes out in order with the rest).
	class LcdFeed
	{
	public:
		template<typename DeskT, typename Read>
		void step(DeskT& _desk, const uint64_t _tick, const Read& _read)
		{
			if(_desk.isInputReady())
			{
				m_last.clear();
				return;
			}
			if(_tick % 8 != 0)
				return;
			std::vector<uint8_t> bits;
			if(_read(bits) && bits != m_last)
			{
				m_last = bits;
				_desk.showLcd(bits);
			}
		}

	private:
		std::vector<uint8_t> m_last;
	};
}
