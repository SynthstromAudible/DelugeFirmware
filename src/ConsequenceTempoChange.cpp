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

#include "ConsequenceTempoChange.h"

#include "song.h"
#include "playbackhandler.h"
#include "ModelStack.h"

ConsequenceTempoChange::ConsequenceTempoChange(uint64_t newTimePerBigBefore, uint64_t newTimePerBigAfter) {
	timePerBig[BEFORE] = newTimePerBigBefore;
	timePerBig[AFTER] = newTimePerBigAfter;
}

int ConsequenceTempoChange::revert(int time, ModelStack* modelStack) {
	float oldBPM = playbackHandler.calculateBPM(modelStack->song->getTimePerTimerTickFloat());

	modelStack->song->setTimePerTimerTick(timePerBig[time], false);

	float newBPM = playbackHandler.calculateBPM(modelStack->song->getTimePerTimerTickFloat());

	if (oldBPM >= 1000 && newBPM < 1000 && playbackHandler.recording != RECORDING_ARRANGEMENT)
		playbackHandler.forceResetPlayPos(modelStack->song);
	return NO_ERROR;
}
