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

#pragma once

#include "definitions_cxx.hpp" // kSampleRate
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/shaping/antialiased_tanh.h"
#include "util/fixedpoint.h"

#include <cstdint>
#include <span>

namespace deluge::dsp::fixed {

/// @brief The compressor's knob positions, each 0 to 2^31 (Q31 of the knob's travel).
struct CompressorKnobs {
	q31 attack{5 << 24};  ///< about 0.5 to 70 ms for a 3 dB change
	q31 release{5 << 24}; ///< about 50 to 400 ms for a 3 dB change
	q31 threshold{0};     ///< the threshold as a fraction of the volume's level in nepers: 0 is 1, 2^31 is 0.2
	q31 ratio{64 << 24};  ///< 0 is 2:1, 2^31 is 256:1
	q31 sidechain_hpf{0}; ///< the level detector's high-pass, about 0 to 100 Hz
	q31 blend{ONE_Q31};   ///< the wet level, ONE_Q31 fully wet
	bool operator==(const CompressorKnobs&) const = default;
};

/// @return The attack time in ms for a 3 dB change at knob @p attack.
[[nodiscard]] float compressor_attack_ms(q31 attack);
/// @return The release time in ms for a 3 dB change at knob @p release.
[[nodiscard]] float compressor_release_ms(q31 release);
/// @return The ratio at knob @p ratio.
[[nodiscard]] float compressor_ratio(q31 ratio);
/// @return The level detector's high-pass cutoff in Hz at knob @p sidechain_hpf.
[[nodiscard]] float compressor_sidechain_hz(q31 sidechain_hpf);
/// @return The wet level @p blend (Q31, ONE_Q31 fully wet) gives, as a whole percentage.
[[nodiscard]] constexpr int32_t compressor_blend_percent(q31 blend) {
	return blend > (127 << 24) ? 100 : 100 * (blend >> 24) >> 7;
}

/// @brief The scratch a compressor needs to blend: the caller's, distinct from the block and at least as long.
using CompressorScratch = std::span<StereoSample>;

/// @brief A feedback compressor: the gain for each block follows the RMS level, in log space, of the block before it.
///
/// Levels and gains in the envelope are in nepers (natural-log units). The compressed signal is saturated through an
/// antialiased tanh at drive 3, which keeps it within ±2^26: fully wet, that is the output's range. Below fully wet,
/// the block as it came in is mixed back at the dry level (1 - blend), so the output's range is ±2^26 at the blend
/// plus the input's own range at the dry level.
class RmsFeedbackCompressor {
public:
	/// @brief A compressor at the default knobs (@ref CompressorKnobs), fully wet, with no gain reduction.
	RmsFeedbackCompressor();

	/// @brief Takes @p knobs, recomputing the time constants, ratio, detector filter and blend only when they changed.
	/// The threshold too; an owner whose threshold is a param overrides it with setThreshold every block.
	void configure(const CompressorKnobs& knobs);

	/// @brief The threshold this block renders with, from a knob position (Q31 of the knob's travel).
	void setThreshold(q31 threshold);

	/// @return The threshold this block renders with, as a knob position (Q31 of the knob's travel).
	[[nodiscard]] q31 threshold() const { return threshold_knob_; }

	/// @brief Makeup gain in nepers at no reduction: 1.35 by default, 0.85 for the song's master.
	///
	/// The reduction is never positive and the makeup envelope never exceeds 2.08, so 1.35 is the most that keeps the
	/// gain under 31.
	void setBaseGain(float base_gain) { base_gain_ = base_gain; }

	/// @brief No gain reduction at the start of the next block. The channel gains, the detector's filters and its
	/// last level carry on.
	void reset();

	/// @brief Compresses @p block in place.
	/// @param volume_l, volume_r The channels' linear gain, 4.27 signed.
	/// @param final_volume The peak-to-peak volume scale, 3.29.
	/// @param scratch Holds the block as it came in, for the blend; read only when the blend is below fully wet.
	void render(std::span<StereoSample> block, q31 volume_l, q31 volume_r, q31 final_volume, CompressorScratch scratch);

	/// @brief render at neutral channel gain, with @p final_volume (3.29) scaled so threshold zero leaves the level
	/// unchanged: for an owner whose volume is applied elsewhere.
	void renderVolumeNeutral(std::span<StereoSample> block, q31 final_volume, CompressorScratch scratch);

	/// @return The last block's gain reduction for the LEDs, 0 to 127 (6.2 fixed-point dB, quadrupled).
	[[nodiscard]] uint8_t gainReduction() const { return gain_reduction_; }

	/// @brief The LEDs show no gain reduction until the next block: for an owner that skips rendering.
	void clearGainReduction() { gain_reduction_ = 0; }

	/// @return The attack time in ms for a 3 dB change.
	[[nodiscard]] float attackMs() const { return attack_ms_; }

	/// @return The release time in ms for a 3 dB change.
	[[nodiscard]] float releaseMs() const { return release_ms_; }

