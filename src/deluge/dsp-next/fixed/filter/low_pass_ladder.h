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

#include "dsp-next/core/types.h"
#include "dsp-next/fixed/filter/dry_wet_fade.h"
#include "dsp-next/fixed/filter/filter_modes.h"
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/shaping/tanh.h"
#include "util/fixedpoint.h"

#include <array>
#include <cstdint>
#include <span>

namespace deluge::dsp::fixed {

/// @brief The transistor-ladder low-pass in its three modes (12dB half ladder, 24dB full ladder, and the driven 24dB
/// ladder), mono or stereo, with the dry/wet fade it runs after being switched on.
///
/// Each sample's cutoff wobbles with heavily low-passed noise drawn from the synth-wide generator (util/waves.h's
/// jcong): one draw per channel per sample, left before right, and two per sample when the drive ladder
/// oversamples.
class LowPassLadder {
public:
	/// @brief Takes one block's settings.
	/// @return @p config's gain (Q31) compensated for this filter's resonance and mode.
	[[nodiscard]] q31 configure(const FilterConfig& config, LowPassLadderMode mode);

	/// @brief Filters a mono @p block in place.
	/// @param scratch At least @p block's size, used while the fade runs.
	void renderMono(std::span<q31> block, FadeScratch scratch);

	/// @brief Filters an interleaved L/R block in place.
	/// @param scratch At least @p interleaved's size, used while the fade runs.
	void renderStereo(std::span<q31> interleaved, FadeScratch scratch);

	/// @brief Silences the ladders' memories (not the noise follower), and with @p fade starts the dry/wet fade.
	void reset(bool fade);

	/// @brief The filter set's zeroing reset: every member zero, the fade included (so no fade runs).
	void zero();

	/// @brief The dry/wet fade, which a filter taking over this one's slot carries on.
	[[nodiscard]] const DryWetFade& fade() const { return fade_; }

	/// @brief Carries on @p fade, the fade of the filter this one takes over from in a slot.
	void carryFade(const DryWetFade& fade) { fade_ = fade; }

private:
	/// One channel's ladder: its noise follower (Q31, a quarter of the noise's range) and its four sections.
	struct Channel {
		q31 noise_last{};
		std::array<OnePole, 4> sections{};

		void reset() {
			for (OnePole& section : sections) {
				section.reset();
			}
		}
	};

	/// @brief The cutoff for this sample: the moveability nudged by the channel's low-passed noise.
	[[nodiscard, gnu::always_inline]] q31 noisyMoveability(Channel& channel) const;
	/// @brief The input less the scaled feedback, normalised; saturated with the morph added when morph or resonance
	/// is up.
	[[nodiscard, gnu::always_inline]] q31 scaleInput(q31 input, q31 feedbacks_sum) const;
	/// @brief One sample (Q31) through the half ladder.
	[[nodiscard, gnu::always_inline]] q31 render12dB(q31 input, Channel& channel) const;
	/// @brief One sample (Q31) through the full ladder.
	[[nodiscard, gnu::always_inline]] q31 render24dB(q31 input, Channel& channel) const;
	/// @brief One step (Q31) of the drive ladder, its feedback saturated; the output is not yet saturated.
	[[nodiscard, gnu::always_inline]] q31 renderDrive(q31 input, Channel& channel) const;
	/// @brief One drive-ladder output sample: run twice over the same input when oversampling (the first output
	/// dropped), then saturated.
	[[nodiscard, gnu::always_inline]] q31 renderDriveOutput(q31 input, Channel& channel, bool oversample) const;
	/// @brief The mode's ladder over a mono block, without the fade.
	void filterMono(std::span<q31> block);
	/// @brief The mode's ladder over an interleaved L/R block, without the fade.
	void filterStereo(std::span<q31> interleaved);

	LowPassLadderMode mode_{};
	Channel left_{};
	Channel right_{};

	/// The resonance as fed back, Q30 (1 is 1073741824).
	q31 processed_resonance_{};
	/// 1 / (1 + the loop gain through the sections and resonance), Q30 (1 is 1073741824).
	q31 divide_by_total_moveability_and_processed_resonance_{};
	/// tan(f) / (1 + tan(f)), Q31, held at least 4317840.
	q31 moveability_{};
	/// The morph, Q31.
	q31 morph_{};
	/// 1 / (1 + tan(f)), Q31: the last section's feedback.
	q31 divide_by_one_plus_tanned_{};
	/// The first three sections' feedbacks, Q30 (1 is 1073741824).
	std::array<q31, 3> feedbacks_{};
	/// Whether the drive ladder runs at twice the rate this block.
	bool oversample_{};

	DryWetFade fade_{};
};

} // namespace deluge::dsp::fixed
