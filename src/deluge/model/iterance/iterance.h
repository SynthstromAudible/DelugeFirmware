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
#include <cstddef>
#include <cstdint>

/// Represents first, last, or an x of y iteration
/// If divisor is zero then instead represents either first or last
struct Iterance {
	enum class DisplayLabelType : uint8_t {
		NUMERIC,
		SHORT,
		LONG,
	};

	struct DisplayOptions {
		char const* step_format;
		char const* prefix = "";
		DisplayLabelType label_type = DisplayLabelType::SHORT;
	};

	uint8_t divisor;
	std::bitset<8> iteranceStep;

	bool operator==(Iterance const& other) const {
		return other.divisor == divisor && other.iteranceStep == iteranceStep;
	}

	[[nodiscard]] bool passesCheck(int32_t repeatCount, bool ending) const {
		// these aren't valid divisors so overloaded to be first/last instead
		if (divisor == 0) {
			if (iteranceStep == 1) {
				return repeatCount == 0;
			}
			else {
				return ending;
			}
		}
		return iteranceStep[repeatCount % divisor];
	}

	[[nodiscard]] uint16_t toInt() const;
	static Iterance fromInt(int32_t value);

	[[nodiscard]] int32_t toPresetIndex() const;
	static Iterance fromPresetIndex(int32_t presetIndex);

	void format_display_value(char* buffer, size_t buffer_size, DisplayOptions options) const;
	static void format_preset_display_value(int32_t preset_index, char* buffer, size_t buffer_size,
	                                        DisplayOptions options);
};
