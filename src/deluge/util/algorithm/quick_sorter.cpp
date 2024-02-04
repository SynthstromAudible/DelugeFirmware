/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

/*
 * Basic code initially copied from https://gist.github.com/adwiteeya3/f1797534506be672b591f465c3366643
 */

#include "util/algorithm/quick_sorter.h"
#include "hid/display/display.h"
#include <string.h>

QuickSorter::QuickSorter(int32_t newElementSize, int32_t keyNumBits, void* newMemory)
    : elementSize(newElementSize), memory(newMemory), keyMask(0xFFFFFFFF >> (32 - keyNumBits)) {
}

void* QuickSorter::getElementAddress(int32_t i) {
	return (void*)((uint32_t)memory + i * elementSize);
}

// GCC doesn't like that workingMemory would be unbounded if elementSize is unbounded
#pragma GCC push
#pragma GCC diagnostic ignored "-Wstack-usage="

// A utility function to swap two elements
void QuickSorter::swap(int32_t i, int32_t j) {
	void* addressI = getElementAddress(i);
	void* addressJ = getElementAddress(j);
	char temp[elementSize];
	memcpy(temp, addressI, elementSize);
	memcpy(addressI, addressJ, elementSize);
	memcpy(addressJ, temp, elementSize);
}
#pragma GCC pop
int32_t QuickSorter::getKey(int32_t i) {
	return *(uint32_t*)getElementAddress(i) & keyMask;
}

/* This function takes last element as pivot, places
the pivot element at its correct position in sorted
array, and places all smaller (smaller than pivot)
to left of pivot and all greater elements to right
of pivot */
int32_t QuickSorter::partition(int32_t low, int32_t high) {
	int32_t pivot = getKey(high); // pivot
	int32_t i = (low - 1);        // Index of smaller element

	for (int32_t j = low; j <= high - 1; j++) {
		// If current element is smaller than the pivot
		if (getKey(j) < pivot) {
			i++; // increment index of smaller element
			swap(i, j);
		}
	}
	swap(i + 1, high);
	return (i + 1);
}

/* The main function that implements QuickSort
low --> Starting index,
high --> Ending index */
void QuickSorter::quickSort(int32_t low, int32_t high) {
	if (low < high) {
		/* pi is partitioning index, arr[p] is now
		at right place */
		int32_t pi = partition(low, high);

		// Separately sort elements before
		// partition and after partition
		quickSort(low, pi - 1);
		quickSort(pi + 1, high);
	}
}

// Make sure numElements isn't 0 before you call this! And you should also make sure it's not just 1 before you bother
// calling.
void QuickSorter::sort(int32_t numElements) {
	quickSort(0, numElements - 1);

#if ALPHA_OR_BETA_VERSION
	int32_t lastKey = getKey(0);
	for (int32_t i = 1; i < numElements; i++) {
		int32_t keyHere = getKey(i);
		if (keyHere < lastKey) {
			FREEZE_WITH_ERROR("SORT");
		}
		lastKey = keyHere;
	}
#endif
}
