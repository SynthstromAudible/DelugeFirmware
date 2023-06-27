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

#include <MenuItemKeyRange.h>
#include "functions.h"
#include "soundeditor.h"

void MenuItemKeyRange::selectEncoderAction(int offset) {

	// If editing the range
	if (soundEditor.editingRangeEdge) {

		// Editing lower
		if (soundEditor.editingRangeEdge == RANGE_EDIT_LEFT) {

			int newValue = lower + offset;
			if (newValue < 0) newValue += 12;
			else if (newValue >= 12) newValue -= 12;

			if (offset == 1) {
				if (lower == upper) goto justDrawRange;
			}
			else {
				if (newValue == upper) goto justDrawRange;
			}

			lower = newValue;
		}

		// Editing upper
		else {

			int newValue = upper + offset;
			if (newValue < 0) newValue += 12;
			else if (newValue >= 12) newValue -= 12;

			if (offset == 1) {
				if (newValue == lower) goto justDrawRange;
			}
			else {
				if (upper == lower) goto justDrawRange;
			}

			upper = newValue;
		}

justDrawRange:
		drawValueForEditingRange(false);
	}

	else {
		if (upper != lower) return;

		lower += offset;
		if (lower < 0) lower += 12;
		else if (lower >= 12) lower -= 12;

		upper = lower;

		drawValue();
	}
}

void MenuItemKeyRange::getText(char* buffer, int* getLeftLength, int* getRightLength, bool mayShowJustOne) {

	*(buffer++) = noteCodeToNoteLetter[lower];
	int leftLength = 1;

	if (noteCodeIsSharp[lower]) {
		*(buffer++) = HAVE_OLED ? '#' : '.';
#if HAVE_OLED
		leftLength++;
#endif
	}

	if (getLeftLength) *getLeftLength = leftLength;

	if (mayShowJustOne && lower == upper) {
		*buffer = 0;
		if (getRightLength) *getRightLength = 0;
		return;
	}

	*(buffer++) = '-';

	*(buffer++) = noteCodeToNoteLetter[upper];
	int rightLength = 1;
	if (noteCodeIsSharp[upper]) {
		*(buffer++) = HAVE_OLED ? '#' : '.';
#if HAVE_OLED
		rightLength++;
#endif
	}

	*buffer = 0;

	if (getRightLength) *getRightLength = rightLength;
}

// Call seedRandom() before you call this
int MenuItemKeyRange::getRandomValueInRange() {
	if (lower == upper) {
		return lower;
	}
	else {
		int range = upper - lower;
		if (range < 0) range += 12;

		int value = lower + random(range);
		if (range >= 12) range -= 12;
		return value;
	}
}

bool MenuItemKeyRange::isTotallyRandom() {
	int range = upper - lower;
	if (range < 0) range += 12;

	return (range == 11);
}
