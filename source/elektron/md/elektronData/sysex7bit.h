#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace elektronData
{
	// Elektron packs 8-bit data into SysEx in groups of 7 bytes: one leading byte
	// carries the top bits (bit 6 = first byte), followed by the 7 low halves.
	// A stream section is encoded as one continuous packing, even when it holds
	// several logical fields.
	constexpr size_t encoded7BitSize(const size_t _rawSize)
	{
		return _rawSize + (_rawSize + 6) / 7;
	}

	std::vector<uint8_t> decode7Bit(const uint8_t* _src, size_t _encodedSize);
	std::vector<uint8_t> encode7Bit(const uint8_t* _src, size_t _rawSize);

	// Elektron user-data dump trailer: 14-bit sum of bytes [9, size-5) and the
	// 14-bit message length (size - 10), each written high 7 bits first.
	void writeDumpTrailer(std::vector<uint8_t>& _message);
	bool isDumpTrailerValid(const std::vector<uint8_t>& _message);
}
