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

/// @brief 2^-y for @p octaves_q26, y in octaves (Q26, any value); 1 is full scale.
/// @details `2^-w` by shift and `2^-f` as `1 - f·P(f)`, P of degree 4, fitted so the whole errs by less than 2^-22.
/// With no constant term to miss and P(0) = ln 2, an exponent near 0 keeps its slope, which a slew applies thousands of
/// times.
[[gnu::always_inline, nodiscard]] inline Q<31> exp2_neg(uint32_t octaves_q26) {
	const uint32_t whole = std::min<uint32_t>(octaves_q26 >> 26, 31);
	// Raw: the exponent's fraction as a Q31 word, and the result shifted by its whole octaves
	const auto f = Q<31>::from_raw(static_cast<int32_t>((octaves_q26 << 5) & 0x7FFF'FFFFU));
	const auto times_f = [f](Q<31> p) { return scale_shl_wrap<1>(Q<31>::from_raw(mul_r(p, f).raw())); };
	Q<31> p = q<31>(0.00106327236);
	p = add_wrap(q<31>(-0.00945633231), times_f(p));
	p = add_wrap(q<31>(0.05547229387), times_f(p));
	p = add_wrap(q<31>(-0.24022551579), times_f(p));
	p = add_wrap(q<31>(std::numbers::ln2), times_f(p));
	return Q<31>::from_raw(sub_wrap(Q<31>::max(), times_f(p)).raw() >> whole);
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
/// Positive is steep at the start, negative bends the other way, 0 is a straight line. The Deluge's decay tables were
/// 64 and 32.
/// @details The falling curve is `(2^(-k·x) - 2^-k) / (1 - 2^-k)`, x the position over the stage; a negative k's is the
/// mirror of its opposite's. An exponent is a whole number of eighths times a 23-bit position, so it is exact in Q26.
struct EnvelopeCurve {
	int8_t eighths = 0; ///< The curve's octaves, in eighths.

	/// @brief The falling curve at @p position, 0 to kEnvelopeStageEnd: full scale at 0 and 0 at the end, exactly.
	[[gnu::always_inline, nodiscard]] Q<31> falling(uint32_t position) const {
		if (position == 0) {
			return Q<31>::max();
		}
		if (position >= kEnvelopeStageEnd) {
			return Q<31>{};
		}
		if (eighths == 0) {
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
		const int64_t scaled = ((int64_t{above.raw()} * norm.scale.raw()) + (int64_t{1} << 26)) >> 27;
		return Q<31>::from_raw(static_cast<int32_t>(std::min<int64_t>(scaled, Q<31>::max().raw())));
	}
};

} // namespace deluge::dsp::fixed
