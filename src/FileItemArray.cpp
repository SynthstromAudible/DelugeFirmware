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

#include "FileItemArray.h"
#include "GeneralMemoryAllocator.h"
#include <string.h>
#include "DString.h"
#include "functions.h"
#include "numericdriver.h"
#include "AudioEngine.h"

inline FileItem* FileItemArray::getFileItem(int index) {
	return (FileItem*)getElementAddress(index);
}

int strcmpfileitem(FileItem* a, FileItem* b) {

#ifdef FEATURE_SORT_FOLDERS_FIRST
#pragma message "Browser will alphabetize folders first, then files"
	if (a->isFolder != b->isFolder)
		return a->isFolder ? -1 : 1;
#else
#pragma message "Browser will alphabetize files and folders equally"
#endif

	return strcmpspecial(a->filename.get(), b->filename.get(), true);
}

// This uses Hoare's partitioning scheme, which has the advantage that it won't go slow if the elements are already sorted - which they often will be as filenames read off an SD card.
int FileItemArray::partition(int low, int high) {

	// Pivot - rightmost element. Though this is very bad if the array is already sorted...
	FileItem* pivotFile = (FileItem*)getElementAddress((low + high) >> 1);

	int i = low - 1;
	int j = high + 1;

	while (true) {
		do {
			i++;
		} while (strcmpfileitem(getFileItem(i), pivotFile) < 0);

		do {
			j--;
		} while (strcmpfileitem(getFileItem(j), pivotFile) > 0);

		if (i >= j) return j;

		swapElements(i, j);
	}
}

void FileItemArray::quickSort(int low, int high) {
	while (low < high) {
		/* pi is partitioning index, arr[p] is now
        at right place */
		int pi = FileItemArray::partition(low, high);

		// Separately sort elements before partition and after partition.
		// The recursive call gets done on the smaller of those two regions
		// - otherwise the worst case for the number of recursions of this function is the number of elements, which overflows the stack like crazy!

		// If most elements to the left of the pi...
		if ((pi << 1) >= (low + high)) {
			quickSort(pi + 1, high);
			high = pi; // If we weren't using Hoare's partitioning scheme, this would be pi - 1.
		}

		// Or if most elements to the right of the pi...
		else {
			quickSort(low, pi); // If we weren't using Hoare's partitioning scheme, this would be pi - 1.
			low = pi + 1;
		}
	}
}

void FileItemArray::sort() {
	if (numElements < 2) return;

	quickSort(0, numElements - 1);
}

// Array must be sorted before you call this.
int FileItemArray::search(char const* searchString, bool* foundExact) {

	int rangeBegin = 0;
	int rangeEnd = numElements;
	int proposedIndex;

	while (rangeBegin != rangeEnd) {
		int rangeSize = rangeEnd - rangeBegin;
		proposedIndex = rangeBegin + (rangeSize >> 1);

		char const* stringHere = ((FileItem*)getElementAddress(proposedIndex))->filename.get();
		int result = strcmpspecial(stringHere, searchString, true);

		if (!result) {
			if (foundExact) *foundExact = true;
			return proposedIndex;
		}
		else if (result < 0) rangeBegin = proposedIndex + 1;
		else rangeEnd = proposedIndex;
	}

	if (foundExact) *foundExact = false;
	return rangeBegin;
}
