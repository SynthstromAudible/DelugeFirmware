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

/// @file
/// @brief Test-local references for the RMS feedback compressor.
///
/// @ref compressor_reference::ReferenceRmsFeedbackCompressor is the legacy compressor copied verbatim, line for line,
/// on the legacy one-pole section and antialiased tanh (reference_filters.h); it adds only `view()`. Its level
/// measurement keeps two defects: the block's sum of squares wraps in 32 bits on a loud block (a negative sum makes
/// the level NaN), and `std::abs` of a detector output of exactly INT32_MIN is undefined. Clang, on the target and the
/// host, takes the larger of the two magnitudes unsigned, so INT32_MIN measures 2^31 there, but nothing guarantees it.
/// That function is marked @ref DELUGE_REFERENCE_LEGACY_ARITHMETIC.
///
/// @ref compressor_reference::FixedReferenceRmsFeedbackCompressor is the firmware compressor, on the one-pole and
/// antialiased tanh kernels, with only those two defects fixed: what a rewrite must match bit for bit.

#pragma once

#include "definitions_cxx.hpp"    // kSampleRate
#include "dsp-next/core/kernel.h" // EmptyBlock
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h" // CompressorKnobs, RmsFeedbackCompressor::View
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/shaping/antialiased_tanh.h"
#include "reference_filters.h"
#include "util/fixedpoint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <type_traits>

namespace compressor_reference {

using deluge::dsp::q31;
using deluge::dsp::StereoSample;
using deluge::dsp::fixed::CompressorKnobs;
using filters_reference::Digest;
using filters_reference::ReferenceAntialiasedTanh;
using filters_reference::ReferenceOnePole;

/// @brief The compressor between blocks, as the firmware's compressor and both references report it.
using View = deluge::dsp::fixed::RmsFeedbackCompressor::View;

/// @brief The scratch a compressor needs to blend: distinct from the block and at least as long.
using CompressorScratch = std::span<StereoSample>;

namespace detail {

/// 1 - 1/ratio at knob @p ratio: 0.5 to 1.
[[nodiscard]] inline float ratio_fraction(q31 ratio) {
	return 0.5f + (static_cast<float>(ratio) / kOneQ31f) / 2;
}

[[nodiscard]] inline float compressor_attack_ms(q31 attack) {
	return 0.5 + (std::exp(2 * static_cast<float>(attack) / kOneQ31f) - 1) * 10;
}

[[nodiscard]] inline float compressor_release_ms(q31 release) {
	return 50 + (std::exp(2 * static_cast<float>(release) / kOneQ31f) - 1) * 50;
}

[[nodiscard]] inline float compressor_sidechain_hz(q31 sidechain_hpf) {
	return (std::exp(1.5 * static_cast<float>(sidechain_hpf) / kOneQ31f) - 1) * 30;
}

} // namespace detail

/// @brief The legacy RMS feedback compressor, copied: its level detector's high-pass on the legacy one-pole, its output
/// saturation on the legacy antialiased tanh, seeded from the first frame of the first block after a reset.
class ReferenceRmsFeedbackCompressor {
public:
	/// @brief A compressor at the default knobs, fully wet, with no gain reduction.
	ReferenceRmsFeedbackCompressor() {
		setAttack(configured_.attack);
		setRelease(configured_.release);
		set_threshold(configured_.threshold);
		setRatio(configured_.ratio);
		setSidechain(configured_.sidechain_hpf);
		setBlend(configured_.blend);
	}

	/// @brief Takes @p knobs, recomputing only when they changed.
	void configure(const CompressorKnobs& knobs) {
		if (knobs == configured_) {
			return;
		}
		setAttack(knobs.attack);
		setRelease(knobs.release);
		if (knobs.threshold != configured_.threshold) {
			set_threshold(knobs.threshold);
		}
		setRatio(knobs.ratio);
		setSidechain(knobs.sidechain_hpf);
		setBlend(knobs.blend);
		configured_ = knobs;
	}

	/// @brief The threshold this block renders with, from a knob position (Q31 of the knob's travel).
	void set_threshold(q31 threshold) {
		threshold_knob_ = threshold;
		threshold_ = 1 - 0.8f * (static_cast<float>(threshold_knob_) / kOneQ31f);
	}

	/// @brief Makeup gain in nepers at no reduction.
	void set_base_gain(float base_gain) { base_gain_ = base_gain; }

