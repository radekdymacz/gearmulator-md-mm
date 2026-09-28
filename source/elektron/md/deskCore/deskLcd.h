#pragma once

#include "elektronData/json.h"

#include <cstdint>
#include <string>
#include <vector>

namespace deskCore
{
	// The firmware's own LCD for the page while it starts (P6: one encoding for both editors):
	// {"type":"lcd","bits":<base64>}, 128 x 64, one bit a pixel, row-major, 16 bytes a row, bit 7
	// the left pixel.
	inline std::string base64(const std::vector<uint8_t>& _bytes)
	{
		static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
		std::string out;
		out.reserve((_bytes.size() + 2) / 3 * 4);
		for(size_t i = 0; i < _bytes.size(); i += 3)
		{
			const uint32_t n = (uint32_t(_bytes[i]) << 16) | (i + 1 < _bytes.size() ? uint32_t(_bytes[i + 1]) << 8 : 0)
				| (i + 2 < _bytes.size() ? uint32_t(_bytes[i + 2]) : 0);
			out += table[(n >> 18) & 63];
			out += table[(n >> 12) & 63];
			out += i + 1 < _bytes.size() ? table[(n >> 6) & 63] : '=';
			out += i + 2 < _bytes.size() ? table[n & 63] : '=';
		}
		return out;
	}

	inline elektronData::json::Value lcdMessage(const std::vector<uint8_t>& _bits)
	{
		auto m = elektronData::json::Value::object();
		m.set("type", "lcd");
		m.set("bits", base64(_bits));
		return m;
	}
}
