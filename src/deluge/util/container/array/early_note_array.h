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

#pragma once

#include "util/container/array/ordered_resizeable_array.h"

struct EarlyNote {
	int16_t note;
	uint8_t velocity;
	bool stillActive;
};

class EarlyNoteArray : public OrderedResizeableArray {
public:
	EarlyNoteArray();

	int32_t insertElementIfNonePresent(int32_t note, int32_t velocity, bool newStillActive = true);
	void noteNoLongerActive(int32_t note);
};
