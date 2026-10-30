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

#include "dsp-next/core/mono.h"
#include "dsp-next/core/q31_ops.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/dynamics/compressor_kernel.h" // CompressorKnobs and their mappings
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/shaping/antialiased_tanh.h"

#include <cstdint>
#include <span>

namespace deluge::dsp::fixed {

/// @return The wet level @p blend (Q31, kOneQ31 fully wet) gives, as a whole percentage.
[[nodiscard]] constexpr int32_t compressor_blend_percent(q31 blend) {
	return blend > (127 << 24) ? 100 : 100 * (blend >> 24) >> 7;
}

/// @brief A feedback compressor: the gain for each block follows the RMS level, in log space, of the block before it.
///
/// The component its owners hold (a sound, a clip's chain, the song's master), around the @ref Compressor kernel. It
/// takes the knobs, the threshold and the base gain as they come, reconfigures the kernel only when they change, and
/// renders each block in place: the block's levels, the saturation's seed when unseeded, the block, then its level.
///
/// Levels and gains in the envelope are in nepers (natural-log units). The compressed signal is saturated through an
/// antialiased tanh at drive 3, which keeps it within ±2^26: fully wet, that is the output's range. Below fully wet,
/// the block as it came in is mixed back at the dry level (1 - blend), so the output's range is ±2^26 at the blend
/// plus the input's own range at the dry level.
class RmsFeedbackCompressor {
public:
	/// @brief A compressor at the default knobs (@ref CompressorKnobs), fully wet, with no gain reduction.
	RmsFeedbackCompressor() : dsp_{settings()} {}

	/// @brief Takes @p knobs, reconfiguring only when they changed: the time constants, ratio, detector filter and
	/// blend apply from the next block, and the envelopes, gains, saturation, detector and level carry on.
	/// The threshold too, when it changed; an owner whose threshold is a param overrides it with set_threshold every
	/// block.
	void configure(const CompressorKnobs& knobs);

	/// @brief The threshold the next block renders with, from a knob position (Q31 of the knob's travel).
	void set_threshold(q31 threshold) { threshold_knob_ = threshold; }

	/// @return The threshold the next block renders with, as a knob position (Q31 of the knob's travel).
	[[nodiscard]] q31 threshold() const { return threshold_knob_; }

	/// @brief Makeup gain in nepers at no reduction: 1.35 by default, 0.85 for the song's master. Applies from the
	/// next block; the rest carries on.
	///
	/// The reduction is never positive and the makeup envelope never exceeds 2.08, so 1.35 is the most that keeps the
	/// gain under 31.
	void set_base_gain(float base_gain);

	/// @brief No gain reduction at the start of the next block. The channel gains, the detector's filters and its
	/// last level carry on.
	void reset() { dsp_.reset(); }

	/// @brief Compresses @p block in place; an empty block renders nothing and changes nothing.
	/// @param volume_l, volume_r The channels' linear gain, 4.27 signed.
	/// @param final_volume The peak-to-peak volume scale, 3.29.
	void render(std::span<StereoSample> block, q31 volume_l, q31 volume_r, q31 final_volume);

	/// @brief render at neutral channel gain, with @p final_volume (3.29) scaled so threshold zero leaves the level
	/// unchanged: for an owner whose volume is applied elsewhere.
	void render_volume_neutral(std::span<StereoSample> block, q31 final_volume) {
		render(block, 1 << 27, 1 << 27, final_volume >> 3);
	}

	/// @return The last block's gain reduction for the LEDs, 0 to 127 (6.2 fixed-point dB, quadrupled).
	[[nodiscard]] uint8_t gain_reduction() const { return dsp_.state().gain_reduction; }

	/// @brief The LEDs show no gain reduction until the next block: for an owner that skips rendering.
	void clear_gain_reduction() { dsp_.on<&zero_gain_reduction>(); }

	/// @return The attack time in ms for a 3 dB change.
	[[nodiscard]] float attack_ms() const { return dsp_.coefficients().attack_ms; }

	/// @return The release time in ms for a 3 dB change.
	[[nodiscard]] float release_ms() const { return dsp_.coefficients().release_ms; }

	/// @brief The compressor between blocks, read-only: every value that carries from one block to the next.
	struct View {
		CompressorKnobs configured;          ///< The knob positions last taken.
		float attack_rate;                   ///< The attack rate, per sample (negative).
		float release_rate;                  ///< The release rate, per sample (negative).
		float fraction;                      ///< 1 - 1/ratio.
		float makeup;                        ///< The makeup envelope, nepers.
		float threshold;                     ///< The threshold the next block renders with, as a fraction of the
		                                     ///< final volume's level.
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
	/// @return The kernel's settings: the knobs last taken and the base gain.
	[[nodiscard]] Compressor::Settings settings() const { return {.knobs = configured_, .base_gain = base_gain_}; }

	/// @brief Event: no gain reduction for the LEDs until the next block.
	static void zero_gain_reduction(Compressor::State& s) { s.gain_reduction = 0; }

	/// The knob positions last taken: when to reconfigure.
	CompressorKnobs configured_{};
	/// The makeup gain in nepers at no reduction.
	float base_gain_ = 1.35f;
	/// The threshold the next block renders with, Q31 of the knob's travel.
	q31 threshold_knob_ = 0;
	/// The kernel: its coefficients from @ref settings, and everything that carries from block to block.
	Mono<Compressor> dsp_;
};

} // namespace deluge::dsp::fixed
