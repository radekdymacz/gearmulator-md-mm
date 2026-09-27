#pragma once

#include "sysex7bit.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace elektronData::dumpIo
{
	// Shared plumbing for the MD user-data dumps. Internal to elektronData.

	constexpr uint8_t g_mdProductId = 0x02;

	// Reads big-endian fields from one decoded 7-bit section.
	class Reader
	{
	public:
		explicit Reader(std::vector<uint8_t> _raw) : m_raw(std::move(_raw)) {}

		uint32_t u32()
		{
			uint32_t v = 0;
			for(size_t i = 0; i < 4; ++i)
				v = (v << 8) | m_raw[m_pos++];
			return v;
		}
		uint8_t u8() { return m_raw[m_pos++]; }

	private:
		std::vector<uint8_t> m_raw;
		size_t m_pos = 0;
	};

	// Collects raw fields, then appends them to a message as one 7-bit section.
	class Writer
	{
	public:
		void u32(const uint32_t _v)
		{
			for(int shift = 24; shift >= 0; shift -= 8)
				m_raw.push_back(static_cast<uint8_t>(_v >> shift));
		}
		void u8(const uint8_t _v) { m_raw.push_back(_v); }

		void appendEncodedTo(std::vector<uint8_t>& _message) const
		{
			const auto encoded = encode7Bit(m_raw.data(), m_raw.size());
			_message.insert(_message.end(), encoded.begin(), encoded.end());
		}

	private:
		std::vector<uint8_t> m_raw;
	};

	inline Reader section(const std::vector<uint8_t>& _sysex, const size_t _offset, const size_t _rawSize)
	{
		return Reader(decode7Bit(_sysex.data() + _offset, encoded7BitSize(_rawSize)));
	}

	// F0 00 20 3C 02 00 <command> <version> <revision> <position>
	inline std::vector<uint8_t> header(const uint8_t _command, const uint8_t _version, const uint8_t _revision,
		const uint8_t _position)
	{
		return {0xf0, 0x00, 0x20, 0x3c, g_mdProductId, 0x00, _command, _version, _revision, _position};
	}

	// A complete, checksum-valid, 7-bit clean MD dump with the given command.
	inline bool isValidDump(const std::vector<uint8_t>& _sysex, const uint8_t _command)
	{
		if(_sysex.size() < 15)
			return false;
		if(_sysex[0] != 0xf0 || _sysex[1] != 0x00 || _sysex[2] != 0x20 || _sysex[3] != 0x3c
			|| _sysex[4] != g_mdProductId || _sysex[6] != _command)
			return false;
		for(size_t i = 1; i + 1 < _sysex.size(); ++i)
			if(_sysex[i] > 0x7f)
				return false;
		return isDumpTrailerValid(_sysex);
	}

	inline void finish(std::vector<uint8_t>& _message)
	{
		_message.resize(_message.size() + 5);
		writeDumpTrailer(_message);
	}

	inline std::vector<uint8_t> request(const uint8_t _command, const uint8_t _value)
	{
		return {0xf0, 0x00, 0x20, 0x3c, g_mdProductId, 0x00, _command, static_cast<uint8_t>(_value & 0x7f), 0xf7};
	}
}
