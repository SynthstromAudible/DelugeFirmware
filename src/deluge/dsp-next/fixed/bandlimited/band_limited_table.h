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
#include <array>
#include <bit>
#include <cstddef>
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
/// its BandlimitedTable. @ref select returns the first band whose `max_phase_increment` covers
/// @p phase_increment — the exact `if (inc <= X)` ladder the legacy `get_table_number` /
/// triangle-ladder used, so substituting it is bit-exact. Pair it with a
/// @ref BandLimitedTableOscillator, which picks the band for the current pitch and renders it.
class BandlimitedTable {
	std::span<const Band> bands_;
	/// For each bit width of a phase increment, the first band that can cover an increment that wide: the bands
	/// whose thresholds are all below 2^(width - 1) are skipped. The thresholds go up by about sqrt(2) per band, so
	/// at most a couple more compares finish the pick, where walking every band took up to twenty.
	std::array<uint8_t, 33> first_for_width_{};

	[[nodiscard]] static Table table_of(const Band& band) {
		return Table{.samples =
		                 std::span<const int16_t>{band.table, (static_cast<std::size_t>(1) << band.magnitude) + 1},
		             .magnitude = band.magnitude};
	}

public:
	constexpr explicit BandlimitedTable(std::span<const Band> bands) : bands_{bands} {
		for (uint32_t width = 1; width <= 32; width++) {
			uint8_t first = 0;
			while (first + 1u < bands_.size() && bands_[first].max_phase_increment < (uint32_t{1} << (width - 1))) {
				first++;
			}
			first_for_width_[width] = first;
		}
	}

	[[nodiscard]] Table select(uint32_t phase_increment) const {
		std::size_t band = first_for_width_[std::bit_width(phase_increment)];
		while (band + 1 < bands_.size() && phase_increment > bands_[band].max_phase_increment) {
			band++;
		}
		return table_of(bands_[band]);
	}
};

namespace detail {
/// The highest phase increment each standard band number is used for; the last is the catch-all.
inline constexpr std::array<uint32_t, 20> kTableNumberBounds = {
    1247086,  1764571,  2494173,  3526245,   4982560,   7040929,   9988296,   14035840,  19701684,  28256363,
    40518559, 55063683, 79536431, 113025455, 165191049, 238609294, 306783378, 429496729, 715827882, 0xFFFFFFFF,
};
/// Each standard band's size magnitude.
inline constexpr std::array<int8_t, 20> kTableSizeMagnitudes = {13, 12, 12, 11, 11, 11, 11, 11, 11, 11,
                                                                11, 11, 11, 11, 10, 10, 10, 10, 9,  9};
/// The lowest band number for each bit width of the phase increment: how many bounds are below 2^(width - 1).
inline constexpr auto kFirstTableForBitWidth = [] {
	std::array<uint8_t, 33> first{};
	for (uint32_t width = 1; width <= 32; width++) {
		for (uint32_t bound : kTableNumberBounds) {
			first[width] += bound < (uint32_t{1} << (width - 1));
		}
	}
	return first;
}();
} // namespace detail

/// @brief The standard band number + size magnitude for a phase increment — the shared ladder
/// used by the saw/square/analog band sets. Still used app-side for the crude/quality gate
/// (`tableNumber < cpuDireness + 6`) and the analog→saw downgrade; the band *tables* now select
/// themselves via @ref BandlimitedTable, but the band *index* is the quality measure.
///
/// The bounds go up by about sqrt(2) per band, so there are at most two of them in each power of two: the bit width
/// gets within two bands, and two compares finish it.
[[nodiscard]] constexpr std::pair<int32_t, int32_t> get_table_number(uint32_t phase_increment) {
	int32_t table = detail::kFirstTableForBitWidth[std::bit_width(phase_increment)];
	table += phase_increment > detail::kTableNumberBounds[table] ? 1 : 0;
	table += phase_increment > detail::kTableNumberBounds[table] ? 1 : 0;
	return {table, detail::kTableSizeMagnitudes[table]};
}

} // namespace deluge::dsp::fixed
