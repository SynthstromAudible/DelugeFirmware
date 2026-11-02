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
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/curves/exp_curve.h"

#include <cstdint>
#include <type_traits>

/// @file
/// @brief ShelfPair: the EQ's bass and treble shelves over stereo frames, as a processor kernel.
namespace deluge::dsp::fixed {

/// @brief Bass and treble shelves, each a one-pole split of the signal, added back scaled by its gain.
/// @details Per frame: the treble is split off (a one-pole low-pass keeps the rest), then the bass is a one-pole
/// low-pass of what remains; the treble and then the bass are added back, each × its gain (Q29, one at a flat knob).
/// The memories, sums and differences wrap as the target's `add` and `sub` do, the bands' add-backs included: a boost
/// at full scale wraps.
///
/// **Not @ref OnePole.** These steps truncate the product (its high word, unrounded), and the treble's step is the
/// Q30 product reformatted to Q31 while the bass's is the Q30 product read as Q31, half of it: the EQ's own
/// arithmetic, kept for bit-exactness.
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
	/// @brief A frequency knob's shift and scale into exp_curve's adjustment.
	static constexpr int kFrequencyShift = 5;
	static constexpr int32_t kFrequencyScale = 6;

	/// @brief The four params, Q31; a band at 0 is flat.
	struct Settings {
		q31 bass{0};             ///< The bass knob.
		q31 treble{0};           ///< The treble knob.
		q31 bass_frequency{0};   ///< The bass split's frequency knob.
		q31 treble_frequency{0}; ///< The treble split's frequency knob.
	};

	/// @brief The bands' moveabilities and gains, and which step.
	struct Coefficients {
		Q<31> bass_m{};         ///< Q31, from exp_curve(kBassPreset, …); 0 while the bass is flat.
		Q<31> treble_m{};       ///< Q31, from exp_curve(kTreblePreset, …); 0 while the treble is flat.
		Q<29> bass_gain{};      ///< (1 + the bass knob)² − 1, Q29: 0 at a flat knob, −1 (no bass) at its minimum.
		Q<29> treble_gain{};    ///< (1 + the treble knob)², Q29: one at a flat knob.
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
		// 1 + the knob, Q30: in [0, 2).
		const Q<30> bass_knob = add_wrap(as_floor<Q<30>>(Q<31>::from_raw(s.bass)), Q<30>::one());
		const Q<30> treble_knob = add_wrap(as_floor<Q<30>>(Q<31>::from_raw(s.treble)), Q<30>::one());
		// Raw: exp_curve's adjustment is an integer scale of the frequency knob.
		return {
		    .bass_m = Q<31>::from_raw(
		        bass ? exp_curve(kBassPreset, (s.bass_frequency >> kFrequencyShift) * kFrequencyScale) : 0),
		    .treble_m = Q<31>::from_raw(
		        treble ? exp_curve(kTreblePreset, (s.treble_frequency >> kFrequencyShift) * kFrequencyScale) : 0),
		    .bass_gain = sub_wrap(mul_r_as_wrap<Q<29>>(bass_knob, bass_knob), Q<29>::one()),
		    .treble_gain = mul_r_as_wrap<Q<29>>(treble_knob, treble_knob),
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
		Q<31> l = Q<31>::from_raw(x.l);
		Q<31> r = Q<31>::from_raw(x.r);
		Q<31> treble_l{};
		Q<31> treble_r{};
		if constexpr (kTreble) {
			const auto without_l = Q<31>::from_raw(s.without_treble.l);
			const auto without_r = Q<31>::from_raw(s.without_treble.r);
			const Q<31> next_l = add_wrap(without_l, mul_as_wrap<Q<31>>(sub_wrap(l, without_l), c.treble_m));
			const Q<31> next_r = add_wrap(without_r, mul_as_wrap<Q<31>>(sub_wrap(r, without_r), c.treble_m));
			s.without_treble = {.l = next_l.raw(), .r = next_r.raw()};
			treble_l = sub_wrap(l, next_l);
			treble_r = sub_wrap(r, next_r);
			l = next_l;
			r = next_r;
		}
		if constexpr (kBass) {
			const auto bass_l = Q<31>::from_raw(s.bass_only.l);
			const auto bass_r = Q<31>::from_raw(s.bass_only.r);
			// The Q30 product read as Q31: half the step.
			s.bass_only.l = add_wrap(bass_l, reinterpret_bits<Q<31>>(mul(sub_wrap(l, bass_l), c.bass_m))).raw();
			s.bass_only.r = add_wrap(bass_r, reinterpret_bits<Q<31>>(mul(sub_wrap(r, bass_r), c.bass_m))).raw();
		}
		if constexpr (kTreble) {
			l = add_wrap(l, mul_as_wrap<Q<31>>(treble_l, c.treble_gain));
			r = add_wrap(r, mul_as_wrap<Q<31>>(treble_r, c.treble_gain));
		}
		if constexpr (kBass) {
			l = add_wrap(l, mul_as_wrap<Q<31>>(Q<31>::from_raw(s.bass_only.l), c.bass_gain));
			r = add_wrap(r, mul_as_wrap<Q<31>>(Q<31>::from_raw(s.bass_only.r), c.bass_gain));
		}
		return {.l = l.raw(), .r = r.raw()};
	}
};

static_assert(processor_kernel<ShelfPair>);
static_assert(!std::is_polymorphic_v<ShelfPair>);

} // namespace deluge::dsp::fixed
