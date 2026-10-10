#pragma once

#include "deskHost/deskHost.h"

#include "elektronData/json.h"

#include "juce_audio_devices/juce_audio_devices.h"

#include <functional>
#include <string>

namespace juce
{
	class AudioProcessor;
	class StandalonePluginHolder;
}

namespace mdJucePlugin
{
	// AUDIO / MIDI (P6), shared by the Machinedrum and Monomachine Editors: the standalone's
	// devices as one document for the page, and the page's commands applied to JUCE's
	// AudioDeviceManager, which stays the engine behind it. The page only renders the
	// document (schema "gm-audio/devices", version 1; doc/modern-ux/data-contract.md, Audio/MIDI).
	// In a plug-in the host owns audio and MIDI: the document says standalone false and the
	// page offers no panel.
	//
	// Page -> plug-in:  {"op":"audio"}                            publish the document
	//                   {"op":"audioSet", "set":..., "device"|"value"|"on"|"index"}   change one thing
	//                   {"op":"audioSet", "do":"test"|"bluetooth"}
	//                   (the names are deskHost::AudioSetting / AudioAction, the row's oneOf)
	//                   {"op":"audioMeter", "on":bool}            the input level while the panel is open
	// Plug-in -> page:  {"type":"audio", "doc":{...}}, {"type":"audioLevel", "in":0..1},
	//                   {"type":"result", "op":"audioSet", ...}   its errors are the only report of a failure
	//                   (B-037: also a MIDI port the system did not open, mdMidiPortRefusal.h)
	// The start-up log (B-036, B-037): the standalone's devices as one line when the link starts and whenever they
	// change (summary), and each MIDI port that did not open.
	class AudioMidiLink final : juce::ChangeListener
	{
	public:
		using ToPage = std::function<void(elektronData::json::Value)>;
		using Log = std::function<void(const std::string&)>;

		AudioMidiLink(juce::AudioProcessor& _processor, ToPage _toPage, Log _log = {});
		~AudioMidiLink() override;

		AudioMidiLink(const AudioMidiLink&) = delete;
		AudioMidiLink& operator=(const AudioMidiLink&) = delete;

		// True when the message was one of the ops above (handled, replied).
		bool handle(deskHost::Action _action, const elektronData::json::Value& _message);
		// The editor's timer: the input level, a few times a second, while the page asks.
		void tick();
		void publish();

		bool standalone() const { return holder() != nullptr; }
		// The standalone's audio and MIDI devices in one line: the driver, the device, its rate and buffer, running or
		// not, the MIDI inputs (on or off) and the output. Empty in a plug-in (the host owns them).
		std::string summary() const;

	private:
		void changeListenerCallback(juce::ChangeBroadcaster*) override;
		juce::StandalonePluginHolder* holder() const;
		elektronData::json::Value document() const;
		// "" when applied, otherwise why not (the command's arguments were checked by deskHost's table).
		std::string apply(const elektronData::json::Value& _command);
		// The summary into the start-up log when it changed.
		void logDevices();

		juce::AudioProcessor& m_processor;
		ToPage m_toPage;
		Log m_log;
		std::string m_logged;	// the summary last logged
		juce::AudioDeviceManager* m_listening = nullptr;
		juce::AudioDeviceManager::LevelMeter::Ptr m_inputLevel;
		bool m_meter = false;
		int m_meterTicks = 0;
	};
}
