/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "model/note/note_row.h"
#include "definitions_cxx.hpp"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "io/debug/log.h"
#include "io/midi/midi_device.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_note_existence.h"
#include "model/drum/drum_name.h"
#include "model/drum/gate_drum.h"
#include "model/instrument/kit.h"
#include "model/note/copied_note_row.h"
#include "model/note/note.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"
#include <new>
#include <string.h>

extern "C" {
#include "RZA1/uart/sio_char.h"
}

NoteRow::NoteRow(int16_t newY) {
	y = newY;
	muted = false;
	colourOffset = random(71);
	drum = NULL;
	firstOldDrumName = NULL;
	soundingStatus = STATUS_OFF;
	ignoreNoteOnsBefore_ = 0;
	probabilityValue = kNumProbabilityValues;
	iteranceValue = kDefaultIteranceValue;
	fillValue = FillMode::OFF;
	loopLengthIfIndependent = 0;
	sequenceDirectionMode = SequenceDirection::OBEY_PARENT;
}

NoteRow::~NoteRow() {
	deleteOldDrumNames(false);
}

void NoteRow::deleteParamManager(bool shouldUpdatePointer) {
	paramManager.destructAndForgetParamCollections();
}

void NoteRow::deleteOldDrumNames(bool shouldUpdatePointer) {
	DrumName* oldFirstOldDrumName = firstOldDrumName; // Don't touch the original variable - for speed
	while (oldFirstOldDrumName) {
		DrumName* toDelete = oldFirstOldDrumName;
		oldFirstOldDrumName = oldFirstOldDrumName->next;
		toDelete->~DrumName();
		delugeDealloc(toDelete);
	}

	if (shouldUpdatePointer) {
		firstOldDrumName = NULL;
	}
}

Error NoteRow::beenCloned(ModelStackWithNoteRow* modelStack, bool shouldFlattenReversing) {
	// No need to clone much stuff - it's been automatically copied already as a block of memory.

	firstOldDrumName = NULL;
	ignoreNoteOnsBefore_ = 0;
	// soundingStatus = STATUS_OFF;

	int32_t effectiveLength = modelStack->getLoopLength();

	int32_t numNotes = notes.getNumElements();
	bool flatteningReversingNow =
	    (shouldFlattenReversing && getEffectiveSequenceDirectionMode(modelStack) == SequenceDirection::REVERSE);

	int32_t reverseWithLength = flatteningReversingNow ? effectiveLength : 0;

	Error error = paramManager.beenCloned(reverseWithLength); // TODO: flatten reversing here too

	if (error != Error::NONE) {
		notes.init(); // Abandon non-yet-cloned stuff
		return error;
	}

	// If we want to reverse the sequence, do that.
	if (flatteningReversingNow && numNotes) {
		NoteVector oldNotes =
		    notes; // Sneakily and temporarily clone this - still pointing to the old NoteRow's notes' memory.
		notes.init();

		error = notes.insertAtIndex(0, numNotes);
		if (error == Error::NONE) {

			InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

			// If yes note tails...
			if (clip->allowNoteTails(modelStack)) {
				int32_t numNotesBesidesWrapping = numNotes;

				// Look at the final note, to see if it's wrapping and deal with that.
				{
					Note* oldNote = (Note*)oldNotes.getElementAddress(numNotes - 1);
					int32_t finalNoteOvershoot = oldNote->pos + oldNote->length - effectiveLength;
					if (finalNoteOvershoot > 0) {
						numNotesBesidesWrapping--;
						Note* newNote = (Note*)notes.getElementAddress(numNotesBesidesWrapping);
						newNote->pos = effectiveLength - finalNoteOvershoot;
						newNote->setLength(oldNote->getLength());
						newNote->setProbability(oldNote->getProbability());
						newNote->setVelocity(oldNote->getVelocity());
						newNote->setLift(oldNote->getLift());
						newNote->setIterance(oldNote->getIterance());
						newNote->setFill(oldNote->getFill());
					}
				}

				for (int32_t iOld = 0; iOld < numNotesBesidesWrapping; iOld++) {
					int32_t iNew = numNotesBesidesWrapping - 1 - iOld;

					Note* oldNote = (Note*)oldNotes.getElementAddress(iOld);
					Note* newNote = (Note*)notes.getElementAddress(iNew);

					int32_t newPos = effectiveLength - oldNote->pos - oldNote->length;
					newNote->pos = newPos;
					newNote->setLength(oldNote->getLength());
					newNote->setProbability(oldNote->getProbability());
					newNote->setVelocity(oldNote->getVelocity());
					newNote->setLift(oldNote->getLift());
					newNote->setIterance(oldNote->getIterance());
					newNote->setFill(oldNote->getFill());
				}
			}

			// No-tails (e.g. one-shot samples):
			else {

				Note* firstNote = (Note*)oldNotes.getElementAddress(0);
				bool anythingAtZero = (firstNote->pos == 0);

				for (int32_t iOld = 0; iOld < numNotes; iOld++) {
					int32_t iNew = -iOld - !anythingAtZero;
					if (iNew < 0) {
						iNew += numNotes;
					}
					Note* oldNote = (Note*)oldNotes.getElementAddress(iOld);
					Note* newNote = (Note*)notes.getElementAddress(iNew);

					int32_t newPos = -oldNote->pos;
					if (newPos < 0) {
						newPos += effectiveLength;
					}
					newNote->pos = newPos;
					newNote->setLength(1);
					newNote->setProbability(oldNote->getProbability());
					newNote->setVelocity(oldNote->getVelocity());
					newNote->setLift(oldNote->getLift());
					newNote->setIterance(oldNote->getIterance());
					newNote->setFill(oldNote->getFill());
				}
			}
		}

		oldNotes.init(); // Because this is about to get destructed, we need to stop it pointing to the old NoteRow's
		                 // notes' memory, cos we don't want that getting deallocated.
	}

	// Or if not reversing the sequence, we can just make a simple call.
	else {
		error = notes.beenCloned();
	}

	if (shouldFlattenReversing && sequenceDirectionMode != SequenceDirection::PINGPONG) {
		sequenceDirectionMode = SequenceDirection::OBEY_PARENT;
	}
	// Pingponging won't have been flattened by a single clone. And we may be about to flatten it with a
	// generateRepeats(), so need to keep this designation for now.

	return error;
}

// initialize row square info array before we iterate through the note row to store square info
void NoteRow::initRowSquareInfo(SquareInfo rowSquareInfo[kDisplayWidth], bool anyNotes) {
	for (int32_t x = 0; x < kDisplayWidth; x++) {
		initSquareInfo(rowSquareInfo[x], anyNotes, x);
	}
}

void NoteRow::initSquareInfo(SquareInfo& squareInfo, bool anyNotes, int32_t x) {
	// if there's notes, get square boundaries (start and end pos)
	// so that we can compare where notes are relative to the square
	// e.g. are they inside the square, extending into the square (tail), etc.
	if (anyNotes) {
		squareInfo.squareStartPos = instrumentClipView.getPosFromSquare(x);
		squareInfo.squareEndPos = instrumentClipView.getPosFromSquare(x + 1);
	}
	// if there's no notes, no need to get square position info
	// because we won't be checking for note placement relative to square
	// boundaries
	else {
		squareInfo.squareStartPos = 0;
		squareInfo.squareEndPos = 0;
	}
	squareInfo.squareType = SQUARE_NO_NOTE;
	squareInfo.numNotes = 0;
	squareInfo.averageVelocity = 0;
	squareInfo.probability = 0;
	squareInfo.iterance = 0;
	squareInfo.fill = 0;
	squareInfo.isValid = true;
}

/// get info about squares for display at current zoom level
void NoteRow::getRowSquareInfo(int32_t effectiveLength, SquareInfo rowSquareInfo[kDisplayWidth]) {
	bool anyNotes = notes.getNumElements();

	initRowSquareInfo(rowSquareInfo, anyNotes);

	// if there are notes, we'll iterate through the note row to store info about each note found
	// in each square of the note row currently displayed on the grid
	// if there are no notes, then the row square info array is initialized to 0
	if (anyNotes) {
		// find the end position of the last square at current xZoom and xScroll resolution
		// it will be the minimum of the width of the grid (kDisplayWidth) or the note row length
		int32_t lastNoteSquareEndPos = std::min(instrumentClipView.getPosFromSquare(kDisplayWidth), effectiveLength);

		// find the last square that we will iterate from
		int32_t lastSquare =
		    instrumentClipView.getSquareFromPos(lastNoteSquareEndPos - 1, NULL, currentSong->xScroll[NAVIGATION_CLIP]);

		// Start by finding the last note to begin *before the right-edge* of the note row displayed
		int32_t i = notes.search(lastNoteSquareEndPos, LESS);

		// Get that last note
		Note* note = notes.getElement(i);

		// now we're going to iterate backwards from right edge
		// in order to update all squares in the note row with the note info
		for (int32_t x = lastSquare; x >= 0; x--) {
			addNotesToSquareInfo(effectiveLength, rowSquareInfo[x], i, &note);
		}

		// calculate average velocity for each square
		// for the notes found above, cumulative velocity info was saved
		// now we'll convert those cumulative velocity totals into averages
		// based on the number of notes in each square
		// this is only required if there is more than one note in a square
		for (int32_t x = 0; x <= lastSquare; x++) {
			calculateSquareAverages(rowSquareInfo[x]);
		}
	}
}

/// get info about the notes in this square at current zoom level
void NoteRow::getSquareInfo(int32_t x, int32_t effectiveLength, SquareInfo& squareInfo) {
	bool anyNotes = notes.getNumElements();

	initSquareInfo(squareInfo, anyNotes, x);

	if (anyNotes) {
		// don't process this square if the note row is shorter
		if (squareInfo.squareStartPos < effectiveLength) {
			// Start by finding the last note to begin *before the right-edge* of the square
			int32_t i = notes.search(squareInfo.squareEndPos, LESS);

			// Get that last note
			Note* note = notes.getElement(i);

			// Update square info with note info found
			addNotesToSquareInfo(effectiveLength, squareInfo, i, &note);

			// calculate average velocity for each square
			// for the notes found above, cumulative velocity info was saved
			// now we'll convert those cumulative velocity totals into averages
			// based on the number of notes in each square
			// this is only required if there is more than one note in a square
			calculateSquareAverages(squareInfo);
		}
	}
}

/// iterate through a specific square
/// returns whether a note square on the grid is:
/// 1) empty (SQUARE_NO_NOTE)
///	2) has one note which is aligned to the very first position in the square (SQUARE_NOTE_HEAD)
/// 3) has multiple notes or one note which is not aligned to the very first position (SQUARE_BLURRED)
/// 4) the square is part of a tail of a previous note (SQUARE_NOTE_TAIL)
/// returns number of notes in a square
/// returns average velocity for a square
void NoteRow::addNotesToSquareInfo(int32_t effectiveLength, SquareInfo& squareInfo, int32_t& noteIndex, Note** note) {
	bool gotFirstNoteParams = false;
	// does the note fall within the square we're looking at
	if (*note && ((*note)->pos >= squareInfo.squareStartPos) && ((*note)->pos < squareInfo.squareEndPos)) {
		while ((noteIndex >= 0)
		       && (*note && ((*note)->pos >= squareInfo.squareStartPos) && ((*note)->pos < squareInfo.squareEndPos))) {
			squareInfo.numNotes += 1;

			if (squareInfo.numNotes == 1 && (*note)->pos == squareInfo.squareStartPos) {
				squareInfo.squareType = SQUARE_NOTE_HEAD;
			}
			else {
				squareInfo.squareType = SQUARE_BLURRED;
			}

			// we'll convert this to an average once we're done counting the number of notes
			// and summing all note velocities in this square
			squareInfo.averageVelocity += (*note)->getVelocity();
			if (!gotFirstNoteParams) {
				squareInfo.probability = (*note)->getProbability();
				squareInfo.iterance = (*note)->getIterance();
				squareInfo.fill = (*note)->getFill();
				gotFirstNoteParams = true;
			}

			// ok we've used this note, so let's move to next one
			noteIndex--;
			*note = notes.getElement(noteIndex);
		}
	}
	// Or if the note starts left of this square, or there's no note there which means we'll look at the final
	// one wrapping around...
	else if ((*note && (*note)->pos < squareInfo.squareStartPos) || (noteIndex == -1)) {
		bool wrapping = (noteIndex == -1);
		if (wrapping) {
			*note = notes.getLast();
		}
		int32_t noteEnd = (*note)->pos + (*note)->getLength();
		if (wrapping) {
			noteEnd -= effectiveLength;
		}

		// If that note's tail does overlap into this square...
		if (noteEnd > squareInfo.squareStartPos) {
			squareInfo.numNotes += 1;
			squareInfo.squareType = SQUARE_NOTE_TAIL;
			squareInfo.averageVelocity += (*note)->getVelocity();
			squareInfo.probability = (*note)->getProbability();
			squareInfo.iterance = (*note)->getIterance();
			squareInfo.fill = (*note)->getFill();
		}
	}
}

/// calculate average velocity for this square based on info on notes
/// previously obtained by calling NoteRow::getRowSquareInfo or NoteRow::getSquareInfo
/// and NoteRow::addNotesToSquareInfo
void NoteRow::calculateSquareAverages(SquareInfo& squareInfo) {
	if (squareInfo.numNotes > 1) {
		squareInfo.averageVelocity = squareInfo.averageVelocity / squareInfo.numNotes;
	}
}

uint8_t NoteRow::getSquareType(int32_t squareStart, int32_t squareWidth, Note** firstNote, Note** lastNote,
                               ModelStackWithNoteRow* modelStack, bool allowNoteTails, int32_t desiredNoteLength,
                               Action* action, bool clipCurrentlyPlaying, bool extendPreviousNoteIfPossible) {

	int32_t effectiveLength = modelStack->getLoopLength();

	if (!notes.getNumElements()) {

addNewNote:
		// We clear MPE data for the note, up until the next note.
		int32_t wrapEditLevel = ((InstrumentClip*)modelStack->getTimelineCounter())->getWrapEditLevel();
		clearMPEUpUntilNextNote(modelStack, squareStart, wrapEditLevel);

		int32_t i = notes.insertAtKey(squareStart);
		if (i == -1) {
			return 0;
		}

		Note* newNote = notes.getElement(i);

		newNote->setVelocity(((Instrument*)((Clip*)modelStack->getTimelineCounter())->output)->defaultVelocity);
		newNote->setLift(kDefaultLiftValue);

		newNote->setProbability(getDefaultProbability());
		newNote->setIterance(getDefaultIterance());
		newNote->setFill(getDefaultFill(modelStack));

		if (i + 1 < notes.getNumElements()) {
			newNote->setLength(std::min(desiredNoteLength, notes.getElement(i + 1)->pos - newNote->pos));
		}
		else {
			newNote->setLength(std::min(desiredNoteLength, notes.getElement(0)->pos + effectiveLength - newNote->pos));
		}

		// Record consequence
		if (action) {
			action->recordNoteExistenceChange((InstrumentClip*)modelStack->getTimelineCounter(), modelStack->noteRowId,
			                                  newNote, ExistenceChangeType::CREATE);
		}

		if (clipCurrentlyPlaying && !muted) {
			((InstrumentClip*)modelStack->getTimelineCounter())->expectEvent();

			if ((runtimeFeatureSettings.get(RuntimeFeatureSettingType::CatchNotes) == RuntimeFeatureStateToggle::On)) {
				// If the play-pos is inside this note, see if we'd like to attempt a late-start of it
				int32_t actualPlayPos = getLivePos(modelStack);

				int32_t howFarIntoNote = actualPlayPos - newNote->pos;
				if (howFarIntoNote < 0) {
					howFarIntoNote += effectiveLength;
				}
				if (howFarIntoNote < newNote->getLength()) {
					attemptLateStartOfNextNoteToPlay(modelStack, newNote);
				}
			}
		}

		*firstNote = *lastNote = newNote;
		return SQUARE_NEW_NOTE;
	}

	// Start by finding the last note to begin *before the right-edge* of this square

	int32_t squareEndPos = squareStart + squareWidth;
	int32_t i = notes.search(squareEndPos, LESS);

	Note* note = notes.getElement(i);

	// If Note starts somewhere within this square...
	if (note && note->pos >= squareStart) {
		*firstNote = *lastNote = note;

		// See if there were any other previous notes in that square
		while (true) {
			i--;
			if (i < 0) {
				break;
			}
			Note* thisNote = notes.getElement(i);
			if (thisNote->pos >= squareStart) {
				*firstNote = thisNote;
			}
			else {
				break;
			}
		}

		// And return whether it was multiple notes or just one
		return (*firstNote == *lastNote) ? SQUARE_NOTE_HEAD : SQUARE_BLURRED;
	}

	// Or if the note starts left of this square, or there's no note there which means we'll look at the final one
	// wrapping around...
	else {
		bool wrapping = (i == -1);
		if (wrapping) {
			note = notes.getLast();
		}
		int32_t noteEnd = note->pos + note->getLength();
		if (wrapping) {
			noteEnd -= effectiveLength;
		}

		// If that note's tail does overlap into this square...
		if (noteEnd > squareStart) {

			// We have a tail! But if tails aren't allowed, cut it off and make a new note here instead
			if (!allowNoteTails) {
				if (action) {
					action->recordNoteArrayChangeIfNotAlreadySnapshotted(
					    (InstrumentClip*)modelStack->getTimelineCounter(), modelStack->noteRowId, &notes, false);
				}
				note->setLength(note->getLength() - (noteEnd - squareStart));
				goto addNewNote;
			}

			*firstNote = *lastNote = note;
			return SQUARE_NOTE_TAIL_UNMODIFIED;
		}

		// Or if note's tail does not overlap this square, making this square completely empty...
		else {

			// If we're wanting to extend, and tails are allowed...
			if (extendPreviousNoteIfPossible && allowNoteTails) {

				int32_t newLength;

				// If previously extended into this square, cut it to go to just the start of the square
				if (noteEnd > squareStart) {
					newLength = note->length - (noteEnd - squareStart);

					// Otherwise, fill the tapped square with it
				}
				else {
					newLength = note->length + squareStart + squareWidth - noteEnd;
				}

				complexSetNoteLength(note, newLength, modelStack, action);

				*firstNote = *lastNote = note;

				return SQUARE_NOTE_TAIL_MODIFIED;
			}
			else {
				goto addNewNote;
			}
		}
	}
}

