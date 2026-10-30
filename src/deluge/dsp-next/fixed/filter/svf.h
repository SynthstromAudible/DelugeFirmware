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
#include "dsp-next/core/q31_ops.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/filter/filter_modes.h"
#include "dsp-next/fixed/shaping/tanh.h"

#include <cstdint>
#include <type_traits>

/// @file
/// @brief Svf: the state-variable filter as a processor kernel.
namespace deluge::dsp::fixed {

/// @brief The state-variable filter, run twice per sample (doubling its reach in cutoff), blending its low, band and
/// high outputs by the morph.
/// @details Per sample, from the integrators low and band, Q31, with x the input scaled by `in`:
///
///     low += 2 (band × fc),   high = x − low − 2 (band × q),   band += 2 (high × fc)
///
/// where each product keeps the high word of a Q31 × Q31 (so the doubling restores Q31). The first pass saturates the
/// band feedback (@ref tanh at drive 3) before the second pass runs; the second pass's band is saturated after the
/// blend reads it. The blend sums both passes' outputs: c_low × (low₁ + low₂) + c_high × (high₁ + high₂), plus
/// c_band × (band₁ + band₂) in BAND mode, each product rounded, then × 3 (× 2 undoes the halving, × 1.5 matches the
/// ladders' level).
///
/// **Modes.** BAND sweeps the morph low → band → high; NOTCH sweeps low → high through the notch their sum makes. Both
/// share the integrators, so a mode change between blocks keeps the state.
///
/// **Wrapping.** The integrators' sums, the passes' sums and the final × 3 wrap modulo 2^32, as the target's `add` and
/// `sub` do; at loud input they leave q31.
///
/// **Gain.** The filter changes no level going in: @ref gain returns the gain the owner set.
struct Svf {
	using value_type = q31; ///< A sample, Q31.
	using step_type = q31;  ///< A sample, Q31: one step per sample.

	/// @brief Which outputs the morph blends.
	enum class Mode : uint8_t {
		BAND,  ///< Low → band → high.
		NOTCH, ///< Low → high, through the notch their sum makes.
	};
	using Modes = ModeList<Mode::BAND, Mode::NOTCH>; ///< Both share the integrators.

	/// @brief What the owner sets each block.
	struct Settings {
		q31 gain = 0; ///< The volume going in, Q31: returned unchanged as the compensation.
		/// The cutoff, resonance (Q29) and morph (Q29). A high-pass slot passes its morph already inverted
		/// ((1 << 29) - 1 - morph), so it sweeps high → band or notch → low.
		FilterConfig config{};
		SvfMode mode = SvfMode::BAND; ///< The slot's SVF mode.
	};

	/// @brief What a step reads.
	struct Coefficients {
		q31 fc = 0;                ///< The integrators' rate, tan(f) / (1 + tan(f)) × 1.25, Q31.
		q31 q = 0;                 ///< The damping, squared, Q31.
		q31 in = 0;                ///< The input's scale, Q31: the square root of the damping, so the level holds.
		q31 c_low = 0;             ///< The low outputs' weight in the blend, Q31.
		q31 c_band = 0;            ///< The band outputs' weight in the blend (BAND only), Q31.
		q31 c_high = 0;            ///< The high outputs' weight in the blend, Q31.
		q31 gain_compensation = 0; ///< The gain the owner set, Q31, which @ref gain returns.
		Mode mode = Mode::BAND;    ///< Which outputs the blend reads, dispatched on once per block.
	};

	/// @brief One channel's integrators, Q31. State{} is silence.
	struct State {
		q31 low = 0;  ///< The low-pass integrator.
		q31 band = 0; ///< The band-pass integrator, saturated at the end of each step.
	};

	/// @brief Half of the morph's Q31 range, where the BAND blend's two halves meet.
	static constexpr q31 kHalf = ONE_Q31 >> 1;

