/*
 * Copyright © 2016-2026 Synthstrom Audible Limited
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

#include <cstdint>
#include <span>

namespace deluge::dsp::fixed {

/// @file
/// @brief Window-match metrics for the time-stretch hop search (shared by the sample time-stretcher and the
/// live-input pitch-shifter). Each compares two equal-length sets of crossfade moving-average totals — the running
/// averages of a candidate splice window vs. the reference one — so the search can pick where the two best line up.

/// @brief Total absolute difference between two windows' moving-average totals: `Σ |b[i] − a[i]|`.
/// The hop search minimises this — the smaller it is, the better the two windows match for a click-free splice.
[[nodiscard]] constexpr int32_t totalDifferenceAbs(std::span<const int32_t> a, std::span<const int32_t> b) {
	int32_t total = 0;
	for (std::size_t i = 0; i < a.size(); ++i) {
		int32_t difference = b[i] - a[i];
		total += (difference < 0) ? -difference : difference;
	}
	return total;
}

/// @brief Net signed change between two windows' moving-average totals: `Σ b[i] − Σ a[i]`.
[[nodiscard]] constexpr int32_t totalChange(std::span<const int32_t> a, std::span<const int32_t> b) {
	int32_t total = 0;
	for (std::size_t i = 0; i < a.size(); ++i) {
		total += b[i] - a[i];
	}
	return total;
}

} // namespace deluge::dsp::fixed
