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

#include "model/clip/note_regrouper.h"
#include "definitions_cxx.hpp"
#include "hid/display/display.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"
#include "model/scale/scale_change.h"
#include "model/song/song.h"
#include "util/exceptions.h"
#include <utility>

using namespace deluge::accidentals;

namespace {

/// A note which is leaving its NoteRow for the one at pitch
struct MovedNote {
	int32_t pitch;
	Note note;
};

/// Where a row and its notes go
struct RowPlan {
	/// Where the notes with no marker go. They share a destination, because they share an anchor.
	NoteDestination unmarkedDestination;
	/// The pitch the row itself moves to: that of its unmarked notes, or if it has none, of its first note
	int32_t newY;
};

class Planner {
public:
	Planner(const MusicalKey& oldKey, const MusicalKey& finalKey, const ScaleChange& change,
	        AccidentalHandling handling, int32_t pendingTranspose)
	    : oldKey_(oldKey), finalKey_(finalKey), change_(change), handling_(handling),
	      pendingTranspose_(pendingTranspose) {}

	[[nodiscard]] RowPlan planRow(NoteRow& row) const {
		RowPlan plan{destinationOf(row, AnchorMarker::derived()), 0};
		plan.newY = plan.unmarkedDestination.pitch;

		int32_t numNotes = row.notes.getNumElements();
		bool anyUnmarked = false;
		for (int32_t n = 0; n < numNotes && !anyUnmarked; n++) {
			anyUnmarked = row.notes.getElement(n)->anchorMarker.isDerived();
		}
		if (!anyUnmarked && numNotes != 0) {
			plan.newY = destinationOf(row, row.notes.getElement(0)->anchorMarker).pitch;
		}
		return plan;
	}

	[[nodiscard]] NoteDestination destinationOf(const NoteRow& row, AnchorMarker marker) const {
		return destination(row.y, marker, oldKey_, change_, handling_);
	}

	[[nodiscard]] NoteDestination destinationOf(const NoteRow& row, const Note& note, const RowPlan& plan) const {
		return note.anchorMarker.isDerived() ? plan.unmarkedDestination : destinationOf(row, note.anchorMarker);
	}

	[[nodiscard]] AnchorMarker markerToStore(const NoteDestination& noteDestination) const {
		return noteDestination.markerUnder(finalKey_, pendingTranspose_);
	}

private:
	SpelledKey oldKey_;
	SpelledKey finalKey_;
	const ScaleChange& change_;
	AccidentalHandling handling_;
	int32_t pendingTranspose_;
};

/// Adds a copy of a note to a row, keeping lift, parameter locks and accidental lineage. Returns false, adding nothing,
/// if it overlaps a note that is already there.
bool addMovedNote(NoteRow* row, const Note& note, ModelStackWithNoteRow* modelStackWithRow) {
	if (!row->attemptNoteAdd(note.pos, note.length, note.velocity, note.probability, note.iterance, note.fill,
	                         modelStackWithRow, nullptr, note.lockSet)) {
		return false;
	}
	Note* added = row->notes.getElement(row->notes.search(note.pos, GREATER_OR_EQUAL));
	added->setLift(note.lift);
	added->anchorMarker = note.anchorMarker;
	return true;
}

} // namespace

