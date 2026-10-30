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
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/curves/exp_curve.h"
#include "util/fixedpoint.h"

#include <cstdint>
#include <type_traits>

/// @file
/// @brief ShelfPair: the EQ's bass and treble shelves over stereo frames, as a processor kernel.
namespace deluge::dsp::fixed {

/// @brief Bass and treble shelves, each a one-pole split of the signal, added back scaled by its gain.
/// @details Per frame: the treble is split off (a one-pole low-pass keeps the rest), then the bass is a one-pole
/// low-pass of what remains; the treble and then the bass are added back, each × its gain ≫ 32 ≪ 3. The memories, sums
/// and differences wrap as the target's `add` and `sub` do.
///
/// **Not @ref OnePole.** These steps truncate the product (its high word, unrounded), and the treble's step is
/// doubled (≪ 1) while the bass's is not: the EQ's own arithmetic, kept for bit-exactness.
///
/// **Modes.** BOTH, BASS and TREBLE step only the bands that are on, so a flat band's memory holds still and resumes
/// where it stopped. With both bands flat the owner does not run the kernel (@ref flat).
struct ShelfPair {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	/// @brief Which bands step.
	enum class Mode : uint8_t {
		BOTH,   ///< Bass and treble.
		BASS,   ///< Bass only; the treble's memory holds.
		TREBLE, ///< Treble only; the bass's memory holds.
	};
	using Modes = ModeList<Mode::BOTH, Mode::BASS, Mode::TREBLE>; ///< Each holds the other bands' memory still.

	/// @brief The exp_curve preset the bass's moveability starts from.
	static constexpr q31 kBassPreset = 120000000;
	/// @brief The exp_curve preset the treble's moveability starts from.
	static constexpr q31 kTreblePreset = 700000000;
	/// @brief The treble step's doubling; the bass's step is not doubled.
	static constexpr int kTrebleStepShift = 1;
	/// @brief The shift on each band's gain product.
	static constexpr int kBandGainShift = 3;
	/// @brief Half of one, Q31: a knob halved is offset by it to [0, 1) before squaring.
	static constexpr q31 kKnobOffset = 1073741824;
	/// @brief Taken from the bass's squared knob, so the knob at 0 is flat and at its minimum removes the bass.
	static constexpr q31 kBassGainOffset = 536870912;

	/// @brief The four params, Q31; a band at 0 is flat.
	struct Settings {
		int32_t bass{0};             ///< The bass knob.
		int32_t treble{0};           ///< The treble knob.
		int32_t bass_frequency{0};   ///< The bass split's frequency knob.
		int32_t treble_frequency{0}; ///< The treble split's frequency knob.
	};

	/// @brief The bands' moveabilities and gains, and which step.
	struct Coefficients {
		q31 bass_m = 0;         ///< Q31, from exp_curve(kBassPreset, …); 0 while the bass is flat.
		q31 treble_m = 0;       ///< Q31, from exp_curve(kTreblePreset, …); 0 while the treble is flat.
		q31 bass_gain = 0;      ///< The bass knob squared, less kBassGainOffset.
		q31 treble_gain = 0;    ///< The treble knob squared; 536870912 is flat.
		Mode mode = Mode::BOTH; ///< The bands that are on.
	};

	/// @brief The two splits' memories. State{} is silence.
	struct State {
		StereoSample without_treble{}; ///< The treble split's low-pass.
		StereoSample bass_only{};      ///< The bass band's low-pass.
	};

	/// @brief Whether both bands are flat, where the owner skips the kernel.
	[[nodiscard]] static constexpr bool flat(const Settings& s) { return s.bass == 0 && s.treble == 0; }

	/// @brief The coefficients for @p s, of which at least one band is on.
	[[nodiscard]] static Coefficients coefficients(const Settings& s) {
		const bool bass = s.bass != 0;
		const bool treble = s.treble != 0;
		const q31 bass_knob = (s.bass >> 1) + kKnobOffset;
		const q31 treble_knob = (s.treble >> 1) + kKnobOffset;
		return {
		    .bass_m = bass ? exp_curve(kBassPreset, (s.bass_frequency >> 5) * 6) : 0,
		    .treble_m = treble ? exp_curve(kTreblePreset, (s.treble_frequency >> 5) * 6) : 0,
		    .bass_gain = (multiply_32x32_rshift32_rounded(bass_knob, bass_knob) << 1) - kBassGainOffset,
		    .treble_gain = multiply_32x32_rshift32_rounded(treble_knob, treble_knob) << 1,
		    .mode = bass && treble ? Mode::BOTH : (bass ? Mode::BASS : Mode::TREBLE),
		};
	}

	/// @brief One frame through the bands @p M steps.
	/// @tparam M The bands that are on.
	/// @param c The moveabilities and gains.
	/// @param s The memories of the bands that are on, stepped.
	/// @param x The frame, Q31.
	/// @return The frame with its shelves, Q31.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& c, const EmptyBlock& /*b*/,
	                                                              State& s, StereoSample x) {
		constexpr bool kTreble = M != Mode::BASS;
		constexpr bool kBass = M != Mode::TREBLE;
		StereoSample treble{};
		if constexpr (kTreble) {
			s.without_treble.l = wrapping_add(s.without_treble.l,
			                                  multiply_32x32_rshift32(wrapping_sub(x.l, s.without_treble.l), c.treble_m)
			                                      << kTrebleStepShift);
			s.without_treble.r = wrapping_add(s.without_treble.r,
			                                  multiply_32x32_rshift32(wrapping_sub(x.r, s.without_treble.r), c.treble_m)
			                                      << kTrebleStepShift);
			treble = {.l = wrapping_sub(x.l, s.without_treble.l), .r = wrapping_sub(x.r, s.without_treble.r)};
			x = s.without_treble;
		}
		if constexpr (kBass) {
			s.bass_only.l =
			    wrapping_add(s.bass_only.l, multiply_32x32_rshift32(wrapping_sub(x.l, s.bass_only.l), c.bass_m));
			s.bass_only.r =
			    wrapping_add(s.bass_only.r, multiply_32x32_rshift32(wrapping_sub(x.r, s.bass_only.r), c.bass_m));
		}
		if constexpr (kTreble) {
			x.l = wrapping_add(x.l, multiply_32x32_rshift32(treble.l, c.treble_gain) << kBandGainShift);
			x.r = wrapping_add(x.r, multiply_32x32_rshift32(treble.r, c.treble_gain) << kBandGainShift);
		}
		if constexpr (kBass) {
			x.l = wrapping_add(x.l, multiply_32x32_rshift32(s.bass_only.l, c.bass_gain) << kBandGainShift);
			x.r = wrapping_add(x.r, multiply_32x32_rshift32(s.bass_only.r, c.bass_gain) << kBandGainShift);
		}
		return x;
	}
};

static_assert(processor_kernel<ShelfPair>);
static_assert(!std::is_polymorphic_v<ShelfPair>);

} // namespace deluge::dsp::fixed
