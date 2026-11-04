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
#include "dsp-next/core/fixed_point_lanes.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/types.h"

#include <cstdint>
#include <type_traits>

/// @file
/// @brief OnePole: a one-pole section, low-, high- or all-pass, as a processor kernel.
namespace deluge::dsp::fixed {

/// @brief A one-pole section with its memory z, Q31: the building block of the ladders, the compressor's detector and
/// the granular kernel's wet low-pass.
/// @details Each step moves z toward the input x by the moveability m = tan(f)/(1+tan(f)), Q31, where f is the cutoff
/// as a fraction of the sample rate times π:
///
///     v = (x − z) × m,   y = v + z,   z ← y + v
///
/// y is the low-pass output. The product is rounded to Q30 (a multiply keeping the high word) and doubled back to Q31,
/// wrapping, so its lowest bit is always clear.
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
	using value_type = Q<31>; ///< A sample.
	using step_type = Q<31>;  ///< A sample: one step per sample.

	/// @brief What a step returns.
	enum class Mode : uint8_t {
		LOW_PASS,  ///< y, the low-pass output.
		HIGH_PASS, ///< x − y, the input less the low-pass output.
		ALL_PASS,  ///< 2y − x, the low-pass output doubled less the input.
	};
	using Modes = ModeList<Mode::LOW_PASS, Mode::HIGH_PASS, Mode::ALL_PASS>; ///< All three share the one memory.

	/// @brief What the owner sets.
	struct Settings {
		Q<31> moveability{};        ///< tan(f)/(1+tan(f)): 0 holds the memory still, toward one it follows x.
		Mode mode = Mode::LOW_PASS; ///< What a step returns.
	};

	/// @brief What a step reads: the moveability and the mode the wrapper dispatches on once per block.
	struct Coefficients {
		Q<31> m{};                  ///< The moveability.
		Mode mode = Mode::LOW_PASS; ///< What a step returns.
	};

	/// @brief The memory, over the value type V: Q<31> for one stream, a LanePack of Q<31> for one per lane. StateT{}
	/// is silence: z at 0 in every lane.
	template <class V>
	struct StateT {
		V z{}; ///< The memory after the last step, Q31.
	};
	using State = StateT<Q<31>>; ///< One stream's memory.

	/// @brief The step is written once over its value type, so `Lanes` steps a whole frame as one LanePack.
	static constexpr bool kLanePackable = true;

	/// @brief The coefficients for @p s: the moveability and the mode, unchanged.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		return {.m = s.moveability, .mode = s.mode};
	}

	/// @brief One sample: v = (x − z) × m, y = v + z, z ← y + v, wrapping; then y, x − y or 2y − x.
	/// @details Written over V: on Q<31> it steps one stream; on a LanePack every operation runs across the lanes, each
	/// lane exactly the scalar step, with the moveability broadcast. The operations are called unqualified, so a pack's
	/// are found by argument-dependent lookup.
	/// @tparam M The block's mode.
	/// @tparam V The value type: Q<31>, or a LanePack of Q<31>. Deduced from @p s.
	/// @param c The moveability, shared by every lane.
	/// @param s The memory, updated before the return value is taken.
	/// @param x The input, Q31.
	/// @return y (LOW_PASS), x − y (HIGH_PASS) or 2y − x (ALL_PASS), Q31.
	template <Mode M, class V>
	[[nodiscard]] [[gnu::always_inline]] static V step(const Coefficients& c, const EmptyBlock& /*b*/, StateT<V>& s,
	                                                   std::type_identity_t<V> x) {
		const V z = s.z;
		// (x − z) × m rounds to Q30, and its doubling back to Q31 wraps: at full scale the product leaves Q31.
		const V v = mul_r_as_wrap<Q<31>>(sub_wrap(x, z), c.m);
		const V y = add_wrap(v, z);
		s.z = add_wrap(y, v);
		if constexpr (M == Mode::LOW_PASS) {
			return y;
		}
		else if constexpr (M == Mode::HIGH_PASS) {
			return sub_wrap(x, y);
		}
		else {
			return sub_wrap(add_wrap(y, y), x);
		}
	}

	/// @brief The memory scaled for a ladder's feedback: z × @p amount, Q31.
	/// @details The rounded product is Q30, reformatted to Q31: at full-scale z and amount the result wraps as two's
	/// complement.
	/// @param s The memory, read and not stepped.
	/// @param amount The feedback amount, Q30.
	/// @return z × @p amount, Q31, wrapping at full scale.
	[[nodiscard]] [[gnu::always_inline]] static Q<31> feedback(const State& s, Q<30> amount) {
		return as_wrap<Q<31>>(mul_r(s.z, amount));
	}

	/// @brief @ref feedback at a quarter of its scale, z × @p amount at Q29, for a caller that sums several at Q29
	/// before reformatting.
	/// @param s The memory, read and not stepped.
	/// @param amount The feedback amount, Q30.
	/// @return z × @p amount, Q29.
	[[nodiscard]] [[gnu::always_inline]] static Q<29> feedback_unshifted(const State& s, Q<30> amount) {
		return mul_r(s.z, amount);
	}
};

static_assert(processor_kernel<OnePole>);
static_assert(!std::is_polymorphic_v<OnePole>);

} // namespace deluge::dsp::fixed
