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

#include <InstrumentClip.h>
#include "ConsequenceNoteExistence.h"
#include "NoteVector.h"
#include "Note.h"
#include "NoteRow.h"

ConsequenceNoteExistence::ConsequenceNoteExistence(InstrumentClip* newClip, int newNoteRowId, Note* note, int newType) {
	clip = newClip;
	noteRowId = newNoteRowId;
	pos = note->pos;
	length = note->getLength();
	velocity = note->getVelocity();
	probability = note->getProbability();
	lift = note->getLift();

	type = newType;
}

int ConsequenceNoteExistence::revert(int time, ModelStack* modelStack) {
	NoteRow* noteRow = clip->getNoteRowFromId(noteRowId);
	if (!noteRow) return ERROR_BUG;

	if (time == type) {
		// Delete a note now
		int i = noteRow->notes.search(pos, GREATER_OR_EQUAL);
		if (i < 0 || i >= noteRow->notes.getNumElements() || noteRow->notes.getElement(i)->pos != pos) {
			return NO_ERROR; // This can happen, and is fine, when redoing a "Clip multiply" action with notes with iteration dependence
		}
		noteRow->notes.deleteAtIndex(i);
	}
	else {
		// Create a note now
		int i = noteRow->notes.insertAtKey(pos);
		Note* note = noteRow->notes.getElement(i);
		if (!note) return ERROR_INSUFFICIENT_RAM;
		note->setLength(length);
		note->setVelocity(velocity);
		note->setProbability(probability);
		note->setLift(lift);
	}

	return NO_ERROR;
}
