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
/// @brief Verbatim test-local copies of the legacy filters: the legacy one-pole section and antialiased tanh they
/// build on (the compressor's reference, reference_compressor.h, builds on them too), the dry/wet fade, the low-pass
/// ladder, the high-pass ladder, the state-variable filter and the filter set.
///
/// Each copy keeps the legacy arithmetic line for line, plain signed `+` and `-` included, and the ladders draw from
/// the synth-wide generator through `deluge::synth_noise.next()`, so a kernel checked against them is checked against
/// the legacy output and noise stream as recorded. Each class keeps the legacy member layout. The copies add only const
/// accessors and `digest`, which folds every member of the state into a @ref Digest.
///
/// The copies whose plain sums overflow at full scale, as the legacy code's do (the target's `add` and `sub` wrap), are
/// marked @ref DELUGE_REFERENCE_LEGACY_ARITHMETIC: the UBSan tree leaves their overflow unchecked, so a spec running a
/// kernel against them there checks the kernel's arithmetic and not the copy's.
///
/// Only what the kernels replace is copied. The shared pieces they keep (the value types and modes in
/// filter_modes.h, `tanh`, `instant_tan`, `quick_log`, the table interpolators, `dry_wet_blend`)
/// come from the firmware.

#pragma once

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/curves/instant_tan.h"
#include "dsp-next/fixed/curves/quick_log.h"
#include "dsp-next/fixed/filter/filter_modes.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "dsp-next/fixed/mix/dry_wet.h"
#include "dsp-next/fixed/shaping/tanh.h"
#include "dsp-next/fixed/shaping/tanh_table.h"
#include "util/fixedpoint.h"
#include "util/synth_noise.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>
#include <tuple>
#include <type_traits>

/// @brief Marks a reference function whose legacy signed sums may overflow at full scale: UBSan's overflow check is
/// off in its body (and only there), wherever it is inlined.
#define DELUGE_REFERENCE_LEGACY_ARITHMETIC __attribute__((no_sanitize("signed-integer-overflow")))

namespace filters_reference {

using deluge::dsp::q31;
using deluge::dsp::StereoSample;
using deluge::dsp::fixed::CurvedFrequency;
using deluge::dsp::fixed::FilterConfig;
using deluge::dsp::fixed::FilterRoute;
using deluge::dsp::fixed::HighPassSlot;
using deluge::dsp::fixed::kBilinearTableInterpolator;
using deluge::dsp::fixed::kLinearTableInterpolator;
using deluge::dsp::fixed::LowPassLadderMode;
using deluge::dsp::fixed::LowPassSlot;
using deluge::dsp::fixed::SvfMode;

/// @brief A 64-bit FNV-1a over 32-bit words: what the records pin.
struct Digest {
	uint64_t value = 14695981039346656037ULL; ///< The digest so far.

	/// @brief Folds in one 32-bit word.
	void add(uint32_t word) { value = (value ^ word) * 1099511628211ULL; }
	/// @brief Folds in a signed word (a q31 sample or coefficient) by its bits.
	void add(int32_t word) { add(static_cast<uint32_t>(word)); }
	/// @brief Folds in a byte, widened to a word.
	void add(uint8_t word) { add(static_cast<uint32_t>(word)); }
	/// @brief Folds in a flag as 0 or 1.
	void add(bool word) { add(static_cast<uint32_t>(word)); }
	/// @brief Folds in a float by its bits, so -0 and NaN payloads count.
	void add(float word) { add(std::bit_cast<uint32_t>(word)); }
	/// @brief Folds in an enumerator by its underlying value.
	template <typename E>
	requires std::is_enum_v<E>
	void add(E word) {
		add(static_cast<uint32_t>(word));
	}
	/// @brief Folds in each sample, in order.
	void add(std::span<const q31> samples) {
		for (const q31 s : samples) {
			add(s);
		}
	}
	/// @brief Folds in each frame, left then right.
	void add(std::span<const StereoSample> samples) {
		for (const StereoSample s : samples) {
			add(s.l);
			add(s.r);
		}
	}
};

/// @brief The cutoff warp the filters configure from: `curve_frequency` (filter_modes.h), copied.
[[nodiscard]] inline CurvedFrequency curve_frequency(q31 frequency) {
	/// One at Q27 (where half of a Q28 tan lands).
	constexpr int32_t kOneQ27 = 134217728;
	/// One at Q58: divided by a Q27 value, it gives the reciprocal at Q31.
	constexpr double kOneQ58 = 288230376151711744.0;
	const q31 tanned = deluge::dsp::fixed::instant_tan(lshift_and_saturate<5>(frequency));
	const auto divide_by_one_plus_tanned = static_cast<q31>(kOneQ58 / static_cast<double>(kOneQ27 + (tanned >> 1)));
	return {
	    .tanned = tanned,
	    .divide_by_one_plus_tanned = divide_by_one_plus_tanned,
	    .fc = multiply_32x32_rshift32_rounded(tanned, divide_by_one_plus_tanned) << 4,
	};
}

/// @brief The legacy one-pole section, copied: its sums are plain signed `+`.
class ReferenceOnePole {
public:
	/// @brief One sample through the low-pass: returns the section's output, Q31, and advances its memory.
	/// @param moveability tan(f)/(1+tan(f)), Q31.
	[[nodiscard, gnu::always_inline]] DELUGE_REFERENCE_LEGACY_ARITHMETIC q31 lowPass(q31 input, q31 moveability) {
		const q31 step = multiply_32x32_rshift32_rounded(input - memory_, moveability) << 1;
		const q31 output = step + memory_;
		memory_ = output + step;
		return output;
	}