Error NoteRow::addCorrespondingNotes(int32_t targetPos, int32_t newNotesLength, uint8_t velocity,
                                     ModelStackWithNoteRow* modelStack, bool allowNoteTails, Action* action) {

	uint32_t wrapEditLevel = ((InstrumentClip*)modelStack->getTimelineCounter())->getWrapEditLevel();
	int32_t posWithinEachScreen = (uint32_t)targetPos % wrapEditLevel;
	int32_t effectiveLength = modelStack->getLoopLength();

	if (newNotesLength > wrapEditLevel) {
		newNotesLength = wrapEditLevel;
	}

	int32_t numScreensToAddNoteOn =
	    (uint32_t)(effectiveLength + wrapEditLevel - posWithinEachScreen - 1) / wrapEditLevel;

	// Allocate all the working memory we're going to need for this operation - that's arrays for searchPos and
	// resultingIndexes
	int32_t* __restrict__ searchTerms =
	    (int32_t*)GeneralMemoryAllocator::get().allocMaxSpeed(numScreensToAddNoteOn * sizeof(int32_t));
	if (!searchTerms) {
		return Error::INSUFFICIENT_RAM;
	}

	// Make new NoteVector to copy into as we go through each screen - and pre-allocate the max amount of memory we
	// might need
	NoteVector newNotes;
	int32_t newNotesInitialSize = notes.getNumElements() + numScreensToAddNoteOn;
	Error error = newNotes.insertAtIndex(0, newNotesInitialSize);
	if (error != Error::NONE) {
		delugeDealloc(searchTerms);
		return error;
	}

	// Populate big list of all the positions we want to insert a note (plus 1)
	int32_t searchPosThisScreen = posWithinEachScreen + 1;
	for (int32_t i = 0; i < numScreensToAddNoteOn; i++) {
		searchTerms[i] = searchPosThisScreen;
		searchPosThisScreen += wrapEditLevel;
	}

	// Search for all those positions. Will return the indexes of the next note at GREATER_OR_EQUAL to that position
	notes.searchMultiple(searchTerms, numScreensToAddNoteOn);

	int32_t nextIndexToCopyFrom = 0;
	int32_t nextIndexToCopyTo = 0;

	Note* __restrict__ sourceNote = NULL;
	Note* __restrict__ destNote = NULL;

	// For each screen we're going to add a note on, copy all notes prior to (and including) the insertion point
	for (int32_t screenIndex = 0; screenIndex < numScreensToAddNoteOn; screenIndex++) {

		int32_t thisResultingIndex = searchTerms[screenIndex];

		// Copy all notes before this one (which is to the right of the insertion pos, so if there is already
		// one at the insertion pos, we'll be copying that too.
		while (nextIndexToCopyFrom < thisResultingIndex) {
			sourceNote = notes.getElement(nextIndexToCopyFrom);
			destNote = newNotes.getElement(nextIndexToCopyTo);

			*destNote = *sourceNote;

			nextIndexToCopyFrom++;
			nextIndexToCopyTo++;
		}

		int32_t posThisScreen = screenIndex * wrapEditLevel + posWithinEachScreen;
		int32_t prevNoteMaxLength;

		// If haven't copied any Notes since last insertion (and remember, there won't always be an insertion because
		// there might already be a note there), then we do want to insert a new one, but not modify any old ones
		if (!sourceNote) {
			goto addNewNote;
		}

		// If we're here cos there's a sourceNote, that always means there's also a destNote, and the two refer to the
		// same note from the user's perspective (though in different arrays).

		// Or if the last thing we did was copy some Notes (including possibly the last one being already *at* the
		// insertion pos for the previous screen)...
		prevNoteMaxLength = posThisScreen - destNote->pos;
		if (prevNoteMaxLength > 0) {

			// Make sure that Note (in its copied destination) doesn't extend past the insertion pos...
			if (destNote->length > prevNoteMaxLength) {
				destNote->setLength(prevNoteMaxLength);
			}

addNewNote:
			sourceNote = NULL; // Reset it - we now (briefly) will not have copied any notes since inserting this one

			// And insert a new note at the position within this screen
			destNote = newNotes.getElement(nextIndexToCopyTo);
			destNote->pos = posThisScreen;
			destNote->setVelocity(velocity);
			destNote->setLift(kDefaultLiftValue);
			destNote->setProbability(getDefaultProbability());
			destNote->setIterance(getDefaultIterance());
			destNote->setFill(getDefaultFill(modelStack));

			int32_t newLength;

			// If there are more notes coming up to the right, just make sure we're not gonna eat into em
			if (notes.getNumElements() > thisResultingIndex) {
				Note* nextNote = notes.getElement(thisResultingIndex);
				newLength = std::min(newNotesLength, (nextNote->pos - posThisScreen));
			}

			// Otherwise, make sure we don't eat into the first note when we wrap back around
			else {
				Note* firstNote = notes.getElement(0);
				newLength = std::min(newNotesLength, (firstNote->pos + effectiveLength - posWithinEachScreen));
			}

			destNote->setLength(newLength);

			nextIndexToCopyTo++;
		}
	}

	// Deallocate working memory - no longer needed
	delugeDealloc(searchTerms);

	// Copy the final notes too - after the insertion-point on the final screen
	while (nextIndexToCopyFrom < notes.getNumElements()) {
		sourceNote = notes.getElement(nextIndexToCopyFrom);
		destNote = newNotes.getElement(nextIndexToCopyTo);

		*destNote = *sourceNote;

		nextIndexToCopyFrom++;
		nextIndexToCopyTo++;
	}

	// For the final note (which could be either one we copied or one we inserted) make sure it doesn't wrap around and
	// eat into the first note (which also could have been either copied or inserted)
	if (destNote && nextIndexToCopyTo >= 2) {
		Note* __restrict__ firstNote = newNotes.getElement(0);

		int32_t maxLengthThisNote = effectiveLength - destNote->pos + firstNote->pos;
		if (destNote->length > maxLengthThisNote) {
			destNote->setLength(maxLengthThisNote);
		}
	}

	// Delete any Notes we initially created (we create in bulk for efficiency) but then ended up not using
	int32_t numToDelete = newNotesInitialSize - nextIndexToCopyTo;
	if (numToDelete > 0) {
		newNotes.deleteAtIndex(nextIndexToCopyTo, numToDelete);
	}

	// Record change, stealing the old note data
	if (action) {
		action->recordNoteArrayChangeIfNotAlreadySnapshotted((InstrumentClip*)modelStack->getTimelineCounter(),
		                                                     modelStack->noteRowId, &notes, true);
	}

	// Swap the new temporary note data into the permanent place
	notes.swapStateWith(&newNotes);

#if ENABLE_SEQUENTIALITY_TESTS
	notes.testSequentiality("E318");
#endif

	((InstrumentClip*)modelStack->getTimelineCounter())->expectEvent();

	return Error::NONE;
}

int32_t NoteRow::getDefaultProbability() {
	return probabilityValue;
}

int32_t NoteRow::getDefaultIterance() {
	return iteranceValue;
}

int32_t NoteRow::getDefaultFill(ModelStackWithNoteRow* modelStack) {
	if (modelStack->song->isFillModeActive()) {
		return FillMode::FILL;
	}
	else {
		return fillValue;
	}
}

// This gets called after we've scrolled and attempted to drag notes. And for recording.
// If you supply an Action, it'll add an individual ConsequenceNoteExistenceChange. Or, you can supply NULL and do
// something else yourself. Returns distanceToNextNote, or 0 on fail.
int32_t NoteRow::attemptNoteAdd(int32_t pos, int32_t length, int32_t velocity, int32_t probability, int32_t iterance,
                                int32_t fill, ModelStackWithNoteRow* modelStack, Action* action) {

	int32_t loopLength = modelStack->getLoopLength();

	int32_t i = 0;
	int32_t distanceToNextNote = loopLength;

	if (notes.getNumElements()) {
		// If there's already a Note overlapping this space, we can't.
		i = notes.search(pos + 1, GREATER_OR_EQUAL);
		int32_t iLeft = i - 1;
		bool wrappingLeft = (iLeft == -1);
		if (wrappingLeft) {
			iLeft = notes.getNumElements() - 1;
		}
		Note* noteLeft = notes.getElement(iLeft);
		int32_t noteLeftEnd = noteLeft->pos + noteLeft->length;
		if (wrappingLeft) {
			noteLeftEnd -= loopLength;
		}

		if (noteLeftEnd > pos) {
			return 0;
		}

		// Figure out distanceToNextNote
		int32_t iRight = i;
		bool wrappingRight = (iRight == notes.getNumElements());
		if (wrappingRight) {
			iRight = 0;
		}
		Note* noteRight = notes.getElement(iRight);
		int32_t noteRightStart = noteRight->pos;
		if (wrappingRight) {
			noteRightStart += loopLength;
		}
		distanceToNextNote = noteRightStart - pos;

		// Ok, there was no Note there, so let's make one
	}

	length = std::min(length, distanceToNextNote); // Limit length
	if (length <= 0) {
		length = 1; // Special case where note added at the end of linear record must temporarily be allowed to eat into
		            // note at position 0
	}
	Error error = notes.insertAtIndex(i);
	if (error != Error::NONE) {
		return 0;
	}
	Note* newNote = notes.getElement(i);
	newNote->pos = pos;
	newNote->setLength(length);
	newNote->setVelocity(velocity);
	newNote->setLift(kDefaultLiftValue);
	newNote->setProbability(probability);
	newNote->setIterance(iterance);
	newNote->setFill(fill);

	// Record consequence
	if (action) {
		action->recordNoteExistenceChange(
		    (InstrumentClip*)modelStack->getTimelineCounter(), modelStack->noteRowId, newNote,
		    ExistenceChangeType::CREATE); // This only gets called (action is only supplied) when drag-scrolling Notes
	}

	modelStack->getTimelineCounter()->expectEvent();
	return distanceToNextNote;
}

// Returns distanceToNextNote, or 0 on fail.
int32_t NoteRow::attemptNoteAddReversed(ModelStackWithNoteRow* modelStack, int32_t pos, int32_t velocity,
                                        bool allowingNoteTails) {

	int32_t loopLength = modelStack->getLoopLength();
	// The length-1 note will be placed at pos-1.
	int32_t insertionPos = pos - (int32_t)allowingNoteTails;
	if (insertionPos < 0) {
		insertionPos += loopLength;
	}

	int32_t i = 0;
	int32_t distanceToNextNote = loopLength;

	if (notes.getNumElements()) {
		i = notes.search(insertionPos + 1, GREATER_OR_EQUAL);
		int32_t iLeft = i - 1;
		bool wrappingLeft = (iLeft == -1);
		if (wrappingLeft) {
			iLeft = notes.getNumElements() - 1;
		}
		Note* noteLeft = notes.getElement(iLeft);
		int32_t noteLeftEnd = noteLeft->pos + noteLeft->length;
		if (wrappingLeft) {
			noteLeftEnd -= loopLength;
		}

		if (noteLeftEnd > insertionPos) {
			return 0;
		}

		distanceToNextNote = pos - noteLeftEnd;

		// Ok, there was no Note there, so let's make one
	}

	Error error = notes.insertAtIndex(i);
	if (error != Error::NONE) {
		return 0;
	}
	Note* newNote = notes.getElement(i);
	newNote->pos = insertionPos;
	newNote->setLength(1);
	newNote->setVelocity(velocity);
	newNote->setLift(kDefaultLiftValue);
	newNote->setProbability(getDefaultProbability());
	newNote->setIterance(getDefaultIterance());
	newNote->setFill(getDefaultFill(modelStack));

	modelStack->getTimelineCounter()->expectEvent();

	return distanceToNextNote;
}

Error NoteRow::clearArea(int32_t areaStart, int32_t areaWidth, ModelStackWithNoteRow* modelStack, Action* action,
                         uint32_t wrapEditLevel, bool actuallyExtendNoteAtStartOfArea) {

	// If no Notes, nothing to do.
	if (!notes.getNumElements()) {
		return Error::NONE;
	}
	// It'd also be tempting to just abort if there were no notes within the area-to-clear, but remember, we also might
	// need to shorten note tails leading into that area

	int32_t effectiveLength = modelStack->getLoopLength();

	areaStart = (uint32_t)areaStart % wrapEditLevel;
	int32_t numScreens = (uint32_t)(effectiveLength - 1) / wrapEditLevel + 1;

	// Allocate all the working memory we're going to need for this operation - that's arrays for searchPos and
	// resultingIndexes
	int32_t* __restrict__ searchTerms =
	    (int32_t*)GeneralMemoryAllocator::get().allocMaxSpeed(numScreens * 2 * sizeof(int32_t));
	if (!searchTerms) {
		return Error::INSUFFICIENT_RAM;
	}

	// Make new NoteVector to copy into as we go through each screen - and pre-allocate the max amount of memory we
	// might need
	NoteVector newNotes;
	int32_t newNotesInitialSize = notes.getNumElements();
	Error error = newNotes.insertAtIndex(0, newNotesInitialSize);
	if (error != Error::NONE) {
		delugeDealloc(searchTerms);
		return error;
	}

	// Populate big list of all the positions we want to search. There's one each for the start *and* the end of each
	// area
	int32_t areaStartThisScreen =
	    areaStart
	    + actuallyExtendNoteAtStartOfArea; // If actuallyExtendNoteAtStartOfArea, then only clear from 1 tick later
	int32_t areaEndThisScreen = areaStart + areaWidth;

	for (int32_t i = 0; i < (numScreens << 1); i++) {
		searchTerms[i] = areaStartThisScreen;
		i++;
		searchTerms[i] = areaEndThisScreen;

		areaStartThisScreen += wrapEditLevel;
		areaEndThisScreen += wrapEditLevel;
	}

	// Search for all those positions. Will return the indexes of the next note at GREATER_OR_EQUAL to that position
	notes.searchMultiple(searchTerms, numScreens << 1);

	int32_t nextIndexToCopyFrom = 0;
	int32_t nextIndexToCopyTo = 0;

	// For each screen, copy all notes prior to area begin
	for (int32_t screenIndex = 0; screenIndex < numScreens; screenIndex++) {

		int32_t areaBeginIndexThisScreen = searchTerms[screenIndex << 1];

		Note* __restrict__ destNote = NULL;

		// Copy all notes before this one (which is to the right of the area begin).
		while (nextIndexToCopyFrom < areaBeginIndexThisScreen) {
			Note* __restrict__ sourceNote = notes.getElement(nextIndexToCopyFrom);
			destNote = newNotes.getElement(nextIndexToCopyTo);

			*destNote = *sourceNote;

			nextIndexToCopyFrom++;
			nextIndexToCopyTo++;
		}

		// If we copied some Notes just then, before the start of this area but after the previous one...
		if (destNote) {

			int32_t areaBeginPosThisScreen = screenIndex * wrapEditLevel + areaStart;

			// If we're actually looking for a Note right at the area-start, to extend to the full width...
			if (actuallyExtendNoteAtStartOfArea) {

				// If the Note does actually begin right at the area-start, make it the full area length
				if (destNote->pos == areaBeginPosThisScreen) {
					destNote->setLength(areaWidth);
				}

				// Or if it started earlier, then we'll flag everything to do with this area, including any clearing
				else {
					continue; // Crucially skip the final line of the loop, below, which says don't copy any more til
					          // after the area
				}
			}

			// Or, if we're just doing a plain old clear, then ensure the last Note doesn't extend into the area
			else {
				int32_t maxLengthThisNote = areaBeginPosThisScreen - destNote->pos;
				if (destNote->length > maxLengthThisNote) {
					destNote->setLength(maxLengthThisNote);
				}
			}
		}

		// Or if we didn't just copy any Notes, then only in the case that we're doing note-extending, don't bother
		// clearing the area
		else {
			if (actuallyExtendNoteAtStartOfArea) {
				continue;
			}
		}

		// And now, don't copy any more until the area end
		nextIndexToCopyFrom = searchTerms[(screenIndex << 1) + 1];
	}

	// Deallocate working memory - no longer needed
	delugeDealloc(searchTerms);

	Note* __restrict__ destNote = NULL;

	// Copy the final notes too - after area end on the final screen
	while (nextIndexToCopyFrom < notes.getNumElements()) {
		Note* __restrict__ sourceNote = notes.getElement(nextIndexToCopyFrom);
		destNote = newNotes.getElement(nextIndexToCopyTo);

		*destNote = *sourceNote;

		nextIndexToCopyFrom++;
		nextIndexToCopyTo++;
	}

	// If we copied some Notes after the final area, ensure the final Note doesn't wrap around into the first area
	if (destNote) {

		int32_t posNotAllowedToExtendPast;

		if (actuallyExtendNoteAtStartOfArea) {
			if (nextIndexToCopyTo < 2) {
				goto thatsDone;
			}
			Note* __restrict__ firstNote = newNotes.getElement(0);
			posNotAllowedToExtendPast = firstNote->pos;
		}
		else {
			posNotAllowedToExtendPast = areaStart;
		}

		int32_t maxLengthThisNote = effectiveLength - destNote->pos + posNotAllowedToExtendPast;
		if (destNote->length > maxLengthThisNote) {
			destNote->setLength(maxLengthThisNote);
		}
	}
thatsDone:

	// Delete any Notes we initially created (we create in bulk for efficiency) but then ended up not using
	int32_t numToDelete = newNotesInitialSize - nextIndexToCopyTo;
	if (numToDelete > 0) {
		newNotes.deleteAtIndex(nextIndexToCopyTo, numToDelete);
	}

	// Record change, stealing the old note data
	if (action) {
		action->recordNoteArrayChangeIfNotAlreadySnapshotted((InstrumentClip*)modelStack->getTimelineCounter(),
		                                                     modelStack->noteRowId, &notes, true);
	}

	// Swap the new temporary note data into the permanent place
	notes.swapStateWith(&newNotes);

#if ENABLE_SEQUENTIALITY_TESTS
	notes.testSequentiality("E319");
#endif

	((InstrumentClip*)modelStack->getTimelineCounter())->expectEvent();

	return Error::NONE;
}

void NoteRow::recordNoteOff(uint32_t noteOffPos, ModelStackWithNoteRow* modelStack, Action* action, int32_t velocity) {

	// If we're just about to pass the actual recorded note, though, don't do it.
	if (noteOffPos < ignoreNoteOnsBefore_) {
		return;
	}

	if (!notes.getNumElements()) {
		return;
	}

	int32_t effectiveLength = modelStack->getLoopLength();
	bool reversed = modelStack->isCurrentlyPlayingReversed();
	Note* note;
	int32_t newLength;
	int32_t newNoteLeftPos;

	int32_t i = notes.search(noteOffPos + !reversed, GREATER_OR_EQUAL) - !reversed;
	bool wrapping = (i == -1 || i == notes.getNumElements());
	if (wrapping) {

		// If pingponging, do something quite unique
		if (getEffectiveSequenceDirectionMode(modelStack) == SequenceDirection::PINGPONG) {
			note = notes.getElement((notes.getNumElements() - 1) * reversed); // Will be 0 if playing forwards
			newNoteLeftPos = note->pos * reversed;                            // Will be 0 if playing forwards
			newLength = reversed ? (effectiveLength - note->pos) : (note->pos + note->length);
			wrapping = false; // Ensure we don't execute that if-block below
			goto modifyNote;
		}
		i = (notes.getNumElements() - 1) * !reversed;
	}

	note = notes.getElement(i);

	{
		int32_t notePos = note->pos;
		if (wrapping) {
			notePos += reversed ? effectiveLength : -effectiveLength;
		}
		newLength = noteOffPos - notePos;
	}
	if (reversed) {
		newLength = -newLength;
	}
	newNoteLeftPos = reversed ? noteOffPos : note->pos;

modifyNote:
	// Don't allow a note to be created that's the length of the whole Clip. Because there's a scenario that can lead to
	// this happening erroneously
	if (newLength < effectiveLength) {
		if (newLength <= 0) {
			newLength = 1;
		}
		if (action) {
			action->recordNoteArrayChangeIfNotAlreadySnapshotted((InstrumentClip*)modelStack->getTimelineCounter(),
			                                                     modelStack->noteRowId, &notes, false);
		}

		// Doing a wrap while reversed is unique because we have to move our note from one end of the array to the other
		if (wrapping && reversed) {
			int32_t probability = note->getProbability();
			int32_t iterance = note->getIterance();
			int32_t fill = note->getFill();
			int32_t noteOnVelocity = note->getVelocity();
			notes.deleteAtIndex(0, 1, false);
			i = notes.getNumElements();
			notes.insertAtIndex(i); // Shouldn't be able to fail.
			note = notes.getElement(i);
			note->setProbability(probability);
			note->setVelocity(noteOnVelocity);
			note->setIterance(iterance);
			note->setFill(fill);
		}

		note->pos = newNoteLeftPos; // This will stay the same if we're pingponging
		note->setLength(newLength);
		note->setLift(velocity);
	}

	ignoreNoteOnsBefore_ = 0;

	((InstrumentClip*)modelStack->getTimelineCounter())->expectEvent();
}

