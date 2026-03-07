/*
 * Copyright © 2025 Synthstrom Audible Limited
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

/// Per-clip time signature (numerator/denominator).
/// Base tick resolution: 96 ticks = 1 whole note (4 quarter notes at 24 PPQN).
struct TimeSignature {
	uint8_t numerator{4};
	uint8_t denominator{4};

	/// Bar length in base ticks (before magnitude scaling).
	/// 4/4 = 96, 3/4 = 72, 7/8 = 84, 6/8 = 72, 5/4 = 120
	[[nodiscard]] constexpr int32_t barLengthInBaseTicks() const {
		return static_cast<int32_t>(numerator) * (96 / denominator);
	}

	/// Beat length in base ticks (before magnitude scaling).
	/// Quarter note = 24, eighth note = 12, half note = 48, sixteenth = 6
	[[nodiscard]] constexpr int32_t beatLengthInBaseTicks() const { return 96 / denominator; }

	[[nodiscard]] constexpr bool isDefault() const { return numerator == 4 && denominator == 4; }

	constexpr bool operator==(TimeSignature const&) const = default;
};
