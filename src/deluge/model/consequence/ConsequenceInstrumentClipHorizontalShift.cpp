/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include <ConsequenceInstrumentClipHorizontalShift.h>
#include "song.h"
#include "InstrumentClip.h"
#include "ModelStack.h"

ConsequenceInstrumentClipHorizontalShift::ConsequenceInstrumentClipHorizontalShift(int32_t newAmount) {
	amount = newAmount;
}

int ConsequenceInstrumentClipHorizontalShift::revert(int time, ModelStack* modelStack) {

	int32_t amountNow = amount;

	if (time == BEFORE) {
		amountNow = -amountNow;
	}

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter =
	    modelStack->addTimelineCounter(modelStack->song->currentClip);

	((InstrumentClip*)modelStackWithTimelineCounter->getTimelineCounter())
	    ->shiftHorizontally(modelStackWithTimelineCounter, amountNow);

	return NO_ERROR;
}
