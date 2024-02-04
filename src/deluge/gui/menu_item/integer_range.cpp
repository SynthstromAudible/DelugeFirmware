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

#include "integer_range.h"
#include "gui/menu_item/range.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "util/functions.h"

namespace deluge::gui::menu_item {

void IntegerRange::beginSession(MenuItem* navigatedBackwardFrom) {
	Range::beginSession(navigatedBackwardFrom);
	if (display->haveOLED()) {
		if (lower != upper) {
			soundEditor.editingRangeEdge = RangeEdit::LEFT;
		}
	}
}

void IntegerRange::selectEncoderAction(int32_t offset) {

	// If editing the range
	if (soundEditor.editingRangeEdge != RangeEdit::OFF) {

		// Editing lower
		if (soundEditor.editingRangeEdge == RangeEdit::LEFT) {
			lower = std::clamp(lower + offset, minValue, maxValue);
			if (upper < lower) {
				upper = lower;
			}
		}

		// Editing upper
		else {
			upper = std::clamp(upper + offset, minValue, maxValue);
			if (upper < lower) {
				lower = upper;
			}
		}

		drawValueForEditingRange(false);
	}

	else {
		if (upper != lower) {
			return;
		}

		lower = std::clamp(lower + offset, minValue, maxValue);
		upper = lower;

justDrawOneNumber:
		drawValue();
	}
}

void IntegerRange::getText(char* buffer, int32_t* getLeftLength, int32_t* getRightLength, bool mayShowJustOne) {

	intToString(lower, buffer);

	int32_t leftLength = strlen(buffer);
	if (getLeftLength) {
		*getLeftLength = leftLength;
	}

	if (mayShowJustOne && lower == upper) {
		if (getRightLength) {
			*getRightLength = 0;
		}
		return;
	}

	char* bufferPos = buffer + leftLength;

	*(bufferPos++) = '-';

	intToString(upper, bufferPos);

	if (getRightLength) {
		*getRightLength = strlen(bufferPos);
	}
}

// Call seedRandom() before you call this
int32_t IntegerRange::getRandomValueInRange() {
	if (lower == upper) {
		return lower;
	}
	else {
		return lower + random(upper - lower);
	}
}
} // namespace deluge::gui::menu_item
