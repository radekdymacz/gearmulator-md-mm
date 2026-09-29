#pragma once

#include <memory>

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;
	class DeskSession;

	// DESIGN-edit-flow.md, the proof in a real host (a test build only: gearmulator_MDMM_EDITFLOW_DRIVER).
	// With GEARMULATOR_EDITFLOW_DRIVE=<file> set, the plug-in's own session is driven on its message
	// thread by the page's messages, replayed as the page sends them (the driver attaches as the page),
	// in timed phases: idle, a Control All drag, idle, a lock-lane draw. It writes each phase's time
	// window and counts to <file>; the host (mdVst3EditFlowHost) times the audio blocks and merges.
	// GEARMULATOR_EDITFLOW_PAGE=old sends the MD tweak as the page did before (16 param messages per
	// move), "new" (the default) as one tweak message. Null when the variable is not set.
	std::shared_ptr<void> startEditFlowDriver(AudioPluginAudioProcessor& _processor, DeskSession& _session);
}
