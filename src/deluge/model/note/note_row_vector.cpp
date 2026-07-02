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
#include "util/exceptions.h"
#include <utility>

NoteRow* NoteRowVector::insertNoteRowAtIndex(int32_t index) {
	if (!insertAt(index)) {
		return nullptr;
	}
	return &(*this)[index];
}

void NoteRowVector::deleteNoteRowAtIndex(int32_t startIndex, int32_t numToDelete) {
	erase(begin() + startIndex, begin() + startIndex + numToDelete); // Runs the NoteRows' destructors
}

NoteRow* NoteRowVector::insertNoteRowAtY(int32_t y, int32_t* getIndex) {
	int32_t i = firstAtOrAfter(y);
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
	// Deep copy via a copy-constructed temporary, then move it into place. (Copy-constructing avoids
	// instantiating NoteRow's deleted copy-assignment, which a direct `*this = *other` would require.) Each
	// NoteRow is element-wise copy-constructed (deep-copying its param manager + notes); leaf OOM surfaces as a
	// deluge::exception, which we turn into a false return.
	try {
		NoteRowVector copy(*other);
		*this = std::move(copy);
	} catch (deluge::exception&) {
		clear();
		return false;
	}
	return true;
}
