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

#pragma once

#include <cstdint>

class QuickSorter {
public:
	QuickSorter(int32_t newElementSize, int32_t keyNumBits, void* newMemory);
	void sort(int32_t numElements);

private:
	void quickSort(int32_t low, int32_t high);
	int32_t partition(int32_t low, int32_t high);
	int32_t getKey(int32_t i);
	void swap(int32_t i, int32_t j);
	void* getElementAddress(int32_t i);

	const int32_t elementSize;
	const uint32_t keyMask;
	void* const memory;
};
