/*
 * Copyright © 2022-2023 Synthstrom Audible Limited
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

#include "util/container/array/c_string_array.h"
#include "memory/general_memory_allocator.h"
#include <string.h>
#include "util/d_string.h"
#include "util/functions.h"
#include "hid/display/numeric_driver.h"
#include "processing/engines/audio_engine.h"

int workCount;

// This uses Hoare's partitioning scheme, which has the advantage that it won't go slow if the elements are already sorted - which they often will be as filenames read off an SD card.
// You must set shouldInterpretNoteNames and octaveStartsFromA before calling this.
int CStringArray::partitionForStrings(int low, int high) {
	char const* pivotString = *(char const**)getElementAddress(
	    (low + high) >> 1); // Pivot - rightmost element. Though this is very bad if the array is already sorted...

	int i = low - 1;
	int j = high + 1;

	while (true) {
		do {
			i++;
		} while (strcmpspecial(*(char const**)getElementAddress(i), pivotString) < 0);

		do {
			j--;
		} while (strcmpspecial(*(char const**)getElementAddress(j), pivotString) > 0);

		if (i >= j) {
			return j;
		}

		swapElements(i, j);
	}
}

// You must set shouldInterpretNoteNames and octaveStartsFromA before calling this.
void CStringArray::quickSortForStrings(int low, int high) {
	while (low < high) {
		/* pi is partitioning index, arr[p] is now
        at right place */
		int pi = partitionForStrings(low, high);

		// Separately sort elements before partition and after partition.
		// The recursive call gets done on the smaller of those two regions
		// - otherwise the worst case for the number of recursions of this function is the number of elements, which overflows the stack like crazy!

		// If most elements to the left of the pi...
		if ((pi << 1) >= (low + high)) {
			quickSortForStrings(pi + 1, high);
			high = pi; // If we weren't using Hoare's partitioning scheme, this would be pi - 1.
		}

		// Or if most elements to the right of the pi...
		else {
			quickSortForStrings(low, pi); // If we weren't using Hoare's partitioning scheme, this would be pi - 1.
			low = pi + 1;
		}
	}
}

// You must set shouldInterpretNoteNames and octaveStartsFromA before calling this.
void CStringArray::sortForStrings() {
	if (numElements < 2) {
		return;
	}

	workCount = 0;
	quickSortForStrings(0, numElements - 1);
}

// Array must be sorted before you call this.
// You must set shouldInterpretNoteNames and octaveStartsFromA before calling this.
int CStringArray::search(char const* searchString, bool* foundExact) {

	int rangeBegin = 0;
	int rangeEnd = numElements;
	int proposedIndex;

	while (rangeBegin != rangeEnd) {
		int rangeSize = rangeEnd - rangeBegin;
		proposedIndex = rangeBegin + (rangeSize >> 1);

		char const* stringHere = *(char const**)getElementAddress(proposedIndex);
		int result = strcmpspecial(stringHere, searchString);

		if (!result) {
			if (foundExact) {
				*foundExact = true;
			}
			return proposedIndex;
		}
		else if (result < 0) {
			rangeBegin = proposedIndex + 1;
		}
		else {
			rangeEnd = proposedIndex;
		}
	}

	if (foundExact) {
		*foundExact = false;
	}
	return rangeBegin;
}