	/// @brief One sample through the all-pass built on the same section: twice the low-pass output less the
	/// input, Q31.
	/// @param moveability tan(f)/(1+tan(f)), Q31.
	[[nodiscard, gnu::always_inline]] DELUGE_REFERENCE_LEGACY_ARITHMETIC q31 allPass(q31 input, q31 moveability) {
		const q31 step = multiply_32x32_rshift32_rounded(input - memory_, moveability) << 1;
		const q31 output = step + memory_;
		memory_ = step + output;
		return (output * 2) - input;
	}

	/// @brief The memory scaled for a ladder's feedback: memory × @p feedback_amount (Q31) × 2.
	[[nodiscard, gnu::always_inline]] q31 feedbackOutput(q31 feedback_amount) const {
		return multiply_32x32_rshift32_rounded(memory_, feedback_amount) << 2;
	}

	/// @brief The memory scaled for a ladder's feedback, at half scale: memory × @p feedback_amount (Q31) ÷ 2.
	[[nodiscard, gnu::always_inline]] q31 feedbackOutputUnshifted(q31 feedback_amount) const {
		return multiply_32x32_rshift32_rounded(memory_, feedback_amount);
	}

	/// @brief Silences the memory.
	void reset() { memory_ = 0; }

	/// @brief The memory, Q31.
	[[nodiscard]] q31 memory() const { return memory_; }

private:
	q31 memory_{0};
};

/// @brief The legacy antialiased tanh, copied: each output interpolated between this sample's and the previous one's
/// position on the curve.
class ReferenceAntialiasedTanh {
public:
	/// The previous position of a silent signal: the middle of the curve.
	static constexpr uint32_t kSilent = deluge::dsp::fixed::kTanhCentre;

	/// @param drive How hard the input is driven into the curve: it is scaled by 2^drive (saturating) first.
	/// @param previous The position the first sample is interpolated against.
	constexpr explicit ReferenceAntialiasedTanh(uint32_t drive = 0, uint32_t previous = kSilent)
	    : drive_{drive}, previous_{previous} {}

	/// @brief Drives the input by 2^@p drive from the next sample on.
	void setDrive(uint32_t drive) { drive_ = drive; }

	/// @brief One sample (Q31) through the curve, shifted down by drive + 1.
	[[nodiscard, gnu::always_inline]] q31 render(q31 sample) {
		const uint32_t position =
		    static_cast<uint32_t>(lshift_and_saturate_unknown(sample, static_cast<uint8_t>(drive_))) + kSilent;
		const int32_t out =
		    kBilinearTableInterpolator.interpolate_signed<deluge::dsp::fixed::kTanhTable>(position, previous_, 32, 32)
		    >> (drive_ + 1);
		previous_ = position;
		return out;
	}

	/// @brief The position the next sample is interpolated against.
	[[nodiscard]] uint32_t previous() const { return previous_; }

	/// @brief The drive, as a shift.
	[[nodiscard]] uint32_t drive() const { return drive_; }

private:
	uint32_t drive_;
	uint32_t previous_;
};

/// @brief The scratch a filter needs while it fades: at least the block's samples (two per frame in stereo).
using FadeScratch = std::span<q31>;

/// @brief The legacy filters' dry/wet fade, copied: the dry level a float stepped by ×0.99 per sample, the fade ending
/// once it falls below 0.001 (compared as a double), the wet level derived from it at Q31.
class ReferenceDryWetFade {
public:
	/// @brief Starts fading in from dry.
	void begin() {
		dry_ = 1;
		wet_ = 0;
	}

	/// @brief Whether a fade is under way: dry not below 0.001 (the float compared as a double).
	[[nodiscard]] bool active() const { return !(dry_ < 0.001); }

	/// @brief Copies the dry @p block into @p scratch before the filter runs over it.
	void keepDry(std::span<const q31> block, FadeScratch scratch) const { std::ranges::copy(block, scratch.begin()); }

	/// @brief Blends the filtered @p block with the dry copy in @p scratch, stepping the fade once per sample (each
	/// channel's sample in an interleaved stereo block steps it).
	void blend(std::span<q31> block, FadeScratch scratch) {
		for (std::size_t i = 0; i < block.size(); i++) {
			block[i] = deluge::dsp::fixed::dry_wet_blend(scratch[i], block[i], wet_);
			step();
		}
	}

	/// @brief The state after the filter set's zeroing reset: no fade (dry 0) and the wet level 0.
	void zero() {
		dry_ = 0;
		wet_ = 0;
	}

	/// @brief The dry level.
	[[nodiscard]] float dry() const { return dry_; }
	/// @brief The wet level, Q31.
	[[nodiscard]] q31 wet() const { return wet_; }

	/// @brief Folds the dry and wet levels into @p d.
	void digest(Digest& d) const {
		d.add(dry_);
		d.add(wet_);
	}

private:
	/// @brief One sample's step: dry × 0.99 (in double, stored as float), wet = kOneQ31 × (1 - dry) in float.
	void step() {
		dry_ = static_cast<float>(dry_ * 0.99);
		wet_ = static_cast<q31>(static_cast<float>(kOneQ31) * (1.0F - dry_));
	}