	/// @brief No gain reduction at the start of the next block, and the saturation seeded again.
	void reset() {
		reduction_envelope_ = 0;
		makeup_ = 0;
		mean_ = 0;
		seeded_ = false;
	}

	/// @brief Compresses @p block in place.
	/// @param volume_l, volume_r The channels' linear gain, 4.27 signed.
	/// @param final_volume The peak-to-peak volume scale, 3.29.
	/// @param scratch Holds the block as it came in, for the blend; read only when the blend is below fully wet.
	void render(std::span<StereoSample> block, q31 volume_l, q31 volume_r, q31 final_volume,
	            CompressorScratch scratch) {
		if (wet_ != kOneQ31) {
			std::ranges::copy(block, scratch.begin());
		}

		if (!seeded_) {
			// The first sample interpolates against the block's own start rather than against silence.
			saturation_[0] = ReferenceAntialiasedTanh{kSaturation, static_cast<uint32_t>(lshift_and_saturate_unknown(
			                                                           block[0].l, static_cast<uint8_t>(kSaturation)))
			                                                           + ReferenceAntialiasedTanh::kSilent};
			saturation_[1] = ReferenceAntialiasedTanh{kSaturation, static_cast<uint32_t>(lshift_and_saturate_unknown(
			                                                           block[0].r, static_cast<uint8_t>(kSaturation)))
			                                                           + ReferenceAntialiasedTanh::kSilent};
			seeded_ = true;
		}
		// Every block, since the song volume may have changed.
		const float threshold = updateMakeup(static_cast<float>(block.size()), final_volume);

		const float over = std::max<float>(0, (log_rms_ - threshold));

		reduction_envelope_ = runEnvelope(reduction_envelope_, over, static_cast<float>(block.size()));

		const float reduction = -reduction_envelope_ * fraction_;

		const float gain_nepers = base_gain_ + makeup_ + reduction;

		float gain = std::exp(gain_nepers);
		gain = std::min<float>(gain, 31);

		const float final_volume_l = gain * static_cast<float>(volume_l >> 9);
		const float final_volume_r = gain * static_cast<float>(volume_r >> 9);

		const q31 increment_l =
		    static_cast<int32_t>((final_volume_l - (current_volume_l_ >> 8)) / static_cast<float>(block.size())) << 8;
		const q31 increment_r =
		    static_cast<int32_t>((final_volume_r - (current_volume_r_ >> 8)) / static_cast<float>(block.size())) << 8;

		for (std::size_t i = 0; i < block.size(); i++) {
			StereoSample& sample = block[i];
			current_volume_l_ += increment_l;
			current_volume_r_ += increment_r;

			sample.l = multiply_32x32_rshift32(sample.l, current_volume_l_) << 4;
			sample.l = saturation_[0].render(sample.l);

			sample.r = multiply_32x32_rshift32(sample.r, current_volume_r_) << 4;
			sample.r = saturation_[1].render(sample.r);

			if (wet_ != kOneQ31) {
				sample.l = multiply_32x32_rshift32(sample.l, wet_);
				sample.l = multiply_accumulate_32x32_rshift32_rounded(sample.l, scratch[i].l, dry_);
				sample.l <<= 1;
				sample.r = multiply_32x32_rshift32(sample.r, wet_);
				sample.r = multiply_accumulate_32x32_rshift32_rounded(sample.r, scratch[i].r, dry_);
				sample.r <<= 1;
			}
		}

		gain_reduction_ = static_cast<uint8_t>(std::clamp<int32_t>(static_cast<int32_t>(-(reduction) * 4 * 4), 0, 127));
		log_rms_ = measureLevel(block);
	}

	/// @brief render at neutral channel gain, with @p final_volume (3.29) scaled.
	void render_volume_neutral(std::span<StereoSample> block, q31 final_volume, CompressorScratch scratch) {
		render(block, 1 << 27, 1 << 27, final_volume >> 3, scratch);
	}

	/// @return The last block's gain reduction for the LEDs.
	[[nodiscard]] uint8_t gain_reduction() const { return gain_reduction_; }

	/// @return Whether the saturation has been seeded since the last reset.
	[[nodiscard]] bool seeded() const { return seeded_; }
	/// @return The output saturation, left then right.
	[[nodiscard]] const std::array<ReferenceAntialiasedTanh, 2>& saturation() const { return saturation_; }
	/// @return The detector's high-pass, left then right.
	[[nodiscard]] const std::array<ReferenceOnePole, 2>& detector() const { return detector_; }