	/// @brief The compressor between blocks, read-only: every value that carries from one block to the next.
	struct View {
		CompressorKnobs configured;          ///< The knob positions last taken.
		float attack_rate;                   ///< The attack rate, per sample (negative).
		float release_rate;                  ///< The release rate, per sample (negative).
		float fraction;                      ///< 1 - 1/ratio.
		float makeup;                        ///< The makeup envelope, nepers.
		float threshold;                     ///< The threshold as a fraction of the final volume's level.
		q31 detector_moveability;            ///< The detector's high-pass moveability, Q31.
		float reduction_envelope;            ///< The reduction envelope, nepers past the threshold.
		q31 current_volume_l;                ///< The left channel's gain, 5.26 signed.
		q31 current_volume_r;                ///< The right channel's gain, 5.26 signed.
		float log_rms;                       ///< The log (nepers) of the last block's RMS level.
		float mean;                          ///< The last block's mean square level, 1 at full scale.
		uint32_t saturation_drive;           ///< The output saturation's drive, as a power of two.
		AntialiasedTanh::State saturation_l; ///< The left saturation's position.
		AntialiasedTanh::State saturation_r; ///< The right saturation's position.
		bool seeded;                         ///< Whether the saturation is seeded since the last reset.
		OnePole::State detector_l;           ///< The left detector's one-pole memory, Q31.
		OnePole::State detector_r;           ///< The right detector's one-pole memory, Q31.
		float attack_ms;                     ///< The attack time in ms for a 3 dB change.
		float release_ms;                    ///< The release time in ms for a 3 dB change.
		float base_gain;                     ///< The makeup gain in nepers at no reduction.
		q31 threshold_knob;                  ///< The threshold, Q31 of the knob's travel.
		q31 dry;                             ///< The dry level in the blend, Q31.
		q31 wet;                             ///< The wet level in the blend, Q31.
		uint8_t gain_reduction;              ///< The last block's gain reduction for the LEDs.
	};

	/// @brief The compressor between blocks, read-only (see @ref View).
	[[nodiscard]] View view() const;

private:
	/// How hard the output is driven into the tanh: by 2^3.
	static constexpr uint32_t kSaturation = 3;
	/// The output saturation's coefficients: the antialiased tanh at drive @ref kSaturation.
	static constexpr AntialiasedTanh::Coefficients kSaturator = AntialiasedTanh::coefficients({.drive = kSaturation});

	void setAttack(q31 attack);
	void setRelease(q31 release);
	void setRatio(q31 ratio);
	void setSidechain(q31 sidechain_hpf);
	void setBlend(q31 blend);

	/// @brief Moves the makeup envelope toward the level @p final_volume (3.29) asks for over @p num_samples.
	/// @return The threshold in nepers this block.
	float updateMakeup(float num_samples, q31 final_volume);

	/// @return @p current moved toward @p desired over @p num_samples: at the attack rate when rising, the release
	/// rate otherwise.
	[[nodiscard]] float runEnvelope(float current, float desired, float num_samples) const;

	/// @return The log (nepers) of the RMS level of @p block, high-passed, smoothed against the last block's.
	float measureLevel(std::span<const StereoSample> block);

	/// The knob positions last taken: when to recompute.
	CompressorKnobs configured_{};

	/// The attack rate, per sample (negative).
	float attack_rate_ = (-1000.0f / kSampleRate);
	/// The release rate, per sample (negative).
	float release_rate_ = (-1000.0f / kSampleRate);
	/// 1 - 1/ratio: how far a level change past the threshold is cut, 0.5 (2:1) to 1 (256:1).
	float fraction_ = 0.5;
	/// The makeup envelope, nepers: from -23.07 at a final volume of 0 to 2.08 at 0x7fffffff.
	float makeup_ = 0;
	/// The threshold as a fraction of the final volume's level in nepers, 0.2 to 1.
	float threshold_ = 1;
	/// The detector's high-pass moveability, Q31.
	q31 detector_moveability_ = ONE_Q15;

	/// The reduction envelope, nepers past the threshold.
	float reduction_envelope_ = 0;
	/// The left channel's gain this sample, 5.26 signed.
	q31 current_volume_l_ = 0;
	/// The right channel's gain this sample, 5.26 signed.
	q31 current_volume_r_ = 0;
	/// The log (nepers) of the last block's RMS level.
	float log_rms_ = 0;
	/// The last block's mean square level, 1 at full scale.
	float mean_ = 0;
	/// The left channel's output saturation: its position on the curve.
	AntialiasedTanh::State saturation_l_{};
	/// The right channel's output saturation: its position on the curve.
	AntialiasedTanh::State saturation_r_{};
	/// Whether the saturation has been seeded from a block's first frame since the last reset.
	bool seeded_ = false;

	/// The left channel's detector high-pass: the memory of the one-pole whose low-pass the detector subtracts.
	OnePole::State detector_l_{};
	/// The right channel's detector high-pass, as @ref detector_l_.
	OnePole::State detector_r_{};

	/// The attack time in ms for a 3 dB change.
	float attack_ms_ = 0;
	/// The release time in ms for a 3 dB change.
	float release_ms_ = 0;

	/// The makeup gain in nepers at no reduction.
	float base_gain_ = 1.35f;

	/// The threshold this block renders with, Q31 of the knob's travel.
	q31 threshold_knob_ = 0;
	/// The dry level in the blend, Q31.
	q31 dry_ = 0;
	/// The wet level in the blend, Q31.
	q31 wet_ = ONE_Q31;
	/// The last block's gain reduction for the LEDs.
	uint8_t gain_reduction_ = 0;
};

} // namespace deluge::dsp::fixed
