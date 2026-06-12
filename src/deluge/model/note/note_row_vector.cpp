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

#include "model/note/note_row_vector.h"
#include <cstring>
#include <new>

NoteRow* NoteRowVector::insertNoteRowAtIndex(int32_t index) {
	Error error = insertAtIndex(index);
	if (error != Error::NONE) {
		return nullptr;
	}
	return &(*this)[index];
}

void NoteRowVector::deleteNoteRowAtIndex(int32_t startIndex, int32_t numToDelete) {
	deleteAtIndex(startIndex, numToDelete); // Runs the NoteRows' destructors
}

NoteRow* NoteRowVector::insertNoteRowAtY(int32_t y, int32_t* getIndex) {
	int32_t i = search(y, GREATER_OR_EQUAL);
	NoteRow* noteRow = insertNoteRowAtIndex(i);
	if (noteRow) {
		if (getIndex) {
			*getIndex = i;
		}
		noteRow->y = y;
	}
	return noteRow;
}

bool NoteRowVector::cloneFrom(NoteRowVector const* other) {
	empty();
	if (!ensureEnoughSpaceAllocated(other->getNumElements())) {
		return false;
	}
	for (int32_t i = 0; i < other->getNumElements(); i++) {
		insertAtIndex(getNumElements()); // Can't fail; space reserved above
		// Byte-copy over the freshly default-constructed row. The default row owns nothing, so nothing leaks;
		// the copy aliases the source row's guts until beenCloned() gives it its own.
		std::memcpy(static_cast<void*>(&(*this)[i]), static_cast<void const*>(&(*other)[i]), sizeof(NoteRow));
	}
	return true;
}