void NoteRow::complexSetNoteLength(Note* thisNote, uint32_t newLength, ModelStackWithNoteRow* modelStack,
                                   Action* action) {

	// If wrap-editing, do that
	if (((InstrumentClip*)modelStack->getTimelineCounter())->wrapEditing
	    && newLength <= ((InstrumentClip*)modelStack->getTimelineCounter())->getWrapEditLevel()) {

		if (newLength != thisNote->length) {
			int32_t areaStart, areaWidth;
			bool actuallyExtendNoteAtStartOfArea = (newLength > thisNote->length);

			if (actuallyExtendNoteAtStartOfArea) { // Increasing length
				areaStart = thisNote->pos;
				areaWidth = newLength;
			}

			else { // Decreasing length
				areaStart = thisNote->pos + newLength;
				areaWidth = thisNote->length - newLength;
			}

			clearArea(areaStart, areaWidth, modelStack, action,
			          ((InstrumentClip*)modelStack->getTimelineCounter())->getWrapEditLevel(), true);
		}
	}

	// If not wrap-editing, it's easy - we can just set the length of this one Note
	else {
		if (action) {
			action->recordNoteArrayChangeIfNotAlreadySnapshotted((InstrumentClip*)modelStack->getTimelineCounter(),
			                                                     modelStack->noteRowId, &notes, false);
		}
		thisNote->setLength(newLength);
	}

	((InstrumentClip*)modelStack->getTimelineCounter())->expectEvent();
}

// Caller must call expectEvent on Clip after this
Error NoteRow::editNoteRepeatAcrossAllScreens(int32_t editPos, int32_t squareWidth, ModelStackWithNoteRow* modelStack,
                                              Action* action, uint32_t wrapEditLevel, int32_t newNumNotes) {

	int32_t numSourceNotes = notes.getNumElements();

	// If no Notes, nothing to do.
	if (!numSourceNotes) {
		return Error::NONE;
	}

	editPos = (uint32_t)editPos % wrapEditLevel;
	int32_t effectiveLength = modelStack->getLoopLength();

	// The "area" is the notes that'll get deleted as a result of the nudge *if* there's a note in each place
	int32_t areaStart = editPos;
	int32_t areaEnd = editPos + squareWidth;

	int32_t numScreens = (uint32_t)(effectiveLength - 1) / wrapEditLevel + 1;

	// Allocate all the working memory we're going to need for this operation - that's arrays for searchPos and
	// resultingIndexes
	int32_t* __restrict__ searchTerms =
	    (int32_t*)GeneralMemoryAllocator::get().allocMaxSpeed(numScreens * 2 * sizeof(int32_t));
	if (!searchTerms) {
		return Error::INSUFFICIENT_RAM;
	}

	bool wrappingLeft = false;

	// Make new NoteVector to copy into as we go through each screen - and pre-allocate the max amount of memory we
	// might need
	NoteVector newNotes;
	int32_t newNotesInitialSize = numSourceNotes + (newNumNotes - 1) * numScreens;
	Error error = newNotes.insertAtIndex(0, newNotesInitialSize);
	if (error != Error::NONE) {
		delugeDealloc(searchTerms);
		return error;
	}

	// Populate big list of all the positions we want to search. There's one each for the start *and* the end of each
	// area
	int32_t areaStartThisScreen = areaStart;
	int32_t areaEndThisScreen = areaEnd;

	for (int32_t i = 0; i < (numScreens << 1); i++) {
		searchTerms[i] = areaStartThisScreen;
		i++;
		searchTerms[i] = areaEndThisScreen;

		areaStartThisScreen += wrapEditLevel;
		areaEndThisScreen += wrapEditLevel;
	}

	// Make sure the last area doesn't extend beyond the Clip length
	if (searchTerms[(numScreens << 1) - 1] > effectiveLength) {
		searchTerms[(numScreens << 1) - 1] = effectiveLength;
	}

	int32_t nextIndexToCopyFrom = 0;
	int32_t nextIndexToCopyTo = 0;

	// Search for all those positions. Will return the indexes of the next note at GREATER_OR_EQUAL to that position
	notes.searchMultiple(searchTerms, numScreens << 1, numSourceNotes);

	// For each screen, copy all notes prior to area begin
	for (int32_t screenIndex = 0; screenIndex < numScreens; screenIndex++) {

		int32_t newNumNotesThisScreen = newNumNotes;
		int32_t squareWidthThisScreen = squareWidth;

		int32_t areaBeginPosThisScreen = wrapEditLevel * screenIndex + areaStart;

		// If on the final screen, we need to make sure our "area" doesn't overlap that
		if (screenIndex == numScreens - 1) {
			int32_t areaEndPosThisScreen = areaBeginPosThisScreen + squareWidthThisScreen;
			if (areaEndPosThisScreen > effectiveLength) {
				squareWidthThisScreen = effectiveLength - areaBeginPosThisScreen;
				D_PRINTLN("square width cut short:  %d", newNumNotesThisScreen);

				// If that's ended up 0 or negative, there's nothing for us to do. Though there'd probably be no harm if
				// this check wasn't here, and in a perfect world maybe we'd check this before deciding how many search
				// terms?
				if (squareWidthThisScreen <= 0) {
					break; // Any further copying will still happen below, outside of loop
				}

				// And limit the number of notes, too
				if (newNumNotesThisScreen > squareWidthThisScreen) {
					newNumNotesThisScreen = squareWidthThisScreen;
				}
			}
		}

		int32_t areaBeginIndexThisScreen = searchTerms[screenIndex << 1];
		int32_t areaEndIndexThisScreen = searchTerms[(screenIndex << 1) + 1];
		int32_t oldNumNotesThisScreen = areaEndIndexThisScreen - areaBeginIndexThisScreen;

		int32_t copyNumRepeatingNotes = std::min(oldNumNotesThisScreen, newNumNotesThisScreen);
		int32_t copyUntil = areaBeginIndexThisScreen + copyNumRepeatingNotes;

		// Copy all notes before this one
		while (nextIndexToCopyFrom < copyUntil) {
			Note* __restrict__ sourceNote = notes.getElement(nextIndexToCopyFrom);
			Note* __restrict__ destNote = newNotes.getElement(nextIndexToCopyTo);

			*destNote = *sourceNote;

			nextIndexToCopyFrom++;
			nextIndexToCopyTo++;
		}

		// If at least one note already existed...
		if (oldNumNotesThisScreen) {

			int32_t firstRepeatingNoteDestIndex = nextIndexToCopyTo - copyNumRepeatingNotes;

			// If adding more notes...
			if (oldNumNotesThisScreen < newNumNotesThisScreen) {

				// We'll need to get the attributes of that first note
				Note* __restrict__ firstNote = newNotes.getElement(firstRepeatingNoteDestIndex);

				// Add in all the new notes we need
				int32_t stopAddingAt = firstRepeatingNoteDestIndex + newNumNotesThisScreen;
				while (nextIndexToCopyTo < stopAddingAt) {
					Note* __restrict__ destNote = newNotes.getElement(nextIndexToCopyTo);
					*destNote = *firstNote;
					nextIndexToCopyTo++;
				}
			}

			// Now go over all those repeating notes and set their positions so they're spaced out
			for (int32_t n = 0; n < newNumNotesThisScreen; n++) {

				Note* __restrict__ newNote = newNotes.getElement(firstRepeatingNoteDestIndex + n);

				int32_t newDistanceIn = squareWidthThisScreen * n / newNumNotesThisScreen;

				newNote->pos = areaBeginPosThisScreen + newDistanceIn;

				int32_t nextDistanceIn = squareWidthThisScreen * (n + 1) / newNumNotesThisScreen;

				newNote->length = std::min(newNote->length, (nextDistanceIn - newDistanceIn));
			}

			nextIndexToCopyFrom = areaEndIndexThisScreen;
		}
	}

	// Deallocate working memory - no longer needed
	delugeDealloc(searchTerms);

	// Copy the final notes too - after area end on the final screen
	while (nextIndexToCopyFrom < numSourceNotes) {
		Note* __restrict__ sourceNote = notes.getElement(nextIndexToCopyFrom);
		Note* __restrict__ destNote = newNotes.getElement(nextIndexToCopyTo);

		*destNote = *sourceNote;

		nextIndexToCopyFrom++;
		nextIndexToCopyTo++;
	}

	// Delete any Notes we initially created (we create in bulk for efficiency) but then ended up not using
	int32_t numToDelete = newNotesInitialSize - nextIndexToCopyTo;
	if (numToDelete > 0) {
		newNotes.deleteAtIndex(nextIndexToCopyTo, numToDelete);
	}
#if ALPHA_OR_BETA_VERSION
	else if (numToDelete < 0) { // If we overshot somehow
		FREEZE_WITH_ERROR("E329");
	}
#endif

	// Record change, stealing the old note data
	if (action) {
		// We "definitely" store the change, because unusually, we may want to revert individual Consequences in the
		// Action one by one
		action->recordNoteArrayChangeDefinitely((InstrumentClip*)modelStack->getTimelineCounter(),
		                                        modelStack->noteRowId, &notes, true);
	}

	// Swap the new temporary note data into the permanent place
	notes.swapStateWith(&newNotes);

#if ENABLE_SEQUENTIALITY_TESTS
	notes.testSequentiality("E328");
#endif

	return Error::NONE;
}

Error NoteRow::nudgeNotesAcrossAllScreens(int32_t editPos, ModelStackWithNoteRow* modelStack, Action* action,
                                          uint32_t wrapEditLevel, int32_t nudgeOffset) {

	int32_t numSourceNotes = notes.getNumElements();

	// If no Notes, nothing to do.
	if (!numSourceNotes) {
		return Error::NONE;
	}

	editPos = (uint32_t)editPos % wrapEditLevel;
	int32_t effectiveLength = modelStack->getLoopLength();

	// The "area" is the notes that'll get deleted as a result of the nudge *if* there's a note in each place
	int32_t areaStart;
	int32_t areaEnd;
	if (nudgeOffset < 0) {
		areaStart = editPos - 1;
		areaEnd = editPos;
	}
	else {
		areaStart = editPos + 1;
		areaEnd = editPos + 2;
	}

	int32_t numScreens = (uint32_t)(effectiveLength - 1) / wrapEditLevel + 1;

	// Allocate all the working memory we're going to need for this operation - that's arrays for searchPos and
	// resultingIndexes
	int32_t* __restrict__ searchTerms =
	    (int32_t*)GeneralMemoryAllocator::get().allocMaxSpeed(numScreens * 2 * sizeof(int32_t));
	if (!searchTerms) {
		return Error::INSUFFICIENT_RAM;
	}

	bool wrappingLeft = false;

	// Make new NoteVector to copy into as we go through each screen - and pre-allocate the max amount of memory we
	// might need
	NoteVector newNotes;
	int32_t newNotesInitialSize = numSourceNotes;
	Error error = newNotes.insertAtIndex(0, newNotesInitialSize);
	if (error != Error::NONE) {
		delugeDealloc(searchTerms);
		return error;
	}

	// Populate big list of all the positions we want to search. There's one each for the start *and* the end of each
	// area
	// TODO: we could really do all this with only 1 search term per screen
	int32_t areaStartThisScreen = areaStart;
	int32_t areaEndThisScreen = areaEnd;

	for (int32_t i = 0; i < (numScreens << 1); i++) {
		searchTerms[i] = areaStartThisScreen;
		i++;
		searchTerms[i] = areaEndThisScreen;

		areaStartThisScreen += wrapEditLevel;
		areaEndThisScreen += wrapEditLevel;
	}

	int32_t nextIndexToCopyFrom = 0;
	int32_t nextIndexToCopyTo = 0;
	Note* __restrict__ destNote = NULL;

	// Deal with wrapping right
	if (nudgeOffset >= 0 && (numScreens - 1) * wrapEditLevel + editPos + 1 == effectiveLength) {
		Note* __restrict__ lastSourceNote = notes.getElement(numSourceNotes - 1);
		if (lastSourceNote->pos == effectiveLength - 1) {
			D_PRINTLN("wrapping right");
			destNote = newNotes.getElement(nextIndexToCopyTo);
			*destNote = *lastSourceNote;
			destNote->pos = 0;

			// Check whether that's chopped off any Note at pos 0 which wouldn't itself get nudged to the right
			Note* __restrict__ firstSourceNote = notes.getElement(0);
			if (firstSourceNote->pos == 0 && editPos != 0) {
				nextIndexToCopyFrom = 1;
			}

			// And now, that note we wrapped around, ensure its length isn't too long
			Note* __restrict__ nextSourceNote = notes.getElement(nextIndexToCopyFrom);
			int32_t maxLength = nextSourceNote->pos;
			if (destNote->length > maxLength) {
				// But only if that next note won't itself get nudged!
				if (((uint32_t)nextSourceNote->pos % wrapEditLevel) != editPos) {
					D_PRINTLN("constraining length in right wrap");
					destNote->length = maxLength;
				}
			}

			destNote = NULL;     // We don't want to do anything further to this Note below like we normally would after
			                     // copying a nudge one
			numSourceNotes--;    // Don't copy the last one when we get to it - we've just done it
			nextIndexToCopyTo++; // We've now copied our first destination note
		}
	}

	// Search for all those positions. Will return the indexes of the next note at GREATER_OR_EQUAL to that position
	notes.searchMultiple(searchTerms, numScreens << 1, numSourceNotes);

	bool firstNoteGotNudgedLeft = false;
	bool nextNoteGetsNudgedLeft = false;

	// For each screen, copy all notes prior to area begin
	for (int32_t screenIndex = 0; screenIndex < numScreens; screenIndex++) {

		int32_t areaBeginIndexThisScreen = searchTerms[screenIndex << 1];

		// Copy all notes before this one (which is to the right of the area begin).
		while (nextIndexToCopyFrom < areaBeginIndexThisScreen) {
			Note* __restrict__ sourceNote = notes.getElement(nextIndexToCopyFrom);
			destNote = newNotes.getElement(nextIndexToCopyTo);

			*destNote = *sourceNote;
			if (nextNoteGetsNudgedLeft) {
				nextNoteGetsNudgedLeft = false;
				destNote->pos--;
			}

			nextIndexToCopyFrom++;
			nextIndexToCopyTo++;
		}

		// If nudging left...
		if (nudgeOffset < 0) {

			// Is there an actual note for us to nudge left?
			int32_t nudgeNoteIndex = searchTerms[(screenIndex << 1) + 1];
			if (nudgeNoteIndex < numSourceNotes) {

				int32_t preNudgeNotePos = wrapEditLevel * screenIndex + editPos;
				Note* __restrict__ noteToNudge = notes.getElement(nudgeNoteIndex);

				if (noteToNudge->pos == preNudgeNotePos) {
					// Ok, we've got one we'll be nudging left.
					D_PRINTLN("nudging note left");

					if (preNudgeNotePos == 0) {
						wrappingLeft = true;
						nextIndexToCopyFrom = nudgeNoteIndex + 1; // Skip it
					}

					else {

						// Make sure the previous one won't eat into this one
						if (destNote) {
							int32_t postNudgeNotePos = preNudgeNotePos - 1;
							int32_t maxLength = postNudgeNotePos - destNote->pos;
							if (destNote->length > maxLength) {
								D_PRINTLN("constraining length of prev note");
								destNote->length = maxLength;
							}
						}
						else {
							firstNoteGotNudgedLeft = true;
						}

						// Now copy / nudge the note to nudge
						nextNoteGetsNudgedLeft = true;
						nextIndexToCopyFrom = nudgeNoteIndex;
					}
				}
			}
		}

		// If nudging right...
		else {
			// We've already copied the nudge note, if there was one.

			if (destNote) {
				int32_t preNudgeNotePos = wrapEditLevel * screenIndex + editPos;

				// If there was a nudge note, it will be the last one we copied. If so...
				if (destNote->pos == preNudgeNotePos) {
					D_PRINTLN("nudging note right");

					int32_t postNudgeNotePos = preNudgeNotePos + 1;
					destNote->pos = postNudgeNotePos; // Nudge it

					nextIndexToCopyFrom =
					    searchTerms[(screenIndex << 1) + 1]; // Skip any Notes that'll get deleted (only now that we
					                                         // know we actually have nudged a Note)

					int32_t maxLength;

					// Now look to the next source Note
					if (nextIndexToCopyFrom < numSourceNotes) { // If there's more Notes
						Note* __restrict__ nextNote = notes.getElement(nextIndexToCopyFrom);
						maxLength =
						    nextNote->pos - postNudgeNotePos; // If it's too close, restrict the nudged note's length

						// But only if that next note won't itself get nudged!
						if ((nextNote->pos % wrapEditLevel) == editPos) {
							continue;
						}
					}
					else { // Or if there's no more Notes, in which case wrap length
						Note* __restrict__ firstNote = newNotes.getElement(0);
						maxLength = firstNote->pos + effectiveLength - postNudgeNotePos;
						D_PRINTLN("potentially wrapping note length");
					}

					if (destNote->length > maxLength) {
						D_PRINTLN("constraining right-nudged note length");
						destNote->length = maxLength;
					}
				}
			}
		}
	}

	// Deallocate working memory - no longer needed
	delugeDealloc(searchTerms);

	// Copy the final notes too - after area end on the final screen
	while (nextIndexToCopyFrom < numSourceNotes) {
		Note* __restrict__ sourceNote = notes.getElement(nextIndexToCopyFrom);
		destNote = newNotes.getElement(nextIndexToCopyTo);

		*destNote = *sourceNote;
		if (nextNoteGetsNudgedLeft) {
			nextNoteGetsNudgedLeft = false;
			destNote->pos--;
		}

		nextIndexToCopyFrom++;
		nextIndexToCopyTo++;
	}

	// If a nudged note wrapped around left
	if (wrappingLeft) {
		D_PRINTLN("placing left-wrapped nudged note at end");

		int32_t nudgedPos = effectiveLength - 1;

		// Check out the right-most destination note before adding this one...
		if (nextIndexToCopyTo > 0) {
			Note* __restrict__ prevNote = newNotes.getElement(nextIndexToCopyTo - 1);

			// Perhaps that's right where we want our Note - in which case overwrite it
			if (prevNote->pos == nudgedPos) {
				nextIndexToCopyTo--;
			}

			// Otherwise, just check it's not too long
			else {
				int32_t maxLength = nudgedPos - prevNote->pos;
				if (prevNote->length > maxLength) {
					prevNote->length = maxLength;
				}
			}
		}

		Note* __restrict__ sourceNote = notes.getElement(0);
		destNote = newNotes.getElement(nextIndexToCopyTo);

		*destNote = *sourceNote;
		destNote->pos = nudgedPos;

		nextIndexToCopyTo++;
	}
	// Or a less extreme case where we just nudged the very first Note left and it didn't wrap - but we still need to
	// check the final Note's length
	else if (firstNoteGotNudgedLeft) {
		D_PRINTLN("checking cos first note got nudged left");
		Note* __restrict__ firstDestNote = newNotes.getElement(0);
		int32_t maxLength = firstDestNote->pos + effectiveLength - destNote->pos;
		if (destNote->length > maxLength) {
			D_PRINTLN("yup, constraining last note's length");
			destNote->length = maxLength;
		}
	}

	// Delete any Notes we initially created (we create in bulk for efficiency) but then ended up not using
	int32_t numToDelete = newNotesInitialSize - nextIndexToCopyTo;
	if (numToDelete > 0) {
		newNotes.deleteAtIndex(nextIndexToCopyTo, numToDelete);
	}

	// Record change, stealing the old note data
	if (action) {
		// We "definitely" store the change, because unusually, we may want to revert individual Consequences in the
		// Action one by one
		action->recordNoteArrayChangeDefinitely((InstrumentClip*)modelStack->getTimelineCounter(),
		                                        modelStack->noteRowId, &notes, true);
	}

	// Swap the new temporary note data into the permanent place
	notes.swapStateWith(&newNotes);

#if ENABLE_SEQUENTIALITY_TESTS
	notes.testSequentiality("E327");
#endif

	return Error::NONE;
}

