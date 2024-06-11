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

#include "model/consequence/consequence_note_row_horizontal_shift.h"
#include "hid/display/display.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"

ConsequenceNoteRowHorizontalShift::ConsequenceNoteRowHorizontalShift(int32_t newNoteRowId, int32_t newAmount,
                                                                     bool newShiftAutomation,
                                                                     bool newShiftSequenceAndMPE) {
	amount = newAmount;
	noteRowId = newNoteRowId;
	shiftAutomation = newShiftAutomation;
	shiftSequenceAndMPE = newShiftSequenceAndMPE;
}

Error ConsequenceNoteRowHorizontalShift::revert(TimeType time, ModelStack* modelStack) {

	int32_t amountNow = amount;

	if (time == BEFORE) {
		amountNow = -amountNow;
	}

	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addTimelineCounter(modelStack->song->getCurrentClip())
	                                                   ->addNoteRowId(noteRowId)
	                                                   ->automaticallyAddNoteRowFromId();

	if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
#if ALPHA_OR_BETA_VERSION
		FREEZE_WITH_ERROR("E377");
#endif
		return Error::BUG;
	}

	((InstrumentClip*)modelStackWithNoteRow->getTimelineCounter())
	    ->shiftOnlyOneNoteRowHorizontally(modelStackWithNoteRow, amountNow, shiftAutomation, shiftSequenceAndMPE);

	return Error::NONE;
}
