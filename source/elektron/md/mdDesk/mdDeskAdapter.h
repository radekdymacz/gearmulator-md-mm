#pragma once

#include "mdDeskModel.h"
#include "mdDeskTelemetry.h"

#include "deskCore/deskCore.h"

#include "mdDataLink/mdDataLink.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mdDesk
{
	// What an engine's wire allows, as data (P6). The emulated MD OS 1.63 and a real Machinedrum
	// over HW MIDI speak the same protocol (mdDataLink); the profile is how they differ. The engine
	// map (the plug-in's session) holds one engine record per engine, with its profile.
	struct Profile
	{
		std::string id;				// the engine map's key
		std::string label;			// the LCD's engine label when ready ("EMU OS 1.63")
		std::string about;			// what the engine is, for the label's tooltip
		bool wire = false;			// at DIN speed: timeouts follow it, the lifecycle follows replies
		bool kitsFirst = false;		// background loads: the 64 small kits first (a pattern is 1.7 s over DIN)
		bool memory = true;			// the device publishes the working kit and the LCD
	};

	const Profile& emulatorProfile();	// "emu"
	const Profile& wireProfile();		// "hw"

	// The device edge an adapter drives: where bytes, CCs and keys go. The engine implements it.
	struct DevicePort
	{
		using Bytes = std::vector<uint8_t>;

		std::function<void(const Bytes&)> sendSysex;
		// Live kit parameter: index 0-23, or 24 for the track level (CCs).
		std::function<void(uint8_t _track, uint8_t _index, uint8_t _value)> sendKitParam;
		std::function<void(uint8_t _track, bool _muted)> sendMute;
		// A panel key press and release; false when not possible here. Keys: "play", "stop",
		// "record", "recordPlay", "page", "trig1".."trig16". Unset: no panel.
		std::function<bool(const std::string& _key)> pressKey;
		// DATA ENTRY knob 0-7 turned by _steps. Unset: no panel.
		std::function<bool(uint8_t _encoder, int _steps)> turnKnob;
		// The machine's MIDI base channel (the active global's), a fact for an engine that encodes
		// CCs itself. Unset: not needed.
		std::function<void(uint8_t _channel)> baseChannel;
		std::function<double()> nowMs;
	};

	// The Machinedrum's adapter interface (P6): deskCore's Machine protocol plus the facts a
	// Machinedrum device reports. The desk holds whichever adapter the engine gives it; MdMachine
	// (mdDataLink over SysEx and panel keys) is the one both engines use today.
	class MdAdapter : public deskCore::Machine<MdModel>
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Probe = deskCore::LifeFacts::Probe;

		// What the device says about the firmware (the emulator; a wire has no probe).
		virtual void setProbe(Probe _probe) = 0;
		// Every tick, also without telemetry (valid = false).
		virtual TelemetryEvents onTelemetry(const Telemetry& _telemetry) = 0;
		// The working-kit region read from memory (elektronData::mdWorkingKitFromMemory).
		virtual void onWorkingKitMemory(const Bytes& _region, const Documents& _view) = 0;
		// A kit parameter changed outside the editor (host automation, MIDI learn). _index 24 = level.
		virtual void onHostKitParam(uint8_t _track, uint8_t _index, uint8_t _value, const Documents& _view) = 0;
		virtual void onHostMute(uint8_t _track, bool _muted) = 0;
		// App modulation: a CC through the parameter layer, like host automation.
		virtual void sendModulation(uint8_t _track, uint8_t _param, uint8_t _value, const Documents& _view) = 0;

		virtual const Telemetry& telemetry() const = 0;
		// The machine's current pattern, kit, song... as the protocol reports them.
		virtual const mdDataLink::Session::State& linkState() const = 0;
		virtual bool replied() const = 0;
		virtual double lastRoundTripMs() const = 0;
	};
}
