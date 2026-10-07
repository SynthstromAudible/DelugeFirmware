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

#include "dsp-next/fixed/filter/state_variable_filter.h"

#include "dsp-next/fixed/shaping/tanh.h"
#include "util/fixedpoint.h"
#include "util/param_functions.h" // lshiftAndSaturate

namespace deluge::dsp::fixed {

q31 StateVariableFilter::configure(q31 gain, const FilterConfig& config, SvfMode mode) {
	q31 morph = lshiftAndSaturate<2>(config.morph);
	fc_ = curve_frequency(config.frequency).fc;
	// The filter's true cutoff is the arcsine of this rate; × 1.25 brings it loosely in line with the ladders'.
	constexpr auto kQuarter = static_cast<q31>(ONE_Q31 * 0.25);
	fc_ = fc_ + multiply_32x32_rshift32(fc_, kQuarter);

	band_mode_ = (mode == SvfMode::BAND);
	// The resonance (Q29 at most) times 4 spans Q31.
	q_ = (ONE_Q31 - 4 * config.resonance);
	in_ = (q_ >> 1) + (ONE_Q31 >> 1);
	// The damping squared matches the ladders better, and makes the input scale its square root.
	q_ = multiply_32x32_rshift32_rounded(q_, q_) << 1;

	// Each branch keeps the weights from overflowing.
	constexpr q31 kOneHalf = ONE_Q31 >> 1;
	if (band_mode_) {
		if (morph > kOneHalf) {
			morph = 2 * (morph - kOneHalf);
			c_low_ = 0;
			c_band_ = ONE_Q31 - morph;
			c_high_ = morph;
		}
		else {
			morph = 2 * morph;
			c_low_ = ONE_Q31 - morph;
			c_band_ = morph;
			c_high_ = 0;
		}
	}
	else {
		c_low_ = ONE_Q31 - morph;
		c_high_ = morph;
		c_band_ = 0;
	}
	return gain;
}

q31 StateVariableFilter::render(q31 input, Channel& channel) const {
	q31 low = channel.low;
	q31 band = channel.band;

	input = multiply_32x32_rshift32(in_, input);

	low = low + 2 * multiply_32x32_rshift32(band, fc_);
	q31 high = input - low;
	high = high - 2 * multiply_32x32_rshift32(band, q_);
	band = 2 * multiply_32x32_rshift32(high, fc_) + band;
	band = tanh<3>(band); // the band feedback saturates

	q31 low_sum = low;
	q31 high_sum = high;
	q31 band_sum = band;

	// The second pass doubles the reach in cutoff.
	low = low + 2 * multiply_32x32_rshift32(band, fc_);
	high = input - low;
	high = high - 2 * multiply_32x32_rshift32(band, q_);
	band = 2 * multiply_32x32_rshift32(high, fc_) + band;

	low_sum = low_sum + low;
	high_sum = high_sum + high;
	band_sum = band_sum + band;

	q31 result = multiply_32x32_rshift32_rounded(low_sum, c_low_);
	result = multiply_accumulate_32x32_rshift32_rounded(result, high_sum, c_high_);
	if (band_mode_) {
		result = multiply_accumulate_32x32_rshift32_rounded(result, band_sum, c_band_);
	}

	band = tanh<3>(band);
	// × 2 undoes each multiply's halving, × 1.5 matches the ladders.
	result = 3 * result;

	channel.low = low;
	channel.band = band;
	return result;
}

void StateVariableFilter::renderMono(std::span<q31> block, FadeScratch scratch) {
	const bool fading = fade_.active();
	if (fading) {
		fade_.keepDry(block, scratch);
	}
	for (q31& sample : block) {
		sample = render(sample, left_);
	}
	if (fading) {
		fade_.blend(block, scratch);
	}
}

void StateVariableFilter::renderStereo(std::span<q31> interleaved, FadeScratch scratch) {
	const bool fading = fade_.active();
	if (fading) {
		fade_.keepDry(interleaved, scratch);
	}
	for (std::size_t i = 0; i < interleaved.size(); i += 2) {
		interleaved[i] = render(interleaved[i], left_);
		interleaved[i + 1] = render(interleaved[i + 1], right_);
	}
	if (fading) {
		fade_.blend(interleaved, scratch);
	}
}

void StateVariableFilter::reset(bool fade) {
	left_ = {};
	right_ = {};
	if (fade) {
		fade_.begin();
	}
}

void StateVariableFilter::zero() {
	*this = StateVariableFilter{};
	fade_.zero();
}

} // namespace deluge::dsp::fixed
