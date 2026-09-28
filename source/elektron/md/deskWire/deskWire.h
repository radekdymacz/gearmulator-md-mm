#pragma once

#include "deskCore/deskPacer.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace deskWire
{
	using Bytes = std::vector<uint8_t>;

	// MIDI's own numbers the wire uses (the MIDI 1.0 specification).
	namespace midi
	{
		constexpr uint8_t g_controlChange = 0xb0;
		constexpr uint8_t g_start = 0xfa;		// realtime: Start
		constexpr uint8_t g_stop = 0xfc;		// realtime: Stop
		// NRPN: parameter number MSB, LSB, then the value as data entry MSB.
		constexpr uint8_t g_nrpnMsb = 99;
		constexpr uint8_t g_nrpnLsb = 98;
		constexpr uint8_t g_dataEntry = 6;
		constexpr uint8_t g_programChange = 0xc0;
		constexpr uint8_t g_channelPressure = 0xd0;
	}

	// A channel voice message at its own MIDI length: program change and channel pressure
	// (0xc0-0xdf) are two bytes (status, data1); every other channel message (note on/off, poly
	// pressure, CC, pitch bend) is three.
	inline Bytes channelMessage(const uint8_t _status, const uint8_t _data1, const uint8_t _data2)
	{
		const uint8_t hi = _status & 0xf0;
		if(hi == midi::g_programChange || hi == midi::g_channelPressure)
			return {_status, _data1};
		return {_status, _data1, _data2};
	}

	// A control change on _channel (0-15).
	inline Bytes controlChange(const uint8_t _channel, const uint8_t _controller, const uint8_t _value)
	{
		return {static_cast<uint8_t>(midi::g_controlChange | (_channel & 0x0f)), static_cast<uint8_t>(_controller & 0x7f),
			static_cast<uint8_t>(_value & 0x7f)};
	}

	// An NRPN write: three control changes on _channel, in order.
	inline std::vector<Bytes> nrpn(const uint8_t _channel, const uint8_t _msb, const uint8_t _lsb, const uint8_t _value)
	{
		return {controlChange(_channel, midi::g_nrpnMsb, _msb), controlChange(_channel, midi::g_nrpnLsb, _lsb),
			controlChange(_channel, midi::g_dataEntry, _value)};
	}

	// The panel keys a MIDI wire has: PLAY and STOP as realtime Start and Stop. Keyed by the key
	// itself: any key enum with Play and Stop (mmDesk::Key); no other key goes over a wire.
	template<typename Key>
	std::optional<uint8_t> realtimeOf(const Key _key)
	{
		if(_key == Key::Play)
			return midi::g_start;
		if(_key == Key::Stop)
			return midi::g_stop;
		return std::nullopt;
	}

	// All the keys as realtime messages, or nothing when one of them has none (a wire presses all
	// or none).
	template<typename Key>
	std::optional<std::vector<Bytes>> realtimeOf(const std::vector<Key>& _keys)
	{
		std::vector<Bytes> out;
		for(const auto k : _keys)
		{
			const auto b = realtimeOf(k);
			if(!b)
				return std::nullopt;
			out.push_back({*b});
		}
		return out;
	}

	// A machine at the end of a MIDI wire (HW MIDI, P4), for either model: what goes out is paced at
	// DIN speed (deskCore::DinPacer), what comes in is handed over as whole messages. Pure: the MIDI
	// out and in are functions and the caller gives the time, so the plug-in (its MIDI ports) and the
	// firmware tests (an emulated cable) use the same wire.
	class MidiWire
	{
	public:
		using Out = std::function<void(const Bytes&)>;
		// The whole messages that arrived since the last call.
		using In = std::function<std::vector<Bytes>()>;

		MidiWire(Out _out, In _in) : m_out(std::move(_out)), m_in(std::move(_in)) {}

		void send(Bytes _message) { m_pacer.push(std::move(_message)); }
		void send(const std::vector<Bytes>& _messages)
		{
			for(const auto& m : _messages)
				send(m);
		}

		// Out what the pace allows by _nowMs, then what came in, to _in.
		void pump(const double _nowMs, const std::function<void(const Bytes&)>& _in)
		{
			for(auto& m : m_pacer.take(_nowMs))
				m_out(m);
			for(const auto& m : m_in())
				_in(m);
		}

		bool idle(const double _nowMs) const { return m_pacer.idle(_nowMs); }

	private:
		Out m_out;
		In m_in;
		deskCore::DinPacer m_pacer;
	};
}
