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
#include "io/debug/log.h"
#include "util/lookuptables/lookuptables.h"
#include <cstdint>

// This methods takes the iterance value and searches the table of iterance presets for a match
// If no match is found it will return kCustomIterancePreset (which is equal to '1of1')
int32_t getIterancePresetIndexFromValue(Iterance value) {
	if (value.iteranceStep.none() && value.divisor == 0) {
		// A value of 0 means OFF
		return 0;
	}
	for (int32_t i = 0; i < kNumIterancePresets; i++) {
		// Check if value is one of the presets
		if (iterancePresets[i].divisor == value.divisor && iterancePresets[i].iteranceStep == value.iteranceStep) {
			return i + 1;
		}
	}

	// Custom iteration
	return kCustomIterancePreset;
}
int32_t getIterancePresetIndexFromIntValue(uint16_t value) {
	return getIterancePresetIndexFromValue(Iterance::fromInt(value));
}

// This method transform back an iterance preset to a real value
// In the case the preset is Custom, the returned real value is kCustomIteranceValue, that is, "1of1"
Iterance getIteranceValueFromPresetIndex(int32_t presetIndex) {
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
uint16_t getIntIteranceValueFromPresetIndex(int32_t presetIndex) {
	return getIteranceValueFromPresetIndex(presetIndex).toInt();
}

// This methods cleans the iterance value to be among the possible valid values,
// just in case we get bad data from the XML file
Iterance convertAndSanitizeIteranceFromInt(int32_t value) {
	Iterance iterance = Iterance::fromInt(value);
	if (iterance.divisor < 1 || iterance.divisor > 8) {
		return kDefaultIteranceValue;
	}
	return iterance;
}
