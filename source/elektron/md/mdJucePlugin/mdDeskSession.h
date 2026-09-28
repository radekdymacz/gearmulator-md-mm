#pragma once

#include "elektronData/json.h"

#include "juce_events/juce_events.h"

#include <functional>
#include <memory>
#include <string>

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;

	// The editor's session (P6), owned by the processor: the desk (core, Machine adapter, the
	// editor's setup), the device edges and the plug-in's own commands (MIDI learn, the engine map,
	// the ROM folder). It lives as long as the plug-in instance, so documents, undo, the clipboard,
	// pushes in flight, the engine choice and the app modulators survive the editor window. A page
	// view attaches to it (its messages go to the view's outbox) and detaches when the window closes.
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
		bool attached() const { return static_cast<bool>(m_toPage); }

		// A page message: the desk's commands and the plug-in's (the command table's owners).
		virtual void onPageMessage(const Value& _message) = 0;
		// One step of the session: the device's facts in, the desk's work, the page's messages out.
		// The timer calls it; tests call it directly.
		virtual void step() = 0;
		// One line of state for the diagnostics log.
		virtual std::string status() const = 0;

	protected:
		// A reply to a plug-in command, in the page protocol's result shape.
		void reply(const Value& _message, bool _ok, const std::string& _note) const;
		void toPage(const Value& _message) const;
		virtual void onAttach() {}
		virtual void onDetach() {}

		AudioPluginAudioProcessor& m_processor;
		uint64_t m_steps = 0;

	private:
		void timerCallback() override { step(); }

		ToPage m_toPage;
	};
}
