#pragma once
#include <array>
#include <bit>
#include <cstdint>

namespace deluge {

namespace fast_divide_detail {
constexpr int32_t kMaxDivisor = 256;

// For each divisor n, m = ceil(2^(31 + l) / n) with l = bit_width(n - 1). By Granlund & Montgomery, floor(m * a / 2^(31
// + l)) = floor(a / n) for every 0 <= a < 2^31, and m fits in 32 bits.
constexpr std::array<uint32_t, kMaxDivisor + 1> kMagic = [] {
	std::array<uint32_t, kMaxDivisor + 1> magic{};
	for (uint32_t n = 2; n <= kMaxDivisor; n++) {
		uint32_t l = std::bit_width(n - 1);
		magic[n] = static_cast<uint32_t>(((uint64_t{1} << (31 + l)) + n - 1) / n);
	}
	return magic;
}();
} // namespace fast_divide_detail

/// x / divisor, rounding towards zero like the operator, but for divisors up to 256 without a call to the software
/// divide (the Cortex-A9 has no divide instruction): a multiply by a reciprocal from a table, and a shift. Meant for
/// per-render ramps, where the divisor is a number of samples. Other divisors fall back to the operator.
[[gnu::always_inline]] inline int32_t divideBySmall(int32_t x, int32_t divisor) {
	using namespace fast_divide_detail;
	if (static_cast<uint32_t>(divisor - 2) > static_cast<uint32_t>(kMaxDivisor - 2)) [[unlikely]] {
		return (divisor == 1) ? x : x / divisor;
	}
	uint32_t magnitude = (x < 0) ? 0u - static_cast<uint32_t>(x) : static_cast<uint32_t>(x);
	uint32_t high = (static_cast<uint64_t>(kMagic[divisor]) * magnitude) >> 32;
	uint32_t quotient = high >> (std::bit_width(static_cast<uint32_t>(divisor - 1)) - 1);
	return (x < 0) ? -static_cast<int32_t>(quotient) : static_cast<int32_t>(quotient);
}

} // namespace deluge
