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

#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/wrapping.h"
#include "util/fixedpoint.h"

#include <cstdint>
#include <type_traits>

/// @file
/// @brief OnePole: a one-pole section, low-, high- or all-pass, as a processor kernel.
namespace deluge::dsp::fixed {

/// @brief A one-pole section with its memory z, Q31: the building block of the ladders, the compressor's detector and
/// the granular processor's wet low-pass.
/// @details Each step moves z toward the input x by the moveability m = tan(f)/(1+tan(f)), Q31, where f is the cutoff
/// as a fraction of the sample rate times π:
///
///     v = (x − z) × m × 2,   y = v + z,   z ← y + v
///
/// y is the low-pass output. The product is rounded to Q30 (a multiply keeping the high word) and doubled back to Q31,
/// so its lowest bit is always clear (see @ref kProductToQ31Shift).
///
/// **Modes.** LOW_PASS returns y. HIGH_PASS returns x − y: the input less the low-pass, from the same updated memory,
/// as the high-pass ladder and the compressor's detector subtract it. ALL_PASS returns 2y − x. All three move z
/// identically, so a mode change between blocks keeps the memory.
///
/// **Wrapping.** The additions and subtractions wrap modulo 2^32, as two's-complement hardware computes them. At full
/// scale x − z leaves q31, and so can 2y − x; the result is the wrapped value the target's `sub` and `add` give, not
/// undefined behaviour.
///
/// **Feedback.** The ladders read z, scaled, for their resonance: @ref feedback and @ref feedback_unshifted read the
/// state without stepping it.
struct OnePole {
	using value_type = q31; ///< A sample, Q31.
	using step_type = q31;  ///< A sample, Q31: one step per sample.

	/// @brief What a step returns.
	enum class Mode : uint8_t {
		LOW_PASS,  ///< y, the low-pass output.
		HIGH_PASS, ///< x − y, the input less the low-pass output.
		ALL_PASS,  ///< 2y − x, the low-pass output doubled less the input.
	};
	using Modes = ModeList<Mode::LOW_PASS, Mode::HIGH_PASS, Mode::ALL_PASS>; ///< All three share the one memory.

	/// @brief The shift from the rounded high word of (x − z) × m, Q30, back to Q31: the step v.
	static constexpr int kProductToQ31Shift = 1;
	/// @brief The shift from the rounded high word of z × amount, Q30, to the feedback: z × amount × 2, Q31.
	static constexpr int kFeedbackShift = 2;

	/// @brief What the owner sets.
	struct Settings {
		q31 moveability = 0;        ///< tan(f)/(1+tan(f)), Q31: 0 holds the memory still, toward ONE_Q31 it follows x.
		Mode mode = Mode::LOW_PASS; ///< What a step returns.
	};

	/// @brief What a step reads: the moveability and the mode the wrapper dispatches on once per block.
	struct Coefficients {
		q31 m = 0;                  ///< The moveability, Q31.
		Mode mode = Mode::LOW_PASS; ///< What a step returns.
	};

	/// @brief The memory. State{} is silence: z at 0.
	struct State {
		q31 z{0}; ///< The memory after the last step, Q31.
	};

	/// @brief The coefficients for @p s: the moveability and the mode, unchanged.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		return {.m = s.moveability, .mode = s.mode};
	}

	/// @brief One sample: v = (x − z) × m × 2, y = v + z, z ← y + v, wrapping; then y, x − y or 2y − x.
	/// @tparam M The block's mode.
	/// @param c The moveability.
	/// @param s The memory, updated before the return value is taken.
	/// @param x The input, Q31.
	/// @return y (LOW_PASS), x − y (HIGH_PASS) or 2y − x (ALL_PASS), Q31.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& c, const EmptyBlock& /*b*/, State& s,
	                                                     q31 x) {
		const q31 v = multiply_32x32_rshift32_rounded(wrapping_sub(x, s.z), c.m) << kProductToQ31Shift;
		const q31 y = wrapping_add(v, s.z);
		s.z = wrapping_add(y, v);
		if constexpr (M == Mode::LOW_PASS) {
			return y;
		}
		else if constexpr (M == Mode::HIGH_PASS) {
			return wrapping_sub(x, y);
		}
		else {
			return wrapping_sub(wrapping_add(y, y), x);
		}
	}

	/// @brief The memory scaled for a ladder's feedback: z × @p amount × 2, Q31.
	/// @param amount The feedback amount, Q31.
	[[nodiscard]] [[gnu::always_inline]] static q31 feedback(const State& s, q31 amount) {
		return multiply_32x32_rshift32_rounded(s.z, amount) << kFeedbackShift;
	}

	/// @brief The memory scaled for a ladder's feedback at a quarter of @ref feedback's scale: z × @p amount ÷ 2, Q31,
	/// for a caller that sums several before shifting.
	/// @param amount The feedback amount, Q31.
	[[nodiscard]] [[gnu::always_inline]] static q31 feedback_unshifted(const State& s, q31 amount) {
		return multiply_32x32_rshift32_rounded(s.z, amount);
	}
};

static_assert(processor_kernel<OnePole>);
static_assert(!std::is_polymorphic_v<OnePole>);

} // namespace deluge::dsp::fixed
