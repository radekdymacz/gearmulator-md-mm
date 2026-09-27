// Pure codec tests: no firmware, no device. Byte offsets asserted here were
// observed on dumps from the emulated MD OS 1.63 after panel edits (see
// doc/modern-ux/P0-RESULT.md); firmware round trips live in
// mdLibTest/patternRoundTripFirmwareTest.cpp.

#include "mdPattern.h"
#include "sysex7bit.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace
{
	int g_failures = 0;

	void check(const bool _condition, const char* _what)
	{
		if(_condition)
			return;
		std::fprintf(stderr, "FAIL: %s\n", _what);
		++g_failures;
	}

	uint32_t nextRandom(uint32_t& _state)
	{
		_state ^= _state << 13;
		_state ^= _state >> 17;
		_state ^= _state << 5;
		return _state;
	}

	void test7Bit()
	{
		uint32_t seed = 0x1234567u;
		for(size_t size = 0; size < 64; ++size)
		{
			std::vector<uint8_t> raw(size);
			for(auto& b : raw)
				b = static_cast<uint8_t>(nextRandom(seed));
			const auto encoded = elektronData::encode7Bit(raw.data(), raw.size());
			check(encoded.size() == elektronData::encoded7BitSize(size), "7-bit encoded size");
			bool clean = true;
			for(const auto b : encoded)
				clean &= b < 0x80;
			check(clean, "7-bit encoding produces data bytes only");
			check(elektronData::decode7Bit(encoded.data(), encoded.size()) == raw, "7-bit round trip");
		}
	}

	elektronData::MdPattern randomPattern(uint32_t& _seed, const bool _extended)
	{
		elektronData::MdPattern p;
		p.extended = _extended;
		p.position = static_cast<uint8_t>(nextRandom(_seed) & 0x7f);
		const auto word64 = [&] { return (uint64_t(nextRandom(_seed)) << 32) | nextRandom(_seed); };
		const auto limit = [&](const uint64_t _v) { return _extended ? _v : (_v & 0xffffffffull); };
		for(auto& t : p.trigs)
			t = limit(word64());
		for(auto& m : p.lockMasks)
			m = nextRandom(_seed) & 0x00ffffffu;
		p.accentPattern = limit(word64());
		p.slidePattern = limit(word64());
		p.swingPattern = limit(word64());
		p.swingAmount = nextRandom(_seed);
		p.accentAmount = static_cast<uint8_t>(nextRandom(_seed) & 0x0f);
		p.length = 64;
		p.scale = 3;
		p.kit = static_cast<uint8_t>(nextRandom(_seed) & 0x3f);
		for(auto& row : p.lockRows)
			for(size_t s = 0; s < (_extended ? 64u : 32u); ++s)
				row[s] = static_cast<uint8_t>(nextRandom(_seed));
		for(auto& v : p.trackAccent)
			v = limit(word64());
		for(auto& v : p.trackSlide)
			v = limit(word64());
		for(auto& v : p.trackSwing)
			v = limit(word64());
		return p;
	}

	void testRoundTrip()
	{
		uint32_t seed = 0xfeedbeefu;
		for(int i = 0; i < 50; ++i)
		{
			const bool extended = (i & 1) != 0;
			const auto p = randomPattern(seed, extended);
			const auto bytes = elektronData::encodeMdPattern(p);
			check(bytes.size() == (extended ? elektronData::MdPattern::g_extendedDumpSize
				: elektronData::MdPattern::g_shortDumpSize), "encoded dump size");
			check(elektronData::isDumpTrailerValid(bytes), "encoded dump checksum");
			const auto decoded = elektronData::decodeMdPattern(bytes);
			check(decoded.has_value() && *decoded == p, "decode(encode(p)) == p");
			check(decoded && elektronData::encodeMdPattern(*decoded) == bytes, "encode(decode(x)) == x");

			auto corrupt = bytes;
			corrupt[0x40] ^= 0x01;
			check(!elektronData::decodeMdPattern(corrupt), "checksum mismatch is rejected");
		}
	}

	// Offsets and values observed on the emulated firmware (MD OS 1.63, pattern A01).
	void testObservedLayout()
	{
		elektronData::MdPattern p;
		p.length = 32;
		p.scale = 1;
		p = elektronData::withTrig(p, 0, 4, true);
		auto bytes = elektronData::encodeMdPattern(p);
		check(bytes[0x0e] == 0x10, "track 1 step 5 trig is bit 4 of byte 0x0e");
		check(bytes[0xb2] == 32, "length byte at 0xb2");

		auto locked = elektronData::withLock(p, 0, 0, 4, 0x47);
		check(locked.has_value(), "first lock fits");
		bytes = elektronData::encodeMdPattern(*locked);
		check(bytes[0x58] == 0x01, "lock mask bit for track 1 parameter 1 at 0x58");
		check(bytes[0xb7] == 0x7b && bytes[0xbc] == 0x47, "lock row 0 step 5 value encoding");
		check(elektronData::lockValue(*locked, 0, 0, 4) == uint8_t{0x47}, "lock value read back");
		check(!elektronData::lockValue(*locked, 0, 0, 5), "other steps are unlocked");
	}

	void testLockRowOrder()
	{
		elektronData::MdPattern p;
		// Panel order on the firmware was parameter 1, 4, then 2; dump rows came
		// back sorted by parameter. The codec must produce the same order.
		auto q = *elektronData::withLock(p, 0, 0, 4, 0x47);
		q = *elektronData::withLock(q, 0, 3, 4, 0x19);
		q = *elektronData::withLock(q, 0, 1, 4, 0x4f);
		check(q.lockMasks[0] == 0x0b, "lock mask after three locks");
		check(q.lockRows[0][4] == 0x47 && q.lockRows[1][4] == 0x4f && q.lockRows[2][4] == 0x19,
			"rows sorted by parameter");
		check(q.lockRows[3][4] == 0x00, "unused rows stay zero");
		q = *elektronData::withLock(q, 2, 5, 0, 0x10);
		q = *elektronData::withLock(q, 1, 7, 9, 0x20);
		check(elektronData::lockRowIndex(q, 1, 7) == size_t{3} && elektronData::lockRowIndex(q, 2, 5) == size_t{4},
			"rows sorted by track");
		check(elektronData::lockValue(q, 2, 5, 0) == uint8_t{0x10}, "moved row keeps its value");
	}

	void testLockBudget()
	{
		elektronData::MdPattern p;
		for(size_t i = 0; i < elektronData::MdPattern::g_lockRows; ++i)
		{
			const auto next = elektronData::withLock(p, i / 24, i % 24, 0, 1);
			check(next.has_value(), "lock within budget");
			p = *next;
		}
		check(elektronData::usedLockRows(p) == 64, "64 rows used");
		check(!elektronData::withLock(p, 15, 23, 0, 1), "65th locked parameter is refused");
		check(elektronData::withLock(p, 0, 0, 7, 9).has_value(), "existing row accepts more steps");
	}

	// Optional: byte-exact round trip of dumps captured from firmware.
	void testCapturedDumps(const int _argc, char** _argv)
	{
		for(int i = 1; i < _argc; ++i)
		{
			std::ifstream in(_argv[i], std::ios::binary);
			const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
			const auto decoded = elektronData::decodeMdPattern(bytes);
			check(decoded.has_value(), "captured dump decodes");
			check(decoded && elektronData::encodeMdPattern(*decoded) == bytes, "captured dump round trips");
			std::printf("captured %s: %s\n", _argv[i], decoded ? "round trip ok" : "not a pattern");
		}
	}
}

int main(const int _argc, char** _argv)
{
	test7Bit();
	testRoundTrip();
	testObservedLayout();
	testLockRowOrder();
	testLockBudget();
	testCapturedDumps(_argc, _argv);
	std::printf("elektronDataTest: %s\n", g_failures ? "FAIL" : "PASS");
	return g_failures ? 1 : 0;
}
