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

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/filter/filter_set.h"
#include "effects/state.h"

#include <cstdint>
#include <span>

namespace deluge::effects {

/// @brief One block's filter params as the owner resolved them: each filter's final values, which the filters run
/// at, and the params' own values, which decide whether each filter runs at all.
struct FilterParams {
	/// The low-pass cutoff's final value, Q31 (see dsp::fixed::FilterConfig::frequency).
	int32_t lpf_frequency;
	/// The low-pass resonance's final value, Q29 at most.
	int32_t lpf_resonance;
	/// The low-pass morph's final value, Q29.
	int32_t lpf_morph;
	/// The high-pass cutoff's final value, Q31.
	int32_t hpf_frequency;
	/// The high-pass resonance's final value, Q29 at most.
	int32_t hpf_resonance;
	/// The high-pass morph's final value, Q29.
	int32_t hpf_morph;
	/// The low-pass cutoff param's own value, Q31 (INT32_MAX fully open).
	int32_t lpf_frequency_param;
	/// The low-pass morph param's own value, Q31 (INT32_MIN at its minimum).
	int32_t lpf_morph_param;
	/// The high-pass cutoff param's own value, Q31 (INT32_MIN fully open).
	int32_t hpf_frequency_param;
	/// The high-pass morph param's own value, Q31 (INT32_MIN at its minimum).
	int32_t hpf_morph_param;
	/// Whether the drive ladder may oversample: false under CPU load. No default, so every caller says which.
	bool allow_oversampling;
};

/// @brief A host chain's filters (a kit's, an audio track's, the song's): a low-pass and a high-pass in the modes and
/// route the owner's filter state names, each running only while its params can change the sound.
///
/// The low-pass runs in drive mode always (its saturation colours the sound even fully open), otherwise while its
/// cutoff param is below fully open or its morph above minimum. The high-pass runs while its cutoff param or its
/// morph is above minimum.
class Filter {
public:
	/// @brief The cutoff param value below which the low-pass runs (Q31): from here up to fully open (INT32_MAX, 45
	/// above) counts as open.
	static constexpr int32_t kLowPassOpen = 2147483602;

	/// @brief Takes one block's settings.
	/// @param state The owner's filter modes and route.
	/// @param params The block's param values.
	/// @param post_fx_volume The owner's post-FX volume going in (Q31).
	/// @return The owner's post-FX volume after the filters' compensation (Q31): each running filter's, and the set's
	/// scaling that keeps the level at 50% resonance.
	[[nodiscard]] int32_t configure(const FilterState& state, const FilterParams& params, int32_t post_fx_volume);

	/// @brief Filters a stereo @p block in place, as last configured.
	/// @param scratch The audio tier's filter scratch, each span at least two samples per frame of @p block.
	/// @param noise The synth's noise stream, which the low-pass ladder draws from.
	void process(std::span<dsp::StereoSample> block, dsp::fixed::FilterScratch scratch, dsp::fixed::Cong& noise);

	/// @brief Both filters off and their state zero (see dsp::fixed::FilterSet::reset).
	void reset() { set_.reset(); }

private:
	dsp::fixed::FilterSet set_;
};

} // namespace deluge::effects