Error NoteRow::quantize(ModelStackWithNoteRow* modelStack, int32_t increment, int32_t amount) {
	if (notes.getNumElements() == 0) {
		// Nothing to do, and checking this now makes some later logic simpler
		return Error::NONE;
	}

	int32_t halfIncrement = increment / 2;
	int32_t effectiveLength = modelStack->getLoopLength();

	// Apply quantization/humanization
	int32_t noteWriteIdx = 0;
	int32_t lastPos = std::numeric_limits<int32_t>::min();
	for (auto i = 0; i < notes.getNumElements(); ++i) {
		Note* note = notes.getElement(i);
		int32_t destination = ((note->pos - 1 + halfIncrement) / increment) * increment;
		if (amount < 0) { // Humanize
			int32_t hmAmout = trunc(random(halfIncrement / 2) - (increment / float{kQuantizationPrecision}));
			destination = note->pos + hmAmout;
		}
		int32_t distance = destination - note->pos;
		distance = (distance * abs(amount)) / kQuantizationPrecision;
		int32_t newPos = note->pos + distance;
		Note* writeNote = notes.getElement(noteWriteIdx);
		if (newPos != lastPos) {
			// The previously written note will not end up zero-length if we inserted this note as a new one so we're
			// safe to write this one and increment the write position.
			//
			// XXX(sapphire): maybe we should do something clever with merging the notes?
			// Consider this situation:
			//       Note A:  ----
			//       Note B:      ---
			// Quantize pos:  ^
			//
			// The current code will create a single note with length 4, but maybe we should merge them (1 note of
			// length 7) or use the shorter length/newer note (length 3)
			//
			// All 3 choices are reasonable in different scenarios and it's not clear which one the user actually wants.
			// This is further complicated by the potential for the notes to have difference in other parameters --
			// Velocity and Probability/Iteration

			*writeNote = *note;
			writeNote->pos = newPos;
			noteWriteIdx++;
		}
		lastPos = newPos;
	}

	// Delete the scrap notes after zero-length elimination.
	if (noteWriteIdx < notes.getNumElements()) {
		notes.deleteAtIndex(noteWriteIdx, notes.getNumElements() - noteWriteIdx);
	}

	int32_t finalNumElements = notes.getNumElements();

	// If the first note has ended up with a negative position, we need to rotate the vector left by the number of notes
	// with a negative position
	{
		int32_t rotateAmount = 0;
		while (rotateAmount < finalNumElements && notes.getElement(rotateAmount)->pos < 0) {
			rotateAmount++;
		}

		// We make a memory-speed tradeoff here and only rotate by 1 note at a time. It would be more CPU-efficient to
		// have a buffer of rotateAmount notes and cycle the notes through there, but creating such a buffer requires
		// allocation which makes it not worthwhile in the common case of only rotating by 1 or 2 notes.
		while (rotateAmount >= 0) {
			// Swap the notes to the right, so the note with a negative position ends up at the end.
			int32_t lastNoteIdx = finalNumElements - 1;
			for (auto i = 1; i < finalNumElements; ++i) {
				notes.swapElements(i - 1, i);
			}
			notes.getElement(lastNoteIdx)->pos += effectiveLength;

			rotateAmount--;
		}
	}

	// Similarly, if the last note has ended up with a position greater than the effectiveLength, we need to rotate the
	// note vector to the right.
	{
		int32_t rotateAmount = 0;
		while (rotateAmount < notes.getNumElements()
		       && notes.getElement(finalNumElements - rotateAmount - 1)->pos >= effectiveLength) {
			rotateAmount++;
		}

		// See comment in the left rotation about why we rotate one note at a time
		while (rotateAmount > 0) {
			// Swap the notes to the left, so the note with a too-large position ends up at the start
			for (auto i = finalNumElements - 1; i > 0; i--) {
				notes.swapElements(i - 1, i);
			}
			notes.getElement(0)->pos -= effectiveLength;
			rotateAmount--;
		}
	}

	// Fix up note lengths so there are no overlaps
	for (auto i = 1; i < notes.getNumElements(); ++i) {
		Note* curr = notes.getElement(i - 1);
		Note* next = notes.getElement(i);

		auto maxLength = next->pos - curr->pos;
		curr->length = std::min(curr->length, maxLength);
	}

	// Handle the wrapping of the last note to the first note
	if (notes.getNumElements() > 1) {
		Note* curr = notes.getElement(notes.getNumElements() - 1);
		Note* next = notes.getElement(0);

		auto maxLength = (effectiveLength - curr->pos) + next->pos;
		curr->length = std::min(maxLength, curr->length);
	}

#if ENABLE_SEQUENTIALITY_TESTS
	notes.testSequentiality("E452");
#endif
	return Error::NONE;
}

Error NoteRow::changeNotesAcrossAllScreens(int32_t editPos, ModelStackWithNoteRow* modelStack, Action* action,
                                           int32_t changeType, int32_t changeValue) {

	// If no Notes, nothing to do.
	if (!notes.getNumElements()) {
		Error::NONE;
	}

	uint32_t wrapEditLevel = ((InstrumentClip*)modelStack->getTimelineCounter())->getWrapEditLevel();

	int32_t numScreens = (uint32_t)(modelStack->getLoopLength() - 1) / wrapEditLevel + 1;

	// Allocate all the working memory we're going to need for this operation - that's arrays for searchPos and
	// resultingIndexes
	int32_t* __restrict__ searchTerms =
	    (int32_t*)GeneralMemoryAllocator::get().allocMaxSpeed(numScreens * sizeof(int32_t));
	if (!searchTerms) {
		return Error::INSUFFICIENT_RAM;
	}

	if (action) {
		action->recordNoteArrayChangeIfNotAlreadySnapshotted((InstrumentClip*)modelStack->getTimelineCounter(),
		                                                     modelStack->noteRowId, &notes,
		                                                     false); // Snapshot for undoability. Don't steal data.
	}

	// Populate big list of all the positions we want to search
	int32_t editPosThisScreen = editPos;

	for (int32_t i = 0; i < numScreens; i++) {
		searchTerms[i] = editPosThisScreen;
		editPosThisScreen += wrapEditLevel;
	}

	// Search for all those positions. Will return the indexes of the next note at GREATER_OR_EQUAL to that position
	notes.searchMultiple(searchTerms, numScreens);

	// For each screen...
	for (int32_t screenIndex = 0; screenIndex < numScreens; screenIndex++) {

		int32_t indexThisScreen = searchTerms[screenIndex];
		int32_t posThisScreen = screenIndex * wrapEditLevel + editPos;

		Note* __restrict__ thisNote = notes.getElement(indexThisScreen);
		if (thisNote->pos == posThisScreen) {
			// Do the action

			switch (changeType) {
			case CORRESPONDING_NOTES_ADJUST_VELOCITY: {
				int32_t newVelocity = std::clamp<int32_t>(thisNote->getVelocity() + changeValue, 1, 127);
				thisNote->setVelocity(newVelocity);
			} break;

			case CORRESPONDING_NOTES_SET_VELOCITY: {
				thisNote->setVelocity(changeValue);
			} break;

			case CORRESPONDING_NOTES_SET_PROBABILITY: {
				thisNote->setProbability(changeValue);
			} break;

			case CORRESPONDING_NOTES_SET_ITERANCE: {
				thisNote->setIterance(changeValue);
			} break;

			case CORRESPONDING_NOTES_SET_FILL: {
				thisNote->setFill(changeValue);
			} break;
			}
		}
	}

	// Deallocate working memory - no longer needed
	delugeDealloc(searchTerms);

	return Error::NONE;
}

void NoteRow::deleteNoteByPos(ModelStackWithNoteRow* modelStack, int32_t pos, Action* action) {
	if (!notes.getNumElements()) {
		return;
	}

	int32_t i = notes.search(pos, GREATER_OR_EQUAL);
	Note* note = notes.getElement(i);
	if (!note) {
		return;
	}
	if (note->pos != pos) {
		return;
	}

	deleteNoteByIndex(i, action, modelStack->noteRowId, ((InstrumentClip*)modelStack->getTimelineCounter()));

	((InstrumentClip*)modelStack->getTimelineCounter())->expectEvent();
}

void NoteRow::deleteNoteByIndex(int32_t index, Action* action, int32_t noteRowId, InstrumentClip* clip) {

	Note* note = notes.getElement(index);
	if (!note) {
		return;
	}

	if (action) {
		action->recordNoteExistenceChange(clip, noteRowId, note, ExistenceChangeType::DELETE);
	}

	notes.deleteAtIndex(index);
}

// note is usually supplied as NULL, and that means you don't get the lift-velocity
void NoteRow::stopCurrentlyPlayingNote(ModelStackWithNoteRow* modelStack, bool actuallySoundChange, Note* note) {
	if (soundingStatus == STATUS_OFF) {
		return;
	}
	if (actuallySoundChange) {
		playNote(false, modelStack, note);
	}
	soundingStatus = STATUS_OFF;
}

// occupancyMask now optional!
void NoteRow::renderRow(TimelineView* editorScreen, RGB rowColour, RGB rowTailColour, RGB rowBlurColour, RGB* image,
                        uint8_t occupancyMask[], bool overwriteExisting, uint32_t effectiveRowLength,
                        bool allowNoteTails, int32_t renderWidth, int32_t xScroll, uint32_t xZoom, int32_t xStartNow,
                        int32_t xEnd, bool drawRepeats) {

	if (overwriteExisting) {
		memset(image, 0, renderWidth * 3);
		if (occupancyMask) {
			memset(occupancyMask, 0, renderWidth);
		}
	}

	if (!notes.getNumElements()) {
		return;
	}

	int32_t squareEndPos[kMaxImageStoreWidth];
	int32_t searchTerms[kMaxImageStoreWidth];

	int32_t whichRepeat = 0;

	int32_t xEndNow;

	do {
		// Start by presuming we'll do all the squares now... though we may decide in a second to do a smaller number
		// now, and come back for more batches
		xEndNow = xEnd;

		// For each square we might do now...
		for (int32_t square = xStartNow; square < xEndNow; square++) {

			// Work out its endPos...
			int32_t thisSquareEndPos =
			    editorScreen->getPosFromSquare(square + 1, xScroll, xZoom) - effectiveRowLength * whichRepeat;

			// If we're drawing repeats and the square ends beyond end of Clip...
			if (drawRepeats && thisSquareEndPos > effectiveRowLength) {

				// If this is the first square we're doing now, we can take a shortcut to skip forward some repeats
				if (square == xStartNow) {
					int32_t numExtraRepeats = (uint32_t)(thisSquareEndPos - 1) / effectiveRowLength;
					whichRepeat += numExtraRepeats;
					thisSquareEndPos -= numExtraRepeats * effectiveRowLength;
				}

				// Otherwise, we'll come back in a moment to do the rest of the repeats - just record that we want to
				// stop rendering before this square until then
				else {
					xEndNow = square;
					break;
				}
			}

			squareEndPos[square - xStartNow] = thisSquareEndPos;
		}

		memcpy(searchTerms, squareEndPos, sizeof(squareEndPos));

		notes.searchMultiple(searchTerms, xEndNow - xStartNow);

		int32_t squareStartPos =
		    editorScreen->getPosFromSquare(xStartNow, xScroll, xZoom) - effectiveRowLength * whichRepeat;

		for (int32_t xDisplay = xStartNow; xDisplay < xEndNow; xDisplay++) {
			if (xDisplay != xStartNow) {
				squareStartPos = squareEndPos[xDisplay - xStartNow - 1];
			}
			int32_t i = searchTerms[xDisplay - xStartNow];
			bool drewNote = false;
			Note* note = notes.getElement(i - 1); // Subtracting 1 to do "LESS"

			RGB& pixel = image[xDisplay];

			// If Note starts somewhere within square, draw the blur colour
			if (note && note->pos > squareStartPos) {
				drewNote = true;
				pixel = rowBlurColour;
				if (occupancyMask) {
					occupancyMask[xDisplay] = 64;
				}
			}

			// Or if Note starts exactly on square...
			else if (note && note->pos == squareStartPos) {
				drewNote = true;
				pixel = rowColour;
				if (occupancyMask) {
					occupancyMask[xDisplay] = 64;
				}
			}

			// Draw wrapped notes
			else if (!drawRepeats || whichRepeat) {

				bool wrapping = (i == 0); // Subtracting 1 to do "LESS"
				if (wrapping) {
					note = notes.getLast();
				}
				int32_t noteEnd = note->pos + note->length;
				if (wrapping) {
					noteEnd -= effectiveRowLength;
				}
				if (noteEnd > squareStartPos && allowNoteTails) {
					drewNote = true;
					pixel = rowTailColour;
					if (occupancyMask) {
						occupancyMask[xDisplay] = 64;
					}
				}
			}
			if (drewNote && currentSong->isFillModeActive()) {
				if (note->fill == FillMode::FILL) {
					pixel = deluge::gui::colours::blue;
				}
				else if (note->fill == FillMode::NOT_FILL) {
					pixel = deluge::gui::colours::red;
				}
			}
		}

		xStartNow = xEndNow;
		whichRepeat++;
		// TODO: if we're gonna repeat, we probably don't need to do the multi search again...
	} while (
	    xStartNow
	    != xEnd); // This will only do another repeat if we'd modified xEndNow, which can only happen if drawRepeats
}

SequenceDirection NoteRow::getEffectiveSequenceDirectionMode(ModelStackWithNoteRow const* modelStack) {
	if (sequenceDirectionMode == SequenceDirection::OBEY_PARENT) {
		return ((Clip*)modelStack->getTimelineCounter())->sequenceDirectionMode;
	}
	else {
		return sequenceDirectionMode;
	}
}

