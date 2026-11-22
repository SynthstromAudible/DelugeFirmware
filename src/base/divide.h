#pragma once

/// @file
/// @brief Exact integer quotients through the floating-point unit.
///
/// The device's Cortex-A9 divides doubles in hardware (`vdiv.f64`) but has no integer divide, so an integer `/` or `%`
/// by a divisor known only at run time is a library call (`__aeabi_idivmod`, and the slower `__aeabi_ldivmod` for 64
/// bits). These helpers divide the operands' magnitudes in double, then correct the quotient with one integer step.
///
/// **Exactness.** Each helper is exact for every @p a and @p b but `INT64_MIN`, with @p b never 0. Each conversion
/// to double, the division, and a reciprocal's two roundings (the device builds with `-funsafe-math-optimizations`)
/// each err by at most 2^-53 relative, so the double quotient of the magnitudes is within 2^-51 of the true one,
/// whether or not the operands are exact as doubles. Below 2^50 that is within one of the true quotient, and one
/// integer step corrects it. A larger estimate is within 2^12 + 1 of it, so the helper first divides what the estimate
/// leaves (a few thousand divisors at most) and adds that. The remainder is formed in `uint64_t`, where |a| and every
/// product compared with it fit.
///
/// **Cost.** Each operand, and the quotient, converts through `int32_t` when it fits, which is an FPU instruction;
/// only a larger one converts through the 64-bit library calls. Frame positions pass 2^24, which is why the helpers
/// divide in double rather than float.
///
/// Every helper is `constexpr`: the conversions and the division are core-language operations, so none needs
/// `<cmath>`.

#include <cstdint>
#include <limits>
#include <optional>

namespace deluge::base {

/// @brief A quotient rounded toward zero and the remainder it leaves, signed as integer `/` and `%` sign them.
struct Quotient {
	int64_t quotient{0};  ///< @p a / @p b, rounded toward zero
	int64_t remainder{0}; ///< @p a - quotient * @p b, with @p a's sign
	bool operator==(const Quotient&) const = default;
};

namespace detail {

/// @brief @p m as a double: through `int32_t` (an FPU conversion) when it fits.
/// @param m A magnitude, at most `INT64_MAX`.
/// @return The nearest double to @p m.
[[nodiscard]] constexpr double to_double(uint64_t m) {
	// [[likely]] keeps this a branch: as a select, the compiler would call the library conversion every time.
	if (m <= std::numeric_limits<int32_t>::max()) [[likely]] {
		return static_cast<double>(static_cast<int32_t>(m));
	}
	return static_cast<double>(static_cast<int64_t>(m));
}

/// @brief @p d truncated to an integer: through `int32_t` (an FPU conversion) when it fits.
/// @param d A quotient of two magnitudes: at least 0, and at most 2^63.
/// @return @p d rounded toward zero.
[[nodiscard]] constexpr uint64_t to_magnitude(double d) {
	if (d < 2147483648.0) [[likely]] { // a branch, as in to_double
		return static_cast<uint64_t>(static_cast<int32_t>(d));
	}
	return static_cast<uint64_t>(d);
}

} // namespace detail

/// @brief @p a / @p b, rounded toward zero, with its remainder.
/// @param a The dividend; never `INT64_MIN`.
/// @param b The divisor; never 0 or `INT64_MIN`.
/// @return The quotient and remainder that integer `/` and `%` give.
[[nodiscard]] constexpr Quotient divide(int64_t a, int64_t b) {
	const uint64_t a_magnitude = a < 0 ? 0 - static_cast<uint64_t>(a) : static_cast<uint64_t>(a);
	const uint64_t b_magnitude = b < 0 ? 0 - static_cast<uint64_t>(b) : static_cast<uint64_t>(b);
	const double divisor = detail::to_double(b_magnitude);
	uint64_t q = detail::to_magnitude(detail::to_double(a_magnitude) / divisor);
	if (q >= (uint64_t{1} << 50)) {
		// The estimate may be off by up to 2^12 + 1, so |b| is below 2^13 and what it leaves, a - q * b, is below
		// 2^26 in magnitude: divide that and add it.
		const auto left = static_cast<int32_t>(static_cast<int64_t>(a_magnitude - q * b_magnitude));
		q += static_cast<uint64_t>(int64_t{static_cast<int32_t>(static_cast<double>(left) / divisor)});
	}
	// The one fix-up step: q is now at most one from the true quotient, so q * b is at most |a| + |b| < 2^64.
	const uint64_t product = q * b_magnitude;
	uint64_t r = 0;
	if (product > a_magnitude) {
		q -= 1;
		r = b_magnitude - (product - a_magnitude);
	}
	else {
		r = a_magnitude - product;
		if (r >= b_magnitude) {
			q += 1;
			r -= b_magnitude;
		}
	}
	const auto quotient = static_cast<int64_t>(q);
	const auto remainder = static_cast<int64_t>(r);
	return {.quotient = (a < 0) != (b < 0) ? -quotient : quotient, .remainder = a < 0 ? -remainder : remainder};
}

/// @brief @p a / @p b rounded toward zero, as an integer `/` rounds.
/// @param a The dividend; never `INT64_MIN`.
/// @param b The divisor; never 0 or `INT64_MIN`.
/// @return The quotient.
[[nodiscard]] constexpr int64_t trunc_div(int64_t a, int64_t b) {
	return divide(a, b).quotient;
}

/// @brief @p a / @p b rounded toward negative infinity.
/// @param a The dividend; never `INT64_MIN`.
/// @param b The divisor; never 0 or `INT64_MIN`.
/// @return The quotient.
[[nodiscard]] constexpr int64_t floor_div(int64_t a, int64_t b) {
	const Quotient d = divide(a, b);
	return d.remainder != 0 && (a < 0) != (b < 0) ? d.quotient - 1 : d.quotient;
}

/// @brief @p a / @p b rounded toward positive infinity.
/// @param a The dividend; never `INT64_MIN`.
/// @param b The divisor; never 0 or `INT64_MIN`.
/// @return The quotient.
[[nodiscard]] constexpr int64_t ceil_div(int64_t a, int64_t b) {
	const Quotient d = divide(a, b);
	return d.remainder != 0 && (a < 0) == (b < 0) ? d.quotient + 1 : d.quotient;
}

/// @brief @p a / @p b when @p b divides @p a exactly.
/// @param a The dividend; never `INT64_MIN`.
/// @param b The divisor; never 0 or `INT64_MIN`.
/// @return The quotient; none when a remainder is left.
[[nodiscard]] constexpr std::optional<int64_t> exact_div(int64_t a, int64_t b) {
	const Quotient d = divide(a, b);
	return d.remainder == 0 ? std::optional{d.quotient} : std::nullopt;
}

} // namespace deluge::base
