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

#include "model/consequence/consequence_note_row_mute.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"

ConsequenceNoteRowMute::ConsequenceNoteRowMute(InstrumentClip* newClip, int32_t newNoteRowId) {
	noteRowId = newNoteRowId;
	clip = newClip;
}

ErrorType ConsequenceNoteRowMute::revert(TimeType time, ModelStack* modelStack) {
	NoteRow* noteRow = clip->getNoteRowFromId(noteRowId);
	if (!noteRow) {
		return ERROR_BUG;
	}

	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addTimelineCounter(clip)->addNoteRow(noteRowId, noteRow);

	// Call this instead of Clip::toggleNoteRowMute(), cos that'd go and log another Action
	noteRow->toggleMute(modelStackWithNoteRow, (playbackHandler.playbackState & PLAYBACK_CLOCK_EITHER_ACTIVE)
	                                               && modelStackWithNoteRow->song->isClipActive(clip));

	return NO_ERROR;
}
