/*
 * Copyright © 2023-2026 Mark Adams, Synthstrom Audible Limited
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

#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h"

#include "util/param_functions.h" // lshiftAndSaturateUnknown

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace deluge::dsp::fixed {

namespace {

/// 1 - 1/ratio at knob @p ratio: 0.5 to 1.
[[nodiscard]] float ratio_fraction(q31 ratio) {
	return 0.5f + (static_cast<float>(ratio) / ONE_Q31f) / 2;
}

} // namespace

float compressor_attack_ms(q31 attack) {
	// The exp runs from 1 to about 7.4, about 2.7 at half the knob.
	return 0.5 + (std::exp(2 * static_cast<float>(attack) / ONE_Q31f) - 1) * 10;
}

float compressor_release_ms(q31 release) {
	// The exp runs from 1 to about 7.4, about 2.7 at half the knob.
	return 50 + (std::exp(2 * static_cast<float>(release) / ONE_Q31f) - 1) * 50;
}

float compressor_ratio(q31 ratio) {
	return 1 / (1 - ratio_fraction(ratio));
}

float compressor_sidechain_hz(q31 sidechain_hpf) {
	// The exp runs from 1 to about 4.5: about 33 Hz at half the knob, 104 Hz at the top.
	return (std::exp(1.5 * static_cast<float>(sidechain_hpf) / ONE_Q31f) - 1) * 30;
}

RmsFeedbackCompressor::RmsFeedbackCompressor() {
	setAttack(configured_.attack);
	setRelease(configured_.release);
	setThreshold(configured_.threshold);
	setRatio(configured_.ratio);
	setSidechain(configured_.sidechain_hpf);
	setBlend(configured_.blend);
}

void RmsFeedbackCompressor::configure(const CompressorKnobs& knobs) {
	if (knobs == configured_) {
		return;
	}
	setAttack(knobs.attack);
	setRelease(knobs.release);
	if (knobs.threshold != configured_.threshold) {
		setThreshold(knobs.threshold);
	}
	setRatio(knobs.ratio);
	setSidechain(knobs.sidechain_hpf);
	setBlend(knobs.blend);
	configured_ = knobs;
}

void RmsFeedbackCompressor::setThreshold(q31 threshold) {
	threshold_knob_ = threshold;
	threshold_ = 1 - 0.8f * (static_cast<float>(threshold_knob_) / ONE_Q31f);
}

void RmsFeedbackCompressor::reset() {
	reduction_envelope_ = 0;
	makeup_ = 0;
	mean_ = 0;
	seeded_ = false;
}

void RmsFeedbackCompressor::setAttack(q31 attack) {
	attack_ms_ = compressor_attack_ms(attack);
	attack_rate_ = (-1000.0f / kSampleRate) / attack_ms_;
}

void RmsFeedbackCompressor::setRelease(q31 release) {
	release_ms_ = compressor_release_ms(release);
	release_rate_ = (-1000.0f / kSampleRate) / release_ms_;
}

void RmsFeedbackCompressor::setRatio(q31 ratio) {
	fraction_ = ratio_fraction(ratio);
}

void RmsFeedbackCompressor::setSidechain(q31 sidechain_hpf) {
	const float fc = compressor_sidechain_hz(sidechain_hpf) / static_cast<float>(kSampleRate);
	const float wc = fc / (1 + fc);
	detector_moveability_ = static_cast<q31>(wc * static_cast<float>(ONE_Q31));
}

void RmsFeedbackCompressor::setBlend(q31 blend) {
	// ONE_Q31 is fully wet: the blend is no modulated param, so it needs no headroom.
	dry_ = ONE_Q31 - blend;
	wet_ = blend;
}

float RmsFeedbackCompressor::updateMakeup(float num_samples, q31 final_volume) {
	// The offset keeps a final volume of zero from giving NaNs. At most 2.08 nepers, at 0x7fffffff (about 8 in 3.29).
	const float song_volume = logf(final_volume + 1e-10);

	const float threshold = song_volume * threshold_;
	// The song volume lands here, so the envelope smooths it against clicks; the makeup gain and the reduction then
	// move at the same rate.
	const float last = makeup_;
	makeup_ = std::max<float>((song_volume - threshold - 1) * fraction_, 0);
	makeup_ = runEnvelope(last, makeup_, num_samples);
	return threshold;
}

void RmsFeedbackCompressor::renderVolumeNeutral(std::span<StereoSample> block, q31 final_volume,
                                                CompressorScratch scratch) {
	render(block, 1 << 27, 1 << 27, final_volume >> 3, scratch);
}

void RmsFeedbackCompressor::render(std::span<StereoSample> block, q31 volume_l, q31 volume_r, q31 final_volume,
                                   CompressorScratch scratch) {
	if (wet_ != ONE_Q31) {
		std::ranges::copy(block, scratch.begin());
	}

	if (!seeded_) {
		// The first sample interpolates against the block's own start rather than against silence.
		saturation_[0] = legacy::AntialiasedTanh{
		    kSaturation, static_cast<uint32_t>(lshiftAndSaturateUnknown(block[0].l, static_cast<uint8_t>(kSaturation)))
		                     + legacy::AntialiasedTanh::kSilent};
		saturation_[1] = legacy::AntialiasedTanh{
		    kSaturation, static_cast<uint32_t>(lshiftAndSaturateUnknown(block[0].r, static_cast<uint8_t>(kSaturation)))
		                     + legacy::AntialiasedTanh::kSilent};
		seeded_ = true;
	}
	// Every block, since the song volume may have changed.
	const float threshold = updateMakeup(static_cast<float>(block.size()), final_volume);

	const float over = std::max<float>(0, (log_rms_ - threshold));

	reduction_envelope_ = runEnvelope(reduction_envelope_, over, static_cast<float>(block.size()));

	const float reduction = -reduction_envelope_ * fraction_;

	// Nepers of gain: above 3.43 the gain would pass 31, the most there is headroom for.
	const float gain_nepers = base_gain_ + makeup_ + reduction;

	float gain = std::exp(gain_nepers);
	gain = std::min<float>(gain, 31);

	// The channels' target gains, 13.18 signed, held as floats on the way to the increments.
	const float final_volume_l = gain * static_cast<float>(volume_l >> 9);
	const float final_volume_r = gain * static_cast<float>(volume_r >> 9);

	// The steps (5.26 signed) that reach the target gains by the end of the block.
	const q31 increment_l =
	    static_cast<int32_t>((final_volume_l - (current_volume_l_ >> 8)) / static_cast<float>(block.size())) << 8;
	const q31 increment_r =
	    static_cast<int32_t>((final_volume_r - (current_volume_r_ >> 8)) / static_cast<float>(block.size())) << 8;

	for (std::size_t i = 0; i < block.size(); i++) {
		StereoSample& sample = block[i];
		current_volume_l_ += increment_l;
		current_volume_r_ += increment_r;

		// << 4: the gains are 5.26 signed rather than 1.30.
		sample.l = multiply_32x32_rshift32(sample.l, current_volume_l_) << 4;
		sample.l = saturation_[0].render(sample.l);

		sample.r = multiply_32x32_rshift32(sample.r, current_volume_r_) << 4;
		sample.r = saturation_[1].render(sample.r);

		if (wet_ != ONE_Q31) {
			// dry_wet_blend's arithmetic (fixed/mix/dry_wet.h), with DryWetBlend's FULL_WET skip around it.
			// << 1 makes up for the two multiplies.
			sample.l = multiply_32x32_rshift32(sample.l, wet_);
			sample.l = multiply_accumulate_32x32_rshift32_rounded(sample.l, scratch[i].l, dry_);
			sample.l <<= 1;
			sample.r = multiply_32x32_rshift32(sample.r, wet_);
			sample.r = multiply_accumulate_32x32_rshift32_rounded(sample.r, scratch[i].r, dry_);
			sample.r <<= 1;
		}
	}

	// × 4 for dB, then × 4 again for the LEDs' range, since a 30 dB reduction is all but silence.
	gain_reduction_ = static_cast<uint8_t>(std::clamp<int32_t>(static_cast<int32_t>(-(reduction) * 4 * 4), 0, 127));
	// The level the next block's gain follows.
	log_rms_ = measureLevel(block);
}

float RmsFeedbackCompressor::runEnvelope(float current, float desired, float num_samples) const {
	if (desired > current) {
		return desired + std::exp(attack_rate_ * num_samples) * (current - desired);
	}
	return desired + std::exp(release_rate_ * num_samples) * (current - desired);
}

float RmsFeedbackCompressor::measureLevel(std::span<const StereoSample> block) {
	q31 sum = 0;
	const float last_mean = mean_;

	for (const StereoSample sample : block) {
		const q31 l = sample.l - detector_[0].lowPass(sample.l, detector_moveability_);
		const q31 r = sample.r - detector_[1].lowPass(sample.r, detector_moveability_);
		const q31 s = std::max(std::abs(l), std::abs(r));
		sum += multiply_32x32_rshift32(s, s);
	}

	const auto ns = static_cast<float>(block.size() * 2);
	mean_ = (static_cast<float>(sum) / ONE_Q31f) / ns;
	// A one-pole IIR rather than a long FIR: close, and far cheaper. The more samples in the block, the more weight
	// on its own mean, so the response keeps its speed when the blocks grow under load.
	mean_ = (mean_ * ns + last_mean) / (1 + ns);
	const float rms = static_cast<float>(ONE_Q31) * std::sqrt(mean_);

	// 0 to about 21.5 (full scale); the DAC clips at about 16.
	return std::log(std::max(rms, 1.0f));
}

} // namespace deluge::dsp::fixed
