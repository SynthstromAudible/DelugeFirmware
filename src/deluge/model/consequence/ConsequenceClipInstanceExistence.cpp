/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include <ClipInstance.h>
#include <ConsequenceClipInstanceExistence.h>
#include "instrument.h"

ConsequenceClipInstanceExistence::ConsequenceClipInstanceExistence(Output* newOutput, ClipInstance* clipInstance,
                                                                   int newType) {
	output = newOutput;
	clip = clipInstance->clip;
	pos = clipInstance->pos;
	length = clipInstance->length;

	type = newType;
}

int ConsequenceClipInstanceExistence::revert(int time, ModelStack* modelStack) {

	if (time == type) { // (Re-)delete
		int i = output->clipInstances.search(pos, GREATER_OR_EQUAL);
		if (i < 0 || i >= output->clipInstances.getNumElements()) return ERROR_BUG;
		output->clipInstances.deleteAtIndex(i);
	}

	else { // (Re-)create
		int i = output->clipInstances.insertAtKey(pos);
		ClipInstance* clipInstance = output->clipInstances.getElement(i);
		if (!clipInstance) return ERROR_INSUFFICIENT_RAM;
		clipInstance->length = length;
		clipInstance->clip = clip;
	}

	return NO_ERROR;
}
