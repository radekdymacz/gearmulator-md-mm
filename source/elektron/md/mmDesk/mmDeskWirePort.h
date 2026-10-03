#pragma once

#include "mmDeskAdapter.h"
#include "mmDeskTelemetry.h"

#include "deskWire/deskWire.h"
#include "deskWire/mmWire.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace mmDesk
{
	// The Monomachine's DevicePort over a MIDI wire (P4 HW MIDI): parameters as deskWire's CCs,
	// NRPN, and PLAY/STOP as realtime, all on the base channel the adapter gives; SysEx straight
	// onto the wire. Pure (no JUCE, no emulator): the plug-in's wire engine
	// (mdJucePlugin/mdSessionMm.cpp) and mdLibTest's deskWirePortTest build the same DevicePort
	// from their own MidiWire and clock, so a test exercises the plug-in's port. _channel is
	// written by the adapter (baseChannel) and read back by every encode; the caller owns it and
	// _wire, and both must outlive the DevicePort.
	inline DevicePort wirePort(deskWire::MidiWire& _wire, uint8_t& _channel, std::function<double()> _nowMs)
	{
		DevicePort p;
		p.sendSysex = [&_wire](const std::vector<uint8_t>& _m) { _wire.send(_m); };
		p.sendParam = [&_wire, &_channel](const uint8_t _t, const uint8_t _pg, const uint8_t _i, const uint8_t _v)
		{
			if(const auto m = deskWire::mm::param(_channel, _t, _pg, _i, _v))
				_wire.send(*m);
		};
		p.sendNrpn = [&_wire, &_channel](const uint8_t _t, const uint8_t _p, const uint8_t _v) { _wire.send(deskWire::mm::nrpn(_channel, _t, _p, _v)); };
		p.pressKeys = [&_wire](const std::vector<Key>& _k)
		{
			const auto bytes = deskWire::realtimeOf(_k);
			if(bytes)
				_wire.send(*bytes);
			return bytes.has_value();
		};
		p.sendNote = [&_wire](const uint8_t _ch, const uint8_t _note, const uint8_t _vel)
		{
			if(const auto m = deskWire::note(_ch, _note, _vel))
				_wire.send(*m);
		};
		p.baseChannel = [&_channel](const uint8_t _ch) { _channel = _ch; };
		p.nowMs = std::move(_nowMs);
		return p;
	}
}
