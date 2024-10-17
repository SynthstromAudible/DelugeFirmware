/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "model/iterance/iterance.h"
#include "definitions_cxx.hpp"
#include "util/lookuptables/lookuptables.h"
#include <cstdint>

// To/from int

uint16_t Iterance::toInt() {
	return (uint16_t)(divisor << 8) | (uint16_t)(iteranceStep.to_ulong() & 0xFF);
}

Iterance Iterance::fromInt(int32_t value) {
	Iterance iterance = Iterance{(uint8_t)(value >> 8), (uint8_t)(value & 0xFF)};
	if (iterance.divisor < 1 || iterance.divisor > 8) {
		// If divisor out of bounds, fall back to default value
		iterance = kDefaultIteranceValue;
	}
	return iterance;
}

// To/from preset index

// This methods takes the iterance value and searches the table of iterance presets for a match
// If no match is found it will return kCustomIterancePreset (which is equal to '1of1')
int32_t Iterance::toPresetIndex() {
	if (iteranceStep.none() && divisor == 0) {
		// A value of 0 means OFF
		return 0;
	}
	for (int32_t i = 0; i < kNumIterancePresets; i++) {
		// Check if value is one of the presets
		if (iterancePresets[i].divisor == divisor && iterancePresets[i].iteranceStep == iteranceStep) {
			return i + 1;
		}
	}

	// Custom iteration
	return kCustomIterancePreset;
}

// This method transform back an iterance preset to a real value
// In the case the preset is Custom, the returned real value is kCustomIteranceValue, that is, "1of1"
Iterance Iterance::fromPresetIndex(int32_t presetIndex) {
	if (presetIndex > 0 && presetIndex <= kNumIterancePresets) {
		return iterancePresets[presetIndex - 1];
	}
	else if (presetIndex == kCustomIterancePreset) {
		// Reset custom iterance to 1of1
		return kCustomIteranceValue;
	}
	else {
		// Default: Off
		return kDefaultIteranceValue;
	}
}
