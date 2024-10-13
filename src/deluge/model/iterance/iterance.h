/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include <bitset>
#include <cstdint>

struct Iterance {
	uint8_t divisor;
	std::bitset<8> iteranceStep;

	bool operator==(Iterance const& other) const {
		return other.divisor == divisor && other.iteranceStep == iteranceStep;
	}
};

// Iterance utils
Iterance convertUint16ToIterance(int32_t value);
uint16_t convertIteranceToUint16(Iterance value);
bool iterancePassesCheck(Iterance iterance, int32_t repeatCount);
int32_t getIterancePresetIndexFromValue(Iterance value);
int32_t getIterancePresetIndexFromUint16Value(uint16_t value);
int32_t getIterancePresetFromEncodedValue(int32_t value);
Iterance getIteranceValueFromPreset(int32_t value);
uint16_t getUint16IteranceValueFromPreset(int32_t value);
Iterance convertAndSanitizeIteranceFromInteger(int32_t iterance);
