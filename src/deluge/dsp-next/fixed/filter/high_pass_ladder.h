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

#include <cstdint>
#include <span>

namespace deluge::dsp::fixed {

/// @brief The transistor-ladder high-pass, mono or stereo, with the dry/wet fade it runs after being switched on.
/// The morph moves each sample's cutoff with the input itself; at high resonance the ladder saturates (antialiased
/// at the highest).
class HighPassLadder {
public:
	/// @brief Takes one block's settings.
	/// @return @p config's gain (Q31) cut as the resonance rises.
	[[nodiscard]] q31 configure(const FilterConfig& config);

	/// @brief Filters a mono @p block in place.
	/// @param scratch At least @p block's size, used while the fade runs.
	void renderMono(std::span<q31> block, FadeScratch scratch);

	/// @brief Filters an interleaved L/R block in place.
	/// @param scratch At least @p interleaved's size, used while the fade runs.
	void renderStereo(std::span<q31> interleaved, FadeScratch scratch);

	/// @brief Silences the ladder's memories (not the saturator's last position), and with @p fade starts the dry/wet
	/// fade.
	void reset(bool fade);

	/// @brief The filter set's zeroing reset: every member zero, the fade included (so no fade runs).
	void zero();

private:
	/// One channel's ladder: its three sections and the antialiased saturator's last position on the curve.
	struct Channel {
		OnePole high_pass_1;
		OnePole low_pass_1;
		OnePole high_pass_3;
		uint32_t last_working_value{};

		void reset() {
			high_pass_1.reset();
			low_pass_1.reset();
			high_pass_3.reset();
		}
	};

	/// @brief One sample (Q31) through the ladder.
	[[nodiscard, gnu::always_inline]] q31 render(q31 input, Channel& channel) const;

	Channel left_{};
	Channel right_{};

	/// tan(f) / (1 + tan(f)), Q31.
	q31 fc_{};
	/// The morph, Q31.
	q31 morph_{};
	/// The resonance as fed back, Q30 (1 is 1073741824), with the extra feedback applied.
	q31 processed_resonance_{};
	/// The output normalisation: 2^31 over the processed resonance's top 8 bits.
	q31 divide_by_processed_resonance_{};
	/// The low-pass section's feedback, Q30.
	q31 low_pass_1_feedback_{};
	/// The third section's feedback, Q30.
	q31 high_pass_3_feedback_{};
	/// 1 / (1 - the loop gain), Q28 (1 is 268435456).
	q31 divide_by_total_moveability_{};

	DryWetFade fade_{};
};

} // namespace deluge::dsp::fixed
