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

#include "model/consequence/consequence_param_change.h"
#include "definitions_cxx.hpp"
#include "model/consequence/consequence.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_node_vector.h"
#include "util/functions.h"
#include <new>

ConsequenceParamChange::ConsequenceParamChange(ModelStackWithAutoParam const* modelStack, bool stealData) {
	type = Consequence::PARAM_CHANGE;
	memcpy(modelStackMemory, modelStack, sizeof(ModelStackWithParamId));

	state.value = modelStack->autoParam->currentValue;

	// Either steal the data...
	if (stealData) {
		state.nodes.swapStateWith(&modelStack->autoParam->nodes);
	}

	// Or clone it...
	else {
		state.nodes.cloneFrom(&modelStack->autoParam->nodes);
	}
}

int32_t ConsequenceParamChange::revert(TimeType time, ModelStack* modelStackWithSong) {

	// We only actually store one state at a time - either the before, or the after. As we revert in either direction,
	// we swap our stored state with that of the param in question - like, actually swap the pointer to the
	// ParamNodeVector, so it's real efficient!

	modelStack.paramCollection->remotelySwapParamState(&state, &modelStack);

	return NO_ERROR;
}
