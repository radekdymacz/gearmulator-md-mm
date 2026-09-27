#pragma once

#include "mdDesk/mdDesk.h"

#include "mdLib/mddevice.h"

#include "baseLib/event.h"
#include "synthLib/midiTypes.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pluginLib
{
	class Parameter;
}

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;
	class Controller;

	// The Machinedrum Editor page's edge to the machine: SysEx in and out, kit
	// parameters through the plug-in's parameter layer (so live edits are CCs,
	// exactly like host automation), panel keys and the audio thread's sequencer
	// telemetry. Nothing here knows documents; that is mdDesk::Desk. Call and
	// receive on the message thread.
	class StudioLink
	{
	public:
		using Bytes = std::vector<uint8_t>;

		StudioLink(AudioPluginAudioProcessor& _processor, Controller& _controller);
		~StudioLink();

		StudioLink(const StudioLink&) = delete;
		StudioLink& operator=(const StudioLink&) = delete;

		void sendSysex(const Bytes& _message) const;
		// Kit parameter 0-23, 24 = level, through pluginLib::Parameter (Origin::Ui).
		bool setKitParam(uint8_t _track, uint8_t _index, uint8_t _value) const;
		bool setMute(uint8_t _track, bool _muted) const;
		// Press and release a front-panel key: "play", "stop", "record", "recordPlay"
		// (hold RECORD, press PLAY), "page", "trig1".."trig16". Local MD only.
		bool pressKey(const std::string& _key);
		// DATA ENTRY knob 0-7 by _steps.
		bool turnKnob(uint8_t _encoder, int _steps) const;

		// Lock-free read of the audio thread's MD OS 1.63 RAM telemetry.
		mdDesk::Telemetry readTelemetry();
		// The working-kit region (elektronData::mdWorkingKitFromMemory) when it changed
		// since the last call; lock-free, MD OS 1.63 only.
		bool readWorkingKit(Bytes& _region);
		// Missing (no valid device / ROM), Unsupported (not MD OS 1.63) or Present.
		mdDesk::Desk::Firmware firmware() const;

		// The name of kit parameter _index (0-24) in the plug-in's parameter layer.
		static const char* parameterName(uint8_t _index);

		// Every SysEx message the machine sends (marshalled to the message thread).
		std::function<void(const Bytes&)> onSysex;

		// Parameters that changed since the last call (host automation, MIDI learn,
		// the panel editor): (track, index 0-24 or 25 = mute, value).
		void drainParameterChanges(const std::function<void(uint8_t, uint8_t, uint8_t)>& _visit);

		Controller& controller() { return m_controller; }

	private:
		void onDeviceSysex(const synthLib::SysexBuffer& _message);
		bool sendPanel(uint8_t _row, uint8_t _mask) const;
		pluginLib::Parameter* parameter(uint8_t _track, uint8_t _index) const;

		AudioPluginAudioProcessor& m_processor;
		Controller& m_controller;
		baseLib::EventListener<synthLib::SysexBuffer> m_sysexListener;
		std::vector<baseLib::EventListener<pluginLib::Parameter*>> m_paramListeners;
		std::array<std::atomic<uint32_t>, 16> m_dirtyParams{};		// bit n = index n (0-25)
		std::shared_ptr<const md::Device::SequencerTelemetry> m_telemetry;
		std::shared_ptr<md::FrontPanelPublisher> m_panel;
		double m_telemetryCheckedMs = -1e9;
		uint32_t m_workingKitSequence = 0;
		const void* m_workingKitSource = nullptr;
		std::shared_ptr<StudioLink*> m_alive;
	};
}
