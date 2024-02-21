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

#include "model/clip/clip_array.h"
#include "definitions_cxx.hpp"

Error ClipArray::insertClipAtIndex(Clip* clip, int32_t index) {
	return insertPointerAtIndex(clip, index);
}

Clip* ClipArray::getClipAtIndex(int32_t index) {
	return (Clip*)getPointerAtIndex(index);
}

int32_t ClipArray::getIndexForClip(Clip* clip) {
	// For each Clip
	for (int32_t c = 0; c < numElements; c++) {
		Clip* thisClip = getClipAtIndex(c);
		if (thisClip == clip) {
			return c;
		}
	}

	return -1;
}