	float dry_{1.0F};
	q31 wet_{kOneQ31};
};

namespace detail {

/// The drive ladder's resonance (Q30) above which it oversamples, by log cutoff.
inline constexpr std::array<int16_t, 65> kResonanceThresholdsForOversampling = {
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384, 16384,
    15500, 20735, 17000, 9000,  9000,  9000,  9000,  9000,  9000,  9000,  9000,  9000,  9000,
};

/// The oversampled drive ladder's highest resonance (Q30), on the same scale.
inline constexpr std::array<int16_t, 65> kResonanceLimits = {
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767,
    28415, 20000, 17000, 17000, 17000, 17000, 17000, 17000, 17000, 17000, 17000, 17000, 17000,
};

/// The next value of the synth-wide noise generator, Q31 across its full range.
[[gnu::always_inline]] inline q31 draw_ladder_noise() {
	return static_cast<q31>(deluge::synth_noise.next());
}

} // namespace detail

/// @brief The legacy transistor-ladder low-pass, copied: 12dB, 24dB and drive, mono or stereo, with its fade. One
/// `deluge::synth_noise.next()` draw per channel per sample, left before right; two per sample when the drive ladder
/// oversamples.
class ReferenceLowPassLadder {
public:
	/// One channel's ladder: its noise follower (Q31, a quarter of the noise's range) and its four sections.
	struct Channel {
		q31 noise_last{};
		std::array<ReferenceOnePole, 4> sections{};

		void reset() {
			for (ReferenceOnePole& section : sections) {
				section.reset();
			}
		}
	};

