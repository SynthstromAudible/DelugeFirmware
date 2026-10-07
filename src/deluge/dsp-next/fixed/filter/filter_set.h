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
#include "dsp-next/fixed/filter/high_pass_ladder.h"
#include "dsp-next/fixed/filter/low_pass_ladder.h"
#include "dsp-next/fixed/filter/state_variable_filter.h"

#include <span>

namespace deluge::dsp::fixed {

/// @brief The scratch a set needs, each at least the block's samples (two per frame in stereo): the fades', and the
/// parallel route's second copy of the block.
struct FilterScratch {
	FadeScratch fade;
	std::span<q31> parallel;
};

/// @brief A low-pass and a high-pass filter in series either way or in parallel.
///
/// The low-pass resets when its family (ladder or SVF) changes and the high-pass when its mode changes at all; either
/// fades in from dry when switched on from off. A fade under way carries on across a change of family. Each slot holds
/// one filter at a time, the one its family needs.
class FilterSet {
public:
	FilterSet() { reset(); }

	/// @brief Takes one block's settings.
	/// @param low The low-pass's settings; its gain (Q31) is the gain going into the set.
	/// @param high The high-pass's settings; its gain is not read (the set hands on the low-pass's).
	/// @return The gain going out of the set (Q31): compensated for each filter that is on, and scaled so that at 50%
	/// resonance the level is unchanged.
	[[nodiscard]] q31 configure(const FilterConfig& low, LowPassSlot low_slot, const FilterConfig& high,
	                            HighPassSlot high_slot, FilterRoute route);

	/// @brief Filters a mono @p block in place.
	void renderMono(std::span<q31> block, FilterScratch scratch);

	/// @brief Filters an interleaved L/R block in place.
	void renderStereo(std::span<q31> interleaved, FilterScratch scratch);

	/// @brief Back to a new set's state: both filters off and every filter's state zero, the fades included (so the
	/// next configure fades nothing in). The slots read as holding a ladder, so a low-pass ladder configured next is
	/// not reset (its state is zero already) and anything else is.
	void reset();

	/// @brief Whether either filter is on.
	[[nodiscard]] bool isOn() const { return isLowPassOn() || isHighPassOn(); }
	/// @brief Whether the low-pass is on.
	[[nodiscard]] bool isLowPassOn() const { return low_slot_.kind != LowPassSlot::Kind::OFF; }
	/// @brief Whether the high-pass is on.
	[[nodiscard]] bool isHighPassOn() const { return high_slot_.kind != HighPassSlot::Kind::OFF; }

private:
	/// The low-pass slot's filter: the ladder or the SVF, whichever @ref low_held_ names.
	union LowPass {
		LowPassLadder ladder;
		StateVariableFilter svf;
		LowPass() : ladder{} {}
	};
	/// The high-pass slot's filter: the ladder or the SVF, whichever @ref high_held_ names.
	union HighPass {
		HighPassLadder ladder;
		StateVariableFilter svf;
		HighPass() : ladder{} {}
	};

	/// @brief Makes the low-pass slot hold @p kind's filter (LADDER or SVF): a new one, zeroed, carrying on the old
	/// one's fade.
	void holdLowPass(LowPassSlot::Kind kind);
	/// @brief Makes the high-pass slot hold @p kind's filter (LADDER or SVF): a new one, zeroed, carrying on the old
	/// one's fade.
	void holdHighPass(HighPassSlot::Kind kind);

	/// @brief The low-pass over a mono block, if on.
	void renderLowPassMono(std::span<q31> block, FadeScratch scratch);
	/// @brief The high-pass over a mono block, if on.
	void renderHighPassMono(std::span<q31> block, FadeScratch scratch);
	/// @brief The low-pass over an interleaved block, if on.
	void renderLowPassStereo(std::span<q31> interleaved, FadeScratch scratch);
	/// @brief The high-pass over an interleaved block, if on.
	void renderHighPassStereo(std::span<q31> interleaved, FadeScratch scratch);

	LowPass low_pass_;
	HighPass high_pass_;

	/// The family @ref low_pass_ holds (LADDER or SVF).
	LowPassSlot::Kind low_held_{LowPassSlot::Kind::LADDER};
	/// The family @ref high_pass_ holds (LADDER or SVF).
	HighPassSlot::Kind high_held_{HighPassSlot::Kind::LADDER};
	/// The low-pass as configured (OFF when off), which the render runs.
	LowPassSlot low_slot_{};
	/// The high-pass as configured (OFF when off), which the render runs.
	HighPassSlot high_slot_{};
	/// The low-pass's family as last configured, OFF when off; after a reset, LADDER (what the zeroed slot holds).
	LowPassSlot::Kind low_last_{LowPassSlot::Kind::LADDER};
	/// The high-pass's mode as last configured, OFF when off; after a reset, the ladder (what the zeroed slot holds).
	HighPassSlot high_last_{.kind = HighPassSlot::Kind::LADDER};
	/// The route as configured.
	FilterRoute route_{FilterRoute::HIGH_TO_LOW};
};

} // namespace deluge::dsp::fixed
