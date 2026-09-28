#pragma once

#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_gui_basics/juce_gui_basics.h"

#include <functional>
#include <string>

namespace mdJucePlugin
{
	class DeskSession;
	class WebPageHost;

	// The editor's diagnostics (P6): an observer of the page view, kept out of the product code
	// and out of release builds (compiled only with MDMM_DIAGNOSTICS, CMake option
	// gearmulator_MDMM_DIAGNOSTICS). One line each to the page's log file: the web view as JUCE
	// has it (a blank window = no view, no size or not showing), the window chrome and the audio
	// setup, and the session's state every five seconds.
	class Diagnostics
	{
	public:
		Diagnostics(WebPageHost& _page, const DeskSession& _session, juce::Component& _root, juce::AudioProcessor& _processor);

		// The view's timer (30 Hz).
		void tick();

	private:
		WebPageHost& m_page;
		const DeskSession& m_session;
		juce::Component& m_root;
		juce::AudioProcessor& m_processor;
		uint32_t m_ticks = 0;
	};
}
