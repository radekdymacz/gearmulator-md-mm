#pragma once

#include "deskWire.h"

#include "mdLib/mdautomation.h"

#include <optional>
#include <string>

namespace deskWire::md
{
	// The Machinedrum's kit parameters as the desk counts them: 24 synthesis/effects/routing values
	// (8 per page), then the level.
	constexpr uint8_t g_levelIndex = 24;

	// A kit parameter (_index 0-23, or g_levelIndex) of track _t as the Machinedrum's CC on its base
	// channel (MD MIDI implementation: four tracks per channel from the base channel on). Nothing
	// when it has no CC.
	inline std::optional<Bytes> kitParam(const uint8_t _baseChannel, const uint8_t _t, const uint8_t _index, const uint8_t _value)
	{
		namespace a = ::md::automation;
		const bool level = _index == g_levelIndex;
		const a::ParameterChange c{static_cast<uint8_t>(level ? a::machinedrum::Level : _index / 8), _t,
			static_cast<uint8_t>(level ? 0 : _index % 8), _value};
		const auto cc = a::encodeParameterChange(::md::MachineModel::Machinedrum, c, _baseChannel);
		if(!cc)
			return std::nullopt;
		return Bytes{(*cc)[0], (*cc)[1], (*cc)[2]};
	}

	// A track's mute as the Machinedrum's mute CC on its base channel.
	inline std::optional<Bytes> mute(const uint8_t _baseChannel, const uint8_t _t, const bool _on)
	{
		namespace a = ::md::automation;
		const auto cc = a::encodeParameterChange(::md::MachineModel::Machinedrum,
			{a::machinedrum::Mute, _t, 0, static_cast<uint8_t>(_on ? 1 : 0)}, _baseChannel);
		if(!cc)
			return std::nullopt;
		return Bytes{(*cc)[0], (*cc)[1], (*cc)[2]};
	}

	// A note on (_velocity 1-127) or note off (0) on MIDI channel _channel (0-15).
	inline std::optional<Bytes> note(const uint8_t _channel, const uint8_t _note, const uint8_t _velocity)
	{
		return deskWire::note(_channel, _note, _velocity);
	}

	// The MD adapter's panel keys are its key names (mdDesk::DevicePort::pressKey); over a wire only
	// "play" and "stop" exist.
	inline std::optional<uint8_t> realtimeOf(const std::string& _key)
	{
		if(_key == "play")
			return midi::g_start;
		if(_key == "stop")
			return midi::g_stop;
		return std::nullopt;
	}
}
