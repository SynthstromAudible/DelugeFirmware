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

#include "model/consequence/consequence_clip_instance_change.h"
#include "definitions_cxx.hpp"
#include "model/clip/clip_instance.h"
#include "model/instrument/instrument.h"

ConsequenceClipInstanceChange::ConsequenceClipInstanceChange(Output* newOutput, ClipInstance* clipInstance,
                                                             int32_t posAfter, int32_t lengthAfter, Clip* clipAfter) {
	output = newOutput;
	pos[BEFORE] = clipInstance->pos;
	pos[AFTER] = posAfter;
	length[BEFORE] = clipInstance->length;
	length[AFTER] = lengthAfter;
	clip[BEFORE] = clipInstance->clip;
	clip[AFTER] = clipAfter;
}

ErrorType ConsequenceClipInstanceChange::revert(TimeType time, ModelStack* modelStack) {
	int32_t i = output->clipInstances.search(pos[1 - time], GREATER_OR_EQUAL);
	ClipInstance* clipInstance = output->clipInstances.getElement(i);
	if (!clipInstance) {
		return ERROR_BUG;
	}
	clipInstance->pos = pos[time];
	clipInstance->length = length[time];
	clipInstance->clip = clip[time];

	return NO_ERROR;
}
