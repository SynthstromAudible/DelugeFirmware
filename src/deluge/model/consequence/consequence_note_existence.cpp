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

#include "model/consequence/consequence_note_existence.h"
#include "definitions_cxx.hpp"
#include "model/clip/instrument_clip.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/note/note_vector.h"
#include "util/misc.h"

ConsequenceNoteExistence::ConsequenceNoteExistence(InstrumentClip* newClip, int32_t newNoteRowId, Note* note,
                                                   ExistenceChangeType newType) {
	clip = newClip;
	noteRowId = newNoteRowId;
	pos = note->pos;
	length = note->getLength();
	velocity = note->getVelocity();
	probability = note->getProbability();
	lift = note->getLift();
	iterance = note->getIterance();
	fill = note->getFill();

	type = newType;
}

Error ConsequenceNoteExistence::revert(TimeType time, ModelStack* modelStack) {
	NoteRow* noteRow = clip->getNoteRowFromId(noteRowId);
	if (!noteRow) {
		return Error::BUG;
	}

	if (time == util::to_underlying(type)) {
		// Delete a note now
		int32_t i = noteRow->notes.search(pos, GREATER_OR_EQUAL);
		if (i < 0 || i >= noteRow->notes.getNumElements() || noteRow->notes.getElement(i)->pos != pos) {
			return Error::NONE; // This can happen, and is fine, when redoing a "Clip multiply" action with notes with
			                    // iteration dependence
		}
		noteRow->notes.deleteAtIndex(i);
	}
	else {
		// Create a note now
		int32_t i = noteRow->notes.insertAtKey(pos);
		Note* note = noteRow->notes.getElement(i);
		if (!note) {
			return Error::INSUFFICIENT_RAM;
		}
		note->setLength(length);
		note->setVelocity(velocity);
		note->setProbability(probability);
		note->setLift(lift);
		note->setIterance(iterance);
		note->setFill(fill);
	}

	return Error::NONE;
}