// Returns num ticks til next event
int32_t NoteRow::processCurrentPos(ModelStackWithNoteRow* modelStack, int32_t ticksSinceLast,
                                   PendingNoteOnList* pendingNoteOnList) {

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	int32_t effectiveLength = modelStack->getLoopLength();
	bool playingReversedNow = modelStack->isCurrentlyPlayingReversed();
	bool didPingpong = false;

	// If we have an independent play-pos, we need to check if we've reached the end (right of left) of this NoteRow.
	if (hasIndependentPlayPos()) {

		// Remember, lastProcessedPosIfIndependent has already been incremented during call to Clip::incrementPos().

		// Deal with recording from session to arrangement.
		if (loopLengthIfIndependent // Only if independent *length* - which isn't always the case when we have
		                            // independent play pos.
		    && playbackHandler.recording == RecordingMode::ARRANGEMENT
		    && lastProcessedPosIfIndependent
		           == (playingReversedNow ? 0 : effectiveLength) // If reached end (or start) of row length.
		    && clip->isArrangementOnlyClip()) {

			char otherModelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithNoteRow* otherModelStackWithNoteRow =
			    clip->duplicateModelStackForClipBeingRecordedFrom(modelStack, otherModelStackMemory);

			NoteRow* otherNoteRow = otherModelStackWithNoteRow->getNoteRowAllowNull();

			if (otherNoteRow) {

				int32_t whichRepeatThisIs = (uint32_t)effectiveLength / (uint32_t)otherNoteRow->loopLengthIfIndependent;

				appendNoteRow(modelStack, otherModelStackWithNoteRow, effectiveLength, whichRepeatThisIs,
				              otherNoteRow->loopLengthIfIndependent);

				loopLengthIfIndependent += otherNoteRow->loopLengthIfIndependent;
				effectiveLength = loopLengthIfIndependent; // Re-get, since we changed it.
			}
		}

		if (playingReversedNow) {

			// If actually got left of zero, it's time to loop, and normally this wouldn't happen if pingponging
			// because direction changes right when we hit zero.
			if (lastProcessedPosIfIndependent < 0) {
				lastProcessedPosIfIndependent += effectiveLength;
				// repeatCountIfIndependent++;
			}

			// Normally we do the pingpong when we hit pos 0, so the direction will change and we'll start going
			// right again now, in time for NoteRows and stuff to know the direction as they're processed and
			// predict what notes we're going to hit next etc.
			if (!lastProcessedPosIfIndependent) { // Possibly only just became the case
				repeatCountIfIndependent++;
				if (getEffectiveSequenceDirectionMode(modelStack) == SequenceDirection::PINGPONG) {
					lastProcessedPosIfIndependent = -lastProcessedPosIfIndependent; // In case it did get left of zero.
					currentlyPlayingReversedIfIndependent = playingReversedNow = !playingReversedNow;
					didPingpong = true;
				}
			}
		}
		else {

			int32_t ticksTilEnd = effectiveLength - lastProcessedPosIfIndependent;
			if (ticksTilEnd <= 0) { // Yes, note it might not always arrive directly at the end. When (Audio)
				                    // Clip length is shortened,
				// the lastProcessedPos is altered, but it could be that many swung ticks have actually passed
				// since we last processed, so there might be a big jump forward and we end up past the loop
				// point.

				lastProcessedPosIfIndependent -= effectiveLength;
				repeatCountIfIndependent++;

				if (getEffectiveSequenceDirectionMode(modelStack) == SequenceDirection::PINGPONG) {
					// Normally we'll have hit the exact loop point, meaning lastProcessedPos will have wrapped
					// to 0, above. But just in case we went further, and need to wrap back to somewhere nearish
					// the right-hand edge of the Clip...
					if (lastProcessedPosIfIndependent > 0) {
						lastProcessedPosIfIndependent = effectiveLength - lastProcessedPosIfIndependent;
					}
					currentlyPlayingReversedIfIndependent = playingReversedNow = !playingReversedNow;
					didPingpong = true;
				}
			}
		}
	}

	int32_t ticksTilNextParamManagerEvent = 2147483647;

	// Do paramManager
	if (paramManager.mightContainAutomation()) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow();

		if (didPingpong) {
			paramManager.notifyPingpongOccurred(modelStackWithThreeMainThings);
		}

		bool mayInterpolate = drum ? drum->type == DrumType::SOUND : (clip->output->type == OutputType::SYNTH);
		// We'll not interpolate for CV, just for efficiency. Since our CV output steps are limited anyway, this
		// is probably reasonably reasonable.

		paramManager.processCurrentPos(modelStackWithThreeMainThings, ticksSinceLast, playingReversedNow, didPingpong,
		                               mayInterpolate);
		ticksTilNextParamManagerEvent = paramManager.ticksTilNextEvent;
	}

	int32_t ticksTilNextNoteEvent = 2147483647;
	int32_t effectiveCurrentPos = modelStack->getLastProcessedPos(); // May have got incremented above
	int32_t effectiveForwardPos =
	    playingReversedNow ? (effectiveLength - effectiveCurrentPos - 1) : effectiveCurrentPos;

	// If we've "arrived" at a note we actually just recorded, reset the ignore state
	if (effectiveForwardPos >= ignoreNoteOnsBefore_
	    || (effectiveForwardPos < ticksSinceLast
	        && (effectiveForwardPos + effectiveLength - ticksSinceLast) >= ignoreNoteOnsBefore_)) {
		ignoreNoteOnsBefore_ = 0;
	}

	if (muted) {
noFurtherNotes:
		// If this NoteRow has independent length set, make sure we come back at its end. Otherwise, the Clip
		// handles this.
		if (loopLengthIfIndependent) {
			ticksTilNextNoteEvent = effectiveCurrentPos;
			if (!playingReversedNow) {
				ticksTilNextNoteEvent = loopLengthIfIndependent - ticksTilNextNoteEvent;
			}
			else if (!ticksTilNextNoteEvent) {
				ticksTilNextNoteEvent =
				    loopLengthIfIndependent; // Wrap from 0 up to length. Need to do this when playing reversed.
			}
		}
	}

	else {
		bool justStoppedConstantNote = false;
		bool alreadySearchedBackwards = false;
		Note* thisNote = NULL;

		// If user is auditioning note...
		if (isAuditioning(modelStack)) {

			// If they've also just recorded a note and it was quantized later, we do need to keep an eye out
			// for it, despite the fact that we're auditioning.
			if (effectiveForwardPos < ignoreNoteOnsBefore_) {
				goto currentlyOff;
			}

			// There's nothing else we can do.
			goto noFurtherNotes;
		}

		// If a note is currently playing, all we can do is see if it's stopped yet.
		if (soundingStatus == STATUS_SEQUENCED_NOTE) {

			if (!notes.getNumElements()) {
stopNote:
				stopCurrentlyPlayingNote(
				    modelStack, true,
				    thisNote); // Ideally (but optionally) supply the note, so lift-velocity can be used
			}
			else {

				// First search for the (should-be-existent) note which begins "earlier" than our current pos.
				int32_t searchLessThan = effectiveCurrentPos;

				// If we're playing backwards, we want to include notes whose pos is *equal* to the current pos,
				// which would mean ending right now (cos we're playing backwards).
				searchLessThan += (bool)playingReversedNow;
				// Buuut, a special condition for pingponging to allow notes touching the right-end of this Clip
				// / NoteRow to just keep sounding as the direction changes. Nah actually don't do that.
				//&& (effectiveCurrentPos || getEffectiveSequenceDirectionMode(modelStack) !=
				// SequenceDirection::PINGPONG));

				int32_t i = notes.search(searchLessThan, LESS);
				bool wrapping = (i == -1);
				if (wrapping) {
					i = notes.getNumElements() - 1;
				}
				thisNote = notes.getElement(i);

				// If playing reversed, we have to check that we've even reached this note yet. Maybe we
				// haven't, and there's actually no note that should be currently playing (e.g. after an undo).
				if (playingReversedNow) {
					int32_t posRelativeToNoteLeftEdge = effectiveCurrentPos - thisNote->pos;
					if (posRelativeToNoteLeftEdge < 0) {
						posRelativeToNoteLeftEdge += effectiveLength;
					}
					if (posRelativeToNoteLeftEdge >= thisNote->length) {
						goto stopNote; // This is probably fine as either ">" or ">="
					}
				}

				int32_t noteLateEdgePos = thisNote->pos; // Depending on play direction, this will be either the
				                                         // left or right edge of the Note.
				if (!playingReversedNow) {
					noteLateEdgePos += thisNote->length;
				}

				if (wrapping) {
					noteLateEdgePos -= effectiveLength;
				}

				ticksTilNextNoteEvent = noteLateEdgePos - effectiveCurrentPos;
				if (playingReversedNow) {
					ticksTilNextNoteEvent = -ticksTilNextNoteEvent;
				}

				// If note ends right now (or even earlier, which shouldn't normally happen but let's be
				// safe)...
				if (ticksTilNextNoteEvent <= 0) {

					// If it's a droning, full-length note...
					if (thisNote->pos == 0 && thisNote->length == effectiveLength) {

						// If it's a cut-mode sample, though, we want it to stop, so it can get retriggered
						// again from the start. Same for time-stretching - although those can loop themselves,
						// caching comes along and stuffs that up, so let's just stop em.
						if (clip->output->type == OutputType::SYNTH) { // For Sounds

							if (((SoundInstrument*)clip->output)->hasCutModeSamples(&clip->paramManager)) {
								goto stopNote;
							}

							if (((SoundInstrument*)clip->output)->hasAnyTimeStretchSyncing(&clip->paramManager)) {
								goto stopNote;
							}
						}
						else if (clip->output->type == OutputType::KIT && drum
						         && drum->type == DrumType::SOUND) { // For Kits
							if (((SoundDrum*)drum)->hasCutModeSamples(&paramManager)) {
								goto stopNote;
							}

							if (((SoundDrum*)drum)->hasAnyTimeStretchSyncing(&paramManager)) {
								goto stopNote;
							}
						}

						justStoppedConstantNote = true;
						soundingStatus = STATUS_OFF;
					}

					// Or normal case - just stop sounding the Note.
					else {
						goto stopNote;
					}
				}
			}
		}

		// Now, if no note is playing (even if that only became the case just now as one ended, above)...
		if (soundingStatus == STATUS_OFF) {
currentlyOff:
			ticksTilNextNoteEvent = 2147483647; // Do it again

			// If no Notes at all...
			if (!notes.getNumElements()) {
				goto noFurtherNotes;
			}

			// Or, more normally, when some Notes do exist...
			else {

				// We want to search for the next note which will sound (which depends on our play direction)...
				int32_t searchPos = effectiveCurrentPos;
				int32_t nextNoteI;

				bool allowingNoteTailsNow; // We'll only bother populating this if playingReversedNow

				// If playing reversed, a bunch of extra logic needs applying
				if (playingReversedNow) {
					allowingNoteTailsNow = clip->allowNoteTails(modelStack);

					// Special case for currentPos 0...
					if (searchPos == 0) {
						if (!allowingNoteTailsNow) {
							if (!alreadySearchedBackwards && notes.getNumElements() && !notes.getElement(0)->pos) {
								nextNoteI = 0;
								goto gotValidNoteIndex;
							}
						}
						effectiveCurrentPos = effectiveLength;
						searchPos = effectiveLength;
					}

					// Or normal case, for all other currentPos's...
					else {
						// Basic case for say synth sounds, when playing reversed, is we want to stop sounding
						// each Note at its "pos". So it's appropriate to just search further-left than that.
						// But for one-shot sounds, we do still sound the Note at the "pos", so we need to
						// include that in the search so we'll see that Note now. But, not if we've actually
						// already sounded it just before (on previous loop of this code) and are now wanting to
						// search past it.
						searchPos += (!allowingNoteTailsNow && !alreadySearchedBackwards);
					}
				}

				nextNoteI = notes.search(searchPos, -(int32_t)playingReversedNow);

				// Or if no further Notes until end of this NoteRow...
				if (nextNoteI < 0 || nextNoteI >= notes.getNumElements()) {
					// If playing reversed, we might still want to try again, taking notice of a "wrapping" note
					// at the right-hand end of this NoteRow.
					if (playingReversedNow && allowingNoteTailsNow) {
						nextNoteI = notes.getNumElements() - 1;
					}

					// Otherwise, yeah, there are no further notes, so change course.
					else {
						goto noFurtherNotes;
					}
				}

				// Or if still here, we've decided on a valid note index
gotValidNoteIndex:
				Note* nextNote = (Note*)notes.getElementAddress(nextNoteI);
				int32_t newTicksTil = nextNote->pos - effectiveCurrentPos; // Assumes we're playing forwards - it'll get
				                                                           // modified just below otherwise

				// If playing reversed...
				if (playingReversedNow) {
					newTicksTil = -newTicksTil;

					// So long as notes have tails (e.g. they're synth sounds, not one-shot), we of course want
					// to start sounding the note at its right-most edge, which we'll hit a while sooner.
					if (allowingNoteTailsNow) {
						newTicksTil -= nextNote->length;
					}

					// If it's past the wrap-point, we don't care where the Note is anymore. But we might care
					// where the wrap-point is.
					if (newTicksTil < 0) {
						goto noFurtherNotes;
					}
				}

				// If we've arrived at a Note right now...
				if (newTicksTil <= 0) {
					if (effectiveForwardPos >= ignoreNoteOnsBefore_) {
						playNote(true, modelStack, nextNote, 0, 0, justStoppedConstantNote, pendingNoteOnList);
					}

					// If playing reversed and not allowing note tails (i.e. doing one-shot drums), we're
					// already at the left-most edge of the note, so immediately stop it again
					// - and we'll then come back near here and look at the *next* Note further to the left, too
					if (playingReversedNow && !allowingNoteTailsNow) {
						alreadySearchedBackwards = true;
						thisNote = nextNote;
						goto stopNote;
					}

					newTicksTil =
					    nextNote->length; // Want this even if we're "skipping" playing the note (why exactly?)
				}

				ticksTilNextNoteEvent = newTicksTil;
			}
		}
	}

	return std::min(ticksTilNextNoteEvent, ticksTilNextParamManagerEvent);
}

bool NoteRow::isAuditioning(ModelStackWithNoteRow* modelStack) {
	Clip* clip = (Clip*)modelStack->getTimelineCounter();
	Output* output = clip->output;

	if (output->type == OutputType::KIT) {
		return drum && drum->auditioned;
	}
	else {
		return (((MelodicInstrument*)output)->notesAuditioned.searchExact(y) != -1);
	}
}

// Only call if we already know the "next note" should have already started
void NoteRow::attemptLateStartOfNextNoteToPlay(ModelStackWithNoteRow* modelStack, Note* note) {

	bool currentlyPlayingReversed = modelStack->isCurrentlyPlayingReversed();

	if (currentlyPlayingReversed && !((InstrumentClip*)modelStack->getTimelineCounter())->allowNoteTails(modelStack)) {
		// Not supported yet - would be tricky cos by the time it's "late", we'd be left of note->pos
		return;
	}

	int32_t numSwungTicksInSinceLastActionedOne = playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick();

	int32_t noteBeginsSoundingAtPos = note->pos;
	if (currentlyPlayingReversed) {
		noteBeginsSoundingAtPos += note->length; // We already know that allowNoteTails() is true, from check above
	}

	int32_t swungTicksAgo = modelStack->getLastProcessedPos() - noteBeginsSoundingAtPos;
	if (currentlyPlayingReversed) {
		swungTicksAgo = -swungTicksAgo;
	}
	swungTicksAgo += numSwungTicksInSinceLastActionedOne;

	int32_t effectiveLoopLength = modelStack->getLoopLength();

	if (swungTicksAgo < 0) {
		swungTicksAgo += effectiveLoopLength;

		// We also have to check and wrap if swungTicksAgo is >= length, which can happen when doing an
		// instant-(late)-start because the last actioned swung tick might have been a while ago, and so
		// clip->currentPos is sitting at some fairly high number (leading to a big swungTicksAgo), because the
		// expectation is it's about to wrap around when it gets a big increment when we do action a tick very
		// shortly. Seems to happen when instant-launching long Clips with short quantization, e.g. the one-bar
		// when no other Clips active. This obviously isn't really ideal - could we do something like
		// "actioning" the tick before we get here, to keep the numbers making more sense? Fix only applied 1
		// Mar 2021, for V3.1.6
	}
	else if (swungTicksAgo >= effectiveLoopLength) {
		swungTicksAgo -= effectiveLoopLength;
	}

	int32_t swungTicksBeforeLastActionedOne =
	    swungTicksAgo - numSwungTicksInSinceLastActionedOne; // This may end up negative, meaning after; that's ok

	int32_t noteOnTime = playbackHandler.getInternalTickTime(playbackHandler.lastSwungTickActioned
	                                                         - swungTicksBeforeLastActionedOne); // Might be negative!

	int32_t timeAgo = AudioEngine::audioSampleTimer - noteOnTime;

	D_PRINTLN("timeAgo:  %d", timeAgo);

	if (timeAgo < 0) { // Gregory J got this. And Vinz
#if ALPHA_OR_BETA_VERSION
		display->displayPopup("E336"); // Popup only
#endif
		timeAgo = 0; // Just don't crash
	}

	/*
	if (playbackHandler.isInternalClockActive())
	    timeAgo += (((uint64_t)ticksAgo * currentSong->timePerTimerTickFraction + 4294967296 -
	playbackHandler.lastTimerTickDoneFraction) >> 32); // This sometimes ends up 1 sample off, I think - not
	quite sure why

	// TODO: that logic won't deliver a perfect result if the tempo has changed though, or there's an imperfect
	external clock source, or swing stuff...

	if (playbackHandler.lastInternalTickDone > 0) {
	    timeAgo += AudioEngine::audioSampleTimer - playbackHandler.timeLastInternalTick;
	}
	*/

	Sound* sound = NULL;
	ParamManagerForTimeline* thisParamManager;
	if (drum && drum->type == DrumType::SOUND) {
		sound = (SoundDrum*)drum;
		thisParamManager = &paramManager;
	}
	else if (((Clip*)modelStack->getTimelineCounter())->output->type == OutputType::SYNTH) {
		sound = (SoundInstrument*)((Clip*)modelStack->getTimelineCounter())->output;
		thisParamManager = &modelStack->getTimelineCounter()->paramManager;
	}

	bool allows = false;

	if ((sound
	     && (allows =
	             sound->allowsVeryLateNoteStart(((InstrumentClip*)modelStack->getTimelineCounter()), thisParamManager)))
	    || timeAgo < kAmountNoteOnLatenessAllowed) {

		D_PRINTLN("doing late");

		if (!allows) {
			swungTicksBeforeLastActionedOne = 0;
			timeAgo = 0; // For one-shot samples, we don't want the sample to start part-way through.
		}
		playNote(true, modelStack, note, swungTicksBeforeLastActionedOne, timeAgo);
	}
}

// Note may be NULL if it's a note-off, in which case you don't get lift-velocity
void NoteRow::playNote(bool on, ModelStackWithNoteRow* modelStack, Note* thisNote, int32_t ticksLate,
                       uint32_t samplesLate, bool noteMightBeConstant, PendingNoteOnList* pendingNoteOnList) {

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();
	Output* output = clip->output;

	if (output->type != OutputType::KIT) {
		// If it's a note-on, we'll send it "soon", after all note-offs

		if (on) {
			if (noteMightBeConstant) {

				// Special case for Sounds
				if (output->type == OutputType::SYNTH) {
					if (((SoundInstrument*)output)->noteIsOn(getNoteCode(), true)
					    && ((SoundInstrument*)output)
					           ->allowNoteTails(
					               modelStack
					                   ->addOtherTwoThings(
					                       ((Clip*)modelStack->getTimelineCounter())->output->toModControllable(),
					                       &modelStack->getTimelineCounter()->paramManager)
					                   ->addSoundFlags())) {
						// Alright yup the note's still sounding from before - no need to do anything
					}

					// Or if those conditions failed, then yes we need to send the note again
					else {
						goto doSentNoteForMelodicInstrument;
					}
				}

				// Or for all other MelodicInstruments, we just don't send
			}
			else {

doSentNoteForMelodicInstrument:
				// If there's room in the buffer, store the note-on to send soon
				if (pendingNoteOnList && pendingNoteOnList->count < kMaxNumNoteOnsPending) {
storePendingNoteOn:
					pendingNoteOnList->pendingNoteOns[pendingNoteOnList->count].noteRow = this;
					pendingNoteOnList->pendingNoteOns[pendingNoteOnList->count].noteRowId = modelStack->noteRowId;
					pendingNoteOnList->pendingNoteOns[pendingNoteOnList->count].velocity = thisNote->getVelocity();
					pendingNoteOnList->pendingNoteOns[pendingNoteOnList->count].probability =
					    thisNote->getProbability();
					pendingNoteOnList->pendingNoteOns[pendingNoteOnList->count].iterance = thisNote->getIterance();
					pendingNoteOnList->pendingNoteOns[pendingNoteOnList->count].fill = thisNote->getFill();
					pendingNoteOnList->pendingNoteOns[pendingNoteOnList->count].sampleSyncLength =
					    thisNote->getLength();
					pendingNoteOnList->pendingNoteOns[pendingNoteOnList->count].ticksLate = ticksLate;
					pendingNoteOnList->count++;
				}
				// FIXME: this is almost certainly a bad idea, we can't handle more than 8-10 note ons per
				// render without culling Otherwise, just send it now.
				else {
					int16_t mpeValues[kNumExpressionDimensions];
					getMPEValues(modelStack, mpeValues);

					ModelStackWithThreeMainThings* modelStackWithThreeMainThings = modelStack->addOtherTwoThings(
					    ((Clip*)modelStack->getTimelineCounter())->output->toModControllable(),
					    &modelStack->getTimelineCounter()->paramManager);
					((MelodicInstrument*)output)
					    ->sendNote(modelStackWithThreeMainThings, true, getNoteCode(), mpeValues, MIDI_CHANNEL_NONE,
					               thisNote->velocity, thisNote->length, ticksLate, samplesLate);
				}
			}
		}

		// Or if a note-off, we can just send it now
		else {
			int32_t lift = kDefaultLiftValue;
			if (thisNote) {
				lift = thisNote->getLift();
			}

			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addOtherTwoThings(((Clip*)modelStack->getTimelineCounter())->output->toModControllable(),
			                                  &modelStack->getTimelineCounter()->paramManager);
			((MelodicInstrument*)output)
			    ->sendNote(modelStackWithThreeMainThings, false, getNoteCode(), NULL, MIDI_CHANNEL_NONE, lift);
		}
	}
	else if (drum) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThings(drum->toModControllable(), &paramManager);

		if (on) {
			if (noteMightBeConstant && drum->hasAnyVoices()
			    && drum->allowNoteTails(modelStackWithThreeMainThings->addSoundFlags())) {
				// Alright yup the note's still sounding from before - no need to do anything
				if (drum->type == DrumType::SOUND) {
					((SoundDrum*)drum)->resetTimeEnteredState();
				}
			}
			else {

				// If there's room in the buffer, store the note-on to send soon
				if (pendingNoteOnList && pendingNoteOnList->count < kMaxNumNoteOnsPending) {
					goto storePendingNoteOn;
				}
				// FIXME: this is almost certainly a bad idea, we can't handle more than 8-10 note ons per
				// render without culling Otherwise, just send it now.
				else {
					int16_t mpeValues[kNumExpressionDimensions];
					getMPEValues(modelStack, mpeValues);

					ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
					    modelStack->addOtherTwoThings(drum->toModControllable(), &paramManager);
					drum->noteOn(modelStackWithThreeMainThings, thisNote->velocity, (Kit*)output, mpeValues,
					             MIDI_CHANNEL_NONE, thisNote->length, ticksLate, samplesLate);
				}
			}
		}
		else {
			int32_t lift = kDefaultLiftValue;
			if (thisNote) {
				lift = thisNote->getLift();
			}

			drum->noteOff(modelStackWithThreeMainThings, lift);
		}
	}

	// And for all cases of a note-on, remember that that's what's happening, for later.
	if (on) {
		if (clip->allowNoteTails(modelStack)) {
			soundingStatus = STATUS_SEQUENCED_NOTE;
		}
	}
}

bool shouldResumePlaybackOnNoteRowLengthSet = true; // Ugly hack global prevention thing.

void NoteRow::setLength(ModelStackWithNoteRow* modelStack, int32_t newLength, Action* actionToRecordTo,
                        int32_t oldPos, // Sometimes needs to be overridden
                        bool hadIndependentPlayPosBefore) {
	Clip* clip = (Clip*)modelStack->getTimelineCounter();

	bool playingReversedBefore = modelStack->isCurrentlyPlayingReversed();

	if (newLength < modelStack->getLoopLength()) {
		trimToLength(newLength, modelStack, actionToRecordTo);
		oldPos = (uint32_t)oldPos % (uint32_t)newLength;
	}

	loopLengthIfIndependent = (newLength == clip->loopLength) ? 0 : newLength;

	if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(clip)) {
		lastProcessedPosIfIndependent = oldPos;
		currentlyPlayingReversedIfIndependent =
		    playingReversedBefore; // Will have no effect if is/was in fact obeying parent Clip

		if (!hadIndependentPlayPosBefore) {
			repeatCountIfIndependent = clip->repeatCount;
		}

		if (shouldResumePlaybackOnNoteRowLengthSet) {
			resumePlayback(modelStack, true);
		}
	}
}

// Action may be NULL
void NoteRow::trimToLength(uint32_t newLength, ModelStackWithNoteRow* modelStack, Action* action) {

	if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow();
		paramManager.trimToLength(newLength, modelStackWithThreeMainThings, action);
	}

	trimNoteDataToNewClipLength(newLength, (InstrumentClip*)modelStack->getTimelineCounter(), action,
	                            modelStack->noteRowId);

	((Clip*)modelStack->getTimelineCounter())->expectEvent();
}

