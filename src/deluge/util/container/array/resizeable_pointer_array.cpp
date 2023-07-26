/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "util/container/array/resizeable_pointer_array.h"
#include "definitions_cxx.hpp"

ResizeablePointerArray::ResizeablePointerArray() : ResizeableArray(sizeof(void*)) {
}

int ResizeablePointerArray::insertPointerAtIndex(void* pointer, int index) {
	int error = insertAtIndex(index);
	if (error) {
		return error;
	}
	*(void**)getElementAddress(index) = pointer;
	return NO_ERROR;
}

void* ResizeablePointerArray::getPointerAtIndex(int index) {
	return *(void**)getElementAddress(index);
}

void ResizeablePointerArray::setPointerAtIndex(void* pointer, int index) {
	*(void**)getElementAddress(index) = pointer;
}
