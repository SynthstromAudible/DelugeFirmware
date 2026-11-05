/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/processor.h"
#include "dsp-next/core/q31_ops.h" // kOneQ31
#include "dsp-next/core/types.h"

#include <type_traits>

/// @file
/// @brief Wavefolder: the voice's pre-filter wavefolder, as a processor.

namespace deluge::dsp::fixed {

/// @brief Folds the signal back on itself the further it is driven: the input, scaled by the fold amount, goes through
/// the cubic 3x − 8x³ twice, which flips the wave around zero more times the harder it is driven.
/// @details Stateless and per-sample, so it runs in place on a mono buffer or on an interleaved stereo one alike. The
/// output is shifted down by 7 to compensate for the fold's gain.
class Wavefolder : public Processor<Wavefolder, Q<31>> {
public:
	/// @brief Sets the fold amount.
	/// @param amount The fold amount, Q31: 0 folds least (a floor of 0.1 always applies), kOneQ31 most. The voice
	/// passes LOCAL_FOLD's final value, and renders the folder only when that is above 0.
	explicit Wavefolder(Q<31> amount)
	    // The amount plus the floor saturates at 1 (Q31); its bits read as Q24 are the gain, 12.8 to 128
	    : drive_(reinterpret_bits<Q<24>>(add_sat(amount, kMinimumDrive))) {}

	/// @brief One sample through the fold.
	/// @param sample Q31.
	/// @return The folded sample, Q31, 7 bits down.
	[[nodiscard, gnu::always_inline]] Q<31> render(Q<31> sample) const {
		// The sample times the gain, Q23, saturated as it is reformatted to Q31
		const auto driven = as_clamp_then_shift<Q<31>>(mul(drive_, sample));
		return scale_shr_floor<7>(fold(driven));
	}

	/// @brief The fold curve: 3x − 8x³ applied twice, x in Q31 with kOneQ31 as 1.
	/// @details Each product of Q31 values is Q30, reformatted back to Q31. The reformats and the final ×4 wrap modulo
	/// 2^32 at full scale, as the target's shifts do.
	[[nodiscard, gnu::always_inline]] static Q<31> fold(Q<31> x) { return cubic(cubic(x)); }

private:
	/// @brief 3x − 8x³, Q31.
	[[nodiscard, gnu::always_inline]] static Q<31> cubic(Q<31> x) {
		const auto x2 = mul_as_wrap<Q<31>>(x, x);
		const auto x3 = mul_as_wrap<Q<31>>(x2, x);
		// ¾x at Q30 less x³'s Q31 bits read as Q30 (2x³): ¾x − 2x³, which × 4 is the curve
		const Q<30> quarter = sub_wrap(mul(kThreeFourths, x), reinterpret_bits<Q<30>>(x3));
		return as_wrap<Q<31>>(scale_shl_wrap<2>(quarter));
	}

	static constexpr Q<31> kMinimumDrive =
	    Q<31>::from_raw(static_cast<int32_t>(0.1 * kOneQ31)); ///< The drive floor, 0.1.
	static constexpr Q<31> kThreeFourths = Q<31>::from_raw(static_cast<int32_t>(0.75 * kOneQ31)); ///< ¾.

	Q<24> drive_; ///< The gain: the fold amount plus the floor, saturated at 1 as Q31, read as Q24.
};

static_assert(!std::is_polymorphic_v<Wavefolder>);

} // namespace deluge::dsp::fixed
