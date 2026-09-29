/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

#include "gui/colour/rgb.h"
#include "util/const_functions.h"
#include <algorithm>
#include <cstdint>

// The pad grid's compositing step, in its own header so the pad animations and their tests share it; util/functions.h
// must not reach gui, because the synth core includes it.

/// Composite @p squareColour (at @p intensity, out of 65536) into a grid square already holding @p square, updating
/// its @p occupancyMask (out of 64): the new colour marginalises the old, which is constrained to the remaining
/// space. @return the blended square colour.
[[nodiscard]] inline RGB drawSquare(const RGB& squareColour, int32_t intensity, const RGB& square,
                                    uint8_t* occupancyMask) {
	int32_t modifiedIntensity = intensity;

	// Make new colour being drawn into this square marginalise the colour already in the square
	int32_t colourRemainingAmount = 65536;

	// We know how much colour we want to add to this square, so constrain any existing colour to the remaining "space"
	// that it may still occupy
	int32_t maxOldOccupancy = (65536 - modifiedIntensity) >> 10;

	// If the square has more colour in it than it's allowed to retain, then plan to reduce it
	if (*occupancyMask > maxOldOccupancy) {
		colourRemainingAmount = (maxOldOccupancy << 16) / *occupancyMask; // out of 65536
	}

	// Add the new colour, reducing the old if that's what we're doing
	int32_t newOccupancyMaskValue =
	    rshift_round(*occupancyMask * colourRemainingAmount, 16) + rshift_round(modifiedIntensity, 10);
	*occupancyMask = std::min<int32_t>(64, newOccupancyMaskValue);

	return RGB::blend2(square, squareColour, colourRemainingAmount, modifiedIntensity);
}
