#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace baseLib
{
	// Finite by the exponent bits, not by std::isfinite: release builds use -Ofast (/fp:fast), whose finite-math
	// assumption lets the compiler fold std::isfinite to true.
	//
	// The value is taken by reference on purpose. Under finite-math clang also marks every float argument and return
	// value as never NaN/inf (nofpclass), so a by-value helper is folded the same way. Reading the bits from memory
	// carries no such assumption. A value produced by float arithmetic in a fast-math translation unit cannot be
	// checked reliably at all (a NaN result is undefined there): code that must catch those is built with
	// -fno-fast-math (/fp:precise) as well.
	template<typename T>
	bool isFinite(const T& _v)
	{
		static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>, "isFinite takes float or double");

		if constexpr(std::is_same_v<T, float>)
		{
			uint32_t bits;
			std::memcpy(&bits, &_v, sizeof(bits));
			constexpr uint32_t exponent = 0x7f800000u;
			return (bits & exponent) != exponent;
		}
		else
		{
			uint64_t bits;
			std::memcpy(&bits, &_v, sizeof(bits));
			constexpr uint64_t exponent = 0x7ff0000000000000ull;
			return (bits & exponent) != exponent;
		}
	}
}
