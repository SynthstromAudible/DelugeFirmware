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

Iterance convertUint16ToIterance(int32_t value) {
	return Iterance{(uint8_t)(value >> 8), (uint8_t)(value & 0xFF)};
}
uint16_t convertIteranceToUint16(Iterance value) {
	uint16_t result = value.divisor << 8 | (value.iteranceStep.to_ulong() & 0xFF);
	return result;
}

// This method checks if this iteration step is active or not depending on the current repeat count
bool iterancePassesCheck(Iterance iterance, int32_t repeatCount) {
	int32_t index = repeatCount % iterance.divisor;
	return iterance.iteranceStep[index];
}

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
int32_t getIterancePresetIndexFromUint16Value(uint16_t value) {
	return getIterancePresetIndexFromValue(convertUint16ToIterance(value));
}

int32_t getIterancePresetFromEncodedValue(int32_t value) {
	if (value == 0) {
		// A value of 0 means OFF
		return 0;
	}
	for (int32_t i = 0; i < kNumIterancePresets; i++) {
		// Check if value is one of the presets
		if (iterancePresets[i].divisor == (uint8_t)(value >> 8) && iterancePresets[i].iteranceStep == (value & 0xFF)) {
			return i + 1;
		}
	}

	// Custom iteration
	return kCustomIterancePreset;
}

// This method transform back an iterance preset to a real value
// In the case the preset is Custom, the returned real value is kCustomIteranceValue, that is, "1of1"
Iterance getIteranceValueFromPreset(int32_t value) {
	if (value > 0 && value <= kNumIterancePresets) {
		return iterancePresets[value - 1];
	}
	else if (value == kCustomIterancePreset) {
		// Reset custom iterance to 1of1
		return kCustomIteranceValue;
	}
	else {
		// Default: Off
		return kDefaultIteranceValue;
	}
}
uint16_t getUint16IteranceValueFromPreset(int32_t value) {
	return convertIteranceToUint16(getIteranceValueFromPreset(value));
}

// This methods cleans the iterance value to be among the possible valid values,
// just in case we get bad data from the XML file
Iterance convertAndSanitizeIteranceFromInteger(int32_t iteranceValue) {
	Iterance iterance = convertUint16ToIterance(iteranceValue);
	if (iterance.divisor < 1 || iterance.divisor > 8) {
		return kDefaultIteranceValue;
	}
	return iterance;
}
