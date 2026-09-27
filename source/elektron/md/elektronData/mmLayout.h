#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace elektronData::mmLayout
{
	// Internal to elektronData. Each MM document defines its raw layout once, as
	// a function template that visits every field in payload order; the same
	// function decodes (Reader) and encodes (Writer), so the two cannot drift.
	// Multi-byte fields are big-endian, as the firmware stores them.

	class Reader
	{
	public:
		explicit Reader(const std::vector<uint8_t>& _raw) : m_raw(_raw) {}

		void u8(uint8_t& _v) { _v = m_raw[m_pos++]; }
		void i8(int8_t& _v) { _v = static_cast<int8_t>(m_raw[m_pos++]); }
		void u16(uint16_t& _v)
		{
			_v = static_cast<uint16_t>((m_raw[m_pos] << 8) | m_raw[m_pos + 1]);
			m_pos += 2;
		}
		void u64(uint64_t& _v)
		{
			_v = 0;
			for(int i = 0; i < 8; ++i)
				_v = (_v << 8) | m_raw[m_pos++];
		}
		template<size_t N> void bytes(std::array<uint8_t, N>& _a) { for(auto& b : _a) u8(b); }
		template<size_t N> void bytes(std::array<int8_t, N>& _a) { for(auto& b : _a) i8(b); }
		template<size_t N> void u64s(std::array<uint64_t, N>& _a) { for(auto& v : _a) u64(v); }
		template<size_t N> void u16s(std::array<uint16_t, N>& _a) { for(auto& v : _a) u16(v); }
		template<size_t N, size_t M> void bytes(std::array<std::array<uint8_t, M>, N>& _a) { for(auto& r : _a) bytes(r); }
		template<size_t N, size_t M> void bytes(std::array<std::array<int8_t, M>, N>& _a) { for(auto& r : _a) bytes(r); }

		size_t position() const { return m_pos; }

	private:
		const std::vector<uint8_t>& m_raw;
		size_t m_pos = 0;
	};

	class Writer
	{
	public:
		void u8(const uint8_t& _v) { m_raw.push_back(_v); }
		void i8(const int8_t& _v) { m_raw.push_back(static_cast<uint8_t>(_v)); }
		void u16(const uint16_t& _v)
		{
			m_raw.push_back(static_cast<uint8_t>(_v >> 8));
			m_raw.push_back(static_cast<uint8_t>(_v));
		}
		void u64(const uint64_t& _v)
		{
			for(int shift = 56; shift >= 0; shift -= 8)
				m_raw.push_back(static_cast<uint8_t>(_v >> shift));
		}
		template<size_t N> void bytes(const std::array<uint8_t, N>& _a) { for(const auto b : _a) u8(b); }
		template<size_t N> void bytes(const std::array<int8_t, N>& _a) { for(const auto b : _a) i8(b); }
		template<size_t N> void u64s(const std::array<uint64_t, N>& _a) { for(const auto v : _a) u64(v); }
		template<size_t N> void u16s(const std::array<uint16_t, N>& _a) { for(const auto v : _a) u16(v); }
		template<size_t N, size_t M> void bytes(const std::array<std::array<uint8_t, M>, N>& _a) { for(const auto& r : _a) bytes(r); }
		template<size_t N, size_t M> void bytes(const std::array<std::array<int8_t, M>, N>& _a) { for(const auto& r : _a) bytes(r); }

		const std::vector<uint8_t>& raw() const { return m_raw; }
		size_t position() const { return m_raw.size(); }

	private:
		std::vector<uint8_t> m_raw;
	};
}
