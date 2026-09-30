#pragma once

#include "mdMidiLearnCommands.h"
#include "mdPageSpec.h"
#include "mdSyxSession.h"

#include "deskCore/deskCapabilities.h"
#include "deskCore/deskCommands.h"
#include "deskCore/deskLcd.h"
#include "deskCore/deskLifecycle.h"

#include "deskHost/deskHost.h"

#include "deskWire/deskWire.h"

#include "elektronData/json.h"

#include "juce_events/juce_events.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;
	pluginLib::Processor& pluginProcessorOf(AudioPluginAudioProcessor& _processor);
	double sessionNowMs();

	// The session's pace. One step every 8 ms: the playhead (the app modulators move on its steps)
	// is looked at faster than the fastest step (300 BPM at 2X: 25 ms). The rest is periods in ms.
	constexpr double g_stepMs = 8;
	constexpr double g_deskTickMs = 32;			// the desk's own work, about 30 Hz
	constexpr double g_lcdMs = 64;				// the machine's screen while it starts, about 15 Hz
	constexpr double g_engineChoicesMs = 1000;	// whether the engines are available (a MIDI out appeared)
	constexpr double g_followHostMs = 2000;		// in a DAW: the machine's global follows the host (followHost)

	// P7: a model's SysEx import traits (mdSessionMd.cpp, mdSessionMm.cpp): the documents' type, the
	// file's and the desk's documents, one document as the contract's JSON, and what may be imported.
	template<typename DeskT> struct SyxTraits;

	// P7: whether the machine follows the host's tempo and transport: in a DAW's plug-in only.
	bool followsHost(AudioPluginAudioProcessor& _processor);

	// Due on the session's first step, then once every _periodMs (_tick counts steps from 1).
	constexpr bool due(const uint64_t _tick, const double _periodMs)
	{
		const auto n = std::max<uint64_t>(1, static_cast<uint64_t>(_periodMs / g_stepMs + 0.5));
		return _tick > 0 && (_tick - 1) % n == 0;
	}

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

		// The Machinedrum or Monomachine session for the processor's model, built and then started
		// (its steps begin once it is whole).
		static std::unique_ptr<DeskSession> create(AudioPluginAudioProcessor& _processor);

		void attach(ToPage _toPage);
		void detach();

		// A page message: the desk's commands (the model's table) and the plug-in's session rows
		// (deskHost's table, actor Session).
		virtual void onPageMessage(const Value& _message) = 0;
		// One step of the session: the engine's facts in, the desk's work, the page's messages out.
		// The timer calls it; tests call it directly.
		virtual void step() = 0;
		// One line of state for the diagnostics log.
		virtual std::string status() const = 0;
		// The page the editor window shows for this session (data: the file, its log, its self-tests).
		virtual const PageSpec& pageSpec() const = 0;

		void toPage(const Value& _message) const;
		AudioPluginAudioProcessor& processor() const { return m_processor; }
		// P7: a firmware file the user chose or dropped on the window: checked (md::checkRom), copied into
		// the ROM folder (never over another file), and the machine started again with it. The page hears
		// {"type":"romInstall", ok, text}. Nothing leaves this computer.
		void installRom(const juce::File& _file);
		// P7: a .syx the user chose or dropped: its preview for the page (syxPreview); and every document
		// the editor holds written to a .syx (syxExport). The model's session does both.
		virtual void openSyx(const juce::File& _file) = 0;
		virtual void exportSyx(const juce::File& _file) = 0;

	protected:
		void reply(const Value& _message, bool _ok, const std::string& _note) const;
		void revealRomFolder(const Value& _message) const;
		// LOAD ROM: {"type":"romInfo", installed, name, os, size, file, folder, inFolder} for the page's firmware
		// dialog; REMOVE deletes the images in the ROM folder and starts the stand-in (the start-up card asks again).
		void romInfo(const Value& _message) const;
		void removeRom(const Value& _message);
	public:
		// A line for the editor's log (diagnostics builds); set by the window while it is open.
		void setLog(std::function<void(const std::string&)> _log) { m_log = std::move(_log); }
	protected:
		void log(const std::string& _line) const { if(m_log) m_log(_line); }
		virtual void onAttach() {}
		virtual void onDetach() {}

		AudioPluginAudioProcessor& m_processor;

	private:
		void timerCallback() override { step(); }

		ToPage m_toPage;
		std::function<void(const std::string&)> m_log;
	};

	// The editor's setup kept with the project (the processor's setup chunk), for either model: the
	// session saves into it and hears when a project restore replaced it. Its own saves are not
	// restores.
	class SetupStore
	{
	public:
		using Value = elektronData::json::Value;

		explicit SetupStore(AudioPluginAudioProcessor& _processor) : m_processor(_processor) {}

		void save(const Value& _setup);
		// The project's setup text when it changed outside the session since the last call (the first
		// call: the project's); "" when the project keeps none.
		std::optional<std::string> restored();

	private:
		AudioPluginAudioProcessor& m_processor;
		std::optional<uint32_t> m_seen;
	};

	// The plug-in's MIDI in and out as a wire, for as long as it lives: while it does, the
	// processor's MIDI goes to the wire (external MIDI on), not to the emulated device. An engine
	// on a wire owns one, so the routing follows the engine and no one else decides it.
	class PluginWire
	{
	public:
		explicit PluginWire(AudioPluginAudioProcessor& _processor);
		~PluginWire();

		PluginWire(const PluginWire&) = delete;
		PluginWire& operator=(const PluginWire&) = delete;

		deskWire::MidiWire& wire() { return m_wire; }

	private:
		AudioPluginAudioProcessor& m_processor;
		deskWire::MidiWire m_wire;
	};

	// Whether an engine can be chosen now, and why not.
	struct Availability
	{
		bool available = true;
		std::string reason;
	};

	// An engine on the plug-in's MIDI: the host or the plug-in's own ports must give a MIDI out.
	Availability midiOutAvailability(const DeskSession& _session);

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
		// its screen). _tick counts the session's steps (g_stepMs) from 1; due() makes periods of it.
		virtual void step(DeskT& _desk, uint64_t _tick) = 0;
		// What the device says about the firmware now (Running for a wire: its replies decide).
		virtual deskCore::LifeFacts::Probe probe() = 0;
	};

	// One engine of the engine map: its profile (id, label, what it offers), how to make it and
	// whether it can be chosen now (none: always). EngineT is what make() returns, typed: a
	// SessionOf<DeskT, EngineT> holds these, so currentEngine() needs no cast (below).
	template<typename DeskT, typename EngineT = Engine<DeskT>>
	struct EngineRecord
	{
		static_assert(std::is_base_of_v<Engine<DeskT>, EngineT>, "EngineRecord's EngineT must derive from Engine<DeskT>");
		using Profile = typename DeskT::Profile;
		Profile profile;
		std::function<std::unique_ptr<EngineT>(DeskSession&)> make;
		std::function<Availability(const DeskSession&)> available;
	};

	// The session for one model (P6: one class for both editors). The engine map, the plug-in's
	// commands, the setup store and the step are here; a model gives its engines, its learnable
	// parameters, its page and its default setup, and adds what is its own (the page's MIDI).
	// EngineT is the model's own engine base (Engine<DeskT> unless the model adds to it, e.g. the
	// MM's MmEngine for its page MIDI): currentEngine() returns it typed, so a model that needs
	// more than device()/step()/probe() casts once here rather than at every call site.
	template<typename DeskT, typename EngineT = Engine<DeskT>>
	class SessionOf : public DeskSession
	{
	public:
		static_assert(std::is_base_of_v<Engine<DeskT>, EngineT>, "SessionOf's EngineT must derive from Engine<DeskT>");

		using Record = EngineRecord<DeskT, EngineT>;
		using Action = deskHost::Action;

		// _defaultSetup: what a project without a setup gets (none: the desk keeps its own).
		SessionOf(AudioPluginAudioProcessor& _processor, std::vector<Record> _engines, MidiLearnCommands::Model _learn,
			PageSpec _page, std::optional<Value> _defaultSetup)
			: DeskSession(_processor)
			, m_engines(std::move(_engines))
			, m_setup(_processor)
			, m_defaultSetup(std::move(_defaultSetup))
			, m_learn(pluginProcessorOf(_processor), std::move(_learn), [this](const Value& _m) { toPage(_m); })
			, m_page(std::move(_page))
			, m_followHost(followsHost(_processor))
		{
			m_engine = m_record->make(*this);
			m_desk = std::make_unique<DeskT>(m_engine->adapter(m_record->profile), port());
			m_choices = choices();
			m_desk->setEngines(m_choices);
			restoreSetup();
		}

		void onPageMessage(const Value& _message) override
		{
			if(m_desk->onPageMessage(_message))
				return;
			const auto* row = deskHost::commands().find(deskCore::opOf(_message));
			if(!row)
			{
				reply(_message, false, "unknown command " + deskCore::opOf(_message));
				return;
			}
			if(row->handler.actor != deskHost::Actor::Session)
			{
				reply(_message, false, "the editor window acts on " + deskCore::opOf(_message));
				return;
			}
			if(const auto errors = deskHost::Table::check(*row, _message); !errors.empty())
			{
				reply(_message, false, errors.front());
				return;
			}
			switch(row->handler.action)
			{
			case Action::Engine: setEngine(_message); break;
			case Action::RecheckFirmware: recheckFirmware(_message); break;
			case Action::RevealRomFolder: revealRomFolder(_message); break;
			case Action::RomInfo: romInfo(_message); break;
			case Action::RemoveRom: removeRom(_message); break;
			case Action::SyxImport: syxImport(_message); break;
			case Action::SyxCancel: m_syx.cancel(); toPage(m_syx.progress("Import stopped.")); reply(_message, true, "Import stopped."); break;
			case Action::Midi: onMidi(_message); break;
			case Action::LearnStart:
			case Action::LearnAdd:
			case Action::LearnSetCc:
			case Action::LearnCancel:
			case Action::LearnRemove:
			case Action::LearnInvert: m_learn.handle(row->handler.action, _message); break;
			default: break;	// the window's rows (actor Window, above)
			}
		}

		void step() override
		{
			const auto t = ++m_tick;
			m_learn.enforce();	// MIDI mapping off: none applied, whatever the disk or the project brought
			m_engine->step(*m_desk, t);
			if(const auto text = m_setup.restored())
				loadSetup(*text);
			if(due(t, g_engineChoicesMs))
				publishChoices();
			if(m_desk->pageSeen() && due(t, g_deskTickMs))
				m_desk->tick();
			// P7: in a DAW the host's tempo and transport reach the machine as MIDI clock, Start and Stop
			// (synthLib::MidiClock); the machine follows them only with its global set for it. The model
			// says how (hostFollowing), the adapter sets it without an undo step; ready machines only.
			// P7: a .syx import goes out a few documents a step, while the machine takes input
			if(m_syx.running() && m_desk->lifecycle() == deskCore::Lifecycle::Ready)
			{
				for(const auto& c : m_syx.next(4))
					m_desk->onPageMessage(c);
				toPage(m_syx.progress(m_syx.running() ? "" : "Imported: the machine takes the documents in over the next moments (the sync slot shows them going out)."));
			}
			if(m_followHost && m_desk->lifecycle() == deskCore::Lifecycle::Ready && due(t, g_followHostMs))
			{
				Value m = Value::object();
				m.set("op", "followHost");
				m_desk->onPageMessage(m);
			}
		}

		std::string status() const override
		{
			return "desk: engine " + m_record->profile.id + " " + deskCore::lifecycleName(m_desk->lifecycle()) + " "
				+ elektronData::json::write(m_desk->status());
		}

		const PageSpec& pageSpec() const override { return m_page; }

		DeskT& desk() { return *m_desk; }
		const DeskT& desk() const { return *m_desk; }
		const Record& record() const { return *m_record; }

	protected:
		virtual void onMidi(const Value& _message) { reply(_message, false, "midi: this editor's page has no keyboard"); }
		virtual const char* missingText() const = 0;
		EngineT& currentEngine() { return *m_engine; }
		void onDetach() override { m_desk->detachPage(); }

	private:
		// The desk's edges that are the session's: the page and the project's setup (the device
		// edge is the engine's adapter).
		typename DeskT::Port port()
		{
			typename DeskT::Port p;
			p.device.nowMs = [] { return sessionNowMs(); };
			p.toPage = [this](const Value& _m) { toPage(_m); };
			p.saveSetup = [this](const Value& _setup) { m_setup.save(_setup); };
			// After the page's ready the plug-in publishes its own document too.
			p.ready = [this] { m_learn.publish(); };
			return p;
		}

		void restoreSetup()
		{
			if(const auto text = m_setup.restored())
				loadSetup(*text);
		}

		// The editor's setup from the project, or the model's default when the project has none.
		void loadSetup(const std::string& _text)
		{
			const auto doc = _text.empty() ? m_defaultSetup : elektronData::json::parse(_text);
			if(doc)
				m_desk->loadSetup(*doc);
		}

		std::vector<deskCore::EngineChoice> choices() const
		{
			std::vector<deskCore::EngineChoice> out;
			for(const auto& r : m_engines)
			{
				const auto a = r.available ? r.available(*this) : Availability{};
				out.push_back({r.profile.id, r.profile.label, a.available, a.reason});
			}
			return out;
		}

		void publishChoices()
		{
			auto next = choices();
			const auto same = [](const deskCore::EngineChoice& _a, const deskCore::EngineChoice& _b)
			{
				return _a.id == _b.id && _a.label == _b.label && _a.available == _b.available && _a.reason == _b.reason;
			};
			if(std::equal(next.begin(), next.end(), m_choices.begin(), m_choices.end(), same))
				return;
			m_choices = std::move(next);
			m_desk->setEngines(m_choices);
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
				if(const auto a = record->available ? record->available(*this) : Availability{}; !a.available)
				{
					reply(_message, false, a.reason);
					return;
				}
				// The new engine first, then the desk takes its adapter (the old adapter goes with it),
				// then the old engine: nothing ever points into an engine that is gone. An engine on a
				// wire routes the plug-in's MIDI for its own lifetime.
				auto engine = record->make(*this);
				m_record = record;
				m_desk->setEngine(engine->adapter(record->profile));
				m_engine = std::move(engine);
				m_choices = choices();
				m_desk->setEngines(m_choices);
			}
			reply(_message, true, "Engine: " + record->profile.label);
		}

		using Syx = SyxTraits<DeskT>;

		void openSyx(const juce::File& _file) override
		{
			juce::MemoryBlock mb;
			if(_file.getSize() > 32 * 1024 * 1024 || !_file.loadFileAsData(mb))
			{
				Value m = Value::object();
				m.set("type", "syxPreview"); m.set("ok", false); m.set("file", _file.getFileName().toStdString());
				m.set("text", "The file could not be read.");
				toPage(m);
				return;
			}
			const auto* d = static_cast<const uint8_t*>(mb.getData());
			toPage(m_syx.open(std::vector<uint8_t>(d, d + mb.getSize()), _file.getFileName().toStdString(), Syx::machine(*m_desk)));
		}

		void syxImport(const Value& _message)
		{
			std::vector<std::string> kinds;
			if(const auto* k = _message.find("kinds"); k && k->isArray())
				for(const auto& v : k->asArray())
					if(v.isString())
						kinds.push_back(v.asString());
			// one gesture id for the whole import: one undo step
			const auto why = m_syx.start(kinds, 0x40000000u + (++m_syxImports), [this](const elektronData::SyxItem& _i) { return Syx::importable(_i, *m_desk); });
			reply(_message, why.empty(), why);
			toPage(m_syx.progress());
		}

		void exportSyx(const juce::File& _file) override
		{
			const auto bytes = elektronData::writeSyx(Syx::machine(*m_desk));
			Value m = Value::object();
			m.set("type", "syxExport");
			const bool ok = !bytes.empty() && _file.replaceWithData(bytes.data(), bytes.size());
			m.set("ok", ok);
			m.set("text", ok ? "Wrote " + _file.getFileName().toStdString() + " (" + std::to_string(bytes.size()) + " bytes): every document the editor holds."
				: "The .syx could not be written.");
			toPage(m);
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
		SetupStore m_setup;
		std::optional<Value> m_defaultSetup;
		MidiLearnCommands m_learn;
		PageSpec m_page;
		std::unique_ptr<EngineT> m_engine;
		std::unique_ptr<DeskT> m_desk;
		std::vector<deskCore::EngineChoice> m_choices;
		uint64_t m_tick = 0;
		// A plug-in in a host (a DAW): not the standalone app, which has no host transport, and not a
		// processor without a plug-in wrapper (the tests).
		const bool m_followHost;
		SyxJob<SyxTraits<DeskT>> m_syx;
		uint32_t m_syxImports = 0;
	};

	// The machine's own screen while it starts, as the page's LCD (both models): every g_lcdMs, only
	// when it changed, through the desk (so it goes out in order with the rest).
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
			if(!due(_tick, g_lcdMs))
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