	/// @brief Takes one block's settings.
	/// @return @p gain (Q31) compensated for this filter's resonance and mode.
	[[nodiscard]] q31 configure(q31 gain, const FilterConfig& config, LowPassLadderMode mode) {
		mode_ = mode;
		morph_ = lshift_and_saturate<2>(config.morph);
		q31 frequency = config.frequency;

		if (mode_ == LowPassLadderMode::TRANSISTOR_24DB_DRIVE) {
			const int32_t resonance = kOneQ31 - (config.resonance << 2);
			processed_resonance_ = kOneQ31 - resonance;

			int32_t log_frequency = deluge::dsp::fixed::quick_log(static_cast<uint32_t>(frequency));
			oversample_ = false;
			log_frequency = std::min(log_frequency, int32_t{63} << 24);

			if (config.allow_oversampling && (log_frequency >> 24) > 51) {
				const int32_t threshold = kLinearTableInterpolator.interpolate_signed(
				    static_cast<uint32_t>(log_frequency), 30, detail::kResonanceThresholdsForOversampling.data(), 6);
				oversample_ = (processed_resonance_ > threshold);
			}

			if (oversample_) {
				frequency >>= 1;
				log_frequency -= 33554432;
				// Oversampling shifts the cutoff slightly; this pulls it back.
				frequency -= (multiply_32x32_rshift32_rounded(log_frequency, frequency) >> 8) * 34;
				// A cutoff near the top would make content the crude downsampling can't take.
				frequency = std::min(int32_t{39056384}, frequency);
				const int32_t limit = kLinearTableInterpolator.interpolate_signed(
				    static_cast<uint32_t>(log_frequency), 30, detail::kResonanceLimits.data(), 6);
				processed_resonance_ = std::min(processed_resonance_, limit);
			}
		}
		else {
			// The resonance falls away as the cutoff goes very low.
			constexpr int32_t kHowMuchToKeep = kOneQ31 - 33;
			constexpr int32_t kResonanceUpperLimit = 510000000; // above this it feeds back lots
			int32_t resonance = kOneQ31 - (std::min(config.resonance, kResonanceUpperLimit) << 2);
			resonance = multiply_32x32_rshift32_rounded(resonance, resonance) << 1;
			processed_resonance_ = kOneQ31 - resonance;
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

	/// @brief Filters a mono @p block in place.
	void render_mono(std::span<q31> block, FadeScratch scratch) {
		const bool fading = fade_.active();
		if (fading) {
			fade_.keepDry(block, scratch);
		}
		filterMono(block);
		if (fading) {
			fade_.blend(block, scratch);
		}
	}

	/// @brief Filters an interleaved L/R block in place.
	void render_stereo(std::span<q31> interleaved, FadeScratch scratch) {
		const bool fading = fade_.active();
		if (fading) {
			fade_.keepDry(interleaved, scratch);
		}
		filterStereo(interleaved);
		if (fading) {
			fade_.blend(interleaved, scratch);
		}
	}

	/// @brief Silences the ladders' memories (not the noise follower), and with @p fade starts the dry/wet fade.
	void reset(bool fade) {
		left_.reset();
		right_.reset();
		if (fade) {
			fade_.begin();
		}
	}

	/// @brief The filter set's zeroing reset: every member zero, the fade included (so no fade runs).
	void zero() {
		*this = ReferenceLowPassLadder{};
		fade_.zero();
	}

	/// @brief The dry/wet fade, which a filter taking over this one's slot carries on.
	[[nodiscard]] const ReferenceDryWetFade& fade() const { return fade_; }

	/// @brief Carries on @p fade, the fade of the filter this one takes over from in a slot.
	void carryFade(const ReferenceDryWetFade& fade) { fade_ = fade; }

	/// @brief The left (or mono) channel's ladder.
	[[nodiscard]] const Channel& left() const { return left_; }
	/// @brief The right channel's ladder.
	[[nodiscard]] const Channel& right() const { return right_; }
	/// @brief Whether the drive ladder oversamples this block.
	[[nodiscard]] bool oversample() const { return oversample_; }

	/// @brief Folds every member, in declaration order, into @p d.
	void digest(Digest& d) const {
		d.add(mode_);
		for (const Channel* channel : {&left_, &right_}) {
			d.add(channel->noise_last);
			for (const ReferenceOnePole& section : channel->sections) {
				d.add(section.memory());
			}
		}
		d.add(processed_resonance_);
		d.add(divide_by_total_moveability_and_processed_resonance_);
		d.add(moveability_);
		d.add(morph_);
		d.add(divide_by_one_plus_tanned_);
		for (const q31 feedback : feedbacks_) {
			d.add(feedback);
		}
		d.add(oversample_);
		fade_.digest(d);
	}

private:
	[[nodiscard, gnu::always_inline]] q31 noisyMoveability(Channel& channel) const {
		const q31 noise = detail::draw_ladder_noise() >> 2;
		const q31 distance_to_go = noise - channel.noise_last;
		channel.noise_last += distance_to_go >> 7;
		return moveability_ + multiply_32x32_rshift32(moveability_, channel.noise_last);
	}

	[[nodiscard, gnu::always_inline]] DELUGE_REFERENCE_LEGACY_ARITHMETIC q31 scaleInput(q31 input,
	                                                                                    q31 feedbacks_sum) const {
		const q31 scaled = multiply_32x32_rshift32_rounded(
		                       (input - (multiply_32x32_rshift32_rounded(feedbacks_sum, processed_resonance_) << 3)),
		                       divide_by_total_moveability_and_processed_resonance_)
		                   << 2;
		if (morph_ > 0 || processed_resonance_ > 510000000) {
			const q31 extra = 2 * multiply_32x32_rshift32(input, morph_);
			return deluge::dsp::fixed::tanh<2>(scaled + extra);
		}
		return scaled;
	}

	[[nodiscard, gnu::always_inline]] DELUGE_REFERENCE_LEGACY_ARITHMETIC q31 render12dB(q31 input,
	                                                                                    Channel& channel) const {
		const q31 noisy_m = noisyMoveability(channel);
		auto& [first, second, third, fourth] = channel.sections;
		const q31 feedbacks_sum = first.feedbackOutput(feedbacks_[0]) + second.feedbackOutput(feedbacks_[1])
		                          + third.feedbackOutput(divide_by_one_plus_tanned_);
		const q31 x = scaleInput(input, feedbacks_sum);
		return third.allPass(second.lowPass(first.lowPass(x, noisy_m), noisy_m), noisy_m) << 1;
	}

	[[nodiscard, gnu::always_inline]] DELUGE_REFERENCE_LEGACY_ARITHMETIC q31 render24dB(q31 input,
	                                                                                    Channel& channel) const {
		const q31 noisy_m = noisyMoveability(channel);
		auto& [first, second, third, fourth] = channel.sections;
		// The last section's feedback "should" be halved onto Q30, but the ladder sounds better (and reaches full
		// resonance) without.
		const q31 feedbacks_sum =
		    (first.feedbackOutputUnshifted(feedbacks_[0]) + second.feedbackOutputUnshifted(feedbacks_[1])
		     + third.feedbackOutputUnshifted(feedbacks_[2])
		     + fourth.feedbackOutputUnshifted(divide_by_one_plus_tanned_))
		    << 2;
		const q31 x = scaleInput(input, feedbacks_sum);
		return fourth.lowPass(third.lowPass(second.lowPass(first.lowPass(x, noisy_m), noisy_m), noisy_m), noisy_m) << 1;
	}

	[[nodiscard, gnu::always_inline]] DELUGE_REFERENCE_LEGACY_ARITHMETIC q31 renderDrive(q31 input,
	                                                                                     Channel& channel) const {
		const q31 noisy_m = noisyMoveability(channel);
		auto& [first, second, third, fourth] = channel.sections;
		const q31 feedbacks_sum =
		    (first.feedbackOutputUnshifted(feedbacks_[0]) + second.feedbackOutputUnshifted(feedbacks_[1])
		     + third.feedbackOutputUnshifted(feedbacks_[2])
		     + fourth.feedbackOutputUnshifted(divide_by_one_plus_tanned_))
		    << 2;
		// The feedback is saturated; the input is not, as that is where it would alias most.
		const q31 x = scaleInput(input, deluge::dsp::fixed::tanh<7>(feedbacks_sum));
		const q31 a = first.lowPass(x, noisy_m);
		const q31 b = second.lowPass(a, noisy_m);
		const q31 c = third.lowPass(b, noisy_m);
		return fourth.lowPass(c, noisy_m) << 1;
	}

	[[nodiscard, gnu::always_inline]] q31 renderDriveOutput(q31 input, Channel& channel, bool oversample) const {
		if (oversample) {
			// The input held for both halves of the sample; the first output is dropped.
			std::ignore = renderDrive(input, channel);
		}
		return deluge::dsp::fixed::tanh<4>(renderDrive(input, channel));
	}

	void filterMono(std::span<q31> block) {
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

	void filterStereo(std::span<q31> interleaved) {
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

	LowPassLadderMode mode_{};
	Channel left_{};
	Channel right_{};

	/// The resonance as fed back, Q30.
	q31 processed_resonance_{};
	/// 1 / (1 + the loop gain through the sections and resonance), Q30.
	q31 divide_by_total_moveability_and_processed_resonance_{};
	/// tan(f) / (1 + tan(f)), Q31, held at least 4317840.
	q31 moveability_{};
	/// The morph, Q31.
	q31 morph_{};
	/// 1 / (1 + tan(f)), Q31: the last section's feedback.
	q31 divide_by_one_plus_tanned_{};
	/// The first three sections' feedbacks, Q30.
	std::array<q31, 3> feedbacks_{};
	/// Whether the drive ladder runs at twice the rate this block.
	bool oversample_{};

	ReferenceDryWetFade fade_{};
};

/// @brief The legacy transistor-ladder high-pass, copied: mono or stereo, with its fade.
class ReferenceHighPassLadder {
public:
	/// One channel's ladder: its three sections and the antialiased saturator's last position on the curve.
	struct Channel {
		ReferenceOnePole high_pass_1;
		ReferenceOnePole low_pass_1;
		ReferenceOnePole high_pass_3;
		uint32_t last_working_value{};

		void reset() {
			high_pass_1.reset();
			low_pass_1.reset();
			high_pass_3.reset();
		}
	};

	/// @brief Takes one block's settings.
	/// @return @p gain (Q31) cut as the resonance rises.
	[[nodiscard]] q31 configure(q31 gain, const FilterConfig& config) {
		constexpr int32_t kExtraFeedback = 1200000000;
		morph_ = lshift_and_saturate<2>(config.morph);
		const CurvedFrequency curved = curve_frequency(config.frequency);
		fc_ = curved.fc;

		constexpr int32_t kResonanceUpperLimit = 536870911;
		int32_t resonance = kOneQ31 - (std::min(config.resonance, kResonanceUpperLimit) << 2);
		resonance = multiply_32x32_rshift32_rounded(resonance, resonance) << 1;
		processed_resonance_ = kOneQ31 - resonance;
		processed_resonance_ = std::max(processed_resonance_, int32_t{134217728}); // the least resonance
		const int32_t processed_resonance_unaltered = processed_resonance_;

		processed_resonance_ = multiply_32x32_rshift32(processed_resonance_, kExtraFeedback) << 1;
		divide_by_processed_resonance_ =
		    static_cast<q31>(2147483648.0 / static_cast<double>(processed_resonance_ >> 23));

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
		const q31 raw_resonance = std::min(config.resonance, q31{kOneQ31 >> 2}) << 2;
		q31 squared = multiply_32x32_rshift32(raw_resonance, raw_resonance) << 1;
		squared = (multiply_32x32_rshift32(squared, squared) >> 4) * 19;
		return multiply_32x32_rshift32(gain, kOneQ31 - squared) << 1;
	}

	/// @brief Filters a mono @p block in place.
	void render_mono(std::span<q31> block, FadeScratch scratch) {
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

	/// @brief Filters an interleaved L/R block in place.
	void render_stereo(std::span<q31> interleaved, FadeScratch scratch) {
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

	/// @brief Silences the ladder's memories (not the saturator's last position), and with @p fade starts the fade.
	void reset(bool fade) {
		left_.reset();
		right_.reset();
		if (fade) {
			fade_.begin();
		}
	}

	/// @brief The filter set's zeroing reset: every member zero, the fade included (so no fade runs).
	void zero() {
		*this = ReferenceHighPassLadder{};
		fade_.zero();
	}

	/// @brief The dry/wet fade, which a filter taking over this one's slot carries on.
	[[nodiscard]] const ReferenceDryWetFade& fade() const { return fade_; }

	/// @brief Carries on @p fade, the fade of the filter this one takes over from in a slot.
	void carryFade(const ReferenceDryWetFade& fade) { fade_ = fade; }

	/// @brief The left (or mono) channel's ladder.
	[[nodiscard]] const Channel& left() const { return left_; }
	/// @brief The right channel's ladder.
	[[nodiscard]] const Channel& right() const { return right_; }

	/// @brief Folds every member, in declaration order, into @p d.
	void digest(Digest& d) const {
		for (const Channel* channel : {&left_, &right_}) {
			d.add(channel->high_pass_1.memory());
			d.add(channel->low_pass_1.memory());
			d.add(channel->high_pass_3.memory());
			d.add(channel->last_working_value);
		}
		d.add(fc_);
		d.add(morph_);
		d.add(processed_resonance_);
		d.add(divide_by_processed_resonance_);
		d.add(low_pass_1_feedback_);
		d.add(high_pass_3_feedback_);
		d.add(divide_by_total_moveability_);
		fade_.digest(d);
	}

private:
	[[nodiscard, gnu::always_inline]] DELUGE_REFERENCE_LEGACY_ARITHMETIC q31 render(q31 input, Channel& channel) const {
		// The inputs are small (about 16 bits), so this nudge is too; the floor is about as low as the cutoff goes
		// without sounding broken.
		constexpr q31 kLowerLimit = -(kOneQ31 >> 8);
		const q31 temp_fc = std::max(multiply_accumulate_32x32_rshift32_rounded(fc_, input << 4, morph_), kLowerLimit);

		const q31 first_output = input - channel.high_pass_1.lowPass(input, temp_fc);
		const q31 feedbacks = channel.high_pass_3.feedbackOutput(high_pass_3_feedback_)
		                      + channel.low_pass_1.feedbackOutput(low_pass_1_feedback_);
		q31 a = multiply_32x32_rshift32_rounded(divide_by_total_moveability_, first_output + feedbacks) << (4 + 1);

		// Saturate (and antialias) only at high resonance.
		if (processed_resonance_ > 900000000) {
			ReferenceAntialiasedTanh saturator{1, channel.last_working_value};
			a = saturator.render(a);
			channel.last_working_value = saturator.previous();
		}
		else {
			// The saturator's position for this sample, kept so that switching it in interpolates from here: placed
			// at shift 2 although the saturator drives at shift 1.
			channel.last_working_value =
			    static_cast<uint32_t>(lshift_and_saturate<2>(a)) + deluge::dsp::fixed::kTanhCentre;
			if (processed_resonance_ > 750000000) {
				a = deluge::dsp::fixed::tanh<2>(a);
			}
		}

		// Only the sections' memories are wanted here: they feed back on the next sample.
		std::ignore = channel.low_pass_1.lowPass(a - channel.high_pass_3.lowPass(a, temp_fc), temp_fc);

		return multiply_32x32_rshift32_rounded(a, divide_by_processed_resonance_) << (8 - 1);
	}

	Channel left_{};
	Channel right_{};

	/// tan(f) / (1 + tan(f)), Q31.
	q31 fc_{};
	/// The morph, Q31.
	q31 morph_{};
	/// The resonance as fed back, Q30, with the extra feedback applied.
	q31 processed_resonance_{};
	/// The output normalisation: 2^31 over the processed resonance's top 8 bits.
	q31 divide_by_processed_resonance_{};
	/// The low-pass section's feedback, Q30.
	q31 low_pass_1_feedback_{};
	/// The third section's feedback, Q30.
	q31 high_pass_3_feedback_{};
	/// 1 / (1 - the loop gain), Q28.
	q31 divide_by_total_moveability_{};

	ReferenceDryWetFade fade_{};
};

/// @brief The legacy state-variable filter, copied: two passes per sample, mono or stereo, with its fade.
class ReferenceStateVariableFilter {
public:
	/// One channel's integrators, Q31.
	struct Channel {
		q31 low{};
		q31 band{};
	};

	/// @brief Takes one block's settings.
	/// @return @p gain, unchanged.
	[[nodiscard]] q31 configure(q31 gain, const FilterConfig& config, SvfMode mode) {
		q31 morph = lshift_and_saturate<2>(config.morph);
		fc_ = curve_frequency(config.frequency).fc;
		// The filter's true cutoff is the arcsine of this rate; × 1.25 brings it loosely in line with the ladders'.
		constexpr auto kQuarter = static_cast<q31>(kOneQ31 * 0.25);
		fc_ = fc_ + multiply_32x32_rshift32(fc_, kQuarter);

		band_mode_ = (mode == SvfMode::BAND);
		// The resonance (Q29 at most) times 4 spans Q31.
		q_ = (kOneQ31 - 4 * config.resonance);
		in_ = (q_ >> 1) + (kOneQ31 >> 1);
		// The damping squared matches the ladders better, and makes the input scale its square root.
		q_ = multiply_32x32_rshift32_rounded(q_, q_) << 1;

		// Each branch keeps the weights from overflowing.
		constexpr q31 kOneHalf = kOneQ31 >> 1;
		if (band_mode_) {
			if (morph > kOneHalf) {
				morph = 2 * (morph - kOneHalf);
				c_low_ = 0;
				c_band_ = kOneQ31 - morph;
				c_high_ = morph;
			}
			else {
				morph = 2 * morph;
				c_low_ = kOneQ31 - morph;
				c_band_ = morph;
				c_high_ = 0;
			}
		}
		else {
			c_low_ = kOneQ31 - morph;
			c_high_ = morph;
			c_band_ = 0;
		}
		return gain;
	}

	/// @brief Filters a mono @p block in place.
	void render_mono(std::span<q31> block, FadeScratch scratch) {
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

	/// @brief Filters an interleaved L/R block in place.
	void render_stereo(std::span<q31> interleaved, FadeScratch scratch) {
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

	/// @brief Silences the integrators, and with @p fade starts the dry/wet fade.
	void reset(bool fade) {
		left_ = {};
		right_ = {};
		if (fade) {
			fade_.begin();
		}
	}

	/// @brief The filter set's zeroing reset: every member zero, the fade included (so no fade runs).
	void zero() {
		*this = ReferenceStateVariableFilter{};
		fade_.zero();
	}

	/// @brief The dry/wet fade, which a filter taking over this one's slot carries on.
	[[nodiscard]] const ReferenceDryWetFade& fade() const { return fade_; }

	/// @brief Carries on @p fade, the fade of the filter this one takes over from in a slot.
	void carryFade(const ReferenceDryWetFade& fade) { fade_ = fade; }

	/// @brief The left (or mono) channel's integrators.
	[[nodiscard]] const Channel& left() const { return left_; }
	/// @brief The right channel's integrators.
	[[nodiscard]] const Channel& right() const { return right_; }

	/// @brief Folds every member, in declaration order, into @p d.
	void digest(Digest& d) const {
		for (const Channel* channel : {&left_, &right_}) {
			d.add(channel->low);
			d.add(channel->band);
		}
		d.add(fc_);
		d.add(q_);
		d.add(in_);
		d.add(c_low_);
		d.add(c_band_);
		d.add(c_high_);
		d.add(band_mode_);
		fade_.digest(d);
	}

private:
	[[nodiscard, gnu::always_inline]] DELUGE_REFERENCE_LEGACY_ARITHMETIC q31 render(q31 input, Channel& channel) const {
		q31 low = channel.low;
		q31 band = channel.band;

		input = multiply_32x32_rshift32(in_, input);

		low = low + 2 * multiply_32x32_rshift32(band, fc_);
		q31 high = input - low;
		high = high - 2 * multiply_32x32_rshift32(band, q_);
		band = 2 * multiply_32x32_rshift32(high, fc_) + band;
		band = deluge::dsp::fixed::tanh<3>(band); // the band feedback saturates

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

		band = deluge::dsp::fixed::tanh<3>(band);
		// × 2 undoes each multiply's halving, × 1.5 matches the ladders.
		result = 3 * result;

		channel.low = low;
		channel.band = band;
		return result;
	}

	Channel left_{};
	Channel right_{};

	/// The integrators' rate: tan(f) / (1 + tan(f)) × 1.25, Q31.
	q31 fc_{};
	/// The damping, squared, Q31.
	q31 q_{};
	/// The input's scale, Q31.
	q31 in_{};
	/// The low output's weight in the blend, Q31.
	q31 c_low_{};
	/// The band output's weight in the blend (band mode only), Q31.
	q31 c_band_{};
	/// The high output's weight in the blend, Q31.
	q31 c_high_{};
	/// Whether the band output joins the blend.
	bool band_mode_{};

	ReferenceDryWetFade fade_{};
};

/// @brief The scratch a set needs: the fades', and the parallel route's second copy of the block.
struct FilterScratch {
	FadeScratch fade;
	std::span<q31> parallel;
};

/// @brief The legacy filter set, copied: a low-pass and a high-pass slot, each holding a ladder or an SVF in a union,
/// in series either way or in parallel (the sum wrapping).
class ReferenceFilterSet {
public:
	/// @brief A set in its reset state: both filters off, nothing fading.
	ReferenceFilterSet() { reset(); }

	/// @brief Takes one block's settings.
	/// @return The gain going out of the set (Q31).
	[[nodiscard]] q31 configure(q31 gain, const FilterConfig& low, LowPassSlot low_slot, const FilterConfig& high,
	                            HighPassSlot high_slot, FilterRoute route) {
		low_slot_ = low_slot;
		high_slot_ = high_slot;
		route_ = route;

		if (low_slot.kind != LowPassSlot::Kind::OFF) {
			hold_low_pass(low_slot.kind);
			const bool family_changed = low_last_ != low_slot.kind;
			const bool from_off = low_last_ == LowPassSlot::Kind::OFF;
			if (low_slot.kind == LowPassSlot::Kind::SVF) {
				if (family_changed) {
					low_pass_.svf.reset(from_off);
				}
				gain = low_pass_.svf.configure(gain, low, low_slot.svf);
			}
			else {
				if (family_changed) {
					low_pass_.ladder.reset(from_off);
				}
				gain = low_pass_.ladder.configure(gain, low, low_slot.ladder);
			}
		}
		low_last_ = low_slot.kind;

		// At 50% resonance this keeps the level the filters had before their resonance compensation.
		gain = multiply_32x32_rshift32(gain, 1720000000) << 1;

		if (high_slot.kind != HighPassSlot::Kind::OFF) {
			hold_high_pass(high_slot.kind);
			FilterConfig config = high;
			// Changes in the resonance's low bits, made too often, rustle.
			config.resonance = (high.resonance >> 21) << 21;
			const bool mode_changed = !same_mode(high_last_, high_slot);
			const bool from_off = high_last_.kind == HighPassSlot::Kind::OFF;
			if (high_slot.kind == HighPassSlot::Kind::LADDER) {
				gain = high_pass_.ladder.configure(gain, config);
				if (mode_changed) {
					high_pass_.ladder.reset(from_off);
				}
			}
			else {
				// The morph inverted, so the high-pass SVF sweeps high → band or notch → low.
				config.morph = ((1 << 29) - 1) - high.morph;
				gain = high_pass_.svf.configure(gain, config, high_slot.svf);
				if (mode_changed) {
					high_pass_.svf.reset(from_off);
				}
			}
		}
		high_last_ = high_slot;

		return gain;
	}

	/// @brief Filters a mono @p block in place.
	void render_mono(std::span<q31> block, FilterScratch scratch) {
		switch (route_) {
		case FilterRoute::HIGH_TO_LOW:
			render_high_pass_mono(block, scratch.fade);
			render_low_pass_mono(block, scratch.fade);
			break;
		case FilterRoute::LOW_TO_HIGH:
			render_low_pass_mono(block, scratch.fade);
			render_high_pass_mono(block, scratch.fade);
			break;
		case FilterRoute::PARALLEL: {
			const std::span<q31> copy = scratch.parallel.first(block.size());
			std::ranges::copy(block, copy.begin());
			render_high_pass_mono(copy, scratch.fade);
			render_low_pass_mono(block, scratch.fade);
			add_wrapping(block, copy);
			break;
		}
		}
	}

	/// @brief Filters an interleaved L/R block in place.
	void render_stereo(std::span<q31> interleaved, FilterScratch scratch) {
		switch (route_) {
		case FilterRoute::HIGH_TO_LOW:
			render_high_pass_stereo(interleaved, scratch.fade);
			render_low_pass_stereo(interleaved, scratch.fade);
			break;
		case FilterRoute::LOW_TO_HIGH:
			render_low_pass_stereo(interleaved, scratch.fade);
			render_high_pass_stereo(interleaved, scratch.fade);
			break;
		case FilterRoute::PARALLEL: {
			const std::span<q31> copy = scratch.parallel.first(interleaved.size());
			std::ranges::copy(interleaved, copy.begin());
			render_high_pass_stereo(copy, scratch.fade);
			render_low_pass_stereo(interleaved, scratch.fade);
			add_wrapping(interleaved, copy);
			break;
		}
		}
	}

	/// @brief Back to a new set's state: both filters off and every filter's state zero, the fades included; both
	/// slots read as holding a ladder.
	void reset() {
		std::construct_at(&low_pass_.ladder)->zero();
		std::construct_at(&high_pass_.ladder)->zero();
		low_held_ = LowPassSlot::Kind::LADDER;
		high_held_ = HighPassSlot::Kind::LADDER;
		low_slot_ = {};
		high_slot_ = {};
		low_last_ = LowPassSlot::Kind::LADDER;
		high_last_ = {.kind = HighPassSlot::Kind::LADDER};
		route_ = FilterRoute::HIGH_TO_LOW;
	}

	/// @brief Whether either filter is on.
	[[nodiscard]] bool isOn() const { return is_low_pass_on() || is_high_pass_on(); }
	/// @brief Whether the low-pass is on.
	[[nodiscard]] bool is_low_pass_on() const { return low_slot_.kind != LowPassSlot::Kind::OFF; }
	/// @brief Whether the high-pass is on.
	[[nodiscard]] bool is_high_pass_on() const { return high_slot_.kind != HighPassSlot::Kind::OFF; }

	/// @brief The low-pass slot's fade: that of the filter the slot holds.
	[[nodiscard]] const ReferenceDryWetFade& lowFade() const {
		return low_held_ == LowPassSlot::Kind::SVF ? low_pass_.svf.fade() : low_pass_.ladder.fade();
	}
	/// @brief The high-pass slot's fade: that of the filter the slot holds.
	[[nodiscard]] const ReferenceDryWetFade& highFade() const {
		return high_held_ == HighPassSlot::Kind::SVF ? high_pass_.svf.fade() : high_pass_.ladder.fade();
	}

	/// @brief Folds the slots' bookkeeping and the held filters' state into @p d: only the union member each slot
	/// holds.
	void digest(Digest& d) const {
		d.add(low_held_);
		if (low_held_ == LowPassSlot::Kind::SVF) {
			low_pass_.svf.digest(d);
		}
		else {
			low_pass_.ladder.digest(d);
		}
		d.add(high_held_);
		if (high_held_ == HighPassSlot::Kind::SVF) {
			high_pass_.svf.digest(d);
		}
		else {
			high_pass_.ladder.digest(d);
		}
		d.add(low_slot_.kind);
		d.add(low_slot_.ladder);
		d.add(low_slot_.svf);
		d.add(high_slot_.kind);
		d.add(high_slot_.svf);
		d.add(low_last_);
		d.add(high_last_.kind);
		d.add(high_last_.svf);
		d.add(route_);
	}

private:
	union LowPass {
		ReferenceLowPassLadder ladder;
		ReferenceStateVariableFilter svf;
		LowPass() : ladder{} {}
	};
	union HighPass {
		ReferenceHighPassLadder ladder;
		ReferenceStateVariableFilter svf;
		HighPass() : ladder{} {}
	};

	[[nodiscard]] static bool same_mode(HighPassSlot a, HighPassSlot b) {
		return a.kind == b.kind && (a.kind != HighPassSlot::Kind::SVF || a.svf == b.svf);
	}

	template <typename Filter>
	static void take_over(Filter& storage, const ReferenceDryWetFade& fade) {
		Filter* filter = std::construct_at(&storage);
		filter->zero();
		filter->carryFade(fade);
	}

	static void add_wrapping(std::span<q31> block, std::span<const q31> other) {
		for (std::size_t i = 0; i < block.size(); i++) {
			block[i] = deluge::dsp::wrapping_add(block[i], other[i]);
		}
	}

	void hold_low_pass(LowPassSlot::Kind kind) {
		if (kind == low_held_) {
			return;
		}
		if (kind == LowPassSlot::Kind::SVF) {
			take_over(low_pass_.svf, ReferenceDryWetFade{low_pass_.ladder.fade()});
		}
		else {
			take_over(low_pass_.ladder, ReferenceDryWetFade{low_pass_.svf.fade()});
		}
		low_held_ = kind;
	}

	void hold_high_pass(HighPassSlot::Kind kind) {
		if (kind == high_held_) {
			return;
		}
		if (kind == HighPassSlot::Kind::SVF) {
			take_over(high_pass_.svf, ReferenceDryWetFade{high_pass_.ladder.fade()});
		}
		else {
			take_over(high_pass_.ladder, ReferenceDryWetFade{high_pass_.svf.fade()});
		}
		high_held_ = kind;
	}

	void render_low_pass_mono(std::span<q31> block, FadeScratch scratch) {
		switch (low_slot_.kind) {
		case LowPassSlot::Kind::LADDER:
			low_pass_.ladder.render_mono(block, scratch);
			break;
		case LowPassSlot::Kind::SVF:
			low_pass_.svf.render_mono(block, scratch);
			break;
		case LowPassSlot::Kind::OFF:
			break;
		}
	}

	void render_high_pass_mono(std::span<q31> block, FadeScratch scratch) {
		switch (high_slot_.kind) {
		case HighPassSlot::Kind::LADDER:
			high_pass_.ladder.render_mono(block, scratch);
			break;
		case HighPassSlot::Kind::SVF:
			high_pass_.svf.render_mono(block, scratch);
			break;
		case HighPassSlot::Kind::OFF:
			break;
		}
	}

	void render_low_pass_stereo(std::span<q31> interleaved, FadeScratch scratch) {
		switch (low_slot_.kind) {
		case LowPassSlot::Kind::LADDER:
			low_pass_.ladder.render_stereo(interleaved, scratch);
			break;
		case LowPassSlot::Kind::SVF:
			low_pass_.svf.render_stereo(interleaved, scratch);
			break;
		case LowPassSlot::Kind::OFF:
			break;
		}
	}

	void render_high_pass_stereo(std::span<q31> interleaved, FadeScratch scratch) {
		switch (high_slot_.kind) {
		case HighPassSlot::Kind::LADDER:
			high_pass_.ladder.render_stereo(interleaved, scratch);
			break;
		case HighPassSlot::Kind::SVF:
			high_pass_.svf.render_stereo(interleaved, scratch);
			break;
		case HighPassSlot::Kind::OFF:
			break;
		}
	}

	LowPass low_pass_;
	HighPass high_pass_;

	LowPassSlot::Kind low_held_{LowPassSlot::Kind::LADDER};
	HighPassSlot::Kind high_held_{HighPassSlot::Kind::LADDER};
	LowPassSlot low_slot_{};
	HighPassSlot high_slot_{};
	LowPassSlot::Kind low_last_{LowPassSlot::Kind::LADDER};
	HighPassSlot high_last_{.kind = HighPassSlot::Kind::LADDER};
	FilterRoute route_{FilterRoute::HIGH_TO_LOW};
};

static_assert(std::is_trivially_copyable_v<ReferenceLowPassLadder>);
static_assert(std::is_trivially_copyable_v<ReferenceHighPassLadder>);
static_assert(std::is_trivially_copyable_v<ReferenceStateVariableFilter>);
static_assert(std::is_trivially_copyable_v<ReferenceFilterSet>);

} // namespace filters_reference
