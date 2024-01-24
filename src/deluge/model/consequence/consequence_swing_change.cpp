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

#include "model/consequence/consequence_swing_change.h"
#include "model/model_stack.h"
#include "model/song/song.h"

ConsequenceSwingChange::ConsequenceSwingChange(int8_t newSwingBefore, int8_t newSwingAfter) {
	swing[BEFORE] = newSwingBefore;
	swing[AFTER] = newSwingAfter;
}

int32_t ConsequenceSwingChange::revert(TimeType time, ModelStack* modelStack) {
	modelStack->song->swingAmount = swing[time];

	return NO_ERROR;
}
