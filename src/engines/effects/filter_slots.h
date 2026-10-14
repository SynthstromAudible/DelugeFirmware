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

#include "dsp-next/fixed/filter/filter_modes.h"
#include "engines/effects/words.h"

namespace deluge::effects {

/// @brief What a filter set's low-pass slot runs for @p mode: a ladder for the three ladder modes, an SVF for the two
/// SVF modes, and nothing for OFF or for HPLADDER (a high-pass mode, which no low-pass runs).
/// @param mode The owner's low-pass filter mode.
/// @return The slot configuration.
[[nodiscard]] constexpr dsp::fixed::LowPassSlot low_pass_slot(FilterMode mode) noexcept {
	using dsp::fixed::LowPassLadderMode;
	using dsp::fixed::LowPassSlot;
	using dsp::fixed::SvfMode;
	switch (mode) {
	case FilterMode::TRANSISTOR_12DB:
		return {.kind = LowPassSlot::Kind::LADDER, .ladder = LowPassLadderMode::TRANSISTOR_12DB};
	case FilterMode::TRANSISTOR_24DB:
		return {.kind = LowPassSlot::Kind::LADDER, .ladder = LowPassLadderMode::TRANSISTOR_24DB};
	case FilterMode::TRANSISTOR_24DB_DRIVE:
		return {.kind = LowPassSlot::Kind::LADDER, .ladder = LowPassLadderMode::TRANSISTOR_24DB_DRIVE};
	case FilterMode::SVF_BAND:
		return {.kind = LowPassSlot::Kind::SVF, .svf = SvfMode::BAND};
	case FilterMode::SVF_NOTCH:
		return {.kind = LowPassSlot::Kind::SVF, .svf = SvfMode::NOTCH};
	case FilterMode::HPLADDER:
	case FilterMode::OFF:
		break;
	}
	return {.kind = LowPassSlot::Kind::OFF};
}

/// @brief What a filter set's high-pass slot runs for @p mode: the ladder for HPLADDER, an SVF for the two SVF modes,
/// and nothing for OFF or for the three low-pass ladder modes (which no high-pass runs).
/// @param mode The owner's high-pass filter mode.
/// @return The slot configuration.
[[nodiscard]] constexpr dsp::fixed::HighPassSlot high_pass_slot(FilterMode mode) noexcept {
	using dsp::fixed::HighPassSlot;
	using dsp::fixed::SvfMode;
	switch (mode) {
	case FilterMode::HPLADDER:
		return {.kind = HighPassSlot::Kind::LADDER};
	case FilterMode::SVF_BAND:
		return {.kind = HighPassSlot::Kind::SVF, .svf = SvfMode::BAND};
	case FilterMode::SVF_NOTCH:
		return {.kind = HighPassSlot::Kind::SVF, .svf = SvfMode::NOTCH};
	case FilterMode::TRANSISTOR_12DB:
	case FilterMode::TRANSISTOR_24DB:
	case FilterMode::TRANSISTOR_24DB_DRIVE:
	case FilterMode::OFF:
		break;
	}
	return {.kind = HighPassSlot::Kind::OFF};
}

/// @brief The filter set's route for @p route (the same three orders).
/// @param route The owner's filter route.
/// @return The dsp-next route.
[[nodiscard]] constexpr dsp::fixed::FilterRoute filter_route(FilterRoute route) noexcept {
	switch (route) {
	case FilterRoute::HIGH_TO_LOW:
		return dsp::fixed::FilterRoute::HIGH_TO_LOW;
	case FilterRoute::LOW_TO_HIGH:
		return dsp::fixed::FilterRoute::LOW_TO_HIGH;
	case FilterRoute::PARALLEL:
		break;
	}
	return dsp::fixed::FilterRoute::PARALLEL;
}

} // namespace deluge::effects
