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

#include <MultiRangeArray.h>
#include "MultisampleRange.h"
#include "MultiWaveTableRange.h"
#include <new>
#include "numericdriver.h"

MultiRangeArray::MultiRangeArray()
    : OrderedResizeableArray(sizeof(MultisampleRange), 16, __builtin_offsetof(MultiRange, topNote), 0, 0) {
}

// This function could sorta be done without...
MultiRange* MultiRangeArray::getElement(int i) {
	return (MultiRange*)getElementAddress(i);
}

MultiRange* MultiRangeArray::insertMultiRange(int i) {
	int error = insertAtIndex(i);
	if (error) return NULL;
	void* memory = getElementAddress(i);
	MultiRange* range;

	if (elementSize == sizeof(MultisampleRange)) {
		range = new (memory) MultisampleRange();
	}
	else {
		range = new (memory) MultiWaveTableRange();
	}
	return range;
}

int MultiRangeArray::changeType(int newSize) {

	if (!numElements) {
		elementSize = newSize;
		return NO_ERROR;
	}

	MultiRangeArray newArray;
	newArray.elementSize = newSize;
	int error = newArray.insertAtIndex(0, numElements);
	if (error) return error;

	// We're changing range types, but want to preserve their topNotes.
	for (int i = 0; i < numElements; i++) {
		MultiRange* oldRange = (MultiRange*)getElementAddress(i);
		void* newMemory = newArray.getElementAddress(i);

		MultiRange* newRange;

		if (newSize == sizeof(MultisampleRange)) {
			newRange = new (newMemory) MultisampleRange();
		}
		else {
			newRange = new (newMemory) MultiWaveTableRange();
		}

		newRange->topNote = oldRange->topNote;

		oldRange
		    ->~MultiRange(); // Always have to do this manually - the Array doesn't otherwise take care of destructing these.
	}

	empty();

	elementSize = newSize;

	swapStateWith(&newArray);
	return NO_ERROR;
}
