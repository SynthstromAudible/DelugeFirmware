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

#include <cstdint>
#include <span>

namespace deluge::dsp::fixed {

/// @brief The state-variable filter, run twice per sample (doubling its reach in cutoff), mono or stereo, with the
/// dry/wet fade it runs after being switched on. The morph blends its low, band and high outputs (@ref SvfMode).
class StateVariableFilter {
public:
	/// @brief Takes one block's settings.
	/// @param config The high-pass slot passes its morph already inverted ((1 << 29) - 1 - morph), so it sweeps
	/// high → band or notch → low.
	/// @return @p config's gain, unchanged.
	[[nodiscard]] q31 configure(const FilterConfig& config, SvfMode mode);

	/// @brief Filters a mono @p block in place.
	/// @param scratch At least @p block's size, used while the fade runs.
	void renderMono(std::span<q31> block, FadeScratch scratch);

	/// @brief Filters an interleaved L/R block in place.
	/// @param scratch At least @p interleaved's size, used while the fade runs.
	void renderStereo(std::span<q31> interleaved, FadeScratch scratch);

	/// @brief Silences the integrators, and with @p fade starts the dry/wet fade.
	void reset(bool fade);

	/// @brief The filter set's zeroing reset: every member zero, the fade included (so no fade runs).
	void zero();

	/// @brief The dry/wet fade, which a filter taking over this one's slot carries on.
	[[nodiscard]] const DryWetFade& fade() const { return fade_; }

	/// @brief Carries on @p fade, the fade of the filter this one takes over from in a slot.
	void carryFade(const DryWetFade& fade) { fade_ = fade; }

private:
	/// One channel's integrators, Q31.
	struct Channel {
		q31 low{};
		q31 band{};
	};

	/// @brief One sample (Q31) through the filter, two passes.
	[[nodiscard, gnu::always_inline]] q31 render(q31 input, Channel& channel) const;

	Channel left_{};
	Channel right_{};

	/// The integrators' rate: tan(f) / (1 + tan(f)) × 1.25, Q31 (each use doubles a halving multiply).
	q31 fc_{};
	/// The damping, squared, Q31.
	q31 q_{};
	/// The input's scale, sqrt of the damping (so the level compensation holds), Q31.
	q31 in_{};
	/// The low output's weight in the blend, Q31.
	q31 c_low_{};
	/// The band output's weight in the blend (band mode only), Q31.
	q31 c_band_{};
	/// The high output's weight in the blend, Q31.
	q31 c_high_{};
	/// Whether the band output joins the blend.
	bool band_mode_{};

	DryWetFade fade_{};
};

} // namespace deluge::dsp::fixed
