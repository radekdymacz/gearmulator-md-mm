#pragma once

#include "mdDeskModel.h"
#include "mdDeskTelemetry.h"

#include "mdDeskSds.h"

#include "deskCore/deskCore.h"
#include "deskCore/deskPush.h"

#include "elektronData/mdAudition.h"
#include "elektronData/mdSamples.h"

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
		bool panel = true;			// the editor can press the machine's keys (live record, chains, TRIG keys)
		// Whole-document pushes (DESIGN-edit-flow.md): at most one dump per document per interval, one
		// read-back at quiet. Over a wire the interval is at least the dump's time on it.
		deskCore::PushPolicy push{200, 150};
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
		// A MIDI note into the machine on MIDI channel _channel (0-15): note on at _velocity 1-127, note off
		// at 0. The firmware's MAP EDITOR turns a note into a track's trig (the page's keyboard). It goes
		// after any kit value sent before it. Unset: this engine cannot play notes.
		std::function<void(uint8_t _channel, uint8_t _note, uint8_t _velocity)> sendNote;
		// A panel key press and release; false when not possible here. Keys: "play", "stop",
		// "record", "recordPlay", "page", "trig1".."trig16". Unset: no panel.
		std::function<bool(const std::string& _key)> pressKey;
		// DATA ENTRY knob 0-7 turned by _steps. Unset: no panel. Held keys (Control All, FUNCTION +
		// a knob) are "hold:function" and "release:function".
		std::function<bool(uint8_t _encoder, int _steps)> turnKnob;
		// The machine's MIDI base channel (the active global's), a fact for an engine that encodes
		// CCs itself. Unset: not needed.
		std::function<void(uint8_t _channel)> baseChannel;
		std::function<double()> nowMs;
		// P9: play a slot's samples once on the plug-in's own output (a null pcm stops); the request's id,
		// 0 = not possible. Unset: this engine has no sound of its own (a real Machinedrum).
		std::function<uint64_t(const elektronData::AuditionClip&)> audition;
		std::function<elektronData::AuditionStatus()> auditionStatus;
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

		// P9, UW samples: _upload to ROM slot _slot (0-47) as SDS, paced by the machine's handshake
		// (SdsSender); the device's other SysEx waits meanwhile. The reason when it cannot start ("" =
		// started).
		virtual std::string sendSample(uint8_t _slot, const elektronData::MdSampleUpload& _upload) = 0;
		virtual void cancelSample() = 0;
		virtual const SdsSender::Progress& sampleProgress() const = 0;
		// P9: _clip heard once on the plug-in's own output (null pcm: stop), the request's id; 0 = this
		// engine cannot. The status of the latest request (another id: it is gone, e.g. a new device).
		virtual uint64_t audition(const elektronData::AuditionClip&) { return 0; }
		virtual elektronData::AuditionStatus auditionStatus() const { return {}; }
	};
}