bool NoteRegrouper::regroup(const ScaleChange& change, AccidentalHandling handling, int32_t pendingTranspose) {
	const MusicalKey oldKey = modelStack_->song->key;
	MusicalKey finalKey = oldKey;
	finalKey.modeNotes = change.target;
	finalKey.rootNote += pendingTranspose;

	// The usual case, with no accidentals anywhere, needs none of the per-note work below
	if (moveWholeRows(change, oldKey, finalKey, pendingTranspose)) {
		return false;
	}

	NoteRowVector& noteRows = clip_.noteRows;
	const Planner planner{oldKey, finalKey, change, handling, pendingTranspose};

	// Work out how many notes will leave their row, and get the memory for everything before changing anything: running
	// out half way would leave the rows half moved.
	int32_t numLeaving = 0;
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* row = noteRows.getElement(i);
		RowPlan plan = planner.planRow(*row);
		for (int32_t n = 0; n < row->notes.getNumElements(); n++) {
			if (planner.destinationOf(*row, *row->notes.getElement(n), plan).pitch != plan.newY) {
				numLeaving++;
			}
		}
	}
	deluge::vector<MovedNote> movedNotes;
	RowFlags wasInScale;
	try {
		movedNotes.reserve(numLeaving);
		wasInScale.resize(noteRows.getNumElements());
	} catch (deluge::exception) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return false; // The clip's notes stay where they were
	}

	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* row = noteRows.getElement(i);
		wasInScale[i] = oldKey.isInKey(row->y);
		RowPlan plan = planner.planRow(*row);
		AnchorMarker unmarkedMarker = planner.markerToStore(plan.unmarkedDestination);

		// Sort the notes into those staying in the row, and those moving elsewhere
		bool anyLeaving = false;
		for (int32_t n = row->notes.getNumElements() - 1; n >= 0; n--) {
			Note* note = row->notes.getElement(n);
			bool unmarked = note->anchorMarker.isDerived();
			NoteDestination noteDestination = planner.destinationOf(*row, *note, plan);
			AnchorMarker marker = unmarked ? unmarkedMarker : planner.markerToStore(noteDestination);
			if (noteDestination.pitch == plan.newY) {
				note->anchorMarker = marker;
				continue;
			}
			if (!anyLeaving) {
				anyLeaving = true;
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStack_->addNoteRow(clip_.getNoteRowId(row, i), row);
				row->stopCurrentlyPlayingNote(modelStackWithNoteRow);
			}
			MovedNote moved{noteDestination.pitch, *note};
			moved.note.anchorMarker = marker;
			movedNotes.push_back(moved); // Within what was reserved
			row->notes.deleteAtIndex(n);
		}

		moveRow(row, i, plan.newY);
	}

	bool merged = sortAndMergeRows(wasInScale);

	// Now put the notes which changed row into theirs, creating it if need be. A note which overlaps one already
	// there can't be kept.
	for (const MovedNote& moved : movedNotes) {
		if (moved.pitch < 0 || moved.pitch > kMaxMIDIValue) {
			continue;
		}
		int32_t index;
		NoteRow* row = clip_.getNoteRowForYNote(moved.pitch, &index);
		if (row == nullptr) {
			row = noteRows.insertNoteRowAtY(moved.pitch, &index);
			if (row == nullptr) {
				continue;
			}
		}
		ModelStackWithNoteRow* modelStackWithRow = modelStack_->addNoteRow(clip_.getNoteRowId(row, index), row);
		if (moved.note.pos < modelStackWithRow->getLoopLength()) {
			addMovedNote(row, moved.note, modelStackWithRow);
		}
	}

	if (!movedNotes.empty()) {
		clip_.expectEvent();
	}

	return merged || !movedNotes.empty();
}

/** The cheap way, for a clip with no accidentals: every row moves as a whole by its scale degree's shift, with no
 * memory allocated and no note touched. Returns false, having changed nothing, unless that is exactly what the
 * per-note way would do, which is when:
 * - the change shifts scale degrees (no letter mapping), so an in-key note which is its own anchor goes to
 *   pitch + change[degree] and stays its own anchor;
 * - every row is in the old key and no note has a marker, so that is every note, and none leaves its row;
 * - every row lands in the final key, so every note's marker stays "derived";
 * - the rows stay in strictly ascending order, so there is nothing to sort or merge.
 */
bool NoteRegrouper::moveWholeRows(const ScaleChange& change, const MusicalKey& oldKey, const MusicalKey& finalKey,
                                  int32_t pendingTranspose) {
	if (change.letterMapping) {
		return false;
	}
	NoteRowVector& noteRows = clip_.noteRows;
	int32_t numRows = noteRows.getNumElements();

	bool anyBelow = false;
	int32_t newYBelow = 0;
	for (int32_t i = 0; i < numRows; i++) {
		NoteRow* row = noteRows.getElement(i);
		int8_t degree = oldKey.degreeOf(row->y);
		if (degree < 0) {
			return false;
		}
		int32_t newY = row->y + change[degree];
		if (!finalKey.isInKey(newY + pendingTranspose) || (anyBelow && newY <= newYBelow)) {
			return false;
		}
		for (int32_t n = 0; n < row->notes.getNumElements(); n++) {
			if (!row->notes.getElement(n)->anchorMarker.isDerived()) {
				return false;
			}
		}
		anyBelow = true;
		newYBelow = newY;
	}

	for (int32_t i = 0; i < numRows; i++) {
		NoteRow* row = noteRows.getElement(i);
		moveRow(row, i, row->y + change[oldKey.degreeOf(row->y)]);
	}
	return true;
}

