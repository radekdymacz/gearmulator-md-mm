#pragma once

#include "mdDesk/mdDeskMod.h"

#include "mdLib/mddevice.h"

#include "juce_events/juce_events.h"

#include <memory>
#include <string>

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;

	// The app modulators run by the plug-in (P5), so they keep moving the machine's parameters
	// with the editor closed. It is the editor's pure mdDesk::ModEngine on the message thread: the
	// machine's playhead from the audio thread's lock-free telemetry in, CCs out through the
	// parameter layer (like host automation). It never touches audio. The setup is the project's
	// md-desk/setup (processor MDSK); with external MIDI (HW MIDI) it pauses, because the
	// emulator's playhead is not what plays then.
	class ModRunner final : juce::Timer
	{
	public:
		explicit ModRunner(AudioPluginAudioProcessor& _processor);
		~ModRunner() override;

		void setSetupJson(const std::string& _json);
		mdDesk::ModReport report();
		// One look at the playhead (the timer's work; tests call it directly).
		void poll();

	private:
		void timerCallback() override { poll(); }

		AudioPluginAudioProcessor& m_processor;
		mdDesk::ModEngine m_engine;
		std::shared_ptr<const md::Device::SequencerTelemetry> m_telemetry;
		double m_telemetryCheckedMs = -1e9;
		uint32_t m_setupVersion = 0;
	};
}
