#pragma once

#include "elektronData/mdPattern.h"

#include "baseLib/event.h"
#include "synthLib/midiTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace mdJucePlugin
{
	class AudioPluginAudioProcessor;
	class Controller;

	// The studio editor's only door to the machine (P0 plumbing proof).
	// Pattern data travels as the firmware's own SysEx dumps; the view never sees
	// bytes, only decoded elektronData values. Call and receive on the message thread.
	class StudioLink
	{
	public:
		StudioLink(AudioPluginAudioProcessor& _processor, Controller& _controller);
		~StudioLink();

		StudioLink(const StudioLink&) = delete;
		StudioLink& operator=(const StudioLink&) = delete;

		// Asks the firmware for its current pattern number, then for that pattern.
		void requestCurrentPattern();
		// Sends the dump, then requests it back so the view shows firmware truth.
		void sendPattern(const elektronData::MdPattern& _pattern);

		// MD OS 1.63 sequencer step (0-based) read from emulated RAM. Empty for a
		// remote device or any other firmware.
		std::optional<uint8_t> readPlayhead() const;

		std::function<void(const elektronData::MdPattern&)> onPattern;

	private:
		void onDeviceSysex(const synthLib::SysexBuffer& _message);
		void sendSysex(const std::vector<uint8_t>& _message) const;

		AudioPluginAudioProcessor& m_processor;
		baseLib::EventListener<synthLib::SysexBuffer> m_sysexListener;
		std::shared_ptr<StudioLink*> m_alive;
	};
}