// Action may be NULL
void NoteRow::trimNoteDataToNewClipLength(uint32_t newLength, InstrumentClip* clip, Action* action, int32_t noteRowId) {

	// If no notes at all, nothing to do
	if (!notes.getNumElements()) {
		return;
	}

	// If final note's tail doesn't reach past new length, also nothing to do
	Note* lastNote = notes.getLast();
	if (lastNote) { // Should always be one...
		int32_t maxLengthLastNote = newLength - lastNote->pos;
		if (lastNote->length <= maxLengthLastNote) {
			return;
		}
	}

	int32_t newNumNotes = notes.search(newLength, GREATER_OR_EQUAL);

	// If there'll still be some notes afterwards...
	if (newNumNotes) {

		// If no action, just basic trim
		if (!action) {
basicTrim:
			int32_t numToDelete = notes.getNumElements() - newNumNotes;
			if (numToDelete >= 0) {
				notes.deleteAtIndex(newNumNotes, numToDelete);
			}

			Note* lastNote = notes.getLast();
			if (lastNote) { // Should always be one...
				int32_t maxLengthLastNote = newLength - lastNote->pos;
				if (lastNote->length > maxLengthLastNote) {
					lastNote->setLength(maxLengthLastNote);
				}
			}
		}

		// Or if action...
		else {

			// If action already has a backed up snapshot for this param, can still just do a basic trim
			if (action->containsConsequenceNoteArrayChange(clip, noteRowId)) {
				goto basicTrim;

				// Or, if we need to snapshot, work with that
			}
			else {

				NoteVector newNotes;
				Error error = newNotes.insertAtIndex(0, newNumNotes);
				if (error != Error::NONE) {
					goto basicTrim;
				}

				for (int32_t i = 0; i < newNumNotes; i++) {
					Note* __restrict__ sourceNote = notes.getElement(i);
					Note* __restrict__ destNote = newNotes.getElement(i);

					*destNote = *sourceNote;
				}

				Note* lastNote = newNotes.getLast();
				if (lastNote) { // Should always be one...
					int32_t maxLengthLastNote = newLength - lastNote->pos;
					if (lastNote->length > maxLengthLastNote) {
						lastNote->setLength(maxLengthLastNote);
					}
				}

				action->recordNoteArrayChangeDefinitely(clip, noteRowId, &notes, true);

				// And, need to swap the new Notes in
				notes.swapStateWith(&newNotes);
			}
		}
	}

	// Or if no notes afterwards...
	else {
		if (action) {
			action->recordNoteArrayChangeIfNotAlreadySnapshotted(clip, noteRowId, &notes, true); // Steal them
		}
		notes.empty(); // Delete them - in case no action, or the above chose not to steal them
	}
}

// Set numRepeatsRounded to 0 to completely flatten iteration dependence
bool NoteRow::generateRepeats(ModelStackWithNoteRow* modelStack, uint32_t oldLoopLength, uint32_t newLoopLength,
                              int32_t numRepeatsRounded, Action* action) {

	bool pingponging = (getEffectiveSequenceDirectionMode(modelStack) == SequenceDirection::PINGPONG);

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow();

	paramManager.generateRepeats(modelStackWithThreeMainThings, oldLoopLength, newLoopLength, pingponging);

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	if (sequenceDirectionMode == SequenceDirection::PINGPONG) {
		// Pingponging is being flattened out, and although there are arguments either way, I think removing
		// that setting now is best.
		sequenceDirectionMode = (clip->sequenceDirectionMode == SequenceDirection::REVERSE)
		                            ? SequenceDirection::FORWARD
		                            : SequenceDirection::OBEY_PARENT;
	}

	int32_t numNotesBefore = notes.getNumElements();

	if (!numNotesBefore) {
		return true;
	}

	// Snapshot how Notes were before, in bulk
	if (action) {
		action->recordNoteArrayChangeIfNotAlreadySnapshotted(clip, modelStack->noteRowId, &notes, false);
	}

	// Deal with single droning note case - but don't do this for samples in CUT or STRETCH mode
	if (numNotesBefore == 1 && notes.getElement(0)->length == oldLoopLength) {
		Sound* sound = NULL;
		ParamManagerForTimeline* paramManagerNow = NULL;

		if (drum && drum->type == DrumType::SOUND) {
			sound = (SoundDrum*)drum;
			paramManagerNow = &paramManager;
		}

		else if (clip->output->type == OutputType::SYNTH) {
			sound = (SoundInstrument*)clip->output;
			paramManagerNow = &clip->paramManager;
		}

		if (!sound
		    || (!sound->hasCutModeSamples(paramManagerNow) && !sound->hasAnyTimeStretchSyncing(paramManagerNow))) {

			notes.getElement(0)->length = newLoopLength;
			return true;
		}
	}

	bool noteTailsAllowed;   // Will only get set if pingponging.
	int32_t lengthAfterWrap; // Will only get set if pingponging and noteTailsAllowed.

	if (pingponging) {

		int32_t numRepeatsRoundedUp = (uint32_t)(newLoopLength - 1) / (uint32_t)oldLoopLength + 1;

		// This is crude and lazy, but the amount of elements I'll create is rounded way up, and we'll delete
		// any extras, below.
		int32_t maxNewNumNotes = numNotesBefore * numRepeatsRoundedUp;
		Error error = notes.insertAtIndex(numNotesBefore, maxNewNumNotes - numNotesBefore);
		if (error != Error::NONE) {
			return false;
		}

		int32_t highestNoteIndex = numNotesBefore - 1; // We'll keep counting this up.

		noteTailsAllowed = clip->allowNoteTails(modelStack);

		// If yes note tails...
		if (noteTailsAllowed) {

			// Investigate whether there's a wrapped note
			Note* lastNote = (Note*)notes.getElementAddress(numNotesBefore - 1);

			int32_t lengthBeforeWrap = oldLoopLength - lastNote->pos;
			lengthAfterWrap = lastNote->length - lengthBeforeWrap;
			bool anyWrapping = (lengthAfterWrap > 0);

			// If there is, we need to edit its length now, to reflect how it would have played in a pingpong
			// scenario.
			if (anyWrapping) {
				lastNote->length = lengthBeforeWrap << 1; // TODO: make sure this doesn't eat past the end-point...
			}

			for (int32_t r = 1; r < numRepeatsRoundedUp; r++) { // For each repeat

				for (int32_t iNewWithinRepeat = 0; iNewWithinRepeat < numNotesBefore; iNewWithinRepeat++) {
					int32_t iOld = iNewWithinRepeat;

					// If this pingpong repeat is reversing...
					if (r & 1) {
						iOld = numNotesBefore - 1 - iOld - anyWrapping;
						if (iOld < 0) {
							iOld = numNotesBefore - 1; // In case of wrapping
						}
					}

					Note* oldNote = (Note*)notes.getElementAddress(iOld);
					int32_t newPos = oldNote->pos;
					int32_t newLength = oldNote->length;

					if (r & 1) {
						newPos = oldLoopLength - newPos - newLength;

						if (newPos < 0) { // This means we've got the wrapped note, while reversing
							newPos = oldLoopLength - lengthAfterWrap;
							newLength = lengthAfterWrap << 1;
						}
					}

					newPos += oldLoopLength * r;
					if (newPos >= newLoopLength) {
						break; // Crude way of stopping part-way through the final repeat if it was only a
						       // partial one.
					}

					int32_t iNew = iNewWithinRepeat + numNotesBefore * r;
					Note* newNote = (Note*)notes.getElementAddress(iNew);
					newNote->pos = newPos;
					newNote->setLength(newLength);
					newNote->setProbability(oldNote->getProbability());
					newNote->setVelocity(oldNote->getVelocity());
					newNote->setLift(oldNote->getLift());
					newNote->setIterance(oldNote->getIterance());
					newNote->setFill(oldNote->getFill());

					highestNoteIndex = iNew;
				}
			}
		}

		// No-tails (e.g. one-shot samples):
		else {

			Note* firstNote = (Note*)notes.getElementAddress(0);
			bool anythingAtZero = (firstNote->pos == 0);

			for (int32_t r = 1; r < numRepeatsRoundedUp; r++) { // For each repeat

				for (int32_t iNewWithinRepeat = 0; iNewWithinRepeat < numNotesBefore; iNewWithinRepeat++) {
					int32_t iOld = iNewWithinRepeat;

					// If this pingpong repeat is reversing...
					if (r & 1) {
						iOld = -iOld - !anythingAtZero;
						if (iOld < 0) {
							iOld += numNotesBefore;
						}
					}

					Note* oldNote = (Note*)notes.getElementAddress(iOld);
					int32_t newPos = oldNote->pos;

					if (r & 1) {
						newPos = -newPos;
						if (newPos < 0) {
							newPos += oldLoopLength;
						}
					}

					newPos += oldLoopLength * r;
					if (newPos >= newLoopLength) {
						break; // Crude way of stopping part-way through the final repeat if it was only a
						       // partial one.
					}

					int32_t iNew = iNewWithinRepeat + numNotesBefore * r;
					Note* newNote = (Note*)notes.getElementAddress(iNew);
					newNote->pos = newPos;
					newNote->setLength(1);
					newNote->setProbability(oldNote->getProbability());
					newNote->setVelocity(oldNote->getVelocity());
					newNote->setLift(oldNote->getLift());
					newNote->setIterance(oldNote->getIterance());
					newNote->setFill(oldNote->getFill());

					highestNoteIndex = iNew;
				}
			}
		}

		// Delete any extra elements we didn't end up using.
		int32_t newNumNotes = highestNoteIndex + 1;
		int32_t numToDelete = maxNewNumNotes - newNumNotes;
		if (numToDelete) {
			notes.deleteAtIndex(newNumNotes, numToDelete);
		}
	}

	else {
		notes.generateRepeats(oldLoopLength, newLoopLength);
	}

	// Ensure final note doesn't go on too long.
	if ((newLoopLength % oldLoopLength) != 0) {
		Note* lastNote = notes.getLast();
		int32_t maxNoteLength = newLoopLength - lastNote->pos;
		lastNote->length = std::min(lastNote->length, maxNoteLength);
	}

	// *** Take care of iteration dependence. ***
	// Largely because this function generates multiple repeats in one go, unlike clone() and appendNoteRow()
	// which only do one, we aim for efficiency by looking at each of the "source" notes in turn, and for each
	// of those, look at all its copies. The result may be that iteration dependence gets flattened entirely
	// (and you can force this by supplying numRepeatsRounded as 0), or if the Clip/NoteRow is still not going
	// to be as long as all of a note's iterations, then its iteration dependence will be modified to keep it
	// sounding the same, to the extent possible.

	// Go through each Note within the original length
	for (int32_t i = 0; i < numNotesBefore; i++) {
		Note* note = notes.getElement(i);
		int32_t iterance = note->iterance & 127;
		int32_t pos = note->pos;

		// If it's iteration dependent...
		if (iterance > kDefaultIteranceValue) {
			int32_t divisor, iterationWithinDivisor;
			dissectIterationDependence(iterance, &divisor, &iterationWithinDivisor);

			int32_t newNumFullLoops = numRepeatsRounded ? newLoopLength / (uint32_t)(oldLoopLength * divisor) : 1;

			int32_t whichFullLoop = 0; // If newNumFullLoops is 0, this will never get above 0
			int32_t whichRepeatWithinLoop = 0;

			// Go through each copy of that original note (i.e. each repeat of oldLength)
			while (true) {
				int32_t whichRepeatTotal = whichFullLoop * divisor + whichRepeatWithinLoop;

				int32_t thisRepeatedNotePos = pos;

				if (pingponging && (whichRepeatTotal & 1)) {
					thisRepeatedNotePos = -thisRepeatedNotePos;
					if (noteTailsAllowed) {
						thisRepeatedNotePos += (oldLoopLength - note->length);
						if (thisRepeatedNotePos < 0) {
							thisRepeatedNotePos =
							    oldLoopLength
							    - lengthAfterWrap; // This means we've got the wrapped note, while reversing.
						}
					}
					else {
						if (thisRepeatedNotePos < 0) {
							thisRepeatedNotePos += oldLoopLength;
						}
					}
				}

				thisRepeatedNotePos += oldLoopLength * whichRepeatTotal;

				if (thisRepeatedNotePos >= newLoopLength) {
					break;
				}

				int32_t thisRepeatedNoteI = notes.search(thisRepeatedNotePos, GREATER_OR_EQUAL);
				Note* thisRepeatedNote = notes.getElement(thisRepeatedNoteI);
				if (!thisRepeatedNote) {
					break; // Shouldn't happen...
				}

				int32_t iterationWithinDivisorWithinRepeat =
				    numRepeatsRounded ? ((uint32_t)iterationWithinDivisor % (uint32_t)numRepeatsRounded)
				                      : iterationWithinDivisor;

				if (whichRepeatWithinLoop != iterationWithinDivisorWithinRepeat) {

					// If deleting the original one, those are what our for loop is currently iterating through,
					// so alter that process
					if (whichRepeatTotal == 0) {
						numNotesBefore--;
						i--;
					}

					notes.deleteAtIndex(thisRepeatedNoteI);
				}
				else {

					int32_t newIterance;
					if (newNumFullLoops == 0) {
						// I think this bit is for, like, if we had a note doing 1of6, and we're doing two
						// repeats total on this Clip, well then to keep it sounding the same, we'd now need the
						// note to be a 1of3.
						int32_t newDivisor = (uint32_t)divisor / (uint32_t)numRepeatsRounded;
						if (newDivisor <= 1) {
							goto switchOff;
						}
						int32_t newIterationWithinDivisor =
						    (uint32_t)iterationWithinDivisor / (uint32_t)numRepeatsRounded;

						newIterance = encodeIterationDependence(newDivisor, newIterationWithinDivisor);
					}
					else {
switchOff:
						newIterance = kDefaultIteranceValue; // Switch off iteration dependence
					}

					thisRepeatedNote->setIterance(newIterance);
				}

				whichRepeatWithinLoop++;
				if (whichRepeatWithinLoop >= divisor) {
					whichRepeatWithinLoop = 0;
					whichFullLoop++;
				}
			}
		}
	}

	clip->expectEvent();
	return true;
}

void NoteRow::toggleMute(ModelStackWithNoteRow* modelStack, bool clipIsActiveAndPlaybackIsOn) {
	muted = !muted;

	if (clipIsActiveAndPlaybackIsOn) {
		// If we just muted it, make sure we're not playing a note
		if (muted) {
			stopCurrentlyPlayingNote(modelStack);
		}

		// If we've just un-muted
		else {
			resumePlayback(modelStack, clipIsActiveAndPlaybackIsOn);
			((InstrumentClip*)modelStack->getTimelineCounter())->expectEvent();
		}
	}
}

// Attempts (possibly late) start of any note at or overlapping the currentPos
void NoteRow::resumePlayback(ModelStackWithNoteRow* modelStack, bool clipMayMakeSound) {
	if (noteRowMayMakeSound(clipMayMakeSound) && soundingStatus == STATUS_OFF && !isAuditioning(modelStack)) {

		if (!notes.getNumElements()) {
			return;
		}

		int32_t effectiveActualCurrentPos = getLivePos(modelStack);

		if ((runtimeFeatureSettings.get(RuntimeFeatureSettingType::CatchNotes) == RuntimeFeatureStateToggle::On)) {
			// See if our play-pos is inside of a note, which we might want to try playing...
			int32_t i = notes.search(effectiveActualCurrentPos, LESS);
			bool wrapping = (i == -1);
			if (wrapping) {
				i = notes.getNumElements() - 1;
			}
			Note* note = notes.getElement(i);
			int32_t noteEnd = note->pos + note->length;
			if (wrapping) {
				noteEnd -= modelStack->getLoopLength();
			}

			if (noteEnd > effectiveActualCurrentPos) {
				attemptLateStartOfNextNoteToPlay(modelStack, note);
			}
		}
	}

	ignoreNoteOnsBefore_ = 0;
}

// TODO: make this compatible with reverse playback
void NoteRow::silentlyResumePlayback(ModelStackWithNoteRow* modelStack) {

	int32_t effectiveCurrentPos = modelStack->getLastProcessedPos(); // Why did we not opt for the "actual" one?

	// See if our play-pos is inside of a note
	int32_t i = notes.search(effectiveCurrentPos, LESS);
	bool wrapping = (i == -1);
	if (wrapping) {
		i = notes.getNumElements() - 1;
	}
	Note* note = notes.getElement(i);
	int32_t noteEnd = note->pos + note->length;
	if (wrapping) {
		noteEnd -= modelStack->getLoopLength();
	}

	if (noteEnd > effectiveCurrentPos) {
		soundingStatus = STATUS_SEQUENCED_NOTE;
	}
}

bool NoteRow::hasNoNotes() {
	return !notes.getNumElements();
}

bool NoteRow::noteRowMayMakeSound(bool clipMayMakeSound) {
	return clipMayMakeSound && !muted;
}

uint32_t NoteRow::getNumNotes() {
	return notes.getNumElements();
}

