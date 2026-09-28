#pragma once

#include "mdDeskAdapter.h"

#include "deskWire/mdWire.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mdDesk
{
	// The Machinedrum's DevicePort over a MIDI wire (P4 HW MIDI): kit parameters and mute as
	// deskWire's CCs on the base channel the adapter gives, PLAY/STOP as realtime, SysEx straight
	// onto the wire. Pure (no JUCE, no emulator): the plug-in's wire engine
	// (mdJucePlugin/mdSessionMd.cpp) and the firmware test rig (mdLibTest/mdDeskFirmwareTest.cpp,
	// HwRig) build the same DevicePort from their own MidiWire and clock, so the HW firmware test
	// exercises the plug-in's port. _channel is written by the adapter (baseChannel) and read back
	// by every encode; the caller owns it and _wire, and both must outlive the DevicePort.
	inline DevicePort wirePort(deskWire::MidiWire& _wire, uint8_t& _channel, std::function<double()> _nowMs)
	{
		const auto send = [&_wire](const std::optional<deskWire::Bytes>& _m)
		{
			if(_m)
				_wire.send(*_m);
		};
		DevicePort p;
		p.sendSysex = [&_wire](const std::vector<uint8_t>& _m) { _wire.send(_m); };
		p.sendKitParam = [&_channel, send](const uint8_t _t, const uint8_t _i, const uint8_t _v) { send(deskWire::md::kitParam(_channel, _t, _i, _v)); };
		p.sendMute = [&_channel, send](const uint8_t _t, const bool _on) { send(deskWire::md::mute(_channel, _t, _on)); };
		p.pressKey = [&_wire](const std::string& _key)
		{
			const auto b = deskWire::md::realtimeOf(_key);
			if(b)
				_wire.send(deskWire::Bytes{*b});
			return b.has_value();
		};
		p.baseChannel = [&_channel](const uint8_t _ch) { _channel = _ch; };
		p.nowMs = std::move(_nowMs);
		return p;
	}
}
