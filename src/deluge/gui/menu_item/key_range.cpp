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

#include "key_range.h"
#include "definitions_cxx.hpp"
#include "gui/menu_item/range.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "storage/flash_storage.h"
#include "util/functions.h"

namespace deluge::gui::menu_item {

void KeyRange::selectEncoderAction(int32_t offset) {
	int32_t const KEY_MIN = 0;
	int32_t const KEY_MAX = 11;

	// If editing the range
	if (soundEditor.editingRangeEdge != RangeEdit::OFF) {

		// Editing lower
		if (soundEditor.editingRangeEdge == RangeEdit::LEFT) {
			// Do not allow lower to pass upper
			lower = std::clamp(lower + offset, KEY_MIN, upper);
		}
		// Editing upper
		else {
			upper = std::clamp(upper + offset, lower, KEY_MAX);
		}

		drawValueForEditingRange(false);
	}

	else {
		if (upper != lower) {
			return;
		}

		lower = std::clamp(lower + offset, KEY_MIN, KEY_MAX);
		upper = lower;

		drawValue();
	}
}

void KeyRange::getText(char* buffer, int32_t* getLeftLength, int32_t* getRightLength, bool mayShowJustOne) {

	bool useSharps = FlashStorage::defaultUseSharps;
	char accidential = useSharps ? '#' : FLAT_CHAR;

	*(buffer++) = useSharps ? noteCodeToNoteLetter[lower] : noteCodeToNoteLetterFlats[lower];

	int32_t leftLength = 1;

	if (noteCodeIsSharp[lower]) {
		*(buffer++) = (display->haveOLED()) ? accidential : '.';
		if (display->haveOLED()) {
			leftLength++;
		}
	}

	if (getLeftLength) {
		*getLeftLength = leftLength;
	}

	if (mayShowJustOne && lower == upper) {
		*buffer = 0;
		if (getRightLength) {
			*getRightLength = 0;
		}
		return;
	}

	*(buffer++) = '-';

	*(buffer++) = useSharps ? noteCodeToNoteLetter[upper] : noteCodeToNoteLetterFlats[upper];
	int32_t rightLength = 1;
	if (noteCodeIsSharp[upper]) {
		*(buffer++) = (display->haveOLED()) ? accidential : '.';
		if (display->haveOLED()) {
			rightLength++;
		}
	}

	*buffer = 0;

	if (getRightLength) {
		*getRightLength = rightLength;
	}
}

// Call seedRandom() before you call this
int32_t KeyRange::getRandomValueInRange() {
	if (lower == upper) {
		return lower;
	}
	else {
		int32_t range = upper - lower;
		if (range < 0) {
			range += 12;
		}

		int32_t value = lower + random(range);
		if (range >= 12) {
			range -= 12;
		}
		return value;
	}
}

bool KeyRange::isTotallyRandom() {
	int32_t range = upper - lower;
	if (range < 0) {
		range += 12;
	}

	return (range == 11);
}
} // namespace deluge::gui::menu_item
