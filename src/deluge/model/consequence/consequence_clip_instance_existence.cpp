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

#include "model/consequence/consequence_clip_instance_existence.h"
#include "definitions_cxx.hpp"
#include "model/clip/clip_instance.h"
#include "model/instrument/instrument.h"
#include "util/misc.h"

ConsequenceClipInstanceExistence::ConsequenceClipInstanceExistence(Output* newOutput, ClipInstance* clipInstance,
                                                                   ExistenceChangeType newType) {
	output = newOutput;
	clip = clipInstance->clip;
	pos = clipInstance->pos;
	length = clipInstance->length;

	type = newType;
}

ErrorType ConsequenceClipInstanceExistence::revert(TimeType time, ModelStack* modelStack) {

	if (time == util::to_underlying(type)) { // (Re-)delete
		int32_t i = output->clipInstances.search(pos, GREATER_OR_EQUAL);
		if (i < 0 || i >= output->clipInstances.getNumElements()) {
			return ERROR_BUG;
		}
		output->clipInstances.deleteAtIndex(i);
	}

	else { // (Re-)create
		int32_t i = output->clipInstances.insertAtKey(pos);
		ClipInstance* clipInstance = output->clipInstances.getElement(i);
		if (!clipInstance) {
			return ERROR_INSUFFICIENT_RAM;
		}
		clipInstance->length = length;
		clipInstance->clip = clip;
	}

	return NO_ERROR;
}