	/// @brief The coefficients for @p s.
	/// @details The cutoff is warped by @ref curve_frequency and raised by a quarter (the filter's true cutoff is the
	/// arcsine of its rate, so × 1.25 brings it loosely in line with the ladders'). The resonance (Q29 at most) times 4
	/// spans Q31: q = ONE_Q31 − 4r, in = q/2 + 1/2, then q is squared, which matches the ladders better and makes `in`
	/// its square root. The morph is lifted to Q31 (saturating); in BAND its lower half moves low → band and its upper
	/// half band → high, each at twice the rate, so no weight overflows.
	[[nodiscard]] [[gnu::always_inline]] static Coefficients coefficients(const Settings& s) {
		Coefficients c{};
		q31 morph = lshiftAndSaturate<2>(s.config.morph);
		c.fc = curve_frequency(s.config.frequency).fc;
		constexpr auto kQuarter = static_cast<q31>(ONE_Q31 * 0.25);
		c.fc = wrapping_add(c.fc, multiply_32x32_rshift32(c.fc, kQuarter));

		c.mode = s.mode == SvfMode::BAND ? Mode::BAND : Mode::NOTCH;
		c.q = ONE_Q31 - (4 * s.config.resonance);
		c.in = (c.q >> 1) + (ONE_Q31 >> 1);
		c.q = multiply_32x32_rshift32_rounded(c.q, c.q) << 1;

		if (c.mode == Mode::BAND) {
			if (morph > kHalf) {
				morph = 2 * (morph - kHalf);
				c.c_low = 0;
				c.c_band = ONE_Q31 - morph;
				c.c_high = morph;
			}
			else {
				morph = 2 * morph;
				c.c_low = ONE_Q31 - morph;
				c.c_band = morph;
				c.c_high = 0;
			}
		}
		else {
			c.c_low = ONE_Q31 - morph;
			c.c_high = morph;
			c.c_band = 0;
		}
		c.gain_compensation = s.gain;
		return c;
	}

	/// @brief The gain compensation: the gain the owner set, unchanged, Q31.
	[[nodiscard]] [[gnu::always_inline]] static constexpr q31 gain(const Coefficients& c) {
		return c.gain_compensation;
	}

	/// @brief One sample through both passes and the blend.
	/// @tparam M The block's mode: BAND adds the band outputs to the blend.
	/// @param c The block's coefficients.
	/// @param s The integrators, updated.
	/// @param x The input, Q31.
	/// @return The blend, Q31.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static q31 step(const Coefficients& c, const EmptyBlock& /*b*/, State& s,
	                                                     q31 x) {
		const q31 input = multiply_32x32_rshift32(c.in, x);
		q31 low = s.low;
		q31 band = s.band;

		low = wrapping_add(low, twice(multiply_32x32_rshift32(band, c.fc)));
		q31 high = wrapping_sub(wrapping_sub(input, low), twice(multiply_32x32_rshift32(band, c.q)));
		band = wrapping_add(twice(multiply_32x32_rshift32(high, c.fc)), band);
		band = tanh<kBandDrive>(band);

		q31 low_sum = low;
		q31 high_sum = high;
		q31 band_sum = band;

		low = wrapping_add(low, twice(multiply_32x32_rshift32(band, c.fc)));
		high = wrapping_sub(wrapping_sub(input, low), twice(multiply_32x32_rshift32(band, c.q)));
		band = wrapping_add(twice(multiply_32x32_rshift32(high, c.fc)), band);

		low_sum = wrapping_add(low_sum, low);
		high_sum = wrapping_add(high_sum, high);
		band_sum = wrapping_add(band_sum, band);

		q31 result = multiply_32x32_rshift32_rounded(low_sum, c.c_low);
		result = multiply_accumulate_32x32_rshift32_rounded(result, high_sum, c.c_high);
		if constexpr (M == Mode::BAND) {
			result = multiply_accumulate_32x32_rshift32_rounded(result, band_sum, c.c_band);
		}

		s.low = low;
		s.band = tanh<kBandDrive>(band);
		return wrapping_add(twice(result), result);
	}

private:
	/// @brief The band feedback's saturation drive, as a power of two.
	static constexpr uint32_t kBandDrive = 3;

	/// @brief @p x doubled, wrapping: a product's high word back to Q31.
	[[nodiscard]] [[gnu::always_inline]] static constexpr q31 twice(q31 x) { return wrapping_add(x, x); }
};

static_assert(processor_kernel<Svf>);
static_assert(!std::is_polymorphic_v<Svf>);

} // namespace deluge::dsp::fixed
