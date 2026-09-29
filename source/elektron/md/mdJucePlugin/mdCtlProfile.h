#pragma once

#include "mdRealtimeQueue.h"

#include "deskController/deskController.h"
#include "deskHost/deskHost.h"

#include "jucePluginLib/externalMidi.h"

#include "elektronData/json.h"

#include <functional>
#include <optional>
#include <vector>

namespace pluginLib
{
	class Processor;
}

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;

	// The controller profile's MIDI input (doc/modern-ux/DESIGN-tr06.md): the processor's input filter
	// (pluginLib::ExternalMidi, the hook upstream's processor already calls for every event). It runs on
	// the MIDI input threads; it is installed only while the profile is on, so with the profile off the
	// processor's MIDI path is exactly what it was. A voice becomes the machine's note at once, in the
	// same block and at the same offset: into the emulated device, or (an engine on the plug-in's MIDI,
	// external MIDI on) out with the audio block to the real machine. A knob is kept for the session
	// (deskController::Input::takeKnob). Nothing ever goes back to the controller. While the page shows
	// the controller (watch), it is in the path with the profile off too, only to count what arrives on
	// each channel (deskController::Monitor): then it takes nothing.
	class CtlInputFilter final : public pluginLib::MidiInputFilter
	{
	public:
		explicit CtlInputFilter(pluginLib::Processor& _processor);
		~CtlInputFilter() override;

		CtlInputFilter(const CtlInputFilter&) = delete;
		CtlInputFilter& operator=(const CtlInputFilter&) = delete;

		// The message thread: the tables, and whether the filter is in the processor's path (the profile
		// on, or the page watching what arrives).
		void configure(const deskController::Setup& _setup, const deskController::Route& _route, bool _watch = false);
		deskController::Input& input() { return m_input; }
		const deskController::Monitor& monitor() const { return m_monitor; }
		deskController::Monitor& monitor() { return m_monitor; }
		bool installed() const { return m_installed; }

		bool filterIn(const synthLib::SMidiEvent& _ev) override;
		void flushOut(juce::MidiBuffer& _midiMessages, pluginLib::MidiPorts& _ports) override;

	private:
		struct Short
		{
			uint8_t a = 0, b = 0, c = 0;
		};

		pluginLib::Processor& m_processor;
		deskController::Input m_input;
		deskController::Monitor m_monitor;
		RealtimeQueue<Short, 256> m_out;
		bool m_installed = false;
	};

	// The controller profile in the session (both editors; message thread): the setup kept with the
	// project (DeskHost's MDCT chunk), the plug-in's ctl* commands (deskHost's table), the page's
	// "controller" document, and the knobs into the machine's own edit path: single-parameter edits,
	// paced as the Control All pump (deskController::KnobPump), on the page's selected track, relative
	// (deskController::RelativeKnobs: a turn moves the value from where it is) or absolute, as the setup's
	// knob mode says. A knob on NOTE moves the notes of the voices on the selected track (Monomachine) or
	// its machine's pitch parameter (Machinedrum). What is the model's (how a voice reaches the machine,
	// how an edit is sent, what the machine holds) comes as hooks.
	class ControllerProfile
	{
	public:
		using Value = elektronData::json::Value;

		struct Hooks
		{
			deskController::Machine machine = deskController::Machine::Md;
			// The route from the machine's active global, as the desk knows it now.
			std::function<deskController::Route(const deskController::Setup&)> route;
			// Edits on the machine's edit path (the desk's commands, as the page sends them).
			std::function<void(const std::vector<deskController::Edit>&)> apply;
			// A track's target's value in the working kit the desk holds now (-1: not known): where a
			// relative knob moves it from.
			std::function<int(uint8_t _track, const deskController::Target&)> current;
			// A track's machine in the working kit (the MD's model, the MM's machine id; -1 not known):
			// the targets' names, and the Machinedrum's NOTE (its pitch parameter).
			std::function<int(uint8_t _track)> model;
			// The MIDI inputs and whether each is enabled; none: the host owns them (a DAW). Unset: the
			// standalone app's (AudioMidiLink::midiInputs).
			std::function<std::optional<std::vector<deskController::MidiInput>>()> inputs;
		};

		ControllerProfile(AudioPluginAudioProcessor& _processor, Hooks _hooks, std::function<void(const Value&)> _publish);

		void handle(deskHost::Action _action, const Value& _message);
		// One session step: a project's setup, the machine's route, the knobs.
		void step(double _now);
		void publish();
		// The page went (its window closed): nothing watches what arrives any more.
		void detach();

		const deskController::Setup& setup() const { return m_setup; }
		const deskController::Route& route() const { return m_route; }
		int selected() const { return m_selected; }
		bool watching() const { return m_watch; }
		const CtlInputFilter& filter() const { return m_filter; }

	private:
		void change(const deskController::Setup& _setup);
		void restore();
		void reply(const Value& _message, bool _ok, const std::string& _note) const;
		void configure();
		void watch(bool _on);
		// The MIDI inputs (the standalone app) and a TR-06 among the enabled ones; true when that changed.
		bool lookAtInputs();
		// One knob's new value (the profile on): a parameter's edit into the pump, or (the Monomachine's
		// NOTE) the voices on the selected track transposed.
		void knob(uint8_t _cc, uint8_t _value, double _now);
		int model() const;

		AudioPluginAudioProcessor& m_processor;
		Hooks m_hooks;
		std::function<void(const Value&)> m_publish;
		deskController::Setup m_setup;
		deskController::Route m_route;
		CtlInputFilter m_filter;
		deskController::KnobPump m_pump;
		deskController::RelativeKnobs m_relative;
		int m_selected = 0;
		int m_model = -1;
		Value m_last;
		bool m_lastChanged = false;
		double m_lastPublishMs = -1e9;
		double m_lastRouteMs = -1e9;
		double m_lastActivityMs = -1e9;
		double m_lastInputsMs = -1e9;
		bool m_watch = false;
		deskController::Activity m_activity;
		deskController::Seen m_inputs;
		std::optional<uint32_t> m_seen;
	};
}
