#include "finite.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// Built with the release flags (-Ofast) on purpose: std::isfinite is folded to true here, baseLib::isFinite must not
// be. The values are written as bits into a buffer, the way audio arrives from the emulator; under -Ofast a float
// passed by value or made by arithmetic is assumed finite, so neither would test anything.

namespace
{
	int g_failures = 0;

	void check(const bool _ok, const char* _what)
	{
		if(_ok)
			return;
		std::fprintf(stderr, "FAIL: %s\n", _what);
		++g_failures;
	}

	template<typename T, typename Bits>
	std::vector<T> fromBits(const std::vector<Bits>& _bits)
	{
		std::vector<T> values(_bits.size());
		std::memcpy(values.data(), _bits.data(), _bits.size() * sizeof(Bits));
		return values;
	}
}

int main()
{
	// volatile: the bit patterns are not compile-time constants
	volatile uint32_t quietNan = 0x7fc00000u;

	const auto f = fromBits<float>(std::vector<uint32_t>{
		quietNan, 0xffc00001u, 0x7f800001u, 0x7f800000u, 0xff800000u,
		0x00000000u, 0x80000000u, 0x00000001u, 0x7f7fffffu, 0x3f800000u});

	check(!baseLib::isFinite(f[0]), "a float quiet NaN is not finite");
	check(!baseLib::isFinite(f[1]), "a negative float NaN is not finite");
	check(!baseLib::isFinite(f[2]), "a float signalling NaN is not finite");
	check(!baseLib::isFinite(f[3]), "float +inf is not finite");
	check(!baseLib::isFinite(f[4]), "float -inf is not finite");
	check(baseLib::isFinite(f[5]), "float 0 is finite");
	check(baseLib::isFinite(f[6]), "float -0 is finite");
	check(baseLib::isFinite(f[7]), "the smallest float denormal is finite");
	check(baseLib::isFinite(f[8]), "the largest float is finite");
	check(baseLib::isFinite(f[9]), "float 1 is finite");

	const auto d = fromBits<double>(std::vector<uint64_t>{
		0x7ff8000000000000ull, 0x7ff0000000000000ull, 0xfff0000000000000ull,
		0x7fefffffffffffffull, 0x0000000000000001ull});

	check(!baseLib::isFinite(d[0]), "a double quiet NaN is not finite");
	check(!baseLib::isFinite(d[1]), "double +inf is not finite");
	check(!baseLib::isFinite(d[2]), "double -inf is not finite");
	check(baseLib::isFinite(d[3]), "the largest double is finite");
	check(baseLib::isFinite(d[4]), "the smallest double denormal is finite");

	// The shape the audio tests use: a scan over a sample buffer.
	int nonFinite = 0;
	for(const auto& sample : f)
		nonFinite += baseLib::isFinite(sample) ? 0 : 1;
	check(nonFinite == 5, "a scan over a buffer finds every non-finite sample");

	if(g_failures)
		return EXIT_FAILURE;
	std::puts("baseLibFiniteTest: ok");
	return EXIT_SUCCESS;
}
