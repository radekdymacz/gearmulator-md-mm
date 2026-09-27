#include "sysex7bit.h"

namespace elektronData
{
	std::vector<uint8_t> decode7Bit(const uint8_t* _src, const size_t _encodedSize)
	{
		std::vector<uint8_t> raw;
		raw.reserve(_encodedSize);
		for(size_t group = 0; group < _encodedSize; group += 8)
		{
			const uint8_t msbs = _src[group];
			for(size_t i = 1; i < 8 && group + i < _encodedSize; ++i)
			{
				const bool high = (msbs & (0x40 >> (i - 1))) != 0;
				raw.push_back(static_cast<uint8_t>(_src[group + i] | (high ? 0x80 : 0)));
			}
		}
		return raw;
	}

	std::vector<uint8_t> encode7Bit(const uint8_t* _src, const size_t _rawSize)
	{
		std::vector<uint8_t> encoded;
		encoded.reserve(encoded7BitSize(_rawSize));
		for(size_t group = 0; group < _rawSize; group += 7)
		{
			const auto msbIndex = encoded.size();
			encoded.push_back(0);
			for(size_t i = 0; i < 7 && group + i < _rawSize; ++i)
			{
				const uint8_t value = _src[group + i];
				if(value & 0x80)
					encoded[msbIndex] |= static_cast<uint8_t>(0x40 >> i);
				encoded.push_back(value & 0x7f);
			}
		}
		return encoded;
	}

	namespace
	{
		uint32_t dumpChecksum(const std::vector<uint8_t>& _message)
		{
			uint32_t sum = 0;
			for(size_t i = 9; i + 5 < _message.size(); ++i)
				sum += _message[i];
			return sum & 0x3fff;
		}
	}

	void writeDumpTrailer(std::vector<uint8_t>& _message)
	{
		const auto size = _message.size();
		if(size < 15)
			return;
		const auto sum = dumpChecksum(_message);
		const auto length = static_cast<uint32_t>(size - 10);
		_message[size - 5] = static_cast<uint8_t>((sum >> 7) & 0x7f);
		_message[size - 4] = static_cast<uint8_t>(sum & 0x7f);
		_message[size - 3] = static_cast<uint8_t>((length >> 7) & 0x7f);
		_message[size - 2] = static_cast<uint8_t>(length & 0x7f);
		_message[size - 1] = 0xf7;
	}

	bool isDumpTrailerValid(const std::vector<uint8_t>& _message)
	{
		const auto size = _message.size();
		if(size < 15 || _message.back() != 0xf7)
			return false;
		const auto sum = dumpChecksum(_message);
		const auto length = static_cast<uint32_t>(size - 10);
		return uint32_t((_message[size - 5] << 7) | _message[size - 4]) == sum
			&& uint32_t((_message[size - 3] << 7) | _message[size - 2]) == length;
	}
}
