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
#include "dsp-next/core/wrapping.h"

#include <cstdint>
#include <type_traits>

/// @file
/// @brief ShiftTracker: a one-pole low-pass whose coefficient is a fixed right shift, as a processor kernel.
namespace deluge::dsp::fixed {

/// @brief A one-pole low-pass at a fixed shift, Q31: each step moves its memory y toward the input x by (x − y) >> N.
/// TRACK returns y, the input's slow part; REMOVE returns x − y, the input less its slow part.
/// @details The pole sits at 1 − 2^−N: the larger @p N, the slower y follows x. The right shift is arithmetic, so it
/// rounds the step toward minus infinity, and y settles up to 2^N − 1 below a constant input, never above it.
///
/// **Order.** Both modes update y first and return from the updated y: TRACK returns the new y, REMOVE returns x less
/// the new y. Neither returns the y the step started from.
///
/// **Wrapping.** The additions and subtractions wrap modulo 2^32, as two's-complement hardware computes them. At full
/// scale x − y leaves q31 (an input at INT32_MAX after y has settled negative), and the result is the wrapped value
/// the target's `sub` gives, not undefined behaviour.
/// @tparam N The shift: the step is the difference over 2^N. 1 to 31.
template <int N>
requires(N >= 1 && N <= 31)
struct ShiftTracker {
	using value_type = q31; ///< A sample, Q31.
	using step_type = q31;  ///< A sample, Q31: one step per sample.

	/// @brief What a step returns: the slow part, or the input without it.
	enum class Mode : uint8_t {
		TRACK,  ///< Returns y, the input's slow part.
		REMOVE, ///< Returns x − y, the input less its slow part: a high-pass.
	};
	using Modes = ModeList<Mode::TRACK, Mode::REMOVE>; ///< Both modes share the one memory.

	/// @brief What the owner sets: the mode.
	struct Settings {
		Mode mode = Mode::TRACK; ///< What a step returns.
	};

	/// @brief The mode, which the wrapper dispatches on once per block.
	struct Coefficients {
		Mode mode = Mode::TRACK; ///< What a step returns.
	};

	/// @brief The memory, over the value type V: q31 for one stream, a LanePack of q31 for one per lane. StateT{} is
	/// silence: y at 0 in every lane.
	template <class V>
	struct StateT {
		V y{}; ///< The slow part after the last step, Q31.
	};
	using State = StateT<q31>; ///< One stream's memory.

	/// @brief The step is written once over its value type, so `Lanes` steps a whole frame as one LanePack.
	static constexpr bool kLanePackable = true;

	/// @brief The coefficients for @p s: the mode, unchanged.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		return {.mode = s.mode};
	}

	/// @brief One sample: y += (x − y) >> N, wrapping, then the new y (TRACK) or x less the new y (REMOVE).
	/// @details Written over V: on q31 it steps one stream; on a LanePack every operation runs across the lanes, each
	/// lane exactly the q31 step. The operations are called unqualified, so a pack's are found by argument-dependent
	/// lookup.
	/// @tparam M The block's mode.
	/// @tparam V The value type: q31, or a LanePack of q31. Deduced from @p s.
	/// @param s The memory, updated before the return value is taken from it.
	/// @param x The input, Q31.
	/// @return The new y, or x less the new y, Q31.
	template <Mode M, class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const Coefficients& /*c*/, const EmptyBlock& /*b*/, StateT<V>& s,
	                                                   std::type_identity_t<V> x) {
		s.y = wrapping_add(s.y, wrapping_sub(x, s.y) >> N);
		if constexpr (M == Mode::TRACK) {
			return s.y;
		}
		else {
			return wrapping_sub(x, s.y);
		}
	}
};

static_assert(processor_kernel<ShiftTracker<11>>);
static_assert(!std::is_polymorphic_v<ShiftTracker<11>>);

} // namespace deluge::dsp::fixed