	/// @brief The compressor between blocks, read-only: every member, as the firmware's @ref View holds it.
	[[nodiscard]] View view() const {
		return {
		    .configured = configured_,
		    .attack_rate = attack_rate_,
		    .release_rate = release_rate_,
		    .fraction = fraction_,
		    .makeup = makeup_,
		    .threshold = threshold_,
		    .detector_moveability = detector_moveability_,
		    .reduction_envelope = reduction_envelope_,
		    .current_volume_l = current_volume_l_,
		    .current_volume_r = current_volume_r_,
		    .log_rms = log_rms_,
		    .mean = mean_,
		    .saturation_drive = saturation_[0].drive(),
		    .saturation_l = {.position = saturation_[0].previous()},
		    .saturation_r = {.position = saturation_[1].previous()},
		    .seeded = seeded_,
		    .detector_l = {.z = detector_[0].memory()},
		    .detector_r = {.z = detector_[1].memory()},
		    .attack_ms = attack_ms_,
		    .release_ms = release_ms_,
		    .base_gain = base_gain_,
		    .threshold_knob = threshold_knob_,
		    .dry = dry_,
		    .wet = wet_,
		    .gain_reduction = gain_reduction_,
		};
	}

private:
	static constexpr uint32_t kSaturation = 3;

	void setAttack(q31 attack) {
		attack_ms_ = detail::compressor_attack_ms(attack);
		attack_rate_ = (-1000.0f / kSampleRate) / attack_ms_;
	}

	void setRelease(q31 release) {
		release_ms_ = detail::compressor_release_ms(release);
		release_rate_ = (-1000.0f / kSampleRate) / release_ms_;
	}

	void setRatio(q31 ratio) { fraction_ = detail::ratio_fraction(ratio); }

	void setSidechain(q31 sidechain_hpf) {
		const float fc = detail::compressor_sidechain_hz(sidechain_hpf) / static_cast<float>(kSampleRate);
		const float wc = fc / (1 + fc);
		detector_moveability_ = static_cast<q31>(wc * static_cast<float>(kOneQ31));
	}

	void setBlend(q31 blend) {
		dry_ = kOneQ31 - blend;
		wet_ = blend;
	}

	float updateMakeup(float num_samples, q31 final_volume) {
		const float song_volume = logf(final_volume + 1e-10);

		const float threshold = song_volume * threshold_;
		const float last = makeup_;
		makeup_ = std::max<float>((song_volume - threshold - 1) * fraction_, 0);
		makeup_ = runEnvelope(last, makeup_, num_samples);
		return threshold;
	}

	[[nodiscard]] float runEnvelope(float current, float desired, float num_samples) const {
		if (desired > current) {
			return desired + std::exp(attack_rate_ * num_samples) * (current - desired);
		}
		return desired + std::exp(release_rate_ * num_samples) * (current - desired);
	}

	DELUGE_REFERENCE_LEGACY_ARITHMETIC float measureLevel(std::span<const StereoSample> block) {
		q31 sum = 0;
		const float last_mean = mean_;

		for (const StereoSample sample : block) {
			const q31 l = sample.l - detector_[0].lowPass(sample.l, detector_moveability_);
			const q31 r = sample.r - detector_[1].lowPass(sample.r, detector_moveability_);
			const q31 s = std::max(std::abs(l), std::abs(r));
			sum += multiply_32x32_rshift32(s, s);
		}

		const auto ns = static_cast<float>(block.size() * 2);
		mean_ = (static_cast<float>(sum) / kOneQ31f) / ns;
		mean_ = (mean_ * ns + last_mean) / (1 + ns);
		const float rms = static_cast<float>(kOneQ31) * std::sqrt(mean_);

		return std::log(std::max(rms, 1.0f));
	}

	CompressorKnobs configured_{};

	float attack_rate_ = (-1000.0f / kSampleRate);
	float release_rate_ = (-1000.0f / kSampleRate);
	float fraction_ = 0.5;
	float makeup_ = 0;
	float threshold_ = 1;
	q31 detector_moveability_ = kOneQ15;

	float reduction_envelope_ = 0;
	q31 current_volume_l_ = 0;
	q31 current_volume_r_ = 0;
	float log_rms_ = 0;
	float mean_ = 0;
	std::array<ReferenceAntialiasedTanh, 2> saturation_{ReferenceAntialiasedTanh{kSaturation},
	                                                    ReferenceAntialiasedTanh{kSaturation}};
	bool seeded_ = false;

