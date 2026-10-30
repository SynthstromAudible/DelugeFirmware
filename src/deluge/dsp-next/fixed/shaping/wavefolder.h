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

#include "dsp-next/core/processor.h"
#include "dsp-next/core/q31_ops.h" // multiply_32x32_rshift32, add_saturate, ONE_Q31
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"

#include <type_traits>

/// @file
/// @brief Wavefolder: the voice's pre-filter wavefolder, as a processor.

namespace deluge::dsp::fixed {

/// @brief Folds the signal back on itself the further it is driven: the input, scaled by the fold amount, goes through
/// the cubic 4(¾x − x³) twice, which flips the wave around zero more times the harder it is driven.
/// @details Stateless and per-sample, so it runs in place on a mono buffer or on an interleaved stereo one alike. The
/// output is shifted down by 7 to compensate for the fold's gain.
class Wavefolder : public Processor<Wavefolder, q31> {
public:
	/// @brief Sets the fold amount.
	/// @param amount The fold amount, Q31: 0 folds least (a floor of 0.1 always applies), ONE_Q31 most. The voice
	/// passes LOCAL_FOLD's final value, and renders the folder only when that is above 0.
	explicit Wavefolder(q31 amount) : drive_(add_saturate(amount, kMinimumDrive)) {}

	/// @brief One sample through the fold.
	/// @param sample Q31.
	/// @return The folded sample, Q31, 7 bits down.
	[[nodiscard, gnu::always_inline]] q31 render(q31 sample) const {
		const q31 driven = lshiftAndSaturateUnknown(multiply_32x32_rshift32(drive_, sample), 8);
		return fold(driven) >> 7;
	}

	/// @brief The fold curve: 4(¾x − x³) applied twice, x in Q31 with ONE_Q31 as 1.
	/// @details Each multiply is `multiply_32x32_rshift32` (a Q31 product halved), so each is doubled back. The
	/// doublings and the ×8 wrap modulo 2^32 at full scale, as the target's shifts do.
	[[nodiscard, gnu::always_inline]] static q31 fold(q31 x) { return cubic(cubic(x)); }

private:
	/// @brief 4(¾x − x³), Q31.
	[[nodiscard, gnu::always_inline]] static q31 cubic(q31 x) {
		const q31 x2 = wrapping_scale<2>(multiply_32x32_rshift32(x, x));
		const q31 x3 = wrapping_scale<2>(multiply_32x32_rshift32(x2, x));
		return wrapping_scale<8>(wrapping_sub(multiply_32x32_rshift32(kThreeFourths, x), x3));
	}

	/// @brief @p x times @p N, wrapping modulo 2^32 (a left shift on the target).
	template <uint32_t N>
	[[nodiscard, gnu::always_inline]] static constexpr q31 wrapping_scale(q31 x) {
		return static_cast<q31>(static_cast<uint32_t>(x) * N);
	}

	static constexpr q31 kMinimumDrive = static_cast<q31>(0.1 * ONE_Q31);  ///< The drive floor, Q31 (0.1).
	static constexpr q31 kThreeFourths = static_cast<q31>(0.75 * ONE_Q31); ///< ¾, Q31.

	q31 drive_; ///< The fold amount plus the floor, saturated, Q31.
};

static_assert(!std::is_polymorphic_v<Wavefolder>);

} // namespace deluge::dsp::fixed
