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

#include <MenuItemIntegerRange.h>
#include "functions.h"
#include "soundeditor.h"

extern "C" {
#include "cfunctions.h"
}

MenuItemIntegerRange::MenuItemIntegerRange(char const* newName, int newMin, int newMax) : MenuItemRange(newName) {
	minValue = newMin;
	maxValue = newMax;
}

void MenuItemIntegerRange::beginSession(MenuItem* navigatedBackwardFrom) {
	MenuItemRange::beginSession(navigatedBackwardFrom);
#if HAVE_OLED
	if (lower != upper) {
		soundEditor.editingRangeEdge = RANGE_EDIT_LEFT;
	}
#endif
}

void MenuItemIntegerRange::selectEncoderAction(int offset) {

	// If editing the range
	if (soundEditor.editingRangeEdge) {

		// Editing lower
		if (soundEditor.editingRangeEdge == RANGE_EDIT_LEFT) {
			if (offset == 1) {
				if (lower == upper) {
					if (upper >= maxValue) goto justDrawRange;
					else upper++;
				}
			}
			else {
				if (lower <= minValue) goto justDrawRange;
			}

			lower += offset;
		}

		// Editing upper
		else {
			if (offset == 1) {
				if (upper >= maxValue) goto justDrawRange;
			}
			else {
				if (upper == lower) {
					if (lower <= minValue) goto justDrawRange;
					else lower--;
				}
			}

			upper += offset;
		}

justDrawRange:
		drawValueForEditingRange(false);
	}

	else {
		if (upper != lower) return;

		if (offset == 1) {
			if (lower == maxValue) goto justDrawOneNumber;
		}
		else {
			if (lower == minValue) goto justDrawOneNumber;
		}

		lower += offset;
		upper = lower;

justDrawOneNumber:
		drawValue();
	}
}

void MenuItemIntegerRange::getText(char* buffer, int* getLeftLength, int* getRightLength, bool mayShowJustOne) {

	intToString(lower, buffer);

	int leftLength = strlen(buffer);
	if (getLeftLength) *getLeftLength = leftLength;

	if (mayShowJustOne && lower == upper) {
		if (getRightLength) *getRightLength = 0;
		return;
	}

	char* bufferPos = buffer + leftLength;

	*(bufferPos++) = '-';

	intToString(upper, bufferPos);

	if (getRightLength) *getRightLength = strlen(bufferPos);
}

// Call seedRandom() before you call this
int MenuItemIntegerRange::getRandomValueInRange() {
	if (lower == upper) {
		return lower;
	}
	else {
		return lower + random(upper - lower);
	}
}
