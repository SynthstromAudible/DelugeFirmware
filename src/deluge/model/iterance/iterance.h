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

	[[nodiscard]] uint16_t toInt() const {
		return (uint16_t)(divisor << 8) | (uint16_t)(iteranceStep.to_ulong() & 0xFF);
	}

	[[nodiscard]] bool passesCheck(int32_t repeatCount) const { return iteranceStep[repeatCount % divisor]; }

	[[nodiscard]] static Iterance fromInt(int32_t value) {
		return Iterance{(uint8_t)(value >> 8), (uint8_t)(value & 0xFF)};
	}
};

// Preset to/from int or Iterance
int32_t getIterancePresetIndexFromValue(Iterance value);
int32_t getIterancePresetIndexFromIntValue(uint16_t value);
Iterance getIteranceValueFromPresetIndex(int32_t presetIndex);
uint16_t getIntIteranceValueFromPresetIndex(int32_t presetIndex);

// Converstion from XML parameter
Iterance convertAndSanitizeIteranceFromInt(int32_t iterance);
