#pragma once

/// @file
/// @brief Exact integer quotients through the floating-point unit.
///
/// The device's Cortex-A9 divides doubles in hardware (`vdiv.f64`) but has no integer divide, so an integer `/` or `%`
/// by a divisor known only at run time is a library call (`__aeabi_idivmod`, and the slower `__aeabi_ldivmod` for 64
/// bits). These helpers divide the operands' magnitudes in double, then correct the quotient with one integer step.
///
/// **Exactness.** Each helper is exact whenever @p a and @p b are exact as doubles and |a / b| is below 2^52. A value
/// is exact as a double when it is below 2^53 in magnitude, or a value below 2^53 shifted left. The double quotient of
/// the magnitudes is then within one of the true quotient, whether the compiler divides or multiplies by the
/// reciprocal (the device builds with `-funsafe-math-optimizations`), and the step corrects it. @p b is never 0, and
/// neither operand is `INT64_MIN`.
///
/// **Cost.** When both magnitudes fit in `int32_t` the conversions are FPU instructions; only larger operands convert
/// through the `int64_t` library calls.
/// Frame positions pass 2^24, which is why the helpers divide in double rather than float.
///
/// Every helper is `constexpr`: the conversions and the division are core-language operations, so none needs
/// `<cmath>`.

#include <cstdint>
#include <optional>

namespace deluge::sample_view {

/// @brief A quotient rounded toward zero and the remainder it leaves, signed as integer `/` and `%` sign them.
struct Quotient {
	int64_t quotient{0};  ///< @p a / @p b, rounded toward zero
	int64_t remainder{0}; ///< @p a - quotient * @p b, with @p a's sign
	bool operator==(const Quotient&) const = default;
};

/// @brief @p a / @p b, rounded toward zero, with its remainder.
[[nodiscard]] constexpr Quotient divide(int64_t a, int64_t b) {
	const int64_t a_magnitude = a < 0 ? -a : a;
	const int64_t b_magnitude = b < 0 ? -b : b;
	int64_t q = 0;
	if (a_magnitude <= INT32_MAX && b_magnitude <= INT32_MAX) {
		// Both fit in 32 bits, so the FPU converts them (vcvt) and the quotient back, with no library call.
		q = static_cast<int32_t>(static_cast<double>(static_cast<int32_t>(a_magnitude))
		                         / static_cast<double>(static_cast<int32_t>(b_magnitude)));
	}
	else {
		q = static_cast<int64_t>(static_cast<double>(a_magnitude) / static_cast<double>(b_magnitude));
	}
	int64_t r = a_magnitude - q * b_magnitude;
	// The one fix-up step: the double quotient of the magnitudes is at most one from the true one.
	if (r < 0) {
		q -= 1;
		r += b_magnitude;
	}
	else if (r >= b_magnitude) {
		q += 1;
		r -= b_magnitude;
	}
	return {.quotient = (a < 0) != (b < 0) ? -q : q, .remainder = a < 0 ? -r : r};
}

/// @brief @p a / @p b rounded toward zero, as an integer `/` rounds.
[[nodiscard]] constexpr int64_t trunc_div(int64_t a, int64_t b) {
	return divide(a, b).quotient;
}

/// @brief @p a / @p b rounded toward negative infinity.
[[nodiscard]] constexpr int64_t floor_div(int64_t a, int64_t b) {
	const Quotient d = divide(a, b);
	return d.remainder != 0 && (a < 0) != (b < 0) ? d.quotient - 1 : d.quotient;
}

/// @brief @p a / @p b rounded toward positive infinity.
[[nodiscard]] constexpr int64_t ceil_div(int64_t a, int64_t b) {
	const Quotient d = divide(a, b);
	return d.remainder != 0 && (a < 0) == (b < 0) ? d.quotient + 1 : d.quotient;
}

/// @brief @p a / @p b when @p b divides @p a exactly.
/// @return The quotient; none when a remainder is left.
[[nodiscard]] constexpr std::optional<int64_t> exact_div(int64_t a, int64_t b) {
	const Quotient d = divide(a, b);
	return d.remainder == 0 ? std::optional{d.quotient} : std::nullopt;
}

} // namespace deluge::sample_view
