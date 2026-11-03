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
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_node_vector.h"
#include "util/exceptions.h"
#include "util/functions.h"
#include <new>

ConsequenceParamChange::ConsequenceParamChange(deluge::project::ParamAddress address,
                                               ModelStackWithAutoParam const* modelStack, bool stealData)
    : address(address) {
	type = Consequence::PARAM_CHANGE;

	state.value = modelStack->autoParam->played();
	state.base = modelStack->doc()->current_value;

	// Either steal the data...
	if (stealData) {
		state.nodes.swap(modelStack->doc()->nodes);
	}

	// Or copy it...
	else {
		try {
			state.nodes = modelStack->doc()->nodes;
		} catch (deluge::exception&) {
			// OOM while recording undo: leave the snapshot's nodes empty (best-effort undo, no crash).
		}
	}
}

Error ConsequenceParamChange::revert(TimeType /*time*/, ModelStack* modelStackWithSong) {
	// One state is stored at a time, the before or the after: reverting either way swaps it with the lane's (the node
	// vectors themselves are swapped, not copied).
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	if (ModelStackWithAutoParam* lane = deluge::bridge::paramStackExact(*modelStackWithSong->song, address, memory)) {
		lane->paramCollection->remotelySwapParamState(&state, lane);
	}
	return Error::NONE;
}
