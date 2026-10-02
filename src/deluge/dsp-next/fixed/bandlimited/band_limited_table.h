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

#include "dsp-next/fixed/bandlimited/table.h"
#include <cstdint>
#include <span>
#include <utility>

namespace deluge::dsp::fixed {

/// @brief One band of a band-limited wave: the table for it, its size magnitude, and the highest
/// phase increment (pitch) it covers. Bands are ordered by increasing @ref max_phase_increment;
/// the last band is the catch-all (`max_phase_increment == UINT32_MAX`).
struct Band {
	const int16_t* table;
	int32_t magnitude;
	uint32_t max_phase_increment;
};

/// @brief A band-limited wave as a set of bands that selects itself by pitch.
///
/// Each wave kernel (saw, square, triangle, analog saw/square) owns its LUTs and exposes only
/// its BandlimitedTable. @ref select walks the bands and returns the first whose
/// `max_phase_increment` covers @p phase_increment — the exact `if (inc <= X)` ladder the legacy
/// `getTableNumber` / triangle-ladder used, so substituting it is bit-exact. Pair it with a
/// @ref BandLimitedTableOscillator, which picks the band for the current pitch and renders it.
class BandlimitedTable {
	std::span<const Band> bands_;

public:
	constexpr explicit BandlimitedTable(std::span<const Band> bands) : bands_{bands} {}

	[[nodiscard]] Table select(uint32_t phase_increment) const {
		for (const Band& band : bands_) {
			if (phase_increment <= band.max_phase_increment) {
				return Table{std::span<const int16_t>{band.table, (static_cast<std::size_t>(1) << band.magnitude) + 1},
				             band.magnitude};
			}
		}
		const Band& last = bands_.back();
		return Table{std::span<const int16_t>{last.table, (static_cast<std::size_t>(1) << last.magnitude) + 1},
		             last.magnitude};
	}
};

/// @brief The standard band number + size magnitude for a phase increment — the shared ladder
/// used by the saw/square/analog band sets. Still used app-side for the crude/quality gate
/// (`tableNumber < cpuDireness + 6`) and the analog→saw downgrade; the band *tables* now select
/// themselves via @ref BandlimitedTable, but the band *index* is the quality measure.
[[nodiscard]] inline std::pair<int32_t, int32_t> getTableNumber(uint32_t phaseIncrement) {
	if (phaseIncrement <= 1247086) {
		return {0, 13};
	}
	if (phaseIncrement <= 1764571) {
		return {1, 12};
	}
	if (phaseIncrement <= 2494173) {
		return {2, 12};
	}
	if (phaseIncrement <= 3526245) {
		return {3, 11};
	}
	if (phaseIncrement <= 4982560) {
		return {4, 11};
	}
	if (phaseIncrement <= 7040929) {
		return {5, 11};
	}
	if (phaseIncrement <= 9988296) {
		return {6, 11};
	}
	if (phaseIncrement <= 14035840) {
		return {7, 11};
	}
	if (phaseIncrement <= 19701684) {
		return {8, 11};
	}
	if (phaseIncrement <= 28256363) {
		return {9, 11};
	}
	if (phaseIncrement <= 40518559) {
		return {10, 11};
	}
	if (phaseIncrement <= 55063683) {
		return {11, 11};
	}
	if (phaseIncrement <= 79536431) {
		return {12, 11};
	}
	if (phaseIncrement <= 113025455) {
		return {13, 11};
	}
	if (phaseIncrement <= 165191049) {
		return {14, 10};
	}
	if (phaseIncrement <= 238609294) {
		return {15, 10};
	}
	if (phaseIncrement <= 306783378) {
		return {16, 10};
	}
	if (phaseIncrement <= 429496729) {
		return {17, 10};
	}
	if (phaseIncrement <= 715827882) {
		return {18, 9};
	}
	return {19, 9};
}

} // namespace deluge::dsp::fixed
