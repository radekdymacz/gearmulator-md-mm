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
	// (deskController::Input::takeKnob). Nothing ever goes back to the controller.
	class CtlInputFilter final : public pluginLib::MidiInputFilter
	{
	public:
		explicit CtlInputFilter(pluginLib::Processor& _processor);
		~CtlInputFilter() override;

		CtlInputFilter(const CtlInputFilter&) = delete;
		CtlInputFilter& operator=(const CtlInputFilter&) = delete;

		// The message thread: the tables, and whether the filter is in the processor's path.
		void configure(const deskController::Setup& _setup, const deskController::Route& _route);
		deskController::Input& input() { return m_input; }

		bool filterIn(const synthLib::SMidiEvent& _ev) override;
		void flushOut(juce::MidiBuffer& _midiMessages, pluginLib::MidiPorts& _ports) override;

	private:
		struct Short
		{
			uint8_t a = 0, b = 0, c = 0;
		};

		pluginLib::Processor& m_processor;
		deskController::Input m_input;
		RealtimeQueue<Short, 256> m_out;
		bool m_installed = false;
	};

	// The controller profile in the session (both editors; message thread): the setup kept with the
	// project (DeskHost's MDCT chunk), the plug-in's ctl* commands (deskHost's table), the page's
	// "controller" document, and the knobs into the machine's own edit path: single-parameter edits,
	// paced as the Control All pump (deskController::KnobPump), on the page's selected track. What is
	// the model's (how a voice reaches the machine, how an edit is sent) comes as hooks.
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
		};

		ControllerProfile(AudioPluginAudioProcessor& _processor, Hooks _hooks, std::function<void(const Value&)> _publish);

		void handle(deskHost::Action _action, const Value& _message);
		// One session step: a project's setup, the machine's route, the knobs.
		void step(double _now);
		void publish();

		const deskController::Setup& setup() const { return m_setup; }
		const deskController::Route& route() const { return m_route; }
		int selected() const { return m_selected; }

	private:
		void change(const deskController::Setup& _setup);
		void restore();
		void reply(const Value& _message, bool _ok, const std::string& _note) const;

		AudioPluginAudioProcessor& m_processor;
		Hooks m_hooks;
		std::function<void(const Value&)> m_publish;
		deskController::Setup m_setup;
		deskController::Route m_route;
		CtlInputFilter m_filter;
		deskController::KnobPump m_pump;
		int m_selected = 0;
		Value m_last;
		bool m_lastChanged = false;
		double m_lastPublishMs = -1e9;
		double m_lastRouteMs = -1e9;
		std::optional<uint32_t> m_seen;
	};
}
