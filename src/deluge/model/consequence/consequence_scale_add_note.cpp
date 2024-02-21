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

#include "model/consequence/consequence_scale_add_note.h"
#include "model/model_stack.h"
#include "model/song/song.h"

ConsequenceScaleAddNote::ConsequenceScaleAddNote(int32_t newNoteWithinOctave) {
	noteWithinOctave = newNoteWithinOctave;
}

Error ConsequenceScaleAddNote::revert(TimeType time, ModelStack* modelStack) {

	// The only thing we actually have to do is delete any NoteRows that had the new yNoteWithinOctave.
	// The changing back of the scale itself is handled by the Action, which
	// keeps a record
	if (time == BEFORE) {
		modelStack->song->removeYNoteFromMode(noteWithinOctave);
	}
	else {}

	return Error::NONE;
}