void NoteRegrouper::moveRow(NoteRow* row, int32_t rowIndex, int32_t newY) {
	if (newY == row->y) {
		return;
	}
	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack_->addNoteRow(clip_.getNoteRowId(row, rowIndex), row);
	row->stopCurrentlyPlayingNote(modelStackWithNoteRow); // Otherwise we'd leave a MIDI note playing
	row->y = newY;
}

/** Restores ascending y order of the rows (which are binary-searched) after they have been moved, and merges rows
 * which ended up with the same y. The survivor of a merge is the row that was in scale, else the one with more notes.
 * Returns whether any rows were merged.
 */
bool NoteRegrouper::sortAndMergeRows(RowFlags& wasInScale) {
	NoteRowVector& noteRows = clip_.noteRows;

	// Insertion sort - rows are few, and moves small. swapElements() is what the container offers for this.
	for (int32_t i = 1; i < noteRows.getNumElements(); i++) {
		for (int32_t j = i; j > 0 && noteRows.getElement(j - 1)->y > noteRows.getElement(j)->y; j--) {
			noteRows.swapElements(j - 1, j);
			std::swap(wasInScale[j - 1], wasInScale[j]);
		}
	}

	bool merged = false;
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		int32_t runEnd = i + 1;
		while (runEnd < noteRows.getNumElements() && noteRows.getElement(runEnd)->y == noteRows.getElement(i)->y) {
			runEnd++;
		}
		if (runEnd - i < 2) {
			continue;
		}
		merged = true;

		int32_t survivorIndex = i;
		for (int32_t k = i + 1; k < runEnd; k++) {
			bool better = wasInScale[k] != wasInScale[survivorIndex]
			                  ? wasInScale[k] != 0
			                  : noteRows.getElement(k)->notes.getNumElements()
			                        > noteRows.getElement(survivorIndex)->notes.getNumElements();
			if (better) {
				survivorIndex = k;
			}
		}

		NoteRow* survivor = noteRows.getElement(survivorIndex);
		int32_t survivorLoopLength =
		    modelStack_->addNoteRow(clip_.getNoteRowId(survivor, survivorIndex), survivor)->getLoopLength();

		for (int32_t k = i; k < runEnd; k++) {
			if (k == survivorIndex) {
				continue;
			}
			NoteRow* source = noteRows.getElement(k);
			ModelStackWithNoteRow* modelStackWithSource =
			    modelStack_->addNoteRow(clip_.getNoteRowId(source, k), source);
			source->stopCurrentlyPlayingNote(modelStackWithSource); // Otherwise we'd leave a MIDI note playing

			for (int32_t n = 0; n < source->notes.getNumElements(); n++) {
				Note* note = source->notes.getElement(n);
				if (note->pos >= survivorLoopLength) {
					continue;
				}
				// A note overlapping one already there is dropped. addNoteRow() mutates the model stack in place, so
				// the source call above clobbered the survivor's; issue it again.
				ModelStackWithNoteRow* modelStackWithSurvivor =
				    modelStack_->addNoteRow(clip_.getNoteRowId(survivor, survivorIndex), survivor);
				addMovedNote(survivor, *note, modelStackWithSurvivor);
			}
		}

		// Delete the merged-away rows, highest index first. The survivor then ends up at index i.
		for (int32_t k = runEnd - 1; k >= i; k--) {
			if (k != survivorIndex) {
				noteRows.deleteNoteRowAtIndex(k);
				wasInScale.erase(wasInScale.begin() + k);
			}
		}
	}
	return merged;
}
