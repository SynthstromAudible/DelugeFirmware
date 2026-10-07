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

#include "dsp-next/fixed/filter/low_pass_ladder.h"

#include "dsp-next/fixed/curves/quick_log.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "util/waves.h" // the synth-wide noise generator (jcong)

#include <algorithm>
#include <tuple>

namespace deluge::dsp::fixed {

namespace {

/// The drive ladder's resonance (Q30) above which it oversamples, by log cutoff: 2^24 per entry of
/// @ref quick_log's scale, 6 bits of table, each entry at 2^-16 of the threshold.
constexpr std::array<int16_t, 65> kResonanceThresholdsForOversampling = {
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    15500, 20735, 17000, 9000,  9000,  9000,  9000,  9000,  9000,  9000,  9000,  9000,  9000,
};

/// The oversampled drive ladder's highest resonance (Q30), on the same scale as
/// @ref kResonanceThresholdsForOversampling: it would feed back too hard near the top.
constexpr std::array<int16_t, 65> kResonanceLimits = {
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    28415, 20000, 17000, 17000, 17000, 17000, 17000, 17000, 17000, 17000, 17000, 17000, 17000,
};

/// The next value of the synth-wide noise generator (util/waves.h's getNoise), Q31 across its full range.
[[gnu::always_inline]] inline q31 draw_ladder_noise() {
	return static_cast<q31>(CONG);
}

} // namespace

q31 LowPassLadder::configure(q31 gain, const FilterConfig& config, LowPassLadderMode mode) {
	mode_ = mode;
	morph_ = lshiftAndSaturate<2>(config.morph);
	q31 frequency = config.frequency;

	if (mode_ == LowPassLadderMode::TRANSISTOR_24DB_DRIVE) {
		const int32_t resonance = ONE_Q31 - (config.resonance << 2);
		processed_resonance_ = ONE_Q31 - resonance;

		int32_t log_frequency = quick_log(static_cast<uint32_t>(frequency));
		oversample_ = false;
		log_frequency = std::min(log_frequency, int32_t{63} << 24);

		if (config.allow_oversampling && (log_frequency >> 24) > 51) {
			const int32_t threshold = linear_table_interpolator.interpolateSigned(
			    static_cast<uint32_t>(log_frequency), 30, kResonanceThresholdsForOversampling.data(), 6);
			oversample_ = (processed_resonance_ > threshold);
		}

		if (oversample_) {
			frequency >>= 1;
			log_frequency -= 33554432;
			// Oversampling shifts the cutoff slightly; this pulls it back.
			frequency -= (multiply_32x32_rshift32_rounded(log_frequency, frequency) >> 8) * 34;
			// A cutoff near the top would make content the crude downsampling can't take.
			frequency = std::min(int32_t{39056384}, frequency);
			const int32_t limit = linear_table_interpolator.interpolateSigned(static_cast<uint32_t>(log_frequency), 30,
			                                                                  kResonanceLimits.data(), 6);
			processed_resonance_ = std::min(processed_resonance_, limit);
		}
	}
	else {
		// The resonance falls away as the cutoff goes very low.
		constexpr int32_t kHowMuchToKeep = ONE_Q31 - 33;
		constexpr int32_t kResonanceUpperLimit = 510000000; // above this it feeds back lots
		int32_t resonance = ONE_Q31 - (std::min(config.resonance, kResonanceUpperLimit) << 2);
		resonance = multiply_32x32_rshift32_rounded(resonance, resonance) << 1;
		processed_resonance_ = ONE_Q31 - resonance;
		processed_resonance_ = multiply_32x32_rshift32_rounded(processed_resonance_, kHowMuchToKeep) << 1;
	}

	const CurvedFrequency curved = curve_frequency(frequency);
	divide_by_one_plus_tanned_ = curved.divide_by_one_plus_tanned;
	moveability_ = std::max(curved.fc, q31{4317840});

	if (mode_ == LowPassLadderMode::TRANSISTOR_12DB) {
		// Between -2 and 0, Q30.
		const int32_t moveability_negative = moveability_ - 1073741824;
		feedbacks_[1] = multiply_32x32_rshift32_rounded(moveability_negative, divide_by_one_plus_tanned_) << 1;
		feedbacks_[0] = multiply_32x32_rshift32_rounded(feedbacks_[1], moveability_) << 1;
		divide_by_total_moveability_and_processed_resonance_ = static_cast<q31>(
		    int64_t{67108864} * 1073741824
		    / (67108864
		       + multiply_32x32_rshift32_rounded(
		           processed_resonance_,
		           multiply_32x32_rshift32_rounded(moveability_negative,
		                                           multiply_32x32_rshift32_rounded(moveability_, moveability_)))));
	}
	else {
		feedbacks_[2] = multiply_32x32_rshift32_rounded(divide_by_one_plus_tanned_, moveability_);
		feedbacks_[1] = multiply_32x32_rshift32_rounded(feedbacks_[2], moveability_) << 1;
		feedbacks_[0] = multiply_32x32_rshift32_rounded(feedbacks_[1], moveability_) << 1;

		// Q26 (1 is 67108864).
		const int32_t one_plus_loop_gain =
		    67108864
		    + multiply_32x32_rshift32_rounded(
		        moveability_,
		        multiply_32x32_rshift32_rounded(
		            moveability_,
		            multiply_32x32_rshift32_rounded(
		                moveability_, multiply_32x32_rshift32_rounded(moveability_, processed_resonance_))));
		divide_by_total_moveability_and_processed_resonance_ =
		    static_cast<q31>(72057594037927936.0 / static_cast<double>(one_plus_loop_gain));
	}

	if (mode_ != LowPassLadderMode::TRANSISTOR_24DB_DRIVE) {
		// Extra feedback, but only while the cutoff is low enough not to alias.
		if (curved.tanned <= 304587486) {
			processed_resonance_ = multiply_32x32_rshift32_rounded(processed_resonance_, 1150000000) << 1;
		}
		else {
			processed_resonance_ >>= 1;
		}

		int32_t cut = std::min(config.resonance, int32_t{536870911});
		cut = 536870912 - cut;
		cut = multiply_32x32_rshift32(cut, cut) << 3;
		cut = 536870912 - cut;
		const int32_t gain_modifier = 268435456 + cut;
		gain = multiply_32x32_rshift32(gain, gain_modifier) << 3;
	}
	else {
		// The drive ladder's output is louder.
		gain = static_cast<q31>(gain * 0.8);
	}
	return gain;
}

q31 LowPassLadder::noisyMoveability(Channel& channel) const {
	const q31 noise = draw_ladder_noise() >> 2;
	const q31 distance_to_go = noise - channel.noise_last;
	channel.noise_last += distance_to_go >> 7;
	return moveability_ + multiply_32x32_rshift32(moveability_, channel.noise_last);
}

q31 LowPassLadder::scaleInput(q31 input, q31 feedbacks_sum) const {
	const q31 scaled = multiply_32x32_rshift32_rounded(
	                       (input - (multiply_32x32_rshift32_rounded(feedbacks_sum, processed_resonance_) << 3)),
	                       divide_by_total_moveability_and_processed_resonance_)
	                   << 2;
	if (morph_ > 0 || processed_resonance_ > 510000000) {
		const q31 extra = 2 * multiply_32x32_rshift32(input, morph_);
		return tanh<2>(scaled + extra);
	}
	return scaled;
}

q31 LowPassLadder::render12dB(q31 input, Channel& channel) const {
	const q31 noisy_m = noisyMoveability(channel);
	auto& [first, second, third, fourth] = channel.sections;
	const q31 feedbacks_sum = first.feedbackOutput(feedbacks_[0]) + second.feedbackOutput(feedbacks_[1])
	                          + third.feedbackOutput(divide_by_one_plus_tanned_);
	const q31 x = scaleInput(input, feedbacks_sum);
	return third.allPass(second.lowPass(first.lowPass(x, noisy_m), noisy_m), noisy_m) << 1;
}

q31 LowPassLadder::render24dB(q31 input, Channel& channel) const {
	const q31 noisy_m = noisyMoveability(channel);
	auto& [first, second, third, fourth] = channel.sections;
	// The last section's feedback "should" be halved onto Q30, but the ladder sounds better (and reaches full
	// resonance) without.
	const q31 feedbacks_sum =
	    (first.feedbackOutputUnshifted(feedbacks_[0]) + second.feedbackOutputUnshifted(feedbacks_[1])
	     + third.feedbackOutputUnshifted(feedbacks_[2]) + fourth.feedbackOutputUnshifted(divide_by_one_plus_tanned_))
	    << 2;
	const q31 x = scaleInput(input, feedbacks_sum);
	return fourth.lowPass(third.lowPass(second.lowPass(first.lowPass(x, noisy_m), noisy_m), noisy_m), noisy_m) << 1;
}

q31 LowPassLadder::renderDrive(q31 input, Channel& channel) const {
	const q31 noisy_m = noisyMoveability(channel);
	auto& [first, second, third, fourth] = channel.sections;
	const q31 feedbacks_sum =
	    (first.feedbackOutputUnshifted(feedbacks_[0]) + second.feedbackOutputUnshifted(feedbacks_[1])
	     + third.feedbackOutputUnshifted(feedbacks_[2]) + fourth.feedbackOutputUnshifted(divide_by_one_plus_tanned_))
	    << 2;
	// The feedback is saturated; the input is not, as that is where it would alias most.
	const q31 x = scaleInput(input, tanh<7>(feedbacks_sum));
	const q31 a = first.lowPass(x, noisy_m);
	const q31 b = second.lowPass(a, noisy_m);
	const q31 c = third.lowPass(b, noisy_m);
	return fourth.lowPass(c, noisy_m) << 1;
}

q31 LowPassLadder::renderDriveOutput(q31 input, Channel& channel, bool oversample) const {
	if (oversample) {
		// The input held for both halves of the sample works better than interpolating it; the first output is
		// dropped (the ladder itself filters what that crude downsampling would alias).
		std::ignore = renderDrive(input, channel);
	}
	return tanh<4>(renderDrive(input, channel));
}

void LowPassLadder::filterMono(std::span<q31> block) {
	switch (mode_) {
	case LowPassLadderMode::TRANSISTOR_12DB:
		for (q31& sample : block) {
			sample = render12dB(sample, left_);
		}
		break;
	case LowPassLadderMode::TRANSISTOR_24DB:
		for (q31& sample : block) {
			sample = render24dB(sample, left_);
		}
		break;
	case LowPassLadderMode::TRANSISTOR_24DB_DRIVE:
		if (oversample_) {
			for (q31& sample : block) {
				sample = renderDriveOutput(sample, left_, true);
			}
		}
		else {
			for (q31& sample : block) {
				sample = renderDriveOutput(sample, left_, false);
			}
		}
		break;
	}
}

void LowPassLadder::filterStereo(std::span<q31> interleaved) {
	switch (mode_) {
	case LowPassLadderMode::TRANSISTOR_12DB:
		for (std::size_t i = 0; i < interleaved.size(); i += 2) {
			interleaved[i] = render12dB(interleaved[i], left_);
			interleaved[i + 1] = render12dB(interleaved[i + 1], right_);
		}
		break;
	case LowPassLadderMode::TRANSISTOR_24DB:
		for (std::size_t i = 0; i < interleaved.size(); i += 2) {
			interleaved[i] = render24dB(interleaved[i], left_);
			interleaved[i + 1] = render24dB(interleaved[i + 1], right_);
		}
		break;
	case LowPassLadderMode::TRANSISTOR_24DB_DRIVE:
		if (oversample_) {
			for (std::size_t i = 0; i < interleaved.size(); i += 2) {
				interleaved[i] = renderDriveOutput(interleaved[i], left_, true);
				interleaved[i + 1] = renderDriveOutput(interleaved[i + 1], right_, true);
			}
		}
		else {
			for (std::size_t i = 0; i < interleaved.size(); i += 2) {
				interleaved[i] = renderDriveOutput(interleaved[i], left_, false);
				interleaved[i + 1] = renderDriveOutput(interleaved[i + 1], right_, false);
			}
		}
		break;
	}
}

void LowPassLadder::renderMono(std::span<q31> block, FadeScratch scratch) {
	const bool fading = fade_.active();
	if (fading) {
		fade_.keepDry(block, scratch);
	}
	filterMono(block);
	if (fading) {
		fade_.blend(block, scratch);
	}
}

void LowPassLadder::renderStereo(std::span<q31> interleaved, FadeScratch scratch) {
	const bool fading = fade_.active();
	if (fading) {
		fade_.keepDry(interleaved, scratch);
	}
	filterStereo(interleaved);
	if (fading) {
		fade_.blend(interleaved, scratch);
	}
}

void LowPassLadder::reset(bool fade) {
	left_.reset();
	right_.reset();
	if (fade) {
		fade_.begin();
	}
}

void LowPassLadder::zero() {
	*this = LowPassLadder{};
	fade_.zero();
}

} // namespace deluge::dsp::fixed
