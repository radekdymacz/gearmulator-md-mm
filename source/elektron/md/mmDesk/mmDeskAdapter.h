#pragma once

#include "mmDeskModel.h"
#include "mmDeskTelemetry.h"

#include "deskCore/deskCore.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mmDesk
{
	// The engine map (P6): the emulated MM OS 1.32B, or a real Monomachine over HW MIDI. Both speak
	// the same protocol; the profile says, as named facts, what the device offers.
	struct Profile
	{
		std::string id;				// the engine map's key
		std::string label;			// the LCD's engine label when ready ("EMU OS 1.32B")
		std::string about;			// what the engine is, for the label's tooltip
		bool wire = false;			// at DIN speed: timeouts follow it, the lifecycle follows replies
		bool memory = true;			// the device publishes the working kit and the LCD
		bool telemetry = true;		// the device publishes the playhead and the screen
		bool panel = true;			// the editor can press the machine's keys (SYSEX RECV, transport)
	};

	const Profile& emulatorProfile();	// "emu"
	const Profile& wireProfile();		// "hw"

	// The device edge an adapter drives. The engine implements it.
	struct DevicePort
	{
		using Bytes = std::vector<uint8_t>;

		std::function<void(const Bytes&)> sendSysex;
		// A kit value as the plug-in's parameter: page 0-6 (DATA pages, CC), 7 = level (index 0),
		// 8 = mute (index 0, value 0/1).
		std::function<void(uint8_t _track, uint8_t _page, uint8_t _index, uint8_t _value)> sendParam;
		// NRPN on the base channel: track 0-5, parameter (0x38-0x3f MIDI page, 0x40-0x45 multi env).
		std::function<void(uint8_t _track, uint8_t _param, uint8_t _value)> sendNrpn;
		// Press these keys one after another (10 ms held, 10 ms apart, machine time). Over HW
		// MIDI only Play and Stop exist (MIDI Start / Stop).
		std::function<bool(const std::vector<Key>&)> pressKeys;
		// The machine's MIDI base channel (the active global's), a fact for an engine that encodes
		// CCs itself. Unset: not needed.
		std::function<void(uint8_t _channel)> baseChannel;
		std::function<double()> nowMs;
	};

	// The Monomachine's adapter interface (P6): deskCore's Machine protocol plus the facts a
	// Monomachine device reports. The desk holds whichever adapter the engine gives it; MmMachine is
	// the one both engines use today.
	class MmAdapter : public deskCore::Machine<MmModel>
	{
	public:
		using Bytes = std::vector<uint8_t>;
		using Probe = deskCore::LifeFacts::Probe;

		virtual void setProbe(Probe _probe) = 0;
		virtual void onTelemetry(const Telemetry& _telemetry) = 0;
		// md::MmTelemetry's working-kit region: [0] kit number, [5..] the raw kit.
		virtual void onWorkingKit(const Bytes& _region) = 0;
		// App modulation of the kit that plays: param = DATA page * 8 + index.
		virtual void sendModulation(uint8_t _track, uint8_t _param, uint8_t _value, const Documents& _view) = 0;

		virtual const Telemetry& telemetry() const = 0;
		// The sequencer plays (the RAM flag, or the step byte advancing).
		virtual bool playing() const = 0;
		virtual int currentPattern() const = 0;
		virtual int currentKit() const = 0;
		virtual int currentSong() const = 0;
		virtual int currentGlobal() const = 0;
	};
}