	std::array<ReferenceOnePole, 2> detector_{};

	float attack_ms_ = 0;
	float release_ms_ = 0;

	float base_gain_ = 1.35f;

	q31 threshold_knob_ = 0;
	q31 dry_ = 0;
	q31 wet_ = kOneQ31;
	uint8_t gain_reduction_ = 0;
};

static_assert(std::is_trivially_copyable_v<ReferenceRmsFeedbackCompressor>);

/// @brief The firmware compressor (`RmsFeedbackCompressor`) with only its two level defects fixed: the target a rewrite
/// matches bit for bit.
///
/// The block's sum of squares is added in 64 bits, so it never wraps, and each detector output's magnitude is taken
/// unsigned, so INT32_MIN measures 2^31. Wherever neither defect fires it renders exactly as the verbatim reference.
class FixedReferenceRmsFeedbackCompressor {
public:
	/// @brief A compressor at the default knobs, fully wet, with no gain reduction.
	FixedReferenceRmsFeedbackCompressor() {
		setAttack(configured_.attack);
		setRelease(configured_.release);
		set_threshold(configured_.threshold);
		setRatio(configured_.ratio);
		setSidechain(configured_.sidechain_hpf);
		setBlend(configured_.blend);
	}

	/// @brief Takes @p knobs, recomputing only when they changed.
	void configure(const CompressorKnobs& knobs) {
		if (knobs == configured_) {
			return;
		}
		setAttack(knobs.attack);
		setRelease(knobs.release);
		if (knobs.threshold != configured_.threshold) {
			set_threshold(knobs.threshold);
		}
		setRatio(knobs.ratio);
		setSidechain(knobs.sidechain_hpf);
		setBlend(knobs.blend);
		configured_ = knobs;
	}

	/// @brief The threshold this block renders with, from a knob position (Q31 of the knob's travel).
	void set_threshold(q31 threshold) {
		threshold_knob_ = threshold;
		threshold_ = 1 - 0.8f * (static_cast<float>(threshold_knob_) / kOneQ31f);
	}

	/// @return The threshold this block renders with, as a knob position (Q31 of the knob's travel).
	[[nodiscard]] q31 threshold() const { return threshold_knob_; }

	/// @brief Makeup gain in nepers at no reduction.
	void set_base_gain(float base_gain) { base_gain_ = base_gain; }

	/// @brief No gain reduction at the start of the next block, and the saturation seeded again.
	void reset() {
		reduction_envelope_ = 0;
		makeup_ = 0;
		mean_ = 0;
		seeded_ = false;
	}

	/// @brief Compresses @p block in place.
	/// @param volume_l, volume_r The channels' linear gain, 4.27 signed.
	/// @param final_volume The peak-to-peak volume scale, 3.29.
	/// @param scratch Holds the block as it came in, for the blend; read only when the blend is below fully wet.
	void render(std::span<StereoSample> block, q31 volume_l, q31 volume_r, q31 final_volume,
	            CompressorScratch scratch) {
		if (wet_ != kOneQ31) {
			std::ranges::copy(block, scratch.begin());
		}

		if (!seeded_) {
			AntialiasedTanh::seed(kSaturator, saturation_l_, block[0].l);
			AntialiasedTanh::seed(kSaturator, saturation_r_, block[0].r);
			seeded_ = true;
		}
		const float threshold = updateMakeup(static_cast<float>(block.size()), final_volume);

		const float over = std::max<float>(0, (log_rms_ - threshold));

		reduction_envelope_ = runEnvelope(reduction_envelope_, over, static_cast<float>(block.size()));

		const float reduction = -reduction_envelope_ * fraction_;

		const float gain_nepers = base_gain_ + makeup_ + reduction;

		float gain = std::exp(gain_nepers);
		gain = std::min<float>(gain, 31);

		const float final_volume_l = gain * static_cast<float>(volume_l >> 9);
		const float final_volume_r = gain * static_cast<float>(volume_r >> 9);

		const q31 increment_l =
		    static_cast<int32_t>((final_volume_l - (current_volume_l_ >> 8)) / static_cast<float>(block.size())) << 8;
		const q31 increment_r =
		    static_cast<int32_t>((final_volume_r - (current_volume_r_ >> 8)) / static_cast<float>(block.size())) << 8;

		for (std::size_t i = 0; i < block.size(); i++) {
			StereoSample& sample = block[i];
			current_volume_l_ += increment_l;
			current_volume_r_ += increment_r;

			sample.l = multiply_32x32_rshift32(sample.l, current_volume_l_) << 4;
			sample.l = AntialiasedTanh::step(kSaturator, EmptyBlock{}, saturation_l_, sample.l);

			sample.r = multiply_32x32_rshift32(sample.r, current_volume_r_) << 4;
			sample.r = AntialiasedTanh::step(kSaturator, EmptyBlock{}, saturation_r_, sample.r);

			if (wet_ != kOneQ31) {
				sample.l = multiply_32x32_rshift32(sample.l, wet_);
				sample.l = multiply_accumulate_32x32_rshift32_rounded(sample.l, scratch[i].l, dry_);
				sample.l <<= 1;
				sample.r = multiply_32x32_rshift32(sample.r, wet_);
				sample.r = multiply_accumulate_32x32_rshift32_rounded(sample.r, scratch[i].r, dry_);
				sample.r <<= 1;
			}
		}

		gain_reduction_ = static_cast<uint8_t>(std::clamp<int32_t>(static_cast<int32_t>(-(reduction) * 4 * 4), 0, 127));
		log_rms_ = measureLevel(block);
	}