Error NoteRow::readFromFile(Deserializer& reader, int32_t* minY, InstrumentClip* parentClip, Song* song,
                            int32_t readAutomationUpToPos) {
	char const* tagName;

	drum = (Drum*)0xFFFFFFFF; // Code for "no drum". We swap this for a real value soon

	int32_t newBendRange = -1; // Temp variable for this because we can't actually create the expressionParams
	                           // before we know what kind of Drum (if any) we have.
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		// D_PRINTLN(tagName); delayMS(50);

		uint16_t noteHexLength;

		if (!strcmp(tagName, "muted")) {
			muted = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "y")) {
			y = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "colourOffset")) {
			colourOffset = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "drumIndex")) {
			drum = (Drum*)reader.readTagOrAttributeValueInt(); // Sneaky - we store an integer in place of this pointer,
			                                                   // then swap it back to something meaningful later
		}

		else if (!strcmp(tagName, "gateOutput")) {
			int32_t gateChannel = reader.readTagOrAttributeValueInt();
			gateChannel = std::clamp<int32_t>(gateChannel, 0, NUM_GATE_CHANNELS - 1);

			drum = (Drum*)(0xFFFFFFFE - gateChannel);
		}

		else if (!strcmp(tagName, "muteMidiCommand")) {
			muteMIDICommand.readNoteFromFile(reader);
		}

		else if (!strcmp(tagName, "soundMidiCommand")) {
			midiInput.readNoteFromFile(reader);
		}

		else if (!strcmp(tagName, "length")) {
			loopLengthIfIndependent = reader.readTagOrAttributeValueInt();
			readAutomationUpToPos = loopLengthIfIndependent; // So we can read automation right up to the actual
			                                                 // length of this NoteRow.
		}

		else if (!strcmp(tagName, "sequenceDirection")) {
			sequenceDirectionMode = stringToSequenceDirectionMode(reader.readTagOrAttributeValue());
		}

		else if (!strcmp(tagName, "bendRange")) {
			newBendRange = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "soundParams")) {

			// Sneaky sorta hack for 2016 files - allow more params to be loaded into a ParamManager that
			// already had some loading done by the Drum
			if (song_firmware_version < FirmwareVersion::official({1, 2, 0}) && parentClip->output) {

				SoundDrum* actualDrum = (SoundDrum*)((Kit*)parentClip->output)->getDrumFromIndex((int32_t)drum);

				if (actualDrum) {
					ParamManager* existingParamManager =
					    song->getBackedUpParamManagerPreferablyWithClip(actualDrum, parentClip);
					if (existingParamManager) {
						Error error = paramManager.cloneParamCollectionsFrom(existingParamManager, false);
						if (error != Error::NONE) {
							return error;
						}
						goto finishedNormalStuff;
					}
				}
			}

			paramManager.setupWithPatching();
			Sound::initParams(&paramManager);

finishedNormalStuff:
			Sound::readParamsFromFile(reader, &paramManager, readAutomationUpToPos);
		}

		// Notes stored as XML (before V1.4) - NOT CONVERTED TO ALSO WORK WITH JSON.
		else if (!strcmp(tagName, "notes")) {
			// Read each Note
			uint32_t minPos = 0;
			while (*(tagName = reader.readNextTagOrAttributeName())) {

				if (!strcmp(tagName, "note")) {
					// Set defaults for this Note
					uint8_t velocity = 64;
					uint32_t pos = 0;
					uint32_t length = 1;

					// Read in this note's data
					while (*(tagName = reader.readNextTagOrAttributeName())) {
						if (!strcmp(tagName, "velocity")) {
							velocity = reader.readTagOrAttributeValueInt();
							velocity = std::min((uint8_t)127, (uint8_t)std::max((uint8_t)1, (uint8_t)velocity));
							reader.exitTag("velocity");
						}

						else if (!strcmp(tagName, "pos")) {
							pos = reader.readTagOrAttributeValueInt();
							pos = std::max(minPos, pos);
							reader.exitTag("pos");
						}

						else if (!strcmp(tagName, "length")) {
							length = reader.readTagOrAttributeValueInt();
							length = std::max((uint32_t)1, (uint32_t)length);
							reader.exitTag("length");
						}

						else {
							reader.exitTag(tagName);
						}
					}

					if (!(pos < minPos || length < 0 || pos > kMaxSequenceLength - length)) {

						minPos = pos + length;

						// Make this Note
						int32_t i = notes.insertAtKey(pos, true);
						if (i == -1) {
							return Error::INSUFFICIENT_RAM;
						}
						Note* newNote = notes.getElement(i);
						newNote->setLength(length);
						newNote->setVelocity(velocity);
						newNote->setLift(kDefaultLiftValue);
						newNote->setProbability(kNumProbabilityValues);
						newNote->setIterance(kDefaultIteranceValue);
						newNote->setFill(FillMode::OFF);
					}

					reader.exitTag("note");
				}
				else {
					reader.exitTag(tagName);
				}
			}
		}

		// Notes stored as hex data (V1.4 onwards)
		else if (!strcmp(tagName, "noteData")) {
			noteHexLength = 20;
doReadNoteData:
			int32_t minPos = 0;

			int32_t numElementsToAllocateFor = 0;

			if (!reader.prepareToReadTagOrAttributeValueOneCharAtATime()) {
				goto getOut;
			}

			{
				char const* firstChars = reader.readNextCharsOfTagOrAttributeValue(2);
				if (!firstChars || *(uint16_t*)firstChars != charsToIntegerConstant('0', 'x')) {
					goto getOut;
				}
			}

			while (true) {

				// Every time we've reached the end of a cluster...
				if (numElementsToAllocateFor <= 0) {

					// See how many more chars before the end of the cluster. If there are any...
					uint32_t charsRemaining = reader.getNumCharsRemainingInValueBeforeEndOfCluster();
					if (charsRemaining) {

						// Allocate space for the right number of notes, and remember how long it'll be before
						// we need to do this check again
						numElementsToAllocateFor = (uint32_t)(charsRemaining - 1) / noteHexLength + 1;
						notes.ensureEnoughSpaceAllocated(
						    numElementsToAllocateFor); // If it returns false... oh well. We'll fail later
					}
				}

				char const* hexChars = reader.readNextCharsOfTagOrAttributeValue(noteHexLength);
				if (!hexChars) {
					goto getOut;
				}

				int32_t pos = hexToIntFixedLength(hexChars, 8);
				int32_t length = hexToIntFixedLength(&hexChars[8], 8);
				uint8_t velocity = hexToIntFixedLength(&hexChars[16], 2);
				uint8_t lift, probability, iterance, fill;

				if (noteHexLength == 26) { // if reading iterance and fill
					fill = hexToIntFixedLength(&hexChars[24], 2);
					iterance = hexToIntFixedLength(&hexChars[22], 2);
					probability = hexToIntFixedLength(&hexChars[20], 2);
					lift = hexToIntFixedLength(&hexChars[18], 2);
					if (lift == 0 || lift > 127) {
						goto useDefaultLift;
					}
				}
				else if (noteHexLength == 22) { // If reading lift...
					probability = hexToIntFixedLength(&hexChars[20], 2);

					if (probability == kOldFillProbabilityValue || probability == kOldNotFillProbabilityValue) {
						if (probability == kOldFillProbabilityValue) {
							fill = FillMode::FILL;
						}
						else {
							fill = FillMode::NOT_FILL;
						}
						iterance = kDefaultIteranceValue;    // iterance off
						probability = kNumProbabilityValues; // 100% probability
					}
					else if (probability > kNumProbabilityValues) {
						fill = FillMode::OFF;
						iterance = probability - kNumProbabilityValues;
						probability = kNumProbabilityValues; // 100% probability
					}
					else {
						fill = FillMode::OFF;
						iterance = kDefaultIteranceValue; // iterance off
					}
					lift = hexToIntFixedLength(&hexChars[18], 2);
					if (lift == 0 || lift > 127) {
						goto useDefaultLift;
					}
				}
				else { // Or if no lift here to read
					probability = hexToIntFixedLength(&hexChars[18], 2);

					if (probability == kOldFillProbabilityValue || probability == kOldNotFillProbabilityValue) {
						if (probability == kOldFillProbabilityValue) {
							fill = FillMode::FILL;
						}
						else {
							fill = FillMode::NOT_FILL;
						}
						iterance = kDefaultIteranceValue;    // iterance off
						probability = kNumProbabilityValues; // 100% probability
					}
					else if (probability > kNumProbabilityValues) {
						fill = FillMode::OFF;
						iterance = probability - kNumProbabilityValues;
						probability = kNumProbabilityValues; // 100% probability
					}
					else {
						fill = FillMode::OFF;
						iterance = kDefaultIteranceValue; // iterance off
					}

					lift = hexToIntFixedLength(&hexChars[18], 2);
					if (lift == 0 || lift > 127) {
						goto useDefaultLift;
					}

useDefaultLift:
					lift = kDefaultLiftValue;
				}

				// See if that's all allowed
				if (length <= 0) {
					length = 1; // This happened somehow in Simon Wollwage's song, May 2020
				}
				if (pos < minPos || pos > kMaxSequenceLength - length) {
					continue;
				}
				if (velocity == 0 || velocity > 127) {
					velocity = 64;
				}
				if ((probability & 127) > kNumProbabilityValues || probability >= (kNumProbabilityValues | 128)) {
					probability = kNumProbabilityValues;
				}
				if ((iterance & 127) > kNumIterationValues || iterance >= (kNumIterationValues | 128)) {
					iterance = kDefaultIteranceValue;
				}
				if (fill < FillMode::OFF || fill > FillMode::FILL) {
					fill = FillMode::OFF;
				}

				minPos = pos + length;

				// Ok, make the note
				int32_t i = notes.insertAtKey(pos, true);
				if (i == -1) {
					return Error::INSUFFICIENT_RAM;
				}
				Note* newNote = notes.getElement(i);
				newNote->setLength(length);
				newNote->setVelocity(velocity);
				newNote->setLift(lift);
				newNote->setProbability(probability);
				newNote->setIterance(iterance);
				newNote->setFill(fill);

				numElementsToAllocateFor--;
			}
getOut: {}
		}

		// Notes stored as hex data including lift (V3.2 onwards)
		else if (!strcmp(tagName, "noteDataWithLift")) {
			noteHexLength = 22;
			goto doReadNoteData;
		}

		// Notes stored as hex data including iterance and fill (community firmware 1.3 onwards)
		else if (!strcmp(tagName, "noteDataWithIteranceAndFill")) {
			noteHexLength = 26;
			goto doReadNoteData;
		}

		else if (!strcmp(tagName, "expressionData")) {
			paramManager.ensureExpressionParamSetExists();
			ParamCollectionSummary* summary = paramManager.getExpressionParamSetSummary();
			ExpressionParamSet* expressionParams = (ExpressionParamSet*)summary->paramCollection;
			if (expressionParams) {
				expressionParams->readFromFile(reader, summary, readAutomationUpToPos);
			}
		}

		reader.exitTag();
	}

	y = std::max(y, (int16_t)*minY);
	*minY = y + 1;

	if (newBendRange != -1) {
		ExpressionParamSet* expressionParams = paramManager.getOrCreateExpressionParamSet();
		if (expressionParams) {
			expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL] = newBendRange;
		}
	}
	reader.match('}');
	return Error::NONE;
}

void NoteRow::writeToFile(Serializer& writer, int32_t drumIndex, InstrumentClip* clip) {
	writer.writeOpeningTagBeginning("noteRow", true);

	bool forKit = (clip->output->type == OutputType::KIT);

	if (!forKit) {
		writer.writeAttribute("y", y);
	}
	if (muted) {
		writer.writeAttribute("muted", muted);
	}

	if (forKit) {
		writer.writeAttribute("colourOffset", getColourOffset(clip));
	}

	if (loopLengthIfIndependent) {
		writer.writeAttribute("length", loopLengthIfIndependent);
	}
	if (sequenceDirectionMode != SequenceDirection::OBEY_PARENT) {
		writer.writeAttribute("sequenceDirection", sequenceDirectionModeToString(sequenceDirectionMode));
	}

	if (notes.getNumElements()) {
		writer.insertCommaIfNeeded();
		writer.write("\n");
		writer.printIndents();
		writer.writeTagNameAndSeperator("noteDataWithIteranceAndFill");
		writer.write("\"0x");
		for (int32_t n = 0; n < notes.getNumElements(); n++) {
			Note* thisNote = notes.getElement(n);

			char buffer[9];

			intToHex(thisNote->pos, buffer);
			writer.write(buffer);

			intToHex(thisNote->getLength(), buffer);
			writer.write(buffer);

			intToHex(thisNote->getVelocity(), buffer, 2);
			writer.write(buffer);

			intToHex(thisNote->getLift(), buffer, 2);
			writer.write(buffer);

			intToHex(thisNote->getProbability(), buffer, 2);
			writer.write(buffer);

			intToHex(thisNote->getIterance(), buffer, 2);
			writer.write(buffer);

			intToHex(thisNote->getFill(), buffer, 2);
			writer.write(buffer);
		}
		writer.write("\"");
	}

	ExpressionParamSet* expressionParams = paramManager.getExpressionParamSet();
	if (expressionParams && forKit) {
		writer.writeAttribute("bendRange", expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL]);
	}

	bool closedOurTagYet = false;

	if (drum) {
		writer.writeAttribute("drumIndex", drumIndex);

		if (paramManager.containsAnyMainParamCollections()) {
			writer.writeOpeningTagEnd();
			closedOurTagYet = true;

			writer.writeOpeningTagBeginning("soundParams");
			Sound::writeParamsToFile(writer, &paramManager, true);
			writer.writeClosingTag("soundParams", true);
		}
	}

	if (expressionParams) {
		bool wroteAny = expressionParams->writeToFile(writer, !closedOurTagYet);
		closedOurTagYet = closedOurTagYet || wroteAny;
	}

	if (closedOurTagYet) {
		writer.writeClosingTag("noteRow", true, true);
	}

	else {
		writer.closeTag(true);
	}
}

int8_t NoteRow::getColourOffset(InstrumentClip* clip) {
	if (clip->output->type == OutputType::KIT) {
		return colourOffset;
	}
	else {
		return 0;
	}
}

/*
void NoteRow::setDrumToNull(ModelStackWithTimelineCounter const* modelStack) {
    if (paramManager.containsAnyParamCollections()) {
        modelStack->song->backUpParamManager((SoundDrum*)drum, (Clip*)modelStack->getTimelineCounter(),
&paramManager);

        paramManager.forgetParamCollections();
    }

    drum = NULL;
}
*/

// If NULL or a gate drum, no need to supply a Kit
// song not required if setting to NULL
// Can handle NULL newParamManager.
void NoteRow::setDrum(Drum* newDrum, Kit* kit, ModelStackWithNoteRow* modelStack,
                      InstrumentClip* favourClipForCloningParamManager, ParamManager* newParamManager,
                      bool backupOldParamManager) {

	if (backupOldParamManager && paramManager.containsAnyMainParamCollections()) {
		modelStack->song->backUpParamManager(
		    (SoundDrum*)drum, (Clip*)modelStack->getTimelineCounter(), &paramManager,
		    false); // Don't steal expression params - we'll keep them here with this NoteRow.
	}
	paramManager.forgetParamCollections();

	drum = (SoundDrum*)newDrum; // Better set this temporarily for this call. See comment above for why we can't
	                            // set it permanently yet

	if (newParamManager) {
		paramManager.stealParamCollectionsFrom(newParamManager, true);
		if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
			trimParamManager(modelStack);
		}
	}

	// Set drum to NULL first - it's crucial because we're about to call the following functions:
	// - currentSong->findParamManagerForDrum() looks for NoteRows with this Drum. We don't want to be one just
	// yet, or we'll be the one that's found
	drum = NULL;

	// Grab new ParamManager from that backed up in Drum
	if (newDrum && newDrum->type == DrumType::SOUND) {

		SoundDrum* soundDrum = (SoundDrum*)newDrum;

		if (!paramManager.containsAnyMainParamCollections()) {
			if (favourClipForCloningParamManager) {
				NoteRow* noteRow = favourClipForCloningParamManager->getNoteRowForDrum(soundDrum);
				if (noteRow) {
					paramManager.cloneParamCollectionsFrom(&noteRow->paramManager, false, true);
					// That might not work if there was insufficient RAM, but we'll still try the other options
					// below
				}
			}

			if (!paramManager.containsAnyMainParamCollections()) {

				drum = soundDrum; // Better set this temporarily for this call. See comment above for why we
				                  // can't set it permanently yet
				bool success = modelStack->song->getBackedUpParamManagerPreferablyWithClip(
				    soundDrum, (Clip*)modelStack->getTimelineCounter(), &paramManager);
				if (success) {
					trimParamManager(modelStack);
				}
				drum = NULL;

				// If there still isn't one, grab from another NoteRow
				if (!paramManager.containsAnyMainParamCollections()) {

					ParamManagerForTimeline* paramManagerForDrum =
					    modelStack->song->findParamManagerForDrum(kit, soundDrum);

					if (paramManagerForDrum) {
						paramManager.cloneParamCollectionsFrom(paramManagerForDrum, false, true);

						// If there also was no RAM...
						if (!paramManager.containsAnyMainParamCollections()) {
							FREEZE_WITH_ERROR("E101");
						}
					}

					// If there is no NoteRow for this Drum... Oh dear. Make a blank ParamManager and pray?
					else {
						Error error = paramManager.setupWithPatching();
						if (error != Error::NONE) {
							FREEZE_WITH_ERROR("E010"); // If there also was no RAM, we're really in trouble.
						}
						Sound::initParams(&paramManager);

						// This is at least not ideal, so we'd better tell the user
						if (ALPHA_OR_BETA_VERSION) {
							display->displayPopup("E073");
						}
					}
				}
			}
		}

		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThings(soundDrum, &paramManager);
		drum = soundDrum;
		soundDrum->ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(modelStackWithThreeMainThings);
		drum = NULL; // Yup this ugliness again, sorry!

		ParamCollectionSummary* patchCablesSummary = paramManager.getPatchCableSetSummary();
		PatchCableSet* patchCableSet = (PatchCableSet*)patchCablesSummary->paramCollection;

		patchCableSet->grabVelocityToLevelFromMIDIInput(
		    &soundDrum->midiInput); // Should happen before we call setupPatching().

		{
			ModelStackWithParamCollection* modelStackWithParamCollection =
			    modelStackWithThreeMainThings->addParamCollection(patchCableSet, patchCablesSummary);
			patchCableSet->setupPatching(modelStackWithParamCollection);
		}

		Clip* clip = (Clip*)modelStack->getTimelineCounter();

		if (playbackHandler.isEitherClockActive() && paramManager.mightContainAutomation()
		    && modelStack->song->isClipActive(clip)) {
			paramManager.setPlayPos(clip->getLivePos(), modelStackWithThreeMainThings,
			                        modelStackWithThreeMainThings->isCurrentlyPlayingReversed());
		}

		if (clip->isActiveOnOutput()) {
			soundDrum->patcher.performInitialPatching(soundDrum, &paramManager);
		}
	}

	drum = newDrum;

	if (drum) {
		drum->noteRowAssignedTemp = true;

		// Copy bend range, if appropriate. This logic is duplicated in View::noteOnReceivedForMidiLearn()
		LearnedMIDI* midiInput = &drum->midiInput;
		if (midiInput->containsSomething() && midiInput->device) {
			int32_t newBendRange;
			int32_t zone = midiInput->channelOrZone - MIDI_CHANNEL_MPE_LOWER_ZONE;
			if (zone >= 0) { // MPE input
				newBendRange = midiInput->device->mpeZoneBendRanges[zone][BEND_RANGE_FINGER_LEVEL];
			}
			else { // Regular MIDI input
				newBendRange = midiInput->device->inputChannels[midiInput->channelOrZone].bendRange;
			}

			if (newBendRange) {
				ExpressionParamSet* expressionParams = paramManager.getOrCreateExpressionParamSet(true);
				if (expressionParams) {
					if (!expressionParams->params[0].isAutomated()) {
						expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL] = newBendRange;
					}
				}
			}
		}
	}
}

void NoteRow::rememberDrumName() {

	if (drum && drum->type == DrumType::SOUND) {

		SoundDrum* soundDrum = (SoundDrum*)drum;

		// If it's all numeric (most likely meaning it's a slice), don't store it
		if (stringIsNumericChars(soundDrum->name.get())) {
			return;
		}

		// Go through all existing old names
		DrumName** prevPointer = &firstOldDrumName;
		while (*prevPointer) {

			// If we'd already stored the name we were gonna store now, no need to do anything
			if ((*prevPointer)->name.equalsCaseIrrespective(&soundDrum->name)) {
				return;
			}

			prevPointer = &((*prevPointer)->next);
		}

		// If we're here, we're at the end of the list, didn't find an instance of the name, and want to add it
		// to the end of the list now Paul: Might make sense to put these into Internal?
		void* drumNameMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(DrumName));
		if (drumNameMemory) {
			*prevPointer = new (drumNameMemory) DrumName(&soundDrum->name);
		}
	}
}

int32_t NoteRow::getDistanceToNextNote(int32_t pos, ModelStackWithNoteRow const* modelStack, bool reversed) {
	int32_t effectiveLength = modelStack->getLoopLength();

	if (!notes.getNumElements()) {
		return effectiveLength;
	}

	int32_t i = notes.search(pos + !reversed, GREATER_OR_EQUAL) - reversed;

	if (i == notes.getNumElements()) {
		i = 0;
	}
	else {
goAgain:
		if (i == -1) {
			i = notes.getNumElements() - 1;
		}
	}

	Note* note = notes.getElement(i);

	int32_t distance = note->pos - pos;
	if (reversed) {
		distance = -distance;
	}

	if (distance <= 0) {
		distance += effectiveLength;
	}

	// If reversed, we need to actually consider the note's rightmost edge, where its "length" ends.
	if (reversed) {
		distance -= note->length;

		// But if that length actually touches or passes right through us...
		if (distance <= 0) {

			// If there only is one note, there's no point looking to the next one - just think about the next
			// time we'll come around.
			if (notes.getNumElements() == 1) {
				distance += effectiveLength;
			}

			// Otherwise, then yes, we want to look at the next note along (left).
			else {
				i--;
				if (i > -effectiveLength)
					goto goAgain;
			}
		}
	}

	return distance;
}

// Caller must call expectEvent() on the Clip, and paramManager->setPlayPos on this NoteRow, if (and only if)
// playbackHandler.isEitherClockActive().
void NoteRow::shiftHorizontally(int32_t amount, ModelStackWithNoteRow* modelStack, bool shiftAutomation,
                                bool shiftSequenceAndMPE) {
	int32_t effectiveLength = modelStack->getLoopLength();

	// the following code iterates through all param collections and shifts automation and MPE separately
	// automation only gets shifted if shiftAutomation is true
	// MPE only gets shifted if shiftSequenceAndMPE is true
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow();

	if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
		ParamCollectionSummary* summary = paramManager.summaries;

		int32_t i = 0;

		while (summary->paramCollection) {

			ModelStackWithParamCollection* modelStackWithParamCollection =
			    modelStackWithThreeMainThings->addParamCollection(summary->paramCollection, summary);

			// Special case for MPE only - not even "mono" / Clip-level expression.
			if (i == paramManager.getExpressionParamSetOffset()) {
				if (shiftSequenceAndMPE) {
					((ExpressionParamSet*)summary->paramCollection)
					    ->shiftHorizontally(modelStackWithParamCollection, amount, effectiveLength);
				}
			}

			// Normal case (non MPE automation)
			else {
				if (shiftAutomation) {
					summary->paramCollection->shiftHorizontally(modelStackWithParamCollection, amount, effectiveLength);
				}
			}
			summary++;
			i++;
		}
	}

	// if shiftSequenceAndMPE is true, shift notes
	if (shiftSequenceAndMPE) {
		notes.shiftHorizontal(amount, effectiveLength);
	}
}

