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
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence.h"
#include "model/note/note_row.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_node_vector.h"
#include "util/functions.h"
#include <new>

ConsequenceParamChange::ConsequenceParamChange(ModelStackWithAutoParam const* modelStack, bool stealData) {
	type = Consequence::PARAM_CHANGE;
	memcpy(modelStackMemory, modelStack, sizeof(ModelStackWithParamId));
	NoteRow* noteRow = modelStack->getNoteRowAllowNull();
	paramsBelongToNoteRow = (noteRow != nullptr && modelStack->paramManager == &noteRow->paramManager);

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

Error ConsequenceParamChange::revert(TimeType time, ModelStack* modelStackWithSong) {

	// We only actually store one state at a time - either the before, or the after. As we revert in either direction,
	// we swap our stored state with that of the param in question - like, actually swap the pointer to the
	// ParamNodeVector, so it's real efficient!

	if (paramsBelongToNoteRow && !refreshNoteRowPointers()) {
		return Error::NONE; // The NoteRow is gone, and these params with it
	}

	modelStack.paramCollection->remotelySwapParamState(&state, &modelStack);

	return Error::NONE;
}

// NoteRows are stored by value in the Clip's NoteRowVector, which moves them when rows are added or removed, so the
// NoteRow, ParamManager and summary pointers copied when this was recorded can be stale by now. The ParamCollection
// itself is allocated separately and doesn't move, so find the NoteRow whose ParamManager holds it now.
bool ConsequenceParamChange::refreshNoteRowPointers() {
	auto* clip = static_cast<InstrumentClip*>(modelStack.getTimelineCounter());
	for (int32_t i = 0; i < clip->noteRows.getNumElements(); i++) {
		NoteRow* noteRow = clip->noteRows.getElement(i);
		for (ParamCollectionSummary* summary = noteRow->paramManager.summaries; summary->paramCollection; summary++) {
			if (summary->paramCollection == modelStack.paramCollection) {
				modelStack.setNoteRow(noteRow, clip->getNoteRowId(noteRow, i));
				modelStack.paramManager = &noteRow->paramManager;
				modelStack.summary = summary;
				return true;
			}
		}
	}
	return false;
}