	/// @brief render at neutral channel gain, with @p final_volume (3.29) scaled.
	void render_volume_neutral(std::span<StereoSample> block, q31 final_volume, CompressorScratch scratch) {
		render(block, 1 << 27, 1 << 27, final_volume >> 3, scratch);
	}

	/// @return The last block's gain reduction for the LEDs, 0 to 127.
	[[nodiscard]] uint8_t gain_reduction() const { return gain_reduction_; }

	/// @brief The compressor between blocks, read-only.
	[[nodiscard]] View view() const {
		return {
		    .configured = configured_,
		    .attack_rate = attack_rate_,
		    .release_rate = release_rate_,
		    .fraction = fraction_,
		    .makeup = makeup_,
		    .threshold = threshold_,
		    .detector_moveability = detector_moveability_,
		    .reduction_envelope = reduction_envelope_,
		    .current_volume_l = current_volume_l_,
		    .current_volume_r = current_volume_r_,
		    .log_rms = log_rms_,
		    .mean = mean_,
		    .saturation_drive = kSaturator.drive,
		    .saturation_l = saturation_l_,
		    .saturation_r = saturation_r_,
		    .seeded = seeded_,
		    .detector_l = detector_l_,
		    .detector_r = detector_r_,
		    .attack_ms = attack_ms_,
		    .release_ms = release_ms_,
		    .base_gain = base_gain_,
		    .threshold_knob = threshold_knob_,
		    .dry = dry_,
		    .wet = wet_,
		    .gain_reduction = gain_reduction_,
		};
	}

private:
	using AntialiasedTanh = deluge::dsp::fixed::AntialiasedTanh;
	using OnePole = deluge::dsp::fixed::OnePole;
	using EmptyBlock = deluge::dsp::EmptyBlock;

	static constexpr uint32_t kSaturation = 3;
	static constexpr AntialiasedTanh::Coefficients kSaturator = AntialiasedTanh::coefficients({.drive = kSaturation});

	/// @return |@p x| as an unsigned value: INT32_MIN gives 2^31.
	[[nodiscard]] static uint32_t magnitude(q31 x) {
		return x < 0 ? 0u - static_cast<uint32_t>(x) : static_cast<uint32_t>(x);
	}

	void setAttack(q31 attack) {
		attack_ms_ = detail::compressor_attack_ms(attack);
		attack_rate_ = (-1000.0f / kSampleRate) / attack_ms_;
	}

	void setRelease(q31 release) {
		release_ms_ = detail::compressor_release_ms(release);
		release_rate_ = (-1000.0f / kSampleRate) / release_ms_;
	}

	void setRatio(q31 ratio) { fraction_ = detail::ratio_fraction(ratio); }

	void setSidechain(q31 sidechain_hpf) {
		const float fc = detail::compressor_sidechain_hz(sidechain_hpf) / static_cast<float>(kSampleRate);
		const float wc = fc / (1 + fc);
		detector_moveability_ = static_cast<q31>(wc * static_cast<float>(kOneQ31));
	}

	void setBlend(q31 blend) {
		dry_ = kOneQ31 - blend;
		wet_ = blend;
	}

