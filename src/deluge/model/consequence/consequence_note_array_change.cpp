/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "model/consequence/consequence_note_array_change.h"
#include "definitions_cxx.hpp"
#include "model/clip/instrument_clip.h"
#include "model/note/note_row.h"

ConsequenceNoteArrayChange::ConsequenceNoteArrayChange(InstrumentClip* newClip, int32_t newNoteRowId,
                                                       NoteVector* newNoteVector, bool stealData) {
	type = Consequence::NOTE_ARRAY_CHANGE;
	clip = newClip;
	noteRowId = newNoteRowId;

	// Either steal the data...
	if (stealData) {
		backedUpNoteVector.swapStateWith(newNoteVector);
	}

	// Or clone it...
	else {
		backedUpNoteVector.cloneFrom(newNoteVector);
	}
}

int32_t ConsequenceNoteArrayChange::revert(TimeType time, ModelStack* modelStack) {

	NoteRow* noteRow = clip->getNoteRowFromId(noteRowId);
	if (!noteRow) {
		return ERROR_BUG;
	}

	noteRow->notes.swapStateWith(&backedUpNoteVector);

	return NO_ERROR;
}
