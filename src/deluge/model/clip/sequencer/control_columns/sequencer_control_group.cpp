/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "model/clip/sequencer/control_columns/sequencer_control_group.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/pad_leds.h"
#include "model/clip/sequencer/sequencer_mode.h"
#include <cstring>

namespace deluge::model::clip::sequencer {

// Constants
namespace {
// Available values for each control type
constexpr int32_t kClockDivValues[] = {-2, // *2 (negative = multiply)
                                       1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
                                       17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
                                       33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
                                       49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64};

constexpr int32_t kOctaveValues[] = {-5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5};

constexpr int32_t kTransposeValues[] = {-12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0,
                                        1,   2,   3,   4,  5,  6,  7,  8,  9,  10, 11, 12};

constexpr int32_t kSceneValues[] = {0, 1, 2, 3, 4, 5, 6, 7};

constexpr int32_t kDirectionValues[] = {0, 1, 2, 3, 4, 5, 6, 7}; // Forward, Backward, Ping Pong, Random, Pedal, Skip 2, Pendulum, Spiral

// Mutation intensity values (0-100%)
constexpr int32_t kMutationValues[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

// Helper: Format signed integer with + or - prefix
void formatSignedInt(char* buffer, int32_t value) {
	if (value >= 0) {
		snprintf(buffer, 16, "+%d", value);
	}
	else {
		snprintf(buffer, 16, "%d", value);
	}
}

// Direction mode names lookup table
constexpr const char* kDirectionNames[] = {"FWD", "BACK", "PING", "RAND", "PEDAL", "SKIP2", "PEND", "SPIR"};
} // namespace

// ========== HELPER FUNCTIONS FOR INDIVIDUAL PAD CONTROL ==========

namespace helpers {

const char* getTypeName(ControlType type) {
	switch (type) {
	case ControlType::NONE:
		return "NONE";
	case ControlType::CLOCK_DIV:
		return "CLOCK";
	case ControlType::OCTAVE:
		return "OCTAVE";
	case ControlType::TRANSPOSE:
		return "TRANSPOSE";
	case ControlType::SCENE:
		return "SCENE";
	case ControlType::DIRECTION:
		return "DIRECTION";
	case ControlType::RESET:
		return "RESET";
	case ControlType::RANDOM:
		return "RANDOM";
	case ControlType::EVOLVE:
		return "EVOLVE";
	default:
		return "UNKNOWN";
	}
}

RGB getColorForType(ControlType type) {
	switch (type) {
	case ControlType::NONE:
		return RGB{0, 0, 0}; // Black/off
	case ControlType::CLOCK_DIV:
		return RGB{255, 0, 0}; // Red
	case ControlType::OCTAVE:
		return RGB{255, 128, 0}; // Orange
	case ControlType::TRANSPOSE:
		return RGB{255, 255, 0}; // Yellow
	case ControlType::SCENE:
		return RGB{0, 128, 255}; // Blue
	case ControlType::DIRECTION:
		return RGB{0, 200, 255}; // Cyan
	case ControlType::RESET:
		return RGB{100, 150, 255}; // Light blue
	case ControlType::RANDOM:
		return RGB{255, 100, 255}; // Magenta
	case ControlType::EVOLVE:
		return RGB{255, 150, 200}; // Pink
	default:
		return RGB{128, 128, 128}; // Gray
	}
}

const int32_t* getAvailableValues(ControlType type) {
	switch (type) {
	case ControlType::CLOCK_DIV:
		return kClockDivValues;
	case ControlType::OCTAVE:
		return kOctaveValues;
	case ControlType::TRANSPOSE:
		return kTransposeValues;
	case ControlType::SCENE:
		return kSceneValues;
	case ControlType::DIRECTION:
		return kDirectionValues;
	case ControlType::RANDOM:
	case ControlType::EVOLVE:
		return kMutationValues;
	default:
		return nullptr;
	}
}

int32_t getNumAvailableValues(ControlType type) {
	switch (type) {
	case ControlType::CLOCK_DIV:
		return sizeof(kClockDivValues) / sizeof(kClockDivValues[0]);
	case ControlType::OCTAVE:
		return sizeof(kOctaveValues) / sizeof(kOctaveValues[0]);
	case ControlType::TRANSPOSE:
		return sizeof(kTransposeValues) / sizeof(kTransposeValues[0]);
	case ControlType::SCENE:
		return sizeof(kSceneValues) / sizeof(kSceneValues[0]);
	case ControlType::DIRECTION:
		return sizeof(kDirectionValues) / sizeof(kDirectionValues[0]);
	case ControlType::RANDOM:
	case ControlType::EVOLVE:
		return sizeof(kMutationValues) / sizeof(kMutationValues[0]);
	default:
		return 0;
	}
}

int32_t getValue(ControlType type, int32_t valueIndex) {
	const int32_t* values = getAvailableValues(type);
	int32_t numValues = getNumAvailableValues(type);

	if (!values || numValues == 0) {
		return 0;
	}

	if (valueIndex < 0 || valueIndex >= numValues) {
		return values[0];
	}

	return values[valueIndex];
}

const char* formatValue(ControlType type, int32_t value) {
	static char buffer[16];

	switch (type) {
	case ControlType::CLOCK_DIV:
		if (value < 0) {
			snprintf(buffer, sizeof(buffer), "*%d", -value);
		}
		else {
			snprintf(buffer, sizeof(buffer), "/%d", value);
		}
		return buffer;

	case ControlType::OCTAVE:
	case ControlType::TRANSPOSE:
		formatSignedInt(buffer, value);
		return buffer;

	case ControlType::SCENE:
		snprintf(buffer, sizeof(buffer), "%d", value + 1);
		return buffer;

	case ControlType::DIRECTION:
		if (value >= 0 && value < 8) {
			return kDirectionNames[value];
		}
		return "?";

	case ControlType::RANDOM:
	case ControlType::EVOLVE:
		snprintf(buffer, sizeof(buffer), "%d%%", value);
		return buffer;

	case ControlType::RESET:
		return ""; // No value for reset

	default:
		return "?";
	}
}

} // namespace helpers

} // namespace deluge::model::clip::sequencer
