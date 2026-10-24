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
#include "lib/printf.h"
#include "util/lookuptables/lookuptables.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

char const* get_first_label(Iterance::DisplayLabelType label_type) {
	return label_type == Iterance::DisplayLabelType::LONG ? "FIRST" : display->haveOLED() ? "1ST" : "1 ST";
}

int32_t get_active_step_display_number(Iterance const& iterance) {
	if (iterance.divisor == 0) {
		return iterance.iteranceStep[0] ? 1 : 0;
	}

	for (int32_t i = iterance.divisor - 1; i >= 0; i--) {
		if (iterance.iteranceStep[i]) {
			return i + 1;
		}
	}

	return 0;
}

void copy_with_prefix(char* buffer, size_t buffer_size, char const* prefix, char const* value) {
	if (buffer_size == 0) {
		return;
	}
	strcpy(buffer, prefix);
	strcat(buffer, value);
}

} // namespace

// To/from int

uint16_t Iterance::toInt() const {
	return (uint16_t)(divisor << 8) | (uint16_t)(iteranceStep.to_ulong() & 0xFF);
}

Iterance Iterance::fromInt(int32_t value) {
	auto divisor = (uint8_t)(value >> 8);
	auto step = (uint8_t)(value & 0xFF);
	// guard against garbage
	if ((divisor == 0 and step != 1 and step != 2) or divisor > 8) {
		return kDefaultIteranceValue;
	}
	return Iterance{.divisor = divisor, .iteranceStep = step};
}

// To/from preset index

// This methods takes the iterance value and searches the table of iterance presets for a match
// If no match is found it will return kCustomIterancePreset (which is equal to '1of1')
int32_t Iterance::toPresetIndex() const {
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

void Iterance::format_display_value(char* buffer, size_t buffer_size, DisplayOptions options) const {
	format_preset_display_value(toPresetIndex(), buffer, buffer_size, options);
}

void Iterance::format_preset_display_value(int32_t preset_index, char* buffer, size_t buffer_size,
                                           DisplayOptions options) {
	if (preset_index == kDefaultIterancePreset) {
		copy_with_prefix(buffer, buffer_size, options.prefix, "OFF");
	}
	else if (preset_index == kCustomIterancePreset) {
		copy_with_prefix(buffer, buffer_size, options.prefix, "CUSTOM");
	}
	else if (options.label_type != DisplayLabelType::NUMERIC && preset_index == kFirstIterancePreset) {
		copy_with_prefix(buffer, buffer_size, options.prefix, get_first_label(options.label_type));
	}
	else if (options.label_type != DisplayLabelType::NUMERIC && preset_index == kLastIterancePreset) {
		copy_with_prefix(buffer, buffer_size, options.prefix, "LAST");
	}
	else if (preset_index > 0 && preset_index <= kNumIterancePresets) {
		Iterance iterance = iterancePresets[preset_index - 1];
		char value_buffer[20];
		sprintf(value_buffer, options.step_format, get_active_step_display_number(iterance), iterance.divisor);
		copy_with_prefix(buffer, buffer_size, options.prefix, value_buffer);
	}
	else {
		copy_with_prefix(buffer, buffer_size, options.prefix, "OFF");
	}
}
