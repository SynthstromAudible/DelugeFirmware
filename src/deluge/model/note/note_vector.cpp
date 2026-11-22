/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "model/note/note_vector.h"
#include "model/note/note.h"
#include <cstring>

NoteVector::NoteVector() : OrderedResizeableArrayWith32bitKey(sizeof(Note)) {
}

Note* NoteVector::getElement(int32_t index) {
	if (index < 0 || index >= getNumElements()) {
		return nullptr;
	}
	return (Note*)getElementAddress(index);
}

Note* NoteVector::getLast() {
	return getElement(getNumElements() - 1);
}

Error NoteVector::insertAtIndex(int32_t i, int32_t numToInsert, void* thingNotToStealFrom) {
	Error error = OrderedResizeableArrayWith32bitKey::insertAtIndex(i, numToInsert, thingNotToStealFrom);
	if (error == Error::NONE) {
		for (int32_t n = i; n < i + numToInsert; n++) {
			Note* note = static_cast<Note*>(getElementAddress(n));
			note->lockSet = 0;
			note->anchorMarker = AnchorMarker::derived();
		}
	}
	return error;
}

int32_t NoteVector::insertAtKey(int32_t key, bool isDefinitelyLast) {
	int32_t i = isDefinitelyLast ? numElements : search(key, GREATER_OR_EQUAL);
	if (insertAtIndex(i) != Error::NONE) {
		return -1;
	}
	setKeyAtIndex(key, i);
	return i;
}
