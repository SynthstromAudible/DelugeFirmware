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

#include "storage/multi_range/multi_range_array.h"
#include "hid/display/display.h"
#include "storage/multi_range/multi_wave_table_range.h"
#include "storage/multi_range/multisample_range.h"
#include <new>

#pragma GCC diagnostic push
// This is supported by GCC and other compilers should error (not warn), so turn off for this
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

MultiRangeArray::MultiRangeArray()
    : OrderedResizeableArray(sizeof(MultisampleRange), 16, __builtin_offsetof(MultiRange, topNote), 0, 0) {
}
#pragma GCC diagnostic pop

// This function could sorta be done without...
MultiRange* MultiRangeArray::getElement(int32_t i) {
	return (MultiRange*)getElementAddress(i);
}

MultiRange* MultiRangeArray::insertMultiRange(int32_t i) {
	int32_t error = insertAtIndex(i);
	if (error) {
		return NULL;
	}
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

int32_t MultiRangeArray::changeType(int32_t newSize) {

	if (!numElements) {
		elementSize = newSize;
		return NO_ERROR;
	}

	MultiRangeArray newArray;
	newArray.elementSize = newSize;
	int32_t error = newArray.insertAtIndex(0, numElements);
	if (error) {
		return error;
	}

	// We're changing range types, but want to preserve their topNotes.
	for (int32_t i = 0; i < numElements; i++) {
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

		oldRange->~MultiRange(); // Always have to do this manually - the Array doesn't otherwise take care of
		                         // destructing these.
	}

	empty();

	elementSize = newSize;

	swapStateWith(&newArray);
	return NO_ERROR;
}
