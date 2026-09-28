#pragma once

#include <memory>

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;
	class DeskSession;

	// The two sessions (mdSessionMd.cpp, mdSessionMm.cpp); DeskSession::create picks one.
	std::unique_ptr<DeskSession> makeMdSession(AudioPluginAudioProcessor& _processor);
	std::unique_ptr<DeskSession> makeMmSession(AudioPluginAudioProcessor& _processor);
}