	float updateMakeup(float num_samples, q31 final_volume) {
		const float song_volume = logf(final_volume + 1e-10);

		const float threshold = song_volume * threshold_;
		const float last = makeup_;
		makeup_ = std::max<float>((song_volume - threshold - 1) * fraction_, 0);
		makeup_ = runEnvelope(last, makeup_, num_samples);
		return threshold;
	}

	[[nodiscard]] float runEnvelope(float current, float desired, float num_samples) const {
		if (desired > current) {
			return desired + std::exp(attack_rate_ * num_samples) * (current - desired);
		}
		return desired + std::exp(release_rate_ * num_samples) * (current - desired);
	}

	float measureLevel(std::span<const StereoSample> block) {
		const float last_mean = mean_;
		const OnePole::Coefficients detector{.m = detector_moveability_, .mode = OnePole::Mode::LOW_PASS};

		int64_t sum = 0;
		for (const StereoSample sample : block) {
			const q31 l = deluge::dsp::wrapping_sub(
			    sample.l, OnePole::step<OnePole::Mode::LOW_PASS>(detector, EmptyBlock{}, detector_l_, sample.l));
			const q31 r = deluge::dsp::wrapping_sub(
			    sample.r, OnePole::step<OnePole::Mode::LOW_PASS>(detector, EmptyBlock{}, detector_r_, sample.r));
			const uint32_t s = std::max(magnitude(l), magnitude(r));
			sum += static_cast<int64_t>((static_cast<uint64_t>(s) * s) >> 32);
		}

		const auto ns = static_cast<float>(block.size() * 2);
		mean_ = (static_cast<float>(sum) / kOneQ31f) / ns;
		mean_ = (mean_ * ns + last_mean) / (1 + ns);
		const float rms = static_cast<float>(kOneQ31) * std::sqrt(mean_);

		return std::log(std::max(rms, 1.0f));
	}

	CompressorKnobs configured_{};

	float attack_rate_ = (-1000.0f / kSampleRate);
	float release_rate_ = (-1000.0f / kSampleRate);
	float fraction_ = 0.5;
	float makeup_ = 0;
	float threshold_ = 1;
	q31 detector_moveability_ = kOneQ15;

	float reduction_envelope_ = 0;
	q31 current_volume_l_ = 0;
	q31 current_volume_r_ = 0;
	float log_rms_ = 0;
	float mean_ = 0;
	AntialiasedTanh::State saturation_l_{};
	AntialiasedTanh::State saturation_r_{};
	bool seeded_ = false;

	OnePole::State detector_l_{};
	OnePole::State detector_r_{};

	float attack_ms_ = 0;
	float release_ms_ = 0;

	float base_gain_ = 1.35f;

	q31 threshold_knob_ = 0;
	q31 dry_ = 0;
	q31 wet_ = kOneQ31;
	uint8_t gain_reduction_ = 0;
};

static_assert(std::is_trivially_copyable_v<FixedReferenceRmsFeedbackCompressor>);

/// @brief Folds every value of @p v into @p d, in the legacy compressor's member order, floats by their bits.
inline void digest(const View& v, Digest& d) {
	d.add(v.configured.attack);
	d.add(v.configured.release);
	d.add(v.configured.threshold);
	d.add(v.configured.ratio);
	d.add(v.configured.sidechain_hpf);
	d.add(v.configured.blend);
	d.add(v.attack_rate);
	d.add(v.release_rate);
	d.add(v.fraction);
	d.add(v.makeup);
	d.add(v.threshold);
	d.add(v.detector_moveability);
	d.add(v.reduction_envelope);
	d.add(v.current_volume_l);
	d.add(v.current_volume_r);
	d.add(v.log_rms);
	d.add(v.mean);
	d.add(v.saturation_drive);
	d.add(v.saturation_l.position);
	d.add(v.saturation_drive);
	d.add(v.saturation_r.position);
	d.add(v.seeded);
	d.add(v.detector_l.z);
	d.add(v.detector_r.z);
	d.add(v.attack_ms);
	d.add(v.release_ms);
	d.add(v.base_gain);
	d.add(v.threshold_knob);
	d.add(v.dry);
	d.add(v.wet);
	d.add(v.gain_reduction);
}

/// @return The digest of @p v alone.
[[nodiscard]] inline uint64_t digest(const View& v) {
	Digest d;
	digest(v, d);
	return d.value;
}

} // namespace compressor_reference
