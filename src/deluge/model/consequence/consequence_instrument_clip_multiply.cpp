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

#include "model/consequence/consequence_instrument_clip_multiply.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/song/song.h"

ConsequenceInstrumentClipMultiply::ConsequenceInstrumentClipMultiply() {
	// TODO Auto-generated constructor stub
}

int32_t ConsequenceInstrumentClipMultiply::revert(TimeType time, ModelStack* modelStack) {
	InstrumentClip* clip = (InstrumentClip*)modelStack->song->getCurrentClip();
	if (time == BEFORE) {
		modelStack->song->setClipLength(clip, clip->loopLength >> 1, NULL);

		// Deal with any NoteRows with independent length.
		clip->halveNoteRowsWithIndependentLength(modelStack->addTimelineCounter(clip));
	}
	else {
		modelStack->song->doubleClipLength(clip);
	}

	return NO_ERROR;
}
