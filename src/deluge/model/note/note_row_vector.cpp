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

#include "model/note/note_row_vector.h"
#include "model/note/note_row.h"
#include "processing/engines/audio_engine.h"
#include <new>

NoteRowVector::NoteRowVector() : OrderedResizeableArray(sizeof(NoteRow), 16, 0, 16, 7) {
}

NoteRowVector::~NoteRowVector() {
	for (int32_t i = 0; i < numElements; i++) {
		AudioEngine::routineWithClusterLoading();

		getElement(i)->~NoteRow();
	}
}

NoteRow* NoteRowVector::insertNoteRowAtIndex(int32_t index) {
	Error error = insertAtIndex(index);
	if (error != Error::NONE) {
		return NULL;
	}
	void* memory = getElementAddress(index);

	return new (memory) NoteRow();
}

void NoteRowVector::deleteNoteRowAtIndex(int32_t startIndex, int32_t numToDelete) {
	for (int32_t i = startIndex; i < startIndex + numToDelete; i++) {
		getElement(i)->~NoteRow();
	}
	deleteAtIndex(startIndex, numToDelete);
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

// Function could theoretically be gotten rid of
NoteRow* NoteRowVector::getElement(int32_t index) {
	return (NoteRow*)getElementAddress(index);
}
