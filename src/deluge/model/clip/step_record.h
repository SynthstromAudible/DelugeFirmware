/*
 * Copyright © 2026
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

/// MIDI note numbers range from 0 to 127.
constexpr uint8_t kNumMidiNotes = 128;

/// Advance the step record cursor by `by` ticks, wrapping at the end of the clip loop back to the start.
///
/// Pure function so the wrap semantics are unit-testable without the model layer.
inline constexpr int32_t stepRecordAdvancePosition(int32_t currentPos, int32_t by, int32_t loopLength) {
	if (loopLength <= 0) {
		return currentPos;
	}
	int32_t newPos = currentPos + by;
	if (newPos >= loopLength) {
		newPos = newPos % loopLength;
	}
	return newPos;
}
