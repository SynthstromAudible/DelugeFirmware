/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "dsp/core/fixed_point.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numbers>

/// @file
/// @brief EnvelopeCurve: main's normalised envelope curve (`modulation/envelope_curve.h` there) in integers.

namespace deluge::dsp::fixed {

/// @brief A stage's length in position steps: main's, which a rate is a fraction of per sample.
inline constexpr uint32_t kEnvelopeStageEnd = 1U << 23;

namespace detail {

/// P(f) of `2^-f = 1 - f·P(f)`, f a Q31 fraction of an octave: degree 4, fitted with P(0) = ln 2 to what the curves'
/// normalising scales, so a curve errs by at most 2^-20.9. It is exact where an octave starts and low where it ends, so
/// a curve rises there, by less than 2^-21 of full scale.
[[gnu::always_inline, nodiscard]] inline Q<31> octave_loss(Q<31> f) {
	const auto times_f = [f](Q<31> p) { return scale_shl_wrap<1>(Q<31>::from_raw(mul_r(p, f).raw())); };
	Q<31> p = q<31>(0.00101239420);
	p = add_wrap(q<31>(-0.00936339423), times_f(p));
	p = add_wrap(q<31>(0.05542235030), times_f(p));
	p = add_wrap(q<31>(-0.24021827523), times_f(p));
	p = add_wrap(q<31>(std::numbers::ln2), times_f(p));
	return p;
}

/// The fraction of an octave in @p octaves_q26, as Q31.
[[gnu::always_inline, nodiscard]] inline Q<31> octave_fraction(uint32_t octaves_q26) {
	return Q<31>::from_raw(static_cast<int32_t>((octaves_q26 << 5) & 0x7FFF'FFFFU)); // Raw: the Q26 fraction's bits
}

} // namespace detail

/// @brief 2^-y for @p octaves_q26, y in octaves (Q26, any value); 1 is full scale. `2^-w` by shift, the rest by
/// polynomial.
[[gnu::always_inline, nodiscard]] inline Q<31> exp2_neg(uint32_t octaves_q26) {
	const Q<31> f = detail::octave_fraction(octaves_q26);
	const Q<31> lost = scale_shl_wrap<1>(Q<31>::from_raw(mul_r(detail::octave_loss(f), f).raw()));
	// Raw: shifted by the whole octaves
	return Q<31>::from_raw(sub_wrap(Q<31>::max(), lost).raw() >> std::min<uint32_t>(octaves_q26 >> 26, 31));
}

/// @brief 1 - 2^-y for @p octaves_q26 as a Q62 word, which keeps the low bits of a y near 0: what a slew covers of its
/// gap in a block of a few samples.
[[nodiscard]] inline int64_t exp2_loss_q62(uint32_t octaves_q26) {
	if ((octaves_q26 >> 26) != 0) {
		return int64_t{sub_wrap(Q<31>::max(), exp2_neg(octaves_q26)).raw()} << 31; // Raw: Q31 to Q62
	}
	const Q<31> f = detail::octave_fraction(octaves_q26);
	return int64_t{detail::octave_loss(f).raw()} * f.raw(); // Raw: Q31 × Q31 is Q62
}

namespace detail {

/// What normalising a curve of so many eighths of an octave takes.
struct CurveNorm {
	Q<31> floor; ///< 2^-k: the unnormalised curve's end.
	Q<27> scale; ///< 1 / (1 - 2^-k), at most 12.05.
};

/// Each curve's, by its |eighths|; entry 0, the straight line, is unused.
inline constexpr std::array<CurveNorm, 101> kCurveNorms = [] {
	std::array<CurveNorm, 101> norms{};
	double power = 1.0;
	for (std::size_t e = 1; e < norms.size(); ++e) {
		power *= 0.91700404320467123174; // 2^(-1/8)
		norms[e] = {.floor = q<31>(power), .scale = q<27>(1.0 / (1.0 - power))};
	}
	return norms;
}();

} // namespace detail

/// @brief One stage's curve: how many eighths of an octave an unnormalised exponential falls over it, -100 to 100.
/// Positive is steep at the start, negative bends the other way, 0 is a straight line. The classic decay and release
/// are 64, the classic attack 32.
/// @details The falling curve is `(2^(-k·x) - 2^-k) / (1 - 2^-k)`, x the position over the stage; a negative k's is the
/// mirror of its opposite's. An exponent is a whole number of eighths times a 23-bit position, so it is exact in Q26.
struct EnvelopeCurve {
	int8_t eighths = 0; ///< The curve's octaves, in eighths.

	/// @brief The falling curve at @p position, 0 to kEnvelopeStageEnd: full scale at 0 and 0 at the end, exactly.
	[[gnu::always_inline, nodiscard]] Q<31> falling(uint32_t position) const {
		if (position == 0) [[unlikely]] {
			return Q<31>::max();
		}
		if (position >= kEnvelopeStageEnd) [[unlikely]] {
			return Q<31>{};
		}
		if (eighths == 0) [[unlikely]] {
			return Q<31>::from_raw(static_cast<int32_t>((kEnvelopeStageEnd - position) << 8)); // Raw: UQ23 to Q31
		}
		return eighths > 0
		           ? steep(static_cast<uint32_t>(eighths), position)
		           : sub_wrap(Q<31>::max(), steep(static_cast<uint32_t>(-eighths), kEnvelopeStageEnd - position));
	}

	/// @brief The least position at which the rising curve, full scale less @ref falling, is at least @p level.
	/// @details By bisection: 23 evaluations, for a retrigger.
	[[nodiscard]] uint32_t position_of(Q<31> level) const {
		uint32_t low = 0;
		uint32_t high = kEnvelopeStageEnd;
		while (low < high) {
			const uint32_t middle = (low + high) / 2;
			if (sub_wrap(Q<31>::max(), falling(middle)) < level) {
				low = middle + 1;
			}
			else {
				high = middle;
			}
		}
		return low;
	}

private:
	/// The falling curve of a positive k.
	[[gnu::always_inline, nodiscard]] static Q<31> steep(uint32_t eighths, uint32_t position) {
		const detail::CurveNorm& norm = detail::kCurveNorms[eighths];
		const Q<31> above = std::max(sub_wrap(exp2_neg(eighths * position), norm.floor), Q<31>{});
		// Raw: Q31 × Q27 is Q58, rounded to Q31 and held at full scale
		// (full scale and a rounding over it at the most, so 32 bits unsigned)
		const auto scaled =
		    static_cast<uint32_t>(((int64_t{above.raw()} * norm.scale.raw()) + (int64_t{1} << 26)) >> 27);
		return Q<31>::from_raw(static_cast<int32_t>(std::min<uint32_t>(scaled, 0x7FFF'FFFFU)));
	}
};

} // namespace deluge::dsp::fixed
