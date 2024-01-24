/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "util/container/array/early_note_array.h"
#include "definitions_cxx.hpp"

EarlyNoteArray::EarlyNoteArray() : OrderedResizeableArray(sizeof(EarlyNote), 16) {
}

int32_t EarlyNoteArray::insertElementIfNonePresent(int32_t note, int32_t velocity, bool newStillActive) {

	int32_t i = search(note, GREATER_OR_EQUAL);

	EarlyNote* earlyNote;

	if (i >= getNumElements()) {
doInsert:
		int32_t error = insertAtIndex(i);
		if (error) {
			return error;
		}
		earlyNote = (EarlyNote*)getElementAddress(i);
		earlyNote->note = note;
	}
	else {
		earlyNote = (EarlyNote*)getElementAddress(i);
		if ((int32_t)earlyNote->note != note) {
			goto doInsert;
		}
	}

	earlyNote->velocity = velocity;
	earlyNote->stillActive = newStillActive;

	return NO_ERROR;
}

void EarlyNoteArray::noteNoLongerActive(int32_t note) {
	int32_t i = searchExact(note);

	if (i != -1) {
		EarlyNote* earlyNote = (EarlyNote*)getElementAddress(i);
		earlyNote->stillActive = false;
	}
}
