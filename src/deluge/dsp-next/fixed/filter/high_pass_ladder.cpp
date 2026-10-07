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

#include "dsp-next/fixed/filter/high_pass_ladder.h"

#include "dsp-next/fixed/shaping/antialiased_tanh.h"
#include "dsp-next/fixed/shaping/tanh.h"
#include "util/fixedpoint.h"
#include "util/param_functions.h" // lshiftAndSaturate

#include <algorithm>
#include <tuple>

namespace deluge::dsp::fixed {

q31 HighPassLadder::configure(const FilterConfig& config) {
	constexpr int32_t kExtraFeedback = 1200000000;
	morph_ = lshiftAndSaturate<2>(config.morph);
	const CurvedFrequency curved = curve_frequency(config.frequency);
	fc_ = curved.fc;

	constexpr int32_t kResonanceUpperLimit = 536870911;
	int32_t resonance = ONE_Q31 - (std::min(config.resonance, kResonanceUpperLimit) << 2);
	resonance = multiply_32x32_rshift32_rounded(resonance, resonance) << 1;
	processed_resonance_ = ONE_Q31 - resonance;
	processed_resonance_ = std::max(processed_resonance_, int32_t{134217728}); // the least resonance
	const int32_t processed_resonance_unaltered = processed_resonance_;

	processed_resonance_ = multiply_32x32_rshift32(processed_resonance_, kExtraFeedback) << 1;
	divide_by_processed_resonance_ = static_cast<q31>(2147483648.0 / static_cast<double>(processed_resonance_ >> 23));

	// Q29 (1 is 536870912).
	const int32_t moveability_times_resonance = multiply_32x32_rshift32(processed_resonance_unaltered, fc_);
	// Q28 (1 is 268435456).
	const int32_t moveability_squared_times_resonance = multiply_32x32_rshift32(moveability_times_resonance, fc_);

	high_pass_3_feedback_ = -multiply_32x32_rshift32_rounded(fc_, curved.divide_by_one_plus_tanned);
	low_pass_1_feedback_ = curved.divide_by_one_plus_tanned >> 1;

	const auto to_divide_by = static_cast<uint32_t>(int32_t{268435456} - (moveability_times_resonance >> 1)
	                                                + moveability_squared_times_resonance);
	divide_by_total_moveability_ = static_cast<int32_t>(static_cast<double>(processed_resonance_) * 67108864.0
	                                                    / static_cast<double>(to_divide_by));

	// The volume falls as the resonance rises.
	const q31 raw_resonance = std::min(config.resonance, q31{ONE_Q31 >> 2}) << 2;
	q31 squared = multiply_32x32_rshift32(raw_resonance, raw_resonance) << 1;
	squared = (multiply_32x32_rshift32(squared, squared) >> 4) * 19;
	return multiply_32x32_rshift32(config.gain, ONE_Q31 - squared) << 1;
}

q31 HighPassLadder::render(q31 input, Channel& channel) const {
	// The inputs are small (about 16 bits), so this nudge is too; the floor is about as low as the cutoff goes
	// without sounding broken.
	constexpr q31 kLowerLimit = -(ONE_Q31 >> 8);
	const q31 temp_fc = std::max(multiply_accumulate_32x32_rshift32_rounded(fc_, input << 4, morph_), kLowerLimit);

	const q31 first_output = input - channel.high_pass_1.lowPass(input, temp_fc);
	const q31 feedbacks = channel.high_pass_3.feedbackOutput(high_pass_3_feedback_)
	                      + channel.low_pass_1.feedbackOutput(low_pass_1_feedback_);
	q31 a = multiply_32x32_rshift32_rounded(divide_by_total_moveability_, first_output + feedbacks) << (4 + 1);

	// Saturate (and antialias) only at high resonance.
	if (processed_resonance_ > 900000000) {
		AntialiasedTanh saturator{1, channel.last_working_value};
		a = saturator.render(a);
		channel.last_working_value = saturator.previous();
	}
	else {
		channel.last_working_value = static_cast<uint32_t>(lshiftAndSaturate<2>(a)) + 2147483648u;
		if (processed_resonance_ > 750000000) {
			a = tanh<2>(a);
		}
	}

	// Only the sections' memories are wanted here: they feed back on the next sample.
	std::ignore = channel.low_pass_1.lowPass(a - channel.high_pass_3.lowPass(a, temp_fc), temp_fc);

	return multiply_32x32_rshift32_rounded(a, divide_by_processed_resonance_) << (8 - 1);
}

void HighPassLadder::renderMono(std::span<q31> block, FadeScratch scratch) {
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

void HighPassLadder::renderStereo(std::span<q31> interleaved, FadeScratch scratch) {
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

void HighPassLadder::reset(bool fade) {
	left_.reset();
	right_.reset();
	if (fade) {
		fade_.begin();
	}
}

void HighPassLadder::zero() {
	*this = HighPassLadder{};
	fade_.zero();
}

} // namespace deluge::dsp::fixed
