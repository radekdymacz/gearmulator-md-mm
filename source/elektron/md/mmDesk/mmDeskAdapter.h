#pragma once

#include "mmDeskModel.h"
#include "mmDeskTelemetry.h"

#include "deskCore/deskCore.h"
#include "deskCore/deskPush.h"
#include "deskCore/deskStream.h"

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
		// Whole-document pushes (DESIGN-edit-flow.md): at most one dump per document per interval, one
		// read-back at quiet. Over a wire the interval is at least the dump's time on it.
		deskCore::PushPolicy push{200, 150};
		// DESIGN-UNIFY.md 4.4: how long a field the editor set (a mute, POLY, the tempo) is shown while
		// memory still disagrees; then memory wins (deskCore::FieldExpectation).
		double settleMs = 1500;
		// B-014: the editor's values (CCs, NRPN), value SysEx (tempo) and dumps through one stream at MIDI cable
		// speed while the machine plays (deskCore::Stream: a pattern every 1.7 s, the newest wins meanwhile), as
		// fast as the machine reads while it stands. The SYSEX RECV session times the dumps themselves. Unset: at once.
		deskCore::StreamPolicy stream;
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
		// MM-P8: hold BANK _bank (0-3: A/E..D/H) and press the TRIG keys _trigs (0-15) in order, each
		// held until all are let go (md::panelKeySequence "chain:"), after the keys pressed before. Two
		// or more make the machine's chain; one is a pattern pick, which ends a chain. Unset: none
		// (HW MIDI: no message reaches them).
		std::function<bool(uint8_t _bank, const std::vector<uint8_t>& _trigs)> pressBankTrigs;
		// The machine's MIDI base channel (the active global's), a fact for an engine that encodes
		// CCs itself. Unset: not needed.
		std::function<void(uint8_t _channel)> baseChannel;
		// A MIDI note into the machine on MIDI channel _channel (0-15): note on at _velocity 1-127, note off
		// at 0 (the page's keyboard, noteOn). Unset: this engine cannot play notes.
		std::function<void(uint8_t _channel, uint8_t _note, uint8_t _velocity)> sendNote;
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
		// Every tick; true on a step edge (the step byte moved), as the MD's TelemetryEvents.stepped.
		virtual bool onTelemetry(const Telemetry& _telemetry) = 0;
		// md::MmTelemetry's working-kit region: [0] kit number, [5..] the raw kit.
		virtual void onWorkingKit(const Bytes& _region) = 0;
		// App modulation of the kit that plays: param = DATA page * 8 + index.
		virtual void sendModulation(uint8_t _track, uint8_t _param, uint8_t _value, const Documents& _view) = 0;

		virtual const Telemetry& telemetry() const = 0;
		// The sequencer plays (the RAM flag, or the step byte advancing).
		virtual bool playing() const = 0;
	};
}