void NoteRow::clear(Action* action, ModelStackWithNoteRow* modelStack, bool clearAutomation, bool clearSequenceAndMPE) {
	// the following code iterates through all param collections and clears automation and MPE separately
	// automation only gets cleared if clearAutomation is true
	// MPE only gets cleared if clearSequenceAndMPE is true
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow();

	if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
		ParamCollectionSummary* summary = paramManager.summaries;

		int32_t i = 0;

		while (summary->paramCollection) {

			ModelStackWithParamCollection* modelStackWithParamCollection =
			    modelStackWithThreeMainThings->addParamCollection(summary->paramCollection, summary);

			// Special case for MPE only - not even "mono" / Clip-level expression.
			if (i == paramManager.getExpressionParamSetOffset()) {
				if (clearSequenceAndMPE) {
					((ExpressionParamSet*)summary->paramCollection)
					    ->deleteAllAutomation(action, modelStackWithParamCollection);
				}
			}

			// Normal case (non MPE automation)
			else {
				if (clearAutomation) {
					summary->paramCollection->deleteAllAutomation(action, modelStackWithParamCollection);
				}
			}
			summary++;
			i++;
		}
	}

	// if clearSequenceAndMPE is true, clear notes
	if (clearSequenceAndMPE) {

		stopCurrentlyPlayingNote(modelStack);

		if (action) {
			Error error = action->recordNoteArrayChangeIfNotAlreadySnapshotted(
			    (InstrumentClip*)modelStack->getTimelineCounter(), modelStack->noteRowId, &notes,
			    true); // Steal data
			if (error != Error::NONE) {
				goto justEmpty;
			}
		}
		else {
justEmpty:
			notes.empty();
		}
	}
}

bool NoteRow::doesProbabilityExist(int32_t apartFromPos, int32_t probability, int32_t secondProbability) {

	for (int32_t n = 0; n < notes.getNumElements(); n++) {
		Note* note = notes.getElement(n);
		if (note->pos != apartFromPos) {
			if (note->getProbability() == probability) {
				return true;
			}
			if (secondProbability != -1 && note->getProbability() == secondProbability) {
				return true;
			}
		}
	}

	return false;
}

bool NoteRow::paste(ModelStackWithNoteRow* modelStack, CopiedNoteRow* copiedNoteRow, float scaleFactor,
                    int32_t screenEndPos, Action* action) {

	int32_t minPos = 0;
	int32_t effectiveLength = modelStack->getLoopLength();
	int32_t maxPos = std::min(screenEndPos, effectiveLength);

	if (action) {
		// Snapshot how Notes were before, in bulk. It's quite likely that this has already been done as the
		// area was cleared - but not if notes was empty
		action->recordNoteArrayChangeIfNotAlreadySnapshotted((InstrumentClip*)modelStack->getTimelineCounter(),
		                                                     modelStack->noteRowId, &notes, false);
	}

	// TODO: this could be done without all these many inserts, and could be improved further by "stealing" the
	// data into the action, above
	for (int32_t n = 0; n < copiedNoteRow->numNotes; n++) {

		Note* noteSource = &copiedNoteRow->notes[n];

		int32_t newPos =
		    modelStack->song->xScroll[NAVIGATION_CLIP] + (int32_t)roundf((float)noteSource->pos * scaleFactor);

		// Make sure that with dividing and rounding, we're not overlapping the previous note - or past the end
		// of the screen / Clip
		if (newPos < minPos || newPos >= maxPos) {
			continue;
		}

		int32_t newLength = roundf((float)noteSource->length * scaleFactor);
		newLength = std::max(newLength, (int32_t)1);
		newLength = std::min(newLength, maxPos - newPos);

		int32_t noteDestI = notes.insertAtKey(newPos);
		Note* noteDest = notes.getElement(noteDestI);
		if (!noteDest) {
			return false;
		}

		noteDest->length = newLength;
		noteDest->velocity = noteSource->velocity;
		noteDest->probability = noteSource->probability;
		noteDest->lift = noteSource->lift;
		noteDest->iterance = noteSource->iterance;
		noteDest->fill = noteSource->fill;

		minPos = newPos + newLength;
	}
	return true;
}

void NoteRow::giveMidiCommandsToDrum() {
	// Prior to V2.0, NoteRows had MIDI sound / mute commands. Now this belongs to the Drum, so copy that over
	if (muteMIDICommand.containsSomething()) {
		if (!drum->muteMIDICommand.containsSomething()) {
			drum->muteMIDICommand = muteMIDICommand;
		}
		muteMIDICommand.clear();
	}

	if (midiInput.containsSomething()) {
		if (!drum->midiInput.containsSomething()) {
			drum->midiInput = midiInput;
		}
		midiInput.clear();
	}
}

void NoteRow::grabMidiCommandsFromDrum() {
	if (drum) {
		muteMIDICommand = drum->muteMIDICommand;
		drum->muteMIDICommand.clear();

		midiInput = drum->midiInput;
		drum->midiInput.clear();
	}
}

// This function completely flattens iteration dependence (but not probability).
Error NoteRow::appendNoteRow(ModelStackWithNoteRow* thisModelStack, ModelStackWithNoteRow* otherModelStack,
                             int32_t offset, int32_t whichRepeatThisIs, int32_t otherNoteRowLength) {

	NoteRow* otherNoteRow = otherModelStack->getNoteRow();
	InstrumentClip* clip = (InstrumentClip*)thisModelStack->getTimelineCounter();

	SequenceDirection effectiveSequenceDirectionMode = otherNoteRow->getEffectiveSequenceDirectionMode(otherModelStack);
	bool pingpongingGenerally = effectiveSequenceDirectionMode == SequenceDirection::PINGPONG;
	bool reversingNow = (effectiveSequenceDirectionMode == SequenceDirection::REVERSE
	                     || (pingpongingGenerally && (whichRepeatThisIs & 1)));

	if (paramManager.containsAnyParamCollectionsIncludingExpression()
	    && otherNoteRow->paramManager.containsAnyParamCollectionsIncludingExpression()) {
		int32_t reverseThisRepeatWithLength = reversingNow ? otherNoteRowLength : 0;

		paramManager.appendParamManager(thisModelStack->addOtherTwoThingsAutomaticallyGivenNoteRow(),
		                                otherModelStack->addOtherTwoThingsAutomaticallyGivenNoteRow(), offset,
		                                reverseThisRepeatWithLength, pingpongingGenerally);
	}

	int32_t numToInsert = otherNoteRow->notes.getNumElements();
	if (!numToInsert) {
		return Error::NONE;
	}

	// Deal with single droning note case - but don't do this for samples in CUT or STRETCH mode
	if (numToInsert == 1 && notes.getElement(0)->length == otherNoteRowLength) {
		Sound* sound = NULL;
		ParamManagerForTimeline* paramManagerNow = NULL;

		if (drum && drum->type == DrumType::SOUND) {
			sound = (SoundDrum*)drum;
			paramManagerNow = &paramManager;
		}

		else {
			if (clip->output->type == OutputType::SYNTH) {
				sound = (SoundInstrument*)clip->output;
				paramManagerNow = &clip->paramManager;
			}
		}

		if (!sound
		    || (!sound->hasCutModeSamples(paramManagerNow) && !sound->hasAnyTimeStretchSyncing(paramManagerNow))) {

			int32_t numNotesHere = notes.getNumElements();
			if (numNotesHere) {
				Note* existingNote = notes.getElement(numNotesHere - 1);
				existingNote->length += otherNoteRowLength;
			}
			return Error::NONE;
		}
	}

	// Or, if still here, do normal case.
	int32_t insertIndex = notes.getNumElements();

	// Pre-emptively insert space for all the notes.
	Error error = notes.insertAtIndex(insertIndex, numToInsert);
	if (error != Error::NONE) {
		return error;
	}

	// If reversing / pingponging backwards now...
	if (reversingNow) {

		// If yes note tails...
		if (clip->allowNoteTails(thisModelStack)) {

			// Investigate whether there's a wrapped note
			Note* lastNote = (Note*)otherNoteRow->notes.getElementAddress(numToInsert - 1);

			int32_t lengthBeforeWrap = otherNoteRowLength - lastNote->pos;
			int32_t lengthAfterWrap = lastNote->length - lengthBeforeWrap;
			bool anyWrapping = (lengthAfterWrap > 0);

			// If there is a wrapped note, we need to edit the length of the copy of it that already exists at
			// the end of this NoteRow *before* we do the appending.
			if (anyWrapping && pingpongingGenerally) {
				if (insertIndex) {
					Note* lastNoteMe = (Note*)notes.getElementAddress(insertIndex - 1);
					int32_t distanceFromEnd = offset - lastNoteMe->pos;
					if (lastNoteMe->length > distanceFromEnd) {
						lastNoteMe->length = distanceFromEnd + lengthBeforeWrap;
					}
				}
			}

			for (int32_t iNewWithinRepeat = 0; iNewWithinRepeat < numToInsert; iNewWithinRepeat++) {
				int32_t iOld = numToInsert - 1 - iNewWithinRepeat - anyWrapping;
				if (iOld < 0) {
					iOld = numToInsert - 1; // In case of wrapping
				}

				Note* oldNote = (Note*)otherNoteRow->notes.getElementAddress(iOld);

				int32_t newLength = oldNote->length;
				int32_t newPos = otherNoteRowLength - oldNote->pos - newLength;

				if (newPos < 0) { // This means we've got the wrapped note, while reversing
					if (pingpongingGenerally) {
						newPos = otherNoteRowLength - lengthAfterWrap;
						newLength = lengthAfterWrap << 1;
					}
					else {
						newPos += otherNoteRowLength;
					}
				}

				Note* newNote = (Note*)notes.getElementAddress(insertIndex++);
				newNote->pos = newPos + offset;
				newNote->setLength(newLength);
				newNote->setProbability(oldNote->getProbability());
				newNote->setVelocity(oldNote->getVelocity());
				newNote->setLift(oldNote->getLift());
				newNote->setIterance(oldNote->getIterance());
				newNote->setFill(oldNote->getFill());
			}
		}

		// No-tails (e.g. one-shot samples):
		else {

			Note* firstNote = (Note*)otherNoteRow->notes.getElementAddress(0);
			bool anythingAtZero = (firstNote->pos == 0);

			for (int32_t iNewWithinRepeat = 0; iNewWithinRepeat < numToInsert; iNewWithinRepeat++) {
				int32_t iOld = -iNewWithinRepeat - !anythingAtZero;
				if (iOld < 0) {
					iOld += numToInsert;
				}

				Note* oldNote = (Note*)otherNoteRow->notes.getElementAddress(iOld);

				int32_t newPos = -oldNote->pos;
				if (newPos < 0) {
					newPos += otherNoteRowLength;
				}

				Note* newNote = (Note*)notes.getElementAddress(insertIndex++);
				newNote->pos = newPos + offset;
				newNote->setLength(1);
				newNote->setProbability(oldNote->getProbability());
				newNote->setVelocity(oldNote->getVelocity());
				newNote->setLift(oldNote->getLift());
				newNote->setIterance(oldNote->getIterance());
				newNote->setFill(oldNote->getFill());
			}
		}
	}

	// Or if not reversing, easier.
	else {

		for (int32_t i = 0; i < numToInsert; i++) {
			Note* oldNote = otherNoteRow->notes.getElement(i);
			Note* newNote = notes.getElement(insertIndex++);
			newNote->pos = oldNote->pos + offset;
			newNote->length = oldNote->length;
			newNote->velocity = oldNote->velocity;
			newNote->setLift(oldNote->getLift());
			newNote->probability = oldNote->probability;
			newNote->iterance = oldNote->iterance;
			newNote->fill = oldNote->fill;
		}
	}

	// We may not have ended up using all the elements we inserted, due to iteration dependence, so delete any
	// extra.
	int32_t numExtraToDelete = notes.getNumElements() - insertIndex;
	if (numExtraToDelete) {
		notes.deleteAtIndex(insertIndex, numExtraToDelete);
	}

	return Error::NONE;
}

// This gets called on the "unique" copy of the original NoteRow
void NoteRow::resumeOriginalNoteRowFromThisClone(ModelStackWithNoteRow* modelStackOriginal,
                                                 ModelStackWithNoteRow* modelStackClone) {

	bool wasSounding = (!muted && soundingStatus == STATUS_SEQUENCED_NOTE);

	NoteRow* originalNoteRow =
	    modelStackOriginal->getNoteRowAllowNull(); // It might be NULL - we'll check for that below.

	if (originalNoteRow && !originalNoteRow->muted) {
		originalNoteRow->silentlyResumePlayback(modelStackOriginal);
	}

	bool stillSounding =
	    (originalNoteRow && !originalNoteRow->muted && originalNoteRow->soundingStatus == STATUS_SEQUENCED_NOTE);

	bool shouldSoundNoteOffNow = (wasSounding && !stillSounding);

	stopCurrentlyPlayingNote(modelStackClone, shouldSoundNoteOffNow);
}

void NoteRow::trimParamManager(ModelStackWithNoteRow* modelStack) {
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow();
	int32_t effectiveLength = modelStackWithThreeMainThings->getLoopLength();
	paramManager.trimToLength(effectiveLength, modelStackWithThreeMainThings, NULL, false);
}

uint32_t NoteRow::getLivePos(ModelStackWithNoteRow const* modelStack) {

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounter();

	// Often this is up to the parent Clip
	if (!hasIndependentPlayPos()) {
		return clip->getLivePos();
	}

	int32_t effectiveLastProcessedPos = lastProcessedPosIfIndependent; // We established this, above

	int32_t numSwungTicksInSinceLastActioned =
	    playbackHandler.getNumSwungTicksInSinceLastActionedSwungTick() + clip->noteRowsNumTicksBehindClip;

	if (modelStack->isCurrentlyPlayingReversed()) {
		numSwungTicksInSinceLastActioned = -numSwungTicksInSinceLastActioned;
	}

	int32_t livePos = effectiveLastProcessedPos + numSwungTicksInSinceLastActioned;

	if (livePos < 0) {
		livePos += modelStack->getLoopLength(); // Could happen if reversing and currentPosHere is 0.
	}

	return livePos;
}

bool NoteRow::hasIndependentPlayPos() {
	return (loopLengthIfIndependent || sequenceDirectionMode != SequenceDirection::OBEY_PARENT);
}

void NoteRow::getMPEValues(ModelStackWithNoteRow* modelStack, int16_t* mpeValues) {

	ExpressionParamSet* mpeParams = paramManager.getExpressionParamSet();
	if (!mpeParams) {
		for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
			mpeValues[m] = 0;
		}
		return;
	}

	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		mpeValues[m] = mpeParams->params[m].getCurrentValue() >> 16;
	}
}

// This is obviously very inefficient, doing deletions for every "screen", sequentially.
// Also, pos is provided as the squareStart, but in a perfect world, we'd actually use the pos of the first note
// within that square - for each "screen"! Or something like that...
void NoteRow::clearMPEUpUntilNextNote(ModelStackWithNoteRow* modelStack, int32_t pos, int32_t wrapEditLevel,
                                      bool shouldJustDeleteNodes) {

	ParamCollectionSummary* mpeParamsSummary = paramManager.getExpressionParamSetSummary();
	ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;
	if (!mpeParams) {
		return;
	}

	/*
	for (int32_t i = 0; i < kNumExpressionDimensions; i++) {
	    if (mpeParams->params[i].isAutomated()) goto needToDoIt;
	}
	return;
	*/

needToDoIt:
	int32_t effectiveLength = modelStack->getLoopLength();

	pos = (uint32_t)pos % (uint32_t)wrapEditLevel;

	do {

		int32_t length = getDistanceToNextNote(pos, modelStack);

		ModelStackWithParamCollection* modelStackWithParamCollection =
		    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(mpeParams, mpeParamsSummary);

		for (int32_t i = 0; i < kNumExpressionDimensions; i++) {
			AutoParam* param = &mpeParams->params[i];
			ModelStackWithAutoParam* modelStackWithAutoParam = modelStackWithParamCollection->addAutoParam(i, param);

			if (shouldJustDeleteNodes) {
				param->deleteNodesWithinRegion(modelStackWithAutoParam, pos, length);
			}
			else {
				param->setValueForRegion(pos, length, 0, modelStackWithAutoParam);
			}
		}

		pos += wrapEditLevel;
	} while (pos < effectiveLength);
}

// Returns whether success.
bool NoteRow::recordPolyphonicExpressionEvent(ModelStackWithNoteRow* modelStack, int32_t newValueBig,
                                              int32_t whichExpressionDimension, bool forDrum) {

	uint32_t livePos = modelStack->getLivePos();
	if (livePos < ignoreNoteOnsBefore_) {
		return false;
	}

	paramManager.ensureExpressionParamSetExists(forDrum);
	ParamCollectionSummary* mpeParamsSummary = paramManager.getExpressionParamSetSummary();
	ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;
	if (!mpeParams) {
		return false;
	}

	AutoParam* param = &mpeParams->params[whichExpressionDimension];

	ModelStackWithAutoParam* modelStackWithAutoParam =
	    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParam(mpeParams, mpeParamsSummary,
	                                                                       whichExpressionDimension, param);

	// Only if this exact TimelineCounter and NoteRow is having automation step-edited, we can set the value for
	// just a region.
	if (view.modLength && modelStackWithAutoParam->noteRowId == view.modNoteRowId
	    && modelStackWithAutoParam->getTimelineCounter()
	           == view.activeModControllableModelStack.getTimelineCounterAllowNull()) {

		// As well as just setting values now, InstrumentClipView keeps a record, for in case the user then
		// releases the note, in which case we'll want the values from when they pressed hardest etc.
		instrumentClipView.reportMPEValueForNoteEditing(whichExpressionDimension, newValueBig);

		// And also, set the values now, for in case they're instead gonna stop editing the note before
		// releasing this MIDI note.
		param->setValueForRegion(view.modPos, view.modLength, newValueBig, modelStackWithAutoParam);
	}
	else {
		int32_t distanceToNextNote =
		    getDistanceToNextNote(livePos, modelStackWithAutoParam, modelStack->isCurrentlyPlayingReversed());
		int32_t distanceToNextNode = param->getDistanceToNextNode(
		    modelStackWithAutoParam, livePos, modelStackWithAutoParam->isCurrentlyPlayingReversed());

		bool doMPEMode = (distanceToNextNode >= distanceToNextNote);

		param->setCurrentValueInResponseToUserInput(
		    newValueBig, modelStackWithAutoParam, true, livePos, false,
		    doMPEMode); // Don't allow deletion of nodes in linear run. See comments above that function
	}

	return true;
}

void NoteRow::setSequenceDirectionMode(ModelStackWithNoteRow* modelStack, SequenceDirection newMode) {
	int32_t lastProcessedPosBefore = modelStack->getLastProcessedPos();

	bool reversedBefore = modelStack->isCurrentlyPlayingReversed();
	sequenceDirectionMode = newMode;
	lastProcessedPosIfIndependent = lastProcessedPosBefore; // We might change this, below.

	// If now pingponging...
	if (getEffectiveSequenceDirectionMode(modelStack) == SequenceDirection::PINGPONG) {
		currentlyPlayingReversedIfIndependent = reversedBefore;
	}

	// Or if now *not* pingponging...
	else {
		// Won't necessarily have an effect - if we're now set to obey-parent.
		currentlyPlayingReversedIfIndependent = (newMode == SequenceDirection::REVERSE);

		// If we just changed direction...
		if (reversedBefore != modelStack->isCurrentlyPlayingReversed()) {
			lastProcessedPosIfIndependent =
			    modelStack->getLoopLength() - lastProcessedPosIfIndependent; // Again, might have no effect.
			if (!muted && playbackHandler.isEitherClockActive()
			    && modelStack->song->isClipActive((Clip*)modelStack->getTimelineCounter())) {
				resumePlayback(modelStack, true);
			}
		}
	}
}

/*
for (int32_t n = 0; n < notes.getNumElements(); n++) {
    Note* note = notes.getElement(n);

}
 */
