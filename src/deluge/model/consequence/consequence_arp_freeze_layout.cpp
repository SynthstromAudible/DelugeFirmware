/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "model/consequence/consequence_arp_freeze_layout.h"
#include "model/clip/arp_freeze.h"
#include "model/clip/instrument_clip.h"
#include "model/note/note_row.h"
#include <utility>

ConsequenceArpFreezeLayout::ConsequenceArpFreezeLayout(InstrumentClip* newClip)
    : clip(newClip), sequenceDirectionMode(newClip->sequenceDirectionMode) {
	rows.reserve(clip->noteRows.getNumElements());
	for (int32_t r = 0; r < clip->noteRows.getNumElements(); r++) {
		NoteRow* note_row = clip->noteRows.getElement(r);
		rows.push_back(
		    {clip->getNoteRowId(note_row, r), note_row->loopLengthIfIndependent, note_row->sequenceDirectionMode});
	}
}

Error ConsequenceArpFreezeLayout::revert(TimeType /*time*/, ModelStack* /*modelStack*/) {
	std::swap(sequenceDirectionMode, clip->sequenceDirectionMode);
	clip->currentlyPlayingReversed = (clip->sequenceDirectionMode == SequenceDirection::REVERSE);

	for (RowLayout& row : rows) {
		NoteRow* note_row = clip->getNoteRowFromId(row.noteRowId);
		if (note_row == nullptr) {
			continue;
		}
		std::swap(row.loopLengthIfIndependent, note_row->loopLengthIfIndependent);
		std::swap(row.sequenceDirectionMode, note_row->sequenceDirectionMode);
		note_row->currentlyPlayingReversedIfIndependent =
		    (note_row->sequenceDirectionMode == SequenceDirection::REVERSE);
	}

	arp_freeze::resyncPlayback(clip);
	return Error::NONE;
}
