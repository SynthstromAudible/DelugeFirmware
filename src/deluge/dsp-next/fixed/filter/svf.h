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
#include "dsp-next/core/types.h"
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
/// where each product keeps the high word of a Q31 × Q31, Q30, and the doubling restores Q31. The scaled input is
/// itself such a high word, x × in at Q30, read as Q31: the passes run at half the input's level, and the blend sums
/// two of them. The first pass saturates the band feedback (@ref tanh at drive 3) before the second pass runs; the
/// second pass's band is saturated after the blend reads it. The blend sums both passes' outputs: c_low × (low₁ + low₂)
/// + c_high × (high₁ + high₂), plus c_band × (band₁ + band₂) in BAND mode, each product rounded to Q30, then tripled
/// and read as Q31: × 1.5, the ladders' level.
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
		Q<31> fc{};                ///< The integrators' rate, tan(f) / (1 + tan(f)) × 1.125, Q31.
		Q<31> q{};                 ///< The damping, squared, Q31.
		Q<31> in{};                ///< The input's scale, Q31: the square root of the damping, so the level holds.
		Q<31> c_low{};             ///< The low outputs' weight in the blend, Q31.
		Q<31> c_band{};            ///< The band outputs' weight in the blend (BAND only), Q31.
		Q<31> c_high{};            ///< The high outputs' weight in the blend, Q31.
		q31 gain_compensation = 0; ///< The gain the owner set, Q31, which @ref gain returns.
		Mode mode = Mode::BAND;    ///< Which outputs the blend reads, dispatched on once per block.
	};

	/// @brief One channel's integrators, Q31. State{} is silence.
	struct State {
		q31 low = 0;  ///< The low-pass integrator.
		q31 band = 0; ///< The band-pass integrator, saturated at the end of each step.
	};

	/// @brief Half of the morph's Q31 range, 2^30 − 1, where the BAND blend's two halves meet.
	static constexpr Q<31> kHalf = Q<31>::from_raw(Q<31>::max().raw() >> 1);

	/// @brief The coefficients for @p s.
	/// @details The cutoff is warped by @ref curve_frequency and raised by an eighth (the filter's true cutoff is the
	/// arcsine of its rate, so × 1.125 brings it loosely in line with the ladders'). The resonance (Q29 at most) is
	/// widened to Q31 (× 4 in raw bits): q = 1 − r, in = q/2 + 1/2, then q is squared, which matches the ladders better
	/// and makes `in` its square root. The morph is lifted to Q31 (saturating); in BAND its lower half moves low → band
	/// and its upper half band → high, each at twice the rate, so no weight overflows.
	[[nodiscard]] [[gnu::always_inline]] static Coefficients coefficients(const Settings& s) {
		Coefficients c{};
		auto morph = as_clamp_then_shift<Q<31>>(Q<29>::from_raw(s.config.morph));
		const Q<31> fc = curve_frequency(s.config.frequency).fc;
		// 2^29 − 1 as a high-word multiplier is an eighth, one LSB short.
		constexpr auto kEighth = UQ<32>::from_raw(static_cast<uint32_t>(Q<31>::max().raw() / 4));
		c.fc = add_wrap(fc, mul(kEighth, fc));

		c.mode = s.mode == SvfMode::BAND ? Mode::BAND : Mode::NOTCH;
		const Q<31> damping = sub_wrap(Q<31>::max(), as_wrap<Q<31>>(Q<29>::from_raw(s.config.resonance)));
		c.in = add_wrap(halve_floor(damping), kHalf);
		c.q = mul_r_as_wrap<Q<31>>(damping, damping);

		if (c.mode == Mode::BAND) {
			if (morph > kHalf) {
				morph = scale_shl_wrap<1>(sub_wrap(morph, kHalf));
				c.c_low = Q<31>{};
				c.c_band = sub_wrap(Q<31>::max(), morph);
				c.c_high = morph;
			}
			else {
				morph = scale_shl_wrap<1>(morph);
				c.c_low = sub_wrap(Q<31>::max(), morph);
				c.c_band = morph;
				c.c_high = Q<31>{};
			}
		}
		else {
			c.c_low = sub_wrap(Q<31>::max(), morph);
			c.c_high = morph;
			c.c_band = Q<31>{};
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
		// x × in at Q30, read as Q31: each pass runs at half the input's level.
		const auto input = reinterpret_bits<Q<31>>(mul(c.in, Q<31>::from_raw(x)));
		Q<31> low = Q<31>::from_raw(s.low);
		Q<31> band = Q<31>::from_raw(s.band);

		low = add_wrap(low, mul_as_wrap<Q<31>>(band, c.fc));
		Q<31> high = sub_wrap(sub_wrap(input, low), mul_as_wrap<Q<31>>(band, c.q));
		band = add_wrap(mul_as_wrap<Q<31>>(high, c.fc), band);
		band = tanh<kBandDrive>(band);

		Q<31> low_sum = low;
		Q<31> high_sum = high;
		Q<31> band_sum = band;

		low = add_wrap(low, mul_as_wrap<Q<31>>(band, c.fc));
		high = sub_wrap(sub_wrap(input, low), mul_as_wrap<Q<31>>(band, c.q));
		band = add_wrap(mul_as_wrap<Q<31>>(high, c.fc), band);

		low_sum = add_wrap(low_sum, low);
		high_sum = add_wrap(high_sum, high);
		band_sum = add_wrap(band_sum, band);

		Q<30> result = mul_r(low_sum, c.c_low);
		result = mla_r(result, high_sum, c.c_high);
		if constexpr (M == Mode::BAND) {
			result = mla_r(result, band_sum, c.c_band);
		}

		s.low = low.raw();
		s.band = tanh<kBandDrive>(band).raw();
		// 3 × the Q30 blend, read as Q31: × 1.5.
		return reinterpret_bits<Q<31>>(add_wrap(scale_shl_wrap<1>(result), result)).raw();
	}

private:
	/// @brief The band feedback's saturation drive, as a power of two.
	static constexpr uint32_t kBandDrive = 3;
};

static_assert(processor_kernel<Svf>);
static_assert(!std::is_polymorphic_v<Svf>);

} // namespace deluge::dsp::fixed
