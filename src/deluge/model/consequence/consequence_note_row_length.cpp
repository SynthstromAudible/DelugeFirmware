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

#include "model/consequence/consequence_note_row_length.h"
#include "model/clip/clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

ConsequenceNoteRowLength::ConsequenceNoteRowLength(int32_t newNoteRowId, int32_t newLength) {
	noteRowId = newNoteRowId;
	backedUpLength = newLength;
}

int32_t ConsequenceNoteRowLength::revert(TimeType time, ModelStack* modelStack) {
	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addTimelineCounter(modelStack->song->currentClip)
	                                                   ->addNoteRowId(noteRowId)
	                                                   ->automaticallyAddNoteRowFromId();
	performChange(modelStackWithNoteRow, NULL, modelStackWithNoteRow->getLastProcessedPos(),
	              modelStackWithNoteRow->getNoteRow()->hasIndependentPlayPos());
	return NO_ERROR;
}

void ConsequenceNoteRowLength::performChange(ModelStackWithNoteRow* modelStack, Action* actionToRecordTo,
                                             int32_t oldPos, // Sometimes needs overriding
                                             bool hadIndependentPlayPosBefore) {
	int32_t prevLength = modelStack->getLoopLength();

	modelStack->getNoteRow()->setLength(modelStack, backedUpLength, actionToRecordTo, oldPos,
	                                    hadIndependentPlayPosBefore);

	backedUpLength = prevLength;
}
