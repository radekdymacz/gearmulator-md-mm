#pragma once

#include "mmDesk/mmDesk.h"

#include "mdLib/mddevice.h"

#include "baseLib/event.h"
#include "synthLib/midiTypes.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
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

	// The Monomachine Editor page's edge to the machine: SysEx in and out, kit
	// values through the plug-in's parameter layer (so live edits are CCs, like
	// host automation), NRPN, panel keys timed in machine time, and the audio
	// thread's MM telemetry and LCD. Nothing here knows documents; that is
	// mmDesk::Desk. Call and receive on the message thread.
	class MmStudioLink
	{
	public:
		using Bytes = std::vector<uint8_t>;

		MmStudioLink(AudioPluginAudioProcessor& _processor, Controller& _controller);
		~MmStudioLink();

		MmStudioLink(const MmStudioLink&) = delete;
		MmStudioLink& operator=(const MmStudioLink&) = delete;

		void sendSysex(const Bytes& _message) const;
		// page 0-6 DATA pages, 7 level, 8 mute (parameterDescriptions_mm.json).
		bool setParam(uint8_t _track, uint8_t _page, uint8_t _index, uint8_t _value) const;
		void sendNrpn(uint8_t _track, uint8_t _param, uint8_t _value) const;
		bool pressKeys(const std::vector<mmDesk::Key>& _keys) const;

		mmDesk::Telemetry readTelemetry();
		bool readWorkingKit(Bytes& _region);
		mmDesk::Desk::Engine engine();
		// The firmware's LCD, 128 x 64, one bit a pixel, rows of 16 bytes (MSB left).
		bool readLcd(std::array<uint8_t, 1024>& _bits) const;

		static const char* parameterName(uint8_t _page, uint8_t _index);

		std::function<void(const Bytes&)> onSysex;
		// (track, page, index, value) for parameters changed since the last call.
		void drainParameterChanges(const std::function<void(uint8_t, uint8_t, uint8_t, uint8_t)>& _visit);

	private:
		void onDeviceSysex(const synthLib::SysexBuffer& _message);
		pluginLib::Parameter* parameter(uint8_t _track, uint8_t _page, uint8_t _index) const;

		AudioPluginAudioProcessor& m_processor;
		Controller& m_controller;
		baseLib::EventListener<synthLib::SysexBuffer> m_sysexListener;
		std::vector<baseLib::EventListener<pluginLib::Parameter*>> m_paramListeners;
		std::array<std::atomic<uint64_t>, 6> m_dirtyParams{};		// bit page*8+index; 56 level, 57 mute
		std::shared_ptr<const md::MmTelemetry> m_telemetry;
		double m_telemetryCheckedMs = -1e9;
		uint32_t m_workingKitSequence = 0;
		const void* m_workingKitSource = nullptr;
		std::shared_ptr<MmStudioLink*> m_alive;
	};
}
