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

#include <ArrangerView.h>
#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <ClipInstance.h>
#include <DString.h>
#include <InstrumentClip.h>
#include <InstrumentClipMinder.h>
#include "NoteRow.h"
#include "soundeditor.h"
#include "functions.h"
#include "lookuptables.h"
#include <math.h>
#include <sounddrum.h>
#include <soundinstrument.h>
#include <SessionView.h>
#include "uart.h"
#include "CVEngine.h"
#include "numericdriver.h"
#include "song.h"
#include "kit.h"
#include "midiengine.h"
#include "View.h"
#include "Note.h"
#include "DrumName.h"
#include "Action.h"
#include "ConsequenceNoteRowMute.h"
#include "ActionLogger.h"
#include "ConsequenceScaleAddNote.h"
#include "GeneralMemoryAllocator.h"
#include "PlaybackMode.h"
#include "Arrangement.h"
#include "Session.h"
#include "MIDIInstrument.h"
#include <new>
#include "storagemanager.h"
#include "CVInstrument.h"
#include "GeneralMemoryAllocator.h"
#include <new>
#include "FlashStorage.h"
#include "ModelStack.h"
#include "ParamSet.h"
#include "PatchCableSet.h"
#include "MIDIParamCollection.h"
#include "MIDIParam.h"
#include "MIDIDevice.h"
#include "ParamNode.h"
#include "Browser.h"
#include "FileItem.h"

#if HAVE_OLED
#include "oled.h"
#endif

// Supplying song is optional, and basically only for the purpose of setting yScroll according to root note
InstrumentClip::InstrumentClip(Song* song) : Clip(CLIP_TYPE_INSTRUMENT) {
	arpeggiatorRate = 0;
	arpeggiatorGate = 0;

	midiBank = 128; // Means none
	midiSub = 128;  // Means none
	midiPGM = 128;  // Means none

	currentlyRecordingLinearly = false;

	if (song) colourOffset -= song->rootNote;

	wrapEditing = false;
	for (int i = 0; i < 4; i++) {
		backedUpInstrumentSlot[i] = 0;
		backedUpInstrumentSubSlot[i] = -1;
	}

	affectEntire = true;

	inScaleMode = (FlashStorage::defaultScale != PRESET_SCALE_NONE);
	onKeyboardScreen = false;

	if (song) {
		int yNote = ((uint16_t)(song->rootNote + 120) % 12) + 60;
		if (yNote > 66) yNote -= 12;
		yScroll = getYVisualFromYNote(
		    yNote,
		    song); // This takes into account the rootNote, which could be anything. Must be called after the above stuff is set up
	}
	else {
		yScroll =
		    0; // Only for safety. Shouldn't actually get here if we're not going to overwrite this elsewhere I think...
	}
	yScrollKeyboardScreen = 60 - (displayHeight >> 2) * KEYBOARD_ROW_INTERVAL;

	instrumentTypeWhileLoading = 0;
}

// You must call prepareForDestruction() before this, preferably by calling Song::deleteClipObject()
// Will call audio routine!!! Necessary to avoid voice cuts, especially when switching song
InstrumentClip::~InstrumentClip() {

	// Note: it's possible that we might be currentlyRecordingLinearly if we're being destructed because of a song-swap. That's ok.
	// Whereas, for AudioClips, it's made sure that all linear recording is stopped first

	deleteBackedUpParamManagerMIDI();
}

void InstrumentClip::deleteBackedUpParamManagerMIDI() {
	if (backedUpParamManagerMIDI.containsAnyMainParamCollections()) {
		backedUpParamManagerMIDI.destructAndForgetParamCollections();
	}
}

void InstrumentClip::copyBasicsFrom(Clip* otherClip) {
	Clip::copyBasicsFrom(otherClip);

	InstrumentClip* otherInstrumentClip = (InstrumentClip*)otherClip;

	midiBank = otherInstrumentClip->midiBank;
	midiSub = otherInstrumentClip->midiSub;
	midiPGM = otherInstrumentClip->midiPGM;

	onKeyboardScreen = otherInstrumentClip->onKeyboardScreen;
	inScaleMode = otherInstrumentClip->inScaleMode;
	wrapEditing = otherInstrumentClip->wrapEditing;
	wrapEditLevel = otherInstrumentClip->wrapEditLevel;
	yScroll = otherInstrumentClip->yScroll;
	yScrollKeyboardScreen = otherInstrumentClip->yScrollKeyboardScreen;
	sequenceDirectionMode = otherInstrumentClip->sequenceDirectionMode;

	affectEntire = otherInstrumentClip->affectEntire;

	memcpy(backedUpInstrumentSlot, otherInstrumentClip->backedUpInstrumentSlot, sizeof(backedUpInstrumentSlot));
	memcpy(backedUpInstrumentSubSlot, otherInstrumentClip->backedUpInstrumentSubSlot,
	       sizeof(backedUpInstrumentSubSlot));
	for (int i = 0; i < 2; i++) {
		backedUpInstrumentName[i].set(&otherInstrumentClip->backedUpInstrumentName[i]);
	}
	for (int i = 0; i < 2; i++) {
		backedUpInstrumentDirPath[i].set(&otherInstrumentClip->backedUpInstrumentDirPath[i]);
	}

	arpSettings.cloneFrom(&otherInstrumentClip->arpSettings);
	arpeggiatorRate = otherInstrumentClip->arpeggiatorRate;
	arpeggiatorGate = otherInstrumentClip->arpeggiatorGate;
}

// Will replace the Clip in the modelStack, if success.
int InstrumentClip::clone(ModelStackWithTimelineCounter* modelStack, bool shouldFlattenReversing) {

	void* clipMemory = generalMemoryAllocator.alloc(sizeof(InstrumentClip), NULL, false, true);
	if (!clipMemory) return ERROR_INSUFFICIENT_RAM;

	InstrumentClip* newClip =
	    new (clipMemory) InstrumentClip(); // Don't supply Song. yScroll will get set in copyBasicsFrom()

	newClip->copyBasicsFrom(this);

	int32_t reverseWithLength = 0;
	if (shouldFlattenReversing && sequenceDirectionMode == SEQUENCE_DIRECTION_REVERSE) reverseWithLength = loopLength;

	int error = newClip->paramManager.cloneParamCollectionsFrom(&paramManager, true, true, reverseWithLength);
	if (error) {
deleteClipAndGetOut:
		newClip->~InstrumentClip();
		generalMemoryAllocator.dealloc(clipMemory);
		return error;
	}

	newClip->activeIfNoSolo = false;
	newClip->soloingInSessionMode = false;
	newClip->output = output;

	if (!newClip->noteRows.cloneFrom(&noteRows)) {
		error = ERROR_INSUFFICIENT_RAM;
		goto deleteClipAndGetOut;
	}

	modelStack->setTimelineCounter(newClip);

	for (int i = 0; i < newClip->noteRows.getNumElements(); i++) {
		NoteRow* noteRow = newClip->noteRows.getElement(i);
		int noteRowId = newClip->getNoteRowId(noteRow, i);
		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);
		int error = noteRow->beenCloned(modelStackWithNoteRow, shouldFlattenReversing);

		// If that fails, we have to keep going, cos otherwise some NoteRows' NoteVector will be left pointing to stuff it shouldn't be
	}

	if (shouldFlattenReversing && newClip->sequenceDirectionMode == SEQUENCE_DIRECTION_REVERSE)
		newClip->sequenceDirectionMode = SEQUENCE_DIRECTION_FORWARD;
	// Leave PINGPONG as it is, because we haven't actually flattened that - its effect wouldn't be seen until a repeat happened.
	// And we may be about to flatten it with a increaseLengthWithRepeats(), so need to keep this designation for now.

	return NO_ERROR;
}

// newLength might not be any longer than we already were - but this function still gets called in case any shorter NoteRows need lengthening.
// So, this function must allow for that case (Clip length staying the same).
void InstrumentClip::increaseLengthWithRepeats(ModelStackWithTimelineCounter* modelStack, int32_t newLength,
                                               int independentNoteRowInstruction,
                                               bool completelyRenderOutIterationDependence, Action* action) {

	int numRepeatsRounded =
	    completelyRenderOutIterationDependence ? 0 : (uint32_t)(newLength + (loopLength >> 1)) / (uint32_t)loopLength;

	// Tell all the noteRows
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		int noteRowId = getNoteRowId(thisNoteRow, i);

		int numRepeatsRoundedHere = numRepeatsRounded;
		int32_t oldLengthHere = loopLength;
		int32_t newLengthHere = newLength;

		// Deal specially with NoteRows with independent length.
		if (thisNoteRow->loopLengthIfIndependent) {

			switch (independentNoteRowInstruction) {
			case INDEPENDENT_NOTEROW_LENGTH_INCREASE_DOUBLE:
				newLengthHere = thisNoteRow->loopLengthIfIndependent << 1;
				break;

			case INDEPENDENT_NOTEROW_LENGTH_INCREASE_ROUND_UP:
				newLengthHere = ((uint32_t)(newLength - 1) / (uint32_t)thisNoteRow->loopLengthIfIndependent + 1)
				                * thisNoteRow->loopLengthIfIndependent;
				break;

			default:
				__builtin_unreachable();
			}

			numRepeatsRoundedHere = completelyRenderOutIterationDependence
			                            ? 0
			                            : (uint32_t)(newLengthHere + (thisNoteRow->loopLengthIfIndependent >> 1))
			                                  / (uint32_t)thisNoteRow->loopLengthIfIndependent;

			oldLengthHere = thisNoteRow->loopLengthIfIndependent;
		}

		if (newLengthHere > oldLengthHere) { // Or do nothing if length staying the same
			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);
			thisNoteRow->generateRepeats(modelStackWithNoteRow, oldLengthHere, newLengthHere, numRepeatsRoundedHere,
			                             action);
		}

		if (thisNoteRow->loopLengthIfIndependent) {
			thisNoteRow->loopLengthIfIndependent = newLengthHere;
		}
	}

	bool pingponging = (sequenceDirectionMode == SEQUENCE_DIRECTION_PINGPONG);

	if (newLength > loopLength) {
		ModelStackWithThreeMainThings* modelStackWithParamManager =
		    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);
		paramManager.generateRepeats(modelStackWithParamManager, loopLength, newLength, pingponging);
	}

	if (pingponging)
		sequenceDirectionMode =
		    SEQUENCE_DIRECTION_FORWARD; // Pingponging has been flattened out, and although there are arguments either way, I think removing that setting now is best.

	loopLength = newLength;
}

// If action is NULL, that means this is being called as part of an undo
// Call this *after* you've set length to its new value (why did I do it this way?)
void InstrumentClip::lengthChanged(ModelStackWithTimelineCounter* modelStack, int32_t oldLength, Action* action) {

	if (loopLength < oldLength) {
		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);

			// Only if NoteRow doesn't have independent length set, then trim it and stuff
			if (!thisNoteRow->loopLengthIfIndependent) {
				ModelStackWithNoteRow* modelStackWithNoteRow =
				    modelStack->addNoteRow(getNoteRowId(thisNoteRow, i), thisNoteRow);
				thisNoteRow->trimToLength(loopLength, modelStackWithNoteRow, action);
			}

			// Or if it does have independent length, are we now the same length as it?
			else {
				if (thisNoteRow->loopLengthIfIndependent == loopLength) {
					thisNoteRow->loopLengthIfIndependent = 0;
				}
			}
		}
	}

	Clip::lengthChanged(modelStack, oldLength, action);
}

// Does this individually for each NoteRow, because they might be different lengths, and some might need repeating while others need chopping.
void InstrumentClip::repeatOrChopToExactLength(ModelStackWithTimelineCounter* modelStack, int32_t newLength) {
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		int32_t oldLengthHere = thisNoteRow->loopLengthIfIndependent;
		if (!oldLengthHere) oldLengthHere = loopLength;

		if (oldLengthHere != newLength) {
			int noteRowId = getNoteRowId(thisNoteRow, i);
			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);

			if (newLength > oldLengthHere) {
				int numRepeatsRounded = (uint32_t)(newLength + (oldLengthHere >> 1)) / (uint32_t)oldLengthHere;
				thisNoteRow->generateRepeats(modelStackWithNoteRow, oldLengthHere, newLength, numRepeatsRounded, NULL);
			}

			else {
				thisNoteRow->trimToLength(newLength, modelStackWithNoteRow, NULL);
			}
		}

		thisNoteRow->loopLengthIfIndependent = 0; // It doesn't need to be independent anymore.
	}

	if (newLength > loopLength) {
		bool pingponging = (sequenceDirectionMode == SEQUENCE_DIRECTION_PINGPONG);

		ModelStackWithThreeMainThings* modelStackWithParamManager =
		    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);

		paramManager.generateRepeats(modelStackWithParamManager, loopLength, newLength, pingponging);

		if (pingponging)
			sequenceDirectionMode =
			    SEQUENCE_DIRECTION_FORWARD; // Pingponging has been flattened out, and although there are arguments either way, I think removing that setting now is best.
	}

	int32_t oldLength = loopLength;

	loopLength = newLength;

	Clip::lengthChanged(
	    modelStack, oldLength,
	    NULL); // Call this on Clip::, not us InstrumentClip, because we've done our own version above of what that call would do.

	if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(this)) {
		resumePlayback(modelStack);
	}
}

// This only gets called when undoing a "multiply Clip".
void InstrumentClip::halveNoteRowsWithIndependentLength(ModelStackWithTimelineCounter* modelStack) {
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* noteRow = noteRows.getElement(i);

		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(getNoteRowId(noteRow, i), noteRow);

		if (noteRow->loopLengthIfIndependent) {
			noteRow->setLength(modelStackWithNoteRow, noteRow->loopLengthIfIndependent >> 1, NULL,
			                   modelStackWithNoteRow->getLastProcessedPos(), true);
		}
	}
}

// Accepts any pos >= -length
void InstrumentClip::setPos(ModelStackWithTimelineCounter* modelStack, int32_t newPos,
                            bool useActualPosForParamManagers) {
	Clip::setPos(modelStack, newPos, useActualPosForParamManagers); // This will also call our own virtual expectEvent()

	noteRowsNumTicksBehindClip = 0;

	Clip::setPosForParamManagers(
	    modelStack,
	    useActualPosForParamManagers); // Call on Clip:: only - below in this function, we're going to do the equivalent
	                                   // of our own setPosForParamManagers().

	uint32_t posForParamManagers = useActualPosForParamManagers ? getLivePos() : lastProcessedPos;

	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		// This function is "supposed" to call setPosForParamManagers() on this InstrumentClip, but instead, we'll do our own thing here, so we only have to iterate through NoteRows once.
		if (thisNoteRow->paramManager.mightContainAutomation()) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addNoteRowAndExtraStuff(i, thisNoteRow);
			thisNoteRow->paramManager.setPlayPos(posForParamManagers, modelStackWithThreeMainThings,
			                                     modelStackWithThreeMainThings->isCurrentlyPlayingReversed());
		}

		// And now, some setting up for NoteRows with independent play-positions.
		if (thisNoteRow->hasIndependentPlayPos()) {

			ModelStackWithNoteRow* modelStackWithNoteRow =
			    modelStack->addNoteRow(getNoteRowId(thisNoteRow, i), thisNoteRow);
			int32_t effectiveLoopLength = modelStackWithNoteRow->getLoopLength();

			// The below basically mirrors the code / logic in Clip::setPos()
			thisNoteRow->repeatCountIfIndependent = (uint32_t)newPos / (uint32_t)effectiveLoopLength;

			int effectiveSequenceDirectionMode = thisNoteRow->getEffectiveSequenceDirectionMode(modelStackWithNoteRow);

			thisNoteRow->currentlyPlayingReversedIfIndependent =
			    (effectiveSequenceDirectionMode
			         == SEQUENCE_DIRECTION_REVERSE // Syncing pingponging with repeatCount is particularly important for when resuming after
			     || (effectiveSequenceDirectionMode == SEQUENCE_DIRECTION_PINGPONG
			         && (thisNoteRow->repeatCountIfIndependent
			             & 1))); // recording a clone of this Clip from session to arranger.

			thisNoteRow->lastProcessedPosIfIndependent =
			    newPos - thisNoteRow->repeatCountIfIndependent * effectiveLoopLength;
			if (thisNoteRow->currentlyPlayingReversedIfIndependent) {
				if (thisNoteRow->lastProcessedPosIfIndependent)
					thisNoteRow->lastProcessedPosIfIndependent =
					    effectiveLoopLength - thisNoteRow->lastProcessedPosIfIndependent;
				else thisNoteRow->repeatCountIfIndependent--;
			}
		}
	}
}

int InstrumentClip::beginLinearRecording(ModelStackWithTimelineCounter* modelStack, int buttonPressLatency) {
	currentlyRecordingLinearly = true;

	if (output->type == INSTRUMENT_TYPE_KIT) {
		Kit* kit = (Kit*)output;

		Action* action = NULL;

		for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {

			int velocity = thisDrum->earlyNoteVelocity;

			if (velocity) {
				thisDrum->earlyNoteVelocity = 0;

				int noteRowIndex;
				NoteRow* noteRow = getNoteRowForDrum(
				    thisDrum, &noteRowIndex); // Remember, I'm planning to introduce a faster search/index for this
				if (noteRow) {

					if (!action) {
						action = actionLogger.getNewAction(ACTION_RECORD, true);
					}

					ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, noteRow);

					noteRow->attemptNoteAdd(0, 1, velocity, NUM_PROBABILITY_VALUES, modelStackWithNoteRow, action);
					if (!thisDrum->earlyNoteStillActive) {
						Uart::println("skipping next note");
						noteRow->skipNextNote = true;
					}
				}
			}
		}
	}

	else {
		MelodicInstrument* melodicInstrument = (MelodicInstrument*)output;
		if (melodicInstrument->earlyNotes.getNumElements()) {

			Action* action = actionLogger.getNewAction(ACTION_RECORD, true);
			bool scaleAltered = false;

			for (int i = 0; i < melodicInstrument->earlyNotes.getNumElements(); i++) {
				EarlyNote* basicNote = (EarlyNote*)melodicInstrument->earlyNotes.getElementAddress(i);

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    getOrCreateNoteRowForYNote(basicNote->note, modelStack, action, &scaleAltered);
				NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
				if (noteRow) {

					noteRow->attemptNoteAdd(0, 1, basicNote->velocity, NUM_PROBABILITY_VALUES, modelStackWithNoteRow,
					                        action);
					if (!basicNote->stillActive) noteRow->skipNextNote = true;
				}
			}

			// If this caused the scale to change, update scroll
			if (action && scaleAltered) {
				action->updateYScrollClipViewAfter();
			}
		}

		melodicInstrument->earlyNotes.empty();
	}

	return Clip::beginLinearRecording(modelStack, buttonPressLatency);
}

// Gets called by Clip::setPos()
void InstrumentClip::setPosForParamManagers(ModelStackWithTimelineCounter* modelStack, bool useActualPos) {

	uint32_t pos = useActualPos ? getLivePos() : lastProcessedPos;
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->paramManager.mightContainAutomation()) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addNoteRowAndExtraStuff(i, thisNoteRow);
			thisNoteRow->paramManager.setPlayPos(pos, modelStackWithThreeMainThings,
			                                     modelStackWithThreeMainThings->isCurrentlyPlayingReversed());
		}
	}

	Clip::setPosForParamManagers(modelStack, useActualPos); // I think the order is not important here
}

// Grabs automated values from current play-pos. To be called after a possible big change made to automation data, e.g. after an undo.
// This is only to be called if playbackHandler.isEitherClockActive().
void InstrumentClip::reGetParameterAutomation(ModelStackWithTimelineCounter* modelStack) {

	if (!isActiveOnOutput()) return; // Definitely don't do this if we're not an active Clip!

	Clip::reGetParameterAutomation(modelStack);

	uint32_t actualPos = getLivePos();
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->paramManager.mightContainAutomation()) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addNoteRowAndExtraStuff(i, thisNoteRow);
			thisNoteRow->paramManager.setPlayPos(actualPos, modelStackWithThreeMainThings,
			                                     modelStackWithThreeMainThings->isCurrentlyPlayingReversed());
		}
	}
}

int InstrumentClip::transferVoicesToOriginalClipFromThisClone(ModelStackWithTimelineCounter* modelStackOriginal,
                                                              ModelStackWithTimelineCounter* modelStackClone) {
	InstrumentClip* originalClip = (InstrumentClip*)modelStackOriginal->getTimelineCounter();

	if (output->type == INSTRUMENT_TYPE_KIT) {
		if (noteRows.getNumElements() != originalClip->noteRows.getNumElements()) return ERROR_UNSPECIFIED;

		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* clonedNoteRow = noteRows.getElement(i);
			NoteRow* originalNoteRow = originalClip->noteRows.getElement(i);

			ModelStackWithNoteRow* modelStackWithNoteRowClone = modelStackClone->addNoteRow(i, clonedNoteRow);
			ModelStackWithNoteRow* modelStackWithNoteRowOriginal = modelStackOriginal->addNoteRow(i, originalNoteRow);

			clonedNoteRow->resumeOriginalNoteRowFromThisClone(modelStackWithNoteRowOriginal,
			                                                  modelStackWithNoteRowClone);
		}
	}

	else {
		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* clonedNoteRow = noteRows.getElement(i);
			NoteRow* originalNoteRow = originalClip->getNoteRowForYNote(
			    clonedNoteRow->y); // Might come back NULL cos it doesn't exist - that's ok

			ModelStackWithNoteRow* modelStackWithNoteRowClone =
			    modelStackClone->addNoteRow(clonedNoteRow->y, clonedNoteRow);
			ModelStackWithNoteRow* modelStackWithNoteRowOriginal =
			    modelStackOriginal->addNoteRow(clonedNoteRow->y, originalNoteRow); // May end up with NULL noteRow

			clonedNoteRow->resumeOriginalNoteRowFromThisClone(modelStackWithNoteRowOriginal,
			                                                  modelStackWithNoteRowClone);
		}
	}

	return NO_ERROR;
}

// Returns error
int InstrumentClip::appendClip(ModelStackWithTimelineCounter* thisModelStack,
                               ModelStackWithTimelineCounter* otherModelStack) {

	InstrumentClip* otherInstrumentClip = (InstrumentClip*)otherModelStack->getTimelineCounter();

	int whichRepeatThisIs = (uint32_t)loopLength / (uint32_t)otherInstrumentClip->loopLength;

	if (output->type == INSTRUMENT_TYPE_KIT) {
		if (noteRows.getNumElements() != otherInstrumentClip->noteRows.getNumElements()) return ERROR_UNSPECIFIED;

		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* otherNoteRow = otherInstrumentClip->noteRows.getElement(i);
			if (otherNoteRow->loopLengthIfIndependent)
				continue; // Skip NoteRows with independent length - they'll take care of themselves.

			NoteRow* thisNoteRow = noteRows.getElement(i);

			ModelStackWithNoteRow* thisModelStackWithNoteRow = thisModelStack->addNoteRow(i, thisNoteRow);
			ModelStackWithNoteRow* otherModelStackWithNoteRow = otherModelStack->addNoteRow(i, otherNoteRow);

			int error = thisNoteRow->appendNoteRow(thisModelStackWithNoteRow, otherModelStackWithNoteRow, loopLength,
			                                       whichRepeatThisIs, otherInstrumentClip->loopLength);
			if (error) return error;
		}
	}

	else {
		for (int i = 0; i < otherInstrumentClip->noteRows.getNumElements(); i++) {
			NoteRow* otherNoteRow = otherInstrumentClip->noteRows.getElement(i);
			if (otherNoteRow->loopLengthIfIndependent)
				continue; // Skip NoteRows with independent length - they'll take care of themselves.

			int noteRowId = otherNoteRow->y;

			ModelStackWithNoteRow* thisModelStackWithNoteRow = getNoteRowForYNote(noteRowId, thisModelStack);
			NoteRow* thisNoteRow = thisModelStackWithNoteRow->getNoteRowAllowNull();
			if (thisNoteRow) {
				ModelStackWithNoteRow* otherModelStackWithNoteRow =
				    otherModelStack->addNoteRow(noteRowId, otherNoteRow);

				int error = thisNoteRow->appendNoteRow(thisModelStackWithNoteRow, otherModelStackWithNoteRow,
				                                       loopLength, whichRepeatThisIs, otherInstrumentClip->loopLength);
				if (error) return error;
			}
		}
	}

	return Clip::appendClip(thisModelStack, otherModelStack);
}

void InstrumentClip::posReachedEnd(ModelStackWithTimelineCounter* thisModelStack) {
	Clip::posReachedEnd(thisModelStack);

	if (playbackHandler.recording == RECORDING_ARRANGEMENT && isArrangementOnlyClip()) {

		char otherModelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* otherModelStack =
		    setupModelStackWithSong(otherModelStackMemory, thisModelStack->song)
		        ->addTimelineCounter(beingRecordedFromClip);

		appendClip(thisModelStack, otherModelStack);
	}
}

bool InstrumentClip::wantsToBeginLinearRecording(Song* song) {
	if (!Clip::wantsToBeginLinearRecording(song)) return false;

	if (isPendingOverdub)
		return true; // Must take precedence - because we may have already placed some new notes at 0 if user hit key just now

	return !containsAnyNotes();
}

void InstrumentClip::pingpongOccurred(ModelStackWithTimelineCounter* modelStack) {
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		if (thisNoteRow->paramManager.containsAnyParamCollectionsIncludingExpression()
		    && !thisNoteRow->hasIndependentPlayPos()) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addNoteRowAndExtraStuff(i, thisNoteRow);
			thisNoteRow->paramManager.notifyPingpongOccurred(modelStackWithThreeMainThings);
		}
	}
}

void InstrumentClip::processCurrentPos(ModelStackWithTimelineCounter* modelStack, uint32_t ticksSinceLast) {

	Clip::processCurrentPos(modelStack, ticksSinceLast);
	if (modelStack->getTimelineCounter() != this) return; // Is this in case it's created a new Clip or something?

	// We already incremented / decremented noteRowsNumTicksBehindClip and ticksTilNextNoteRowEvent, in the call to incrementPos().

	if (ticksTilNextNoteRowEvent <= 0) {

		// Ok, time to do some ticks

		// We need to at least come back when the Clip wraps
#if HAVE_SEQUENCE_STEP_CONTROL
		if (lastProcessedPos && currentlyPlayingReversed) ticksTilNextNoteRowEvent = lastProcessedPos;
		else
#endif
			ticksTilNextNoteRowEvent = loopLength - lastProcessedPos;

		static PendingNoteOnList
		    pendingNoteOnList; // Making this static, which it really should have always been, actually didn't help max stack usage at all somehow...
		pendingNoteOnList.count = 0;

		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);

			ModelStackWithNoteRow* modelStackWithNoteRow =
			    modelStack->addNoteRow(getNoteRowId(thisNoteRow, i), thisNoteRow);

			int32_t noteRowTicksTilNextEvent =
			    thisNoteRow->processCurrentPos(modelStackWithNoteRow, noteRowsNumTicksBehindClip, &pendingNoteOnList);
			if (noteRowTicksTilNextEvent < ticksTilNextNoteRowEvent)
				ticksTilNextNoteRowEvent = noteRowTicksTilNextEvent;
		}

		noteRowsNumTicksBehindClip = 0;

		// Count up how many of each probability there are
		uint8_t probabilityCount[NUM_PROBABILITY_VALUES];
		memset(probabilityCount, 0, sizeof(probabilityCount));

		// Check whether special case where all probability adds up to 100%
		int probabilitySum = 0;

		bool doingSumTo100 = false;
		int winningI;

		for (int i = 0; i < pendingNoteOnList.count; i++) {

			// If we found a 100%, we know we're not doing sum-to-100
			if (pendingNoteOnList.pendingNoteOns[i].probability >= NUM_PROBABILITY_VALUES) {
				goto skipDoingSumTo100;
			}

			// If any follow-previous-probability, skip this statistics-grabbing
			if (pendingNoteOnList.pendingNoteOns[i].probability & 128) continue;

			// Add to probability total sum - only if we hadn't already found a pending note-on with this probability value
			//if (probabilityCount[pendingNoteOnList.pendingNoteOns[i].probability - 1] == 0)
			probabilitySum += pendingNoteOnList.pendingNoteOns[i].probability;

			probabilityCount[pendingNoteOnList.pendingNoteOns[i].probability - 1]++;
		}

		doingSumTo100 = (probabilitySum == NUM_PROBABILITY_VALUES);

		if (doingSumTo100) {
			int probabilityValueForSummers = ((unsigned int)getRandom255() * NUM_PROBABILITY_VALUES) >> 8;

			int probabilitySumSecondPass = 0;

			bool foundWinner = false;

			for (int i = 0; i < pendingNoteOnList.count; i++) {

				// If any follow-previous-probability, skip this statistics-grabbing
				if (pendingNoteOnList.pendingNoteOns[i].probability & 128) continue;

				int probability = pendingNoteOnList.pendingNoteOns[i].probability;

				probabilitySumSecondPass += probability;

				lastProbabiltyPos[probability] = lastProcessedPos;

				if (!foundWinner && probabilitySumSecondPass > probabilityValueForSummers) {
					winningI = i;
					lastProbabilities[probability] = true;

					foundWinner = true;
				}

				else {
					// Mark down this "loser"
					lastProbabilities[probability] = false;
				}
			}
		}

skipDoingSumTo100:

		// Go through each pending note-on
		for (int i = 0; i < pendingNoteOnList.count; i++) {

			bool conditionPassed;

			// If it's a 100%, which usually will be the case...
			if (pendingNoteOnList.pendingNoteOns[i].probability == NUM_PROBABILITY_VALUES) {
				conditionPassed = true;
			}

			// Otherwise...
			else {
				int probability = pendingNoteOnList.pendingNoteOns[i].probability & 127;

				// If it's an iteration dependence...
				if (probability > NUM_PROBABILITY_VALUES) {

					int divisor, iterationWithinDivisor;
					dissectIterationDependence(probability, &divisor, &iterationWithinDivisor);

					ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(
					    pendingNoteOnList.pendingNoteOns[i].noteRowId, pendingNoteOnList.pendingNoteOns[i].noteRow);

					conditionPassed = (iterationWithinDivisor
					                   == ((uint32_t)modelStackWithNoteRow->getRepeatCount() % (uint32_t)divisor));
				}

				// Or if it's an actual probability kind...
				else {

					// If based on a previous probability...
					if (pendingNoteOnList.pendingNoteOns[i].probability & 128) {

						// Check that that previous probability value is still valid. It normally should be, unless the user has changed the probability of that "previous" note
						if (lastProbabiltyPos[probability] == -1 || lastProbabiltyPos[probability] == lastProcessedPos)
							goto doNewProbability;

						conditionPassed = lastProbabilities[probability];
					}

					// Or if not based on a previous probability...
					else {

						// If we're summing to 100...
						if (doingSumTo100) {
							conditionPassed = (i == winningI);
						}

						// Or if not summing to 100...
						else {
doNewProbability:
							// If the outcome of this probability has already been decided (by another note with same probability)
							if (probabilityCount[probability - 1] >= 254) {
								conditionPassed = probabilityCount[probability - 1] == 255;
							}

							// Otherwise, decide it now
							else {
								int probabilityValue = ((unsigned int)getRandom255() * NUM_PROBABILITY_VALUES) >> 8;
								conditionPassed = (probabilityValue < probability);

								lastProbabilities[NUM_PROBABILITY_VALUES - probability] = !conditionPassed;
								lastProbabiltyPos[NUM_PROBABILITY_VALUES - probability] = lastProcessedPos;

								lastProbabilities[probability] = conditionPassed;
								lastProbabiltyPos[probability] = lastProcessedPos;

								// Store the outcome, for any neighbouring notes
								probabilityCount[probability - 1] = conditionPassed ? 255 : 254;
							}
						}
					}
				}
			}

			if (conditionPassed) {
				sendPendingNoteOn(modelStack, &pendingNoteOnList.pendingNoteOns[i]);
			}
			else {
				pendingNoteOnList.pendingNoteOns[i].noteRow->soundingStatus = STATUS_OFF;
			}
		}
	}

	if (ticksTilNextNoteRowEvent < playbackHandler.swungTicksTilNextEvent)
		playbackHandler.swungTicksTilNextEvent = ticksTilNextNoteRowEvent;
}

void InstrumentClip::sendPendingNoteOn(ModelStackWithTimelineCounter* modelStack, PendingNoteOn* pendingNoteOn) {

	ModelStackWithNoteRow* modelStackWithNoteRow =
	    modelStack->addNoteRow(pendingNoteOn->noteRowId, pendingNoteOn->noteRow);

	int16_t mpeValues[NUM_EXPRESSION_DIMENSIONS];
	pendingNoteOn->noteRow->getMPEValues(modelStackWithNoteRow, mpeValues);

	if (output->type == INSTRUMENT_TYPE_KIT) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings = modelStackWithNoteRow->addOtherTwoThings(
		    pendingNoteOn->noteRow->drum->toModControllable(), &pendingNoteOn->noteRow->paramManager);

		pendingNoteOn->noteRow->drum->noteOn(modelStackWithThreeMainThings, pendingNoteOn->velocity, (Kit*)output,
		                                     mpeValues, MIDI_CHANNEL_NONE, pendingNoteOn->sampleSyncLength,
		                                     pendingNoteOn->ticksLate);
	}
	else {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStackWithNoteRow->addOtherTwoThings(output->toModControllable(), &paramManager);
		((MelodicInstrument*)output)
		    ->sendNote(modelStackWithThreeMainThings, true, pendingNoteOn->noteRow->getNoteCode(), mpeValues,
		               MIDI_CHANNEL_NONE, pendingNoteOn->velocity, pendingNoteOn->sampleSyncLength,
		               pendingNoteOn->ticksLate);
	}
}

void InstrumentClip::toggleNoteRowMute(ModelStackWithNoteRow* modelStack) {

	// Record action
	Action* action = actionLogger.getNewAction(ACTION_MISC);
	if (action) {
		void* consMemory = generalMemoryAllocator.alloc(sizeof(ConsequenceNoteRowMute));

		if (consMemory) {
			ConsequenceNoteRowMute* newConsequence =
			    new (consMemory) ConsequenceNoteRowMute(this, modelStack->noteRowId);
			action->addConsequence(newConsequence);
		}
	}

	modelStack->getNoteRow()->toggleMute(modelStack,
	                                     playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(this));
}

// May set noteRow to NULL, of course.
ModelStackWithNoteRow* InstrumentClip::getNoteRowOnScreen(int yDisplay, ModelStackWithTimelineCounter* modelStack) {
	int noteRowIndex;
	NoteRow* noteRow = getNoteRowOnScreen(yDisplay, modelStack->song, &noteRowIndex);
	int noteRowId;
	if (noteRow) noteRowId = getNoteRowId(noteRow, noteRowIndex);
	return modelStack->addNoteRow(noteRowId, noteRow);
}

NoteRow* InstrumentClip::getNoteRowOnScreen(int yDisplay, Song* song, int* getIndex) {
	// Kit
	if (output->type == INSTRUMENT_TYPE_KIT) {
		int i = yDisplay + yScroll;
		if (i < 0 || i >= noteRows.getNumElements()) return NULL;
		if (getIndex) *getIndex = i;
		return noteRows.getElement(i);
	}

	// Non-kit
	else {
		int yNote = getYNoteFromYDisplay(yDisplay, song);
		return getNoteRowForYNote(yNote, getIndex);
	}
}

// Will set noteRow to NULL if one couldn't be found.
ModelStackWithNoteRow* InstrumentClip::getNoteRowForYNote(int yNote, ModelStackWithTimelineCounter* modelStack) {
	int noteRowIndex;
	NoteRow* noteRow = getNoteRowForYNote(yNote, &noteRowIndex);
	int noteRowId;
	if (noteRow) noteRowId = getNoteRowId(noteRow, noteRowIndex);
	return modelStack->addNoteRow(noteRowId, noteRow);
}

NoteRow* InstrumentClip::getNoteRowForYNote(int yNote, int* getIndex) {

	int i = noteRows.search(yNote, GREATER_OR_EQUAL);
	if (i < noteRows.getNumElements()) {
		NoteRow* noteRow = noteRows.getElement(i);
		if (noteRow->y == yNote) {
			if (getIndex) *getIndex = i;
			return noteRow;
		}
	}

	return NULL;
}

// May set noteRow to NULL, of course.
// Will correctly do that if we're not a Kit Clip.
ModelStackWithNoteRow* InstrumentClip::getNoteRowForSelectedDrum(ModelStackWithTimelineCounter* modelStack) {
	int noteRowId;
	NoteRow* noteRow = NULL;
	if (output->type == INSTRUMENT_TYPE_KIT) {
		Kit* kit = (Kit*)output;
		if (kit->selectedDrum) {
			noteRow = getNoteRowForDrum(kit->selectedDrum, &noteRowId);
		}
	}
	return modelStack->addNoteRow(noteRowId, noteRow);
}

ModelStackWithNoteRow* InstrumentClip::getNoteRowForDrum(ModelStackWithTimelineCounter* modelStack, Drum* drum) {
	int noteRowId;
	NoteRow* noteRow = getNoteRowForDrum(drum, &noteRowId);
	return modelStack->addNoteRow(noteRowId, noteRow);
}

NoteRow* InstrumentClip::getNoteRowForDrum(Drum* drum, int* getIndex) {

	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->drum == drum) {
			if (getIndex) *getIndex = i;
			return thisNoteRow;
		}
	}

	return NULL;
}

// Should only be called for Kit Clips
ModelStackWithNoteRow* InstrumentClip::getNoteRowForDrumName(ModelStackWithTimelineCounter* modelStack,
                                                             char const* name) {

	int i;
	NoteRow* thisNoteRow;

	for (i = 0; i < noteRows.getNumElements(); i++) {
		thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->drum && thisNoteRow->paramManager.containsAnyMainParamCollections()
		    && thisNoteRow->drum->type == DRUM_TYPE_SOUND) {
			SoundDrum* thisDrum = (SoundDrum*)thisNoteRow->drum;

			if (thisDrum->name.equalsCaseIrrespective(name)) goto foundIt;
		}
	}

	thisNoteRow = NULL;

foundIt:
	return modelStack->addNoteRow(i, thisNoteRow);
}

// Beware - this may change yScroll (via currentSong->setRootNote())
// *scaleAltered will not be set to false first - set it yourself. So that this can be called multiple times
ModelStackWithNoteRow* InstrumentClip::getOrCreateNoteRowForYNote(int yNote, ModelStackWithTimelineCounter* modelStack,
                                                                  Action* action, bool* scaleAltered) {
	ModelStackWithNoteRow* modelStackWithNoteRow = getNoteRowForYNote(yNote, modelStack);

	// If one didn't already exist, create one
	if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
		int noteRowIndex;
		NoteRow* thisNoteRow = noteRows.insertNoteRowAtY(yNote, &noteRowIndex);

		// If that created successfully (i.e. enough RAM)...
		if (thisNoteRow) {

			// Check that this yNote is allowed within our scale, if we have a scale. And if not allowed, then...
			if (!modelStack->song->isYNoteAllowed(yNote, inScaleMode)) {

				if (scaleAltered) *scaleAltered = true;

				// Recalculate the scale
				int newI = thisNoteRow->notes.insertAtKey(
				    0); // Total hack - make it look like the NoteRow has a Note, so it doesn't get discarded during setRootNote(). We set it back (and then will soon give it a real note) really soon
				modelStack->song->setRootNote(modelStack->song->rootNote);

				thisNoteRow = getNoteRowForYNote(yNote); // Must re-get it
				if (ALPHA_OR_BETA_VERSION && !thisNoteRow) numericDriver.freezeWithError("E -1");

				thisNoteRow->notes.empty(); // Undo our "total hack", above

				if (action) {
					void* consMemory = generalMemoryAllocator.alloc(sizeof(ConsequenceScaleAddNote));

					if (consMemory) {
						ConsequenceScaleAddNote* newConsequence =
						    new (consMemory) ConsequenceScaleAddNote((yNote + 120) % 12);
						action->addConsequence(newConsequence);
					}

					action->numModeNotes[AFTER] = modelStack->song->numModeNotes;
					memcpy(action->modeNotes[AFTER], modelStack->song->modeNotes, sizeof(modelStack->song->modeNotes));
				}
			}

			modelStackWithNoteRow->setNoteRow(thisNoteRow, yNote);
		}
	}
	return modelStackWithNoteRow;
}

// I think you need to check (playbackHandler.isEitherClockActive() && song->isClipActive(thisClip)) before calling this.
void InstrumentClip::resumePlayback(ModelStackWithTimelineCounter* modelStack, bool mayMakeSound) {
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (!thisNoteRow->muted) {
			int noteRowId = getNoteRowId(thisNoteRow, i);
			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);
			thisNoteRow->resumePlayback(modelStackWithNoteRow, mayMakeSound);
		}
	}
	expectEvent();
}

void InstrumentClip::expectNoFurtherTicks(Song* song, bool actuallySoundChange) {

	// If it's actually another Clip, that we're recording into the arranger...
	if (output->activeClip && output->activeClip->beingRecordedFromClip == this) {
		output->activeClip->expectNoFurtherTicks(song, actuallySoundChange);
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, song, this); // TODO: make caller supply this

	stopAllNotesPlaying(modelStack, actuallySoundChange && !currentlyRecordingLinearly); // Stop all sound

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);

	if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
		paramManager.expectNoFurtherTicks(modelStackWithThreeMainThings);
	}

	if (output->type == INSTRUMENT_TYPE_KIT) {
		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			if (thisNoteRow->drum && thisNoteRow->paramManager.containsAnyParamCollectionsIncludingExpression()) {
				ModelStackWithThreeMainThings* modelStackWithThreeMainThingsForNoteRow =
				    modelStack->addNoteRow(i, thisNoteRow)
				        ->addOtherTwoThings(thisNoteRow->drum->toModControllable(), &thisNoteRow->paramManager);
				thisNoteRow->paramManager.expectNoFurtherTicks(modelStackWithThreeMainThingsForNoteRow);
			}
		}
	}

#if PLAYBACK_STOP_SHOULD_CLEAR_MONO_EXPRESSION
	else if (output->type == INSTRUMENT_TYPE_SYNTH || output->type == INSTRUMENT_TYPE_CV) {
		ParamCollectionSummary* expressionParamsSummary = paramManager.getExpressionParamSetSummary();
		if (expressionParamsSummary->paramCollection) {
			ModelStackWithParamCollection* modelStackWithParamCollection =
			    modelStackWithThreeMainThings->addParamCollectionSummary(expressionParamsSummary);

			((ExpressionParamSet*)modelStackWithParamCollection->paramCollection)
			    ->clearValues(modelStackWithParamCollection);
		}
	}
#endif

	currentlyRecordingLinearly = false;
}

// Stops currently-playing notes by actually sending a note-off right now.
// Check that we're allowed to make sound before you call this (nowhere does, is that bad?)
void InstrumentClip::stopAllNotesPlaying(ModelStackWithTimelineCounter* modelStack, bool actuallySoundChange) {
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    modelStack->addNoteRow(getNoteRowId(thisNoteRow, i), thisNoteRow);
		thisNoteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow, actuallySoundChange);
	}
}

// Returns false in rare case that there wasn't enough ram to do this
NoteRow* InstrumentClip::createNewNoteRowForYVisual(int yVisual, Song* song) {
	int y = getYNoteFromYVisual(yVisual, song);
	return noteRows.insertNoteRowAtY(y);
}

// Returns false in rare case that there wasn't enough ram to do this
NoteRow* InstrumentClip::createNewNoteRowForKit(ModelStackWithTimelineCounter* modelStack, bool atStart,
                                                int* getIndex) {

	int index = atStart ? 0 : noteRows.getNumElements();

	Drum* newDrum = ((Kit*)output)->getFirstUnassignedDrum(this);

	NoteRow* newNoteRow = noteRows.insertNoteRowAtIndex(index);
	if (!newNoteRow) return NULL;

	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(index, newNoteRow);

	newNoteRow->setDrum(newDrum, (Kit*)output, modelStackWithNoteRow); // It might end up NULL. That's fine

	if (atStart) {
		yScroll++;

		// Adjust colour offset, because colour offset is relative to the lowest NoteRow, and we just made a new lowest one
		colourOffset--;
	}

	if (getIndex) *getIndex = index;
	return newNoteRow;
}

void InstrumentClip::getMainColourFromY(int yNote, int8_t noteRowColourOffset, uint8_t rgb[]) {
	hueToRGB((yNote + colourOffset + noteRowColourOffset) * -8 / 3, rgb);
}

void InstrumentClip::musicalModeChanged(uint8_t yVisualWithinOctave, int change,
                                        ModelStackWithTimelineCounter* modelStack) {
	if (!isScaleModeClip()) return;
	// Find all NoteRows which belong to this yVisualWithinOctave, and change their note
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (modelStack->song->yNoteIsYVisualWithinOctave(thisNoteRow->y, yVisualWithinOctave)) {
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    modelStack->addNoteRow(getNoteRowId(thisNoteRow, i), thisNoteRow);

			thisNoteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow); // Otherwise we'd leave a MIDI note playing
			thisNoteRow->y += change;
		}
	}
}

void InstrumentClip::noteRemovedFromMode(int yNoteWithinOctave, Song* song) {
	if (!isScaleModeClip()) return;

	for (int i = 0; i < noteRows.getNumElements();) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		if ((thisNoteRow->y + 120) % 12 == yNoteWithinOctave) {
			noteRows.deleteNoteRowAtIndex(i);
		}
		else i++;
	}
}

void InstrumentClip::seeWhatNotesWithinOctaveArePresent(bool notesWithinOctavePresent[], int newRootNote, Song* song,
                                                        bool deleteEmptyNoteRows) {
	song->rootNote =
	    newRootNote; // Not ideal to be setting the global root note here... but as it happens, there's no scenario (currently) where this would cause problems

	for (int i = 0; i < noteRows.getNumElements();) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		if (!thisNoteRow->hasNoNotes()) {
			notesWithinOctavePresent[song->getYNoteWithinOctaveFromYNote(thisNoteRow->getNoteCode())] = true;
			i++;
		}

		// If this NoteRow has no notes, delete it, otherwise we'll have problems as the musical mode is changed
		else {
			noteRows.deleteNoteRowAtIndex(i);
		}
	}
}

void InstrumentClip::transpose(int change, ModelStackWithTimelineCounter* modelStack) {
	// Make sure no notes sounding
	stopAllNotesPlaying(modelStack);

	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		thisNoteRow->y += change;
	}
	yScroll += change;
	colourOffset -= change;
}

// Lock rendering before calling this!
bool InstrumentClip::renderAsSingleRow(ModelStackWithTimelineCounter* modelStack, TimelineView* editorScreen,
                                       int32_t xScroll, uint32_t xZoom, uint8_t* image, uint8_t occupancyMask[],
                                       bool addUndefinedArea, int noteRowIndexStart, int noteRowIndexEnd, int xStart,
                                       int xEnd, bool allowBlur, bool drawRepeats) {

	AudioEngine::logAction("InstrumentClip::renderAsSingleRow");

	// Special case if we're a simple keyboard-mode Clip
	if (onKeyboardScreen && !containsAnyNotes()) {
		int increment = (displayWidth + (displayHeight * KEYBOARD_ROW_INTERVAL)) / displayWidth;
		for (int x = xStart; x < xEnd; x++) {
			getMainColourFromY(yScrollKeyboardScreen + x * increment, 0, &image[x * 3]);
		}
		return true;
	}

	Clip::renderAsSingleRow(modelStack, editorScreen, xScroll, xZoom, image, occupancyMask, addUndefinedArea,
	                        noteRowIndexStart, noteRowIndexEnd, xStart, xEnd, allowBlur, drawRepeats);

	noteRowIndexStart = getMax(noteRowIndexStart, 0);
	noteRowIndexEnd = getMin(noteRowIndexEnd, noteRows.getNumElements());

	bool rowAllowsNoteTails;

	// Render every NoteRow into this, taking into account our search boundary
	for (int i = noteRowIndexStart; i < noteRowIndexEnd; i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		if (!(i & 15)) {
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
			AudioEngine::logAction("renderAsSingleRow still");
		}

		int yNote;

		if (output->type == INSTRUMENT_TYPE_KIT) yNote = i;
		else yNote = thisNoteRow->y;

		uint8_t mainColour[3];
		uint8_t tailColour[3];
		uint8_t blurColour[3];

		getMainColourFromY(yNote, thisNoteRow->getColourOffset(this), mainColour);
		getTailColour(tailColour, mainColour);
		if (allowBlur) getBlurColour(blurColour, mainColour);
		else memcpy(blurColour, mainColour, 3);

		if (i == noteRowIndexStart || output->type == INSTRUMENT_TYPE_KIT) {
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    modelStack->addNoteRow(getNoteRowId(thisNoteRow, i), thisNoteRow);
			rowAllowsNoteTails = allowNoteTails(modelStackWithNoteRow);
		}

		thisNoteRow->renderRow(editorScreen, mainColour, tailColour, blurColour, image, occupancyMask, false,
		                       loopLength, rowAllowsNoteTails, displayWidth, xScroll, xZoom, xStart, xEnd, drawRepeats);
	}
	if (addUndefinedArea)
		drawUndefinedArea(xScroll, xZoom, loopLength, image, occupancyMask, displayWidth, editorScreen,
		                  currentSong->tripletsOn);

	return true;
}

int InstrumentClip::getYVisualFromYNote(int yNote, Song* song) { // TODO: this necessary?
	return song->getYVisualFromYNote(yNote, inScaleMode);
}

int InstrumentClip::getYNoteFromYVisual(int yVisual, Song* song) {
	if (output->type == INSTRUMENT_TYPE_KIT) return yVisual;

	else return song->getYNoteFromYVisual(yVisual, inScaleMode);
}

int InstrumentClip::guessRootNote(Song* song, int previousRoot) {
	bool notesPresent[12];
	for (int i = 0; i < 12; i++)
		notesPresent[i] = false;

	seeWhatNotesWithinOctaveArePresent(
	    notesPresent, 0, song,
	    false); // Don't delete anything yet, since we're still going to make use of the noteRowsOnScreen!

	// If no NoteRows, not much we can do
	if (noteRows.getNumElements() == 0) return previousRoot;

	previousRoot = previousRoot % 12;
	if (previousRoot < 0) previousRoot += 12;

	int lowestNote = noteRows.getElement(0)->getNoteCode() % 12;
	if (lowestNote < 0) lowestNote += 12;

	uint8_t lowestIncompatibility = 255;
	uint8_t mostViableRoot = 0;

	// Go through each possible root note
	for (int root = 0; root < 12; root++) {
		uint8_t incompatibility = 255;

		if (notesPresent[root]) { // || root == previousRoot) {
			// Assess viability of this being the root note
			uint8_t majorIncompatibility = 0;
			if (notesPresent[(root + 1) % 12]) majorIncompatibility++;
			if (notesPresent[(root + 3) % 12]) majorIncompatibility += 2;
			if (notesPresent[(root + 6) % 12]) majorIncompatibility++;
			if (notesPresent[(root + 8) % 12]) majorIncompatibility++;
			if (notesPresent[(root + 10) % 12]) majorIncompatibility++;

			uint8_t minorIncompatibility = 0;
			if (notesPresent[(root + 1) % 12]) minorIncompatibility++;
			if (notesPresent[(root + 4) % 12]) minorIncompatibility += 2;
			if (notesPresent[(root + 6) % 12]) minorIncompatibility++;
			if (notesPresent[(root + 9) % 12]) minorIncompatibility++;
			if (notesPresent[(root + 11) % 12]) minorIncompatibility++;

			incompatibility = getMin(majorIncompatibility, minorIncompatibility);
		}

		if (incompatibility < lowestIncompatibility
		    || (incompatibility == lowestIncompatibility
		        && (root == lowestNote || root == previousRoot // Favour the previous root and the lowest note
		            ))) {
			lowestIncompatibility = incompatibility;
			mostViableRoot = root;
		}
	}

	return mostViableRoot;
}

int InstrumentClip::getNumNoteRows() {
	return noteRows.getNumElements();
}

int InstrumentClip::setNonAudioInstrument(Instrument* newInstrument, Song* song, ParamManager* newParamManager) {

	// New addition - need expression params... hopefully fine?
	// Maybe this function should have the ability to do something equivalent to solicitParamManager(), for the purpose of getting bend ranges from other Clips with same Instrument?
	// Though it's an obscure requirement that's probably hardly needed.
	if (newParamManager) {
		paramManager.stealParamCollectionsFrom(newParamManager, true);
	}

	if (newInstrument->type == INSTRUMENT_TYPE_MIDI_OUT) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithModControllable* modelStack =
		    setupModelStackWithModControllable(modelStackMemory, song, this, newInstrument->toModControllable());
		restoreBackedUpParamManagerMIDI(modelStack);
		if (!paramManager.containsAnyMainParamCollections()) {
			int error = paramManager.setupMIDI();
			if (error) {
				if (ALPHA_OR_BETA_VERSION) numericDriver.freezeWithError("E052");
				return error;
			}
		}
	}
	output = newInstrument;
	affectEntire = true; // Moved here from changeInstrument, March 2021

	return NO_ERROR;
}

// Does not set up patching!
int InstrumentClip::setInstrument(Instrument* newInstrument, Song* song, ParamManager* newParamManager,
                                  InstrumentClip* favourClipForCloningParamManager) {

	// If MIDI or CV...
	if (newInstrument->type == INSTRUMENT_TYPE_MIDI_OUT || newInstrument->type == INSTRUMENT_TYPE_CV) {
		return setNonAudioInstrument(newInstrument, song, newParamManager);
	}

	// Or if Synth or Kit...
	else {
		return setAudioInstrument(
		    newInstrument, song, false, newParamManager,
		    favourClipForCloningParamManager); // Tell it not to setup patching - this will happen back here in changeInstrumentPreset() after all Drums matched up
	}
}

void InstrumentClip::prepareToEnterKitMode(Song* song) {
	// Make sure all rows on screen have a NoteRow. Any RAM problems and we'll just quit
	for (int yDisplay = 0; yDisplay < displayHeight; yDisplay++) {
		NoteRow* noteRow = getNoteRowOnScreen(yDisplay, song);
		if (!noteRow) {
			noteRow = createNewNoteRowForYVisual(yDisplay + yScroll, song);
			if (!noteRow) return;
		}
	}

	// Delete empty NoteRows that aren't onscreen
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		int yDisplay = getYVisualFromYNote(thisNoteRow->y, song) - yScroll;

		if ((yDisplay < 0 || yDisplay >= displayHeight) && thisNoteRow->hasNoNotes()) {
			noteRows.deleteNoteRowAtIndex(i);
		}
		else i++;
	}

	// Figure out the new scroll value
	if (noteRows.getNumElements()) {
		yScroll -= getYVisualFromYNote(noteRows.getElement(0)->y, song);
	}
	else yScroll = 0;
}

// Returns error code in theory - but in reality we're screwed if we get to that stage.
// newParamManager is optional - normally it's not supplied, and will be searched for
int InstrumentClip::changeInstrument(ModelStackWithTimelineCounter* modelStack, Instrument* newInstrument,
                                     ParamManagerForTimeline* newParamManager, int instrumentRemovalInstruction,
                                     InstrumentClip* favourClipForCloningParamManager, bool keepNoteRowsWithMIDIInput,
                                     bool giveMidiAssignmentsToNewInstrument) {

	bool shouldBackUpExpressionParamsToo = false;

	// If switching to Kit
	if (newInstrument->type == INSTRUMENT_TYPE_KIT) {

		// ... from non-Kit
		if (output->type != INSTRUMENT_TYPE_KIT) {

			// Makes sure all NoteRows onscreen are populated, and deletes any empty NoteRows not onscreen.
			prepareToEnterKitMode(modelStack->song);

			shouldBackUpExpressionParamsToo =
			    true; // If switching from non-Kit to Kit, expression params won't get used, so store them with the backup in case the old MelodicInstrument gets used again later. Actually is this ideal?
		}
	}

	Instrument* oldInstrument = (Instrument*)output;
	int oldYScroll = yScroll;

	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	AudioEngine::audioRoutineLocked = true;

	/* Further stuff to optimize in here:
     * -- Delete surplus NoteRows in advance (must stop those Drums playing)
     * -- Guess we could even search out the Drums with the names beforehand
     * -- Allocate RAM beforehand
     * -- Save ParamManagers to a quick list to properly back up later. And if we're deleting the Instrument, don't even end up doing that
     */

	if (isActiveOnOutput() && playbackHandler.isEitherClockActive())
		expectNoFurtherTicks(modelStack->song); // Still necessary? Probably.

	detachFromOutput(modelStack, true, (newInstrument->type == INSTRUMENT_TYPE_KIT), false, keepNoteRowsWithMIDIInput,
	                 giveMidiAssignmentsToNewInstrument,
	                 shouldBackUpExpressionParamsToo); // Will unassignAllNoteRowsFromDrums(), and remember Drum names

	int error = setInstrument(
	    newInstrument, modelStack->song, newParamManager,
	    favourClipForCloningParamManager); // Tell it not to setup patching - this will happen back here in changeInstrumentPreset() after all Drums matched up
	if (error) {
		numericDriver.freezeWithError("E039");
		return error; // TODO: we'll need to get the old Instrument back...
	}

	// If a synth...
	if (newInstrument->type == INSTRUMENT_TYPE_SYNTH) {

		SoundInstrument* synth = (SoundInstrument*)newInstrument;

		paramManager.getPatchCableSet()->grabVelocityToLevelFromMIDIInput(
		    &synth->midiInput); // Should happen before we call setupPatching().

		// Set up patching now. If a Kit, we do the drums individually below.
		synth->setupPatching(modelStack);
	}

	// If Clip (now) has a ParamManager (i.e. is not a CV Clip (wait, not anymore?)), set its pos now. Don't do it for NoteRows yet - that happens as Drums are set, below
	if (playbackHandler.isEitherClockActive() && paramManager.mightContainAutomation()
	    && modelStack->song->isClipActive(this)) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);
		paramManager.setPlayPos(getLivePos(), modelStackWithThreeMainThings, currentlyPlayingReversed);
	}

	// If newInstrument has no activeClip, we must set that right now before the audio routine is called - otherwise it won't be able to find its ParamManager.
	// This prevents a crash if we just navigated this Clip into this Instrument and it already existed and had no Clips
	if (!newInstrument->activeClip) {
		newInstrument->setActiveClip(modelStack, false);
	}

	// Can safely call audio routine again now
	AudioEngine::audioRoutineLocked = false;
	AudioEngine::bypassCulling = true;
	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	// If now a Kit, match NoteRows back up to Drums
	if (newInstrument->type == INSTRUMENT_TYPE_KIT) {

		Kit* kit = (Kit*)newInstrument;
		kit->resetDrumTempValues();

		// For each NoteRow, see if one of the new Drums has the right name for it
		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);

			// Cycle through the backed up drum names for this NoteRow
			for (DrumName* oldDrumName = thisNoteRow->firstOldDrumName; oldDrumName; oldDrumName = oldDrumName->next) {

				// See if a Drum (which hasn't been assigned yet) has this name
				SoundDrum* thisDrum = kit->getDrumFromName(oldDrumName->name.get(), true);

				// If so, and if it's not already assigned to another NoteRow...
				if (thisDrum) {

					ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(i, thisNoteRow);

					thisNoteRow->setDrum(thisDrum, kit, modelStackWithNoteRow,
					                     favourClipForCloningParamManager); // Sets up patching
					if (giveMidiAssignmentsToNewInstrument) thisNoteRow->giveMidiCommandsToDrum();

					// And get out
					break;
				}
			}

			// TODO: we surely don't need to call this every time through
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
		}

		int numNoteRowsDeletedFromBottom = (oldInstrument->type == INSTRUMENT_TYPE_KIT) ? oldYScroll - yScroll : 0;

		assignDrumsToNoteRows(
		    modelStack, true,
		    numNoteRowsDeletedFromBottom); // If any unassigned Drums, give them to any NoteRows without a Drum - or create them a new NoteRow. Sets up patching

		// If changing from a kit to a kit, we may have ended up with 0 NoteRows. We do need to keep at least 1
		if (!noteRows.getNumElements()) {
			noteRows.insertNoteRowAtIndex(0);
		}
	}

	// Or if now a MelodicInstrument...
	else {

		// If the MelodicInstrument has an input MIDIDevice with bend range(s), we'll often want to grab those. The same logic can be found in View::noteOnReceivedForMidiLearn().
		LearnedMIDI* midiInput = &((MelodicInstrument*)newInstrument)->midiInput;
		if (midiInput->containsSomething() && midiInput->device) {
			MIDIDevice* device = midiInput->device;

			int zone = midiInput->channelOrZone - MIDI_CHANNEL_MPE_LOWER_ZONE;

			uint8_t newBendRanges[2];

			// MPE input
			if (zone >= 0) {
				newBendRanges[BEND_RANGE_MAIN] = device->mpeZoneBendRanges[zone][BEND_RANGE_MAIN];
				newBendRanges[BEND_RANGE_FINGER_LEVEL] = device->mpeZoneBendRanges[zone][BEND_RANGE_FINGER_LEVEL];

				if (newBendRanges[BEND_RANGE_FINGER_LEVEL]) {
					if (!hasAnyPitchExpressionAutomationOnNoteRows()) {
						ExpressionParamSet* expressionParams = paramManager.getOrCreateExpressionParamSet();
						if (expressionParams) {
							expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL] =
							    newBendRanges[BEND_RANGE_FINGER_LEVEL];
						}
					}
				}

				goto probablyApplyBendRangeMain;
			}

			// Normal single-channel MIDI input
			else {
				newBendRanges[BEND_RANGE_MAIN] = device->inputChannels[midiInput->channelOrZone].bendRange;
probablyApplyBendRangeMain:
				// If we actually have a bend range to apply...
				if (newBendRanges[BEND_RANGE_MAIN]) {
					ExpressionParamSet* expressionParams = paramManager.getOrCreateExpressionParamSet();

					// And only if mono pitch doesn't already contain data/automation...
					if (expressionParams && !expressionParams->params[0].isAutomated()) {
						expressionParams->bendRanges[BEND_RANGE_MAIN] = newBendRanges[BEND_RANGE_MAIN];
					}
				}
			}
		}

		// And if previously a kit (as well as now being a MelodicInstrument)...
		if (oldInstrument->type == INSTRUMENT_TYPE_KIT) {
			prepNoteRowsForExitingKitMode(modelStack->song);

			yScroll += getYVisualFromYNote(noteRows.getElement(0)->y, modelStack->song);
		}
	}

	// Dispose of old Instrument down here, now that we can breathe (we've done all the stuff above quickly because we couldn't call the audio routine during it).
	if (instrumentRemovalInstruction == INSTRUMENT_REMOVAL_DELETE_OR_HIBERNATE_IF_UNUSED) {
		modelStack->song->deleteOrHibernateOutputIfNoClips(oldInstrument);
	}
	else if (instrumentRemovalInstruction == INSTRUMENT_REMOVAL_DELETE) {
		modelStack->song->deleteOutputThatIsInMainList(oldInstrument);
	}

	return NO_ERROR;
}

void InstrumentClip::deleteEmptyNoteRowsAtEitherEnd(bool onlyIfNoDrum, ModelStackWithTimelineCounter* modelStack,
                                                    bool mustKeepLastOne, bool keepOnesWithMIDIInput) {

	// Prioritize deleting from end of list first, cos this won't mess up scroll
	int firstToDelete = noteRows.getNumElements();
	for (int i = noteRows.getNumElements() - 1; i >= mustKeepLastOne; i--) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		// If we're keeping this one, stop searching
		if (!possiblyDeleteEmptyNoteRow(thisNoteRow, onlyIfNoDrum, modelStack->song, false, keepOnesWithMIDIInput))
			break;

		firstToDelete = i;
	}

	int numToDelete = noteRows.getNumElements() - firstToDelete;
	if (numToDelete > 0) {
		for (int i = firstToDelete; i < noteRows.getNumElements(); i++) {
			NoteRow* noteRow = noteRows.getElement(i);
			if (noteRow->drum) {
				int noteRowId = getNoteRowId(noteRow, i);
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);
				noteRow->setDrum(NULL, (Kit*)output, modelStackWithNoteRow);
			}
		}
		noteRows.deleteNoteRowAtIndex(firstToDelete, numToDelete);
	}

	// Then try deleting from start
	int firstToKeep = 0;
	for (int i = 0; i < noteRows.getNumElements() - mustKeepLastOne; i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (!possiblyDeleteEmptyNoteRow(thisNoteRow, onlyIfNoDrum, modelStack->song, true, keepOnesWithMIDIInput))
			break;

		firstToKeep = i + 1;
	}

	if (firstToKeep > 0) {
		for (int i = 0; i < firstToKeep; i++) {
			NoteRow* noteRow = noteRows.getElement(i);
			if (noteRow->drum) {
				int noteRowId = getNoteRowId(noteRow, i);
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);
				noteRow->setDrum(NULL, (Kit*)output, modelStackWithNoteRow);
			}
		}
		noteRows.deleteNoteRowAtIndex(0, firstToKeep);

		yScroll -= firstToKeep;
	}
}

void InstrumentClip::actuallyDeleteEmptyNoteRow(ModelStackWithNoteRow* modelStack) {
	NoteRow* noteRow = modelStack->getNoteRow();
	if (noteRow->drum) noteRow->setDrum(NULL, (Kit*)output, modelStack);
	noteRow->~NoteRow();
	generalMemoryAllocator.dealloc(noteRow);
}

// Returns whether to delete it
bool InstrumentClip::possiblyDeleteEmptyNoteRow(NoteRow* noteRow, bool onlyIfNoDrum, Song* song, bool onlyIfNonNumeric,
                                                bool keepIfHasMIDIInput) {
	// If it has notes, our work is done
	if (!noteRow->hasNoNotes()) return false;

	// If MIDI assignment on NoteRow, keep it
	if (noteRow->midiInput.containsSomething() || noteRow->muteMIDICommand.containsSomething()) {
		return false;
	}

	Drum* drum = noteRow->drum;
	// If it has a drum, our work might be done, depending on what the caller wanted
	if (drum) {
		if (onlyIfNoDrum) return false;

		if (onlyIfNonNumeric && drum->type == DRUM_TYPE_SOUND && stringIsNumericChars(((SoundDrum*)drum)->name.get()))
			return false;

		if (keepIfHasMIDIInput) {
			// If MIDI assignment on Drum, keep it
			if (drum->midiInput.containsSomething() || drum->muteMIDICommand.containsSomething()) return false;
		}
	}

	return true;
}

// Before calling this, you must ensure that each Drum's temp value represents whether it has a NoteRow assigned
void InstrumentClip::assignDrumsToNoteRows(ModelStackWithTimelineCounter* modelStack,
                                           bool shouldGiveMIDICommandsToDrums,
                                           int numNoteRowsPreviouslyDeletedFromBottom) {

	Kit* kit = (Kit*)output;

	Drum* nextPotentiallyUnassignedDrum = kit->firstDrum;

	// We first need to know whether any NoteRows already have a Drum
	int firstNoteRowToHaveADrum = -1;
	Drum* lowestDrumOnscreen = NULL;
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->drum) {
			firstNoteRowToHaveADrum = i;
			lowestDrumOnscreen = thisNoteRow->drum;
			break;
		}
	}

	int maxNumNoteRowsToInsertAtBottom;

	// If at least one NoteRow already did have a Drum, then we want to put the first unassigned drums (up til the first assigned one) and their new NoteRows at the bottom of the screen
	if (firstNoteRowToHaveADrum >= 0) {

		// If first NoteRow already had a Drum, we can insert as many new ones below it as we want
		if (firstNoteRowToHaveADrum == 0) maxNumNoteRowsToInsertAtBottom = 2147483647;

		// Otherwise, only allow enough new ones to be inserted that, combined with the drum-less ones at the bottom, it'll take us up to the drum in question
		else {
			maxNumNoteRowsToInsertAtBottom = kit->getDrumIndex(lowestDrumOnscreen) - firstNoteRowToHaveADrum;
		}

insertSomeAtBottom:
		int numNoteRowsInsertedAtBottom = 0;

		while (nextPotentiallyUnassignedDrum && numNoteRowsInsertedAtBottom < maxNumNoteRowsToInsertAtBottom) {

			Drum* thisDrum = nextPotentiallyUnassignedDrum;
			nextPotentiallyUnassignedDrum = nextPotentiallyUnassignedDrum->next;

			// If this Drum is already assigned to a NoteRow...
			if (thisDrum->noteRowAssignedTemp) break;

			// Create the NoteRow
			NoteRow* newNoteRow = noteRows.insertNoteRowAtIndex(numNoteRowsInsertedAtBottom);
			if (!newNoteRow) break;

			int noteRowId = getNoteRowId(newNoteRow, numNoteRowsInsertedAtBottom);
			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, newNoteRow);

			newNoteRow->setDrum(thisDrum, kit, modelStackWithNoteRow);
			numNoteRowsInsertedAtBottom++;
		}
		yScroll += numNoteRowsInsertedAtBottom;
	}

	else {
		if (numNoteRowsPreviouslyDeletedFromBottom > 0) {
			// We don't actually get here very often at all
			maxNumNoteRowsToInsertAtBottom = numNoteRowsPreviouslyDeletedFromBottom;
			goto insertSomeAtBottom;
		}
	}

	bool anyNoteRowsRemainingWithoutDrum = false;

	// For any NoteRow without a Drum assigned, give it an unused Drum if there is one
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (!thisNoteRow->drum) {

			if (!nextPotentiallyUnassignedDrum) {
noUnassignedDrumsLeft:
				anyNoteRowsRemainingWithoutDrum = true;
				continue;
			}

			while (nextPotentiallyUnassignedDrum->noteRowAssignedTemp) {
				nextPotentiallyUnassignedDrum = nextPotentiallyUnassignedDrum->next;
				if (!nextPotentiallyUnassignedDrum) goto noUnassignedDrumsLeft;
			}

			int noteRowId = getNoteRowId(thisNoteRow, i);
			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);

			thisNoteRow->setDrum(nextPotentiallyUnassignedDrum, kit, modelStackWithNoteRow);
			nextPotentiallyUnassignedDrum = nextPotentiallyUnassignedDrum->next;

			if (shouldGiveMIDICommandsToDrums) {
				thisNoteRow->giveMidiCommandsToDrum();
			}
		}
	}

	// If any NoteRows with no Drum remain (which means more NoteRows than Drums), then delete them if they're at the end of the list and are empty (but not if it's the last one left)
	if (anyNoteRowsRemainingWithoutDrum) {
		deleteEmptyNoteRowsAtEitherEnd(true, modelStack);
	}

	// Or, if all NoteRows which exist (possibly none) have a Drum, we'd better check if there are any Drums with no NoteRow, and make them one
	else {

		for (; nextPotentiallyUnassignedDrum; nextPotentiallyUnassignedDrum = nextPotentiallyUnassignedDrum->next) {

			// If this Drum is already assigned to a NoteRow...
			if (nextPotentiallyUnassignedDrum->noteRowAssignedTemp) continue;

			// Create the NoteRow
			int i = noteRows.getNumElements();
			NoteRow* newNoteRow = noteRows.insertNoteRowAtIndex(i);
			if (!newNoteRow) break;

			int noteRowId = getNoteRowId(newNoteRow, i);
			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, newNoteRow);

			newNoteRow->setDrum(nextPotentiallyUnassignedDrum, kit, modelStackWithNoteRow);
		}
	}
}

void InstrumentClip::unassignAllNoteRowsFromDrums(ModelStackWithTimelineCounter* modelStack,
                                                  bool shouldRememberDrumNames, bool shouldRetainLinksToSounds,
                                                  bool shouldGrabMidiCommands, bool shouldBackUpExpressionParamsToo) {
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->drum) {
			if (shouldRememberDrumNames) thisNoteRow->rememberDrumName();
			AudioEngine::logAction("InstrumentClip::unassignAllNoteRowsFromDrums");
			AudioEngine::routineWithClusterLoading(); // -----------------------------------

			// If we're retaining links to Sounds, like if we're undo-ably "deleting" a Clip, just backup (and remove link to) the paramManager
			if (shouldRetainLinksToSounds) {
				if (thisNoteRow->paramManager.containsAnyMainParamCollections()) {
					modelStack->song->backUpParamManager((SoundDrum*)thisNoteRow->drum, this,
					                                     &thisNoteRow->paramManager, shouldBackUpExpressionParamsToo);
				}
			}

			// Or, the more normal thing...
			else {
				if (shouldGrabMidiCommands) thisNoteRow->grabMidiCommandsFromDrum();

				int noteRowId = getNoteRowId(thisNoteRow, i);
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);
				thisNoteRow->setDrum(NULL, (Kit*)output, modelStackWithNoteRow);
			}
		}
	}
}

// Returns error code.
// Should only call for Kit Clips.
int InstrumentClip::undoUnassignmentOfAllNoteRowsFromDrums(ModelStackWithTimelineCounter* modelStack) {
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* noteRow = noteRows.getElement(i);
		if (noteRow->drum && noteRow->drum->type == DRUM_TYPE_SOUND) {

			bool success = modelStack->song->getBackedUpParamManagerPreferablyWithClip((SoundDrum*)noteRow->drum, this,
			                                                                           &noteRow->paramManager);

			if (!success) {
				if (ALPHA_OR_BETA_VERSION) numericDriver.freezeWithError("E229");
				return ERROR_BUG;
			}

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(i, noteRow);
			noteRow->trimParamManager(modelStackWithNoteRow);
		}
	}

	return NO_ERROR;
}

// Do *not* use this function to set it to NULL if you don't want to completely delete the old one
// I should make this "steal".
void InstrumentClip::setBackedUpParamManagerMIDI(ParamManagerForTimeline* newOne) {
	if (backedUpParamManagerMIDI.containsAnyMainParamCollections()) {
		// Delete the old one
		backedUpParamManagerMIDI.destructAndForgetParamCollections();
	}
	backedUpParamManagerMIDI.stealParamCollectionsFrom(newOne);
}

void InstrumentClip::restoreBackedUpParamManagerMIDI(ModelStackWithModControllable* modelStack) {
	if (!backedUpParamManagerMIDI.containsAnyMainParamCollections()) return;

	paramManager.stealParamCollectionsFrom(&backedUpParamManagerMIDI);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = modelStack->addParamManager(&paramManager);

	paramManager.trimToLength(loopLength, modelStackWithThreeMainThings, NULL,
	                          false); // oldLength actually has no consequence anyway
}

// Can assume there always was an old Instrument to begin with.
// Does not dispose of the old Instrument - the caller has to do this.
// You're likely to want to call pickAnActiveClipIfPossible() after this.
void InstrumentClip::detachFromOutput(ModelStackWithTimelineCounter* modelStack, bool shouldRememberDrumNames,
                                      bool shouldDeleteEmptyNoteRowsAtEitherEnd, bool shouldRetainLinksToOutput,
                                      bool keepNoteRowsWithMIDIInput, bool shouldGrabMidiCommands,
                                      bool shouldBackUpExpressionParamsToo) {

	if (isActiveOnOutput()) {
		output->detachActiveClip(modelStack->song);
	}

	if (output->type == INSTRUMENT_TYPE_MIDI_OUT) {
		if (paramManager
		        .containsAnyMainParamCollections()) { // Wouldn't this always be? Or is there some case where we might be calling this just after it's been created, and no paramManager yet?
			setBackedUpParamManagerMIDI(&paramManager);
		}
	}
	else if (output->type != INSTRUMENT_TYPE_CV) {

		if (output->type == INSTRUMENT_TYPE_KIT) {

			if (shouldDeleteEmptyNoteRowsAtEitherEnd) { // Only true when called from changeInstrument()
				deleteEmptyNoteRowsAtEitherEnd(
				    false, modelStack, false,
				    keepNoteRowsWithMIDIInput); // Might call audio routine (?). Will back up ParamManagers for any NoteRows deleted with Drums
				// That does not enforce keeping the last NoteRow. This is ok because we know if we're here that we're remaining a Kit
			}
			unassignAllNoteRowsFromDrums(modelStack, shouldRememberDrumNames, shouldRetainLinksToOutput,
			                             shouldGrabMidiCommands, shouldBackUpExpressionParamsToo);
		}

		modelStack->song->backUpParamManager((ModControllableAudio*)output->toModControllable(), this, &paramManager,
		                                     shouldBackUpExpressionParamsToo);
	}

	if (!shouldRetainLinksToOutput) output = NULL;
}

// Returns error code
int InstrumentClip::undoDetachmentFromOutput(ModelStackWithTimelineCounter* modelStack) {

	// We really just need all our ParamManagers back

	if (output->type == INSTRUMENT_TYPE_MIDI_OUT) {

		ModelStackWithModControllable* modelStackWithModControllable =
		    modelStack->addModControllableButNoNoteRow(output->toModControllable());
		restoreBackedUpParamManagerMIDI(modelStackWithModControllable);

		if (!paramManager.containsAnyMainParamCollections()) {
			if (ALPHA_OR_BETA_VERSION) numericDriver.freezeWithError("E230");
			return ERROR_BUG;
		}
	}
	else if (output->type != INSTRUMENT_TYPE_CV) {

		if (output->type == INSTRUMENT_TYPE_KIT) {
			int error = undoUnassignmentOfAllNoteRowsFromDrums(modelStack);
			if (error) return error;
		}

		return Clip::undoDetachmentFromOutput(modelStack);
	}

	return NO_ERROR;
}

// If newInstrument is a Kit, you must call assignDrumsToNoteRows() after this
int InstrumentClip::setAudioInstrument(Instrument* newInstrument, Song* song, bool shouldSetupPatching,
                                       ParamManager* newParamManager,
                                       InstrumentClip* favourClipForCloningParamManager) {

	output = newInstrument;
	affectEntire = (newInstrument->type != INSTRUMENT_TYPE_KIT); // Moved here from changeInstrument, March 2021

	int error = solicitParamManager(song, newParamManager, favourClipForCloningParamManager);
	if (error) return error;

	// Arp stuff, so long as not a Kit (but remember, Sound/Synth is the only other option in this function)
	if (newInstrument->type == INSTRUMENT_TYPE_SYNTH) {
		arpSettings.cloneFrom(&((SoundInstrument*)newInstrument)->defaultArpSettings);
	}

	if (shouldSetupPatching) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = setupModelStackWithTimelineCounter(modelStackMemory, song, this);
		((Instrument*)output)->setupPatching(modelStack);
	}

	return NO_ERROR;
}

void InstrumentClip::writeDataToFile(Song* song) {

	storageManager.writeAttribute("inKeyMode", inScaleMode);
	storageManager.writeAttribute("yScroll", yScroll);
	storageManager.writeAttribute("yScrollKeyboard", yScrollKeyboardScreen);
	if (onKeyboardScreen) storageManager.writeAttribute("onKeyboardScreen", (char*)"1");
	if (wrapEditing) storageManager.writeAttribute("crossScreenEditLevel", wrapEditLevel);
	if (output->type == INSTRUMENT_TYPE_KIT) storageManager.writeAttribute("affectEntire", affectEntire);

	Instrument* instrument = (Instrument*)output;

	if (output->type == INSTRUMENT_TYPE_MIDI_OUT) {
		storageManager.writeAttribute("midiChannel", ((MIDIInstrument*)instrument)->channel);

		if (((MIDIInstrument*)instrument)->channelSuffix != -1) {
			storageManager.writeAttribute("midiChannelSuffix", ((MIDIInstrument*)instrument)->channelSuffix);
		}

		// MIDI PGM
		if (midiBank != 128) {
			storageManager.writeAttribute("midiBank", midiBank);
		}
		if (midiSub != 128) {
			storageManager.writeAttribute("midiSub", midiSub);
		}
		if (midiPGM != 128) {
			storageManager.writeAttribute("midiPGM", midiPGM);
		}
	}
	else if (output->type == INSTRUMENT_TYPE_CV) {
		storageManager.writeAttribute("cvChannel", ((CVInstrument*)instrument)->channel);
	}
	else {
		storageManager.writeAttribute("instrumentPresetName", output->name.get());

		if (!instrument->dirPath.isEmpty()) {
			storageManager.writeAttribute("instrumentPresetFolder", instrument->dirPath.get());
		}
	}

	Clip::writeDataToFile(song);

	if (output->type == INSTRUMENT_TYPE_MIDI_OUT) {
		paramManager.getMIDIParamCollection()->writeToFile();
	}

	if (output->type != INSTRUMENT_TYPE_KIT) {
		if (arpSettings.mode != ARP_MODE_OFF) {
			storageManager.writeOpeningTagBeginning("arpeggiator");
			storageManager.writeAttribute("mode", (char*)arpModeToString(arpSettings.mode));
			storageManager.writeAttribute("numOctaves", arpSettings.numOctaves);
			storageManager.writeAttribute("syncLevel", arpSettings.syncLevel);

			if (output->type == INSTRUMENT_TYPE_MIDI_OUT || output->type == INSTRUMENT_TYPE_CV) {
				storageManager.writeAttribute("gate", arpeggiatorGate);
				storageManager.writeAttribute("rate", arpeggiatorRate);
			}
			storageManager.closeTag();
		}
	}

	if (output->type == INSTRUMENT_TYPE_KIT) {
		storageManager.writeOpeningTagBeginning("kitParams");
		GlobalEffectableForClip::writeParamAttributesToFile(&paramManager, true);
		storageManager.writeOpeningTagEnd();
		GlobalEffectableForClip::writeParamTagsToFile(&paramManager, true);
		storageManager.writeClosingTag("kitParams");
	}
	else if (output->type == INSTRUMENT_TYPE_SYNTH) {
		storageManager.writeOpeningTagBeginning("soundParams");
		Sound::writeParamsToFile(&paramManager, true);
		storageManager.writeClosingTag("soundParams");
	}

	if (output->type != INSTRUMENT_TYPE_KIT) {
		ExpressionParamSet* expressionParams = paramManager.getExpressionParamSet();
		if (expressionParams) {
			expressionParams->writeToFile();

			if (output->type != INSTRUMENT_TYPE_MIDI_OUT) {
				storageManager.writeTag("bendRange", expressionParams->bendRanges[BEND_RANGE_MAIN]);
				storageManager.writeTag("bendRangeMPE", expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL]);
			}
		}
	}

	if (noteRows.getNumElements()) {
		storageManager.writeOpeningTag("noteRows");

		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			int drumIndex = 65535;

			// If a Kit, and the drum isn't a GateDrum, see what Drum this NoteRow has
			if (output->type == INSTRUMENT_TYPE_KIT && thisNoteRow->drum) {
				drumIndex = ((Kit*)output)->getDrumIndex(thisNoteRow->drum);
			}

			thisNoteRow->writeToFile(drumIndex, this);
		}

		storageManager.writeClosingTag("noteRows");
	}
}

int InstrumentClip::readFromFile(Song* song) {

	int error;

	if (false) {
ramError:

		error = ERROR_INSUFFICIENT_RAM;

someError:
		// Clear out all NoteRows of phony info stored where their drum pointer would normally go

		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			thisNoteRow->drum = NULL;
		}

		return error;
	}

	instrumentWasLoadedByReferenceFromClip = NULL;

	char const* tagName;

	int16_t instrumentPresetSlot = 0;
	int8_t instrumentPresetSubSlot = -1;
	String instrumentPresetName;
	String instrumentPresetDirPath;
	bool dirPathHasBeenSpecified = false;

	int32_t readAutomationUpToPos = MAX_SEQUENCE_LENGTH;

	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		//Uart::println(tagName); delayMS(30);

		int temp;

		if (!strcmp(tagName, "inKeyMode")) {
			inScaleMode = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "instrumentPresetSlot")) {
			int slotHere = storageManager.readTagOrAttributeValueInt();
			String slotChars;
			slotChars.setInt(slotHere, 3);
			slotChars.concatenate(&instrumentPresetName);
			instrumentPresetName.set(&slotChars);
		}

		else if (!strcmp(tagName, "instrumentPresetSubSlot")) {
			int subSlotHere = storageManager.readTagOrAttributeValueInt();
			if (subSlotHere >= 0 && subSlotHere < 26) {
				char buffer[2];
				buffer[0] = 'A' + subSlotHere;
				buffer[1] = 0;
				instrumentPresetName.concatenate(buffer);
			}
		}

		else if (!strcmp(tagName, "instrumentPresetName")) {
			storageManager.readTagOrAttributeValueString(&instrumentPresetName);
		}

		else if (!strcmp(tagName, "instrumentPresetFolder")) {
			storageManager.readTagOrAttributeValueString(&instrumentPresetDirPath);
			dirPathHasBeenSpecified = true;
		}

		else if (!strcmp(tagName, "midiChannel")) {
			instrumentTypeWhileLoading = INSTRUMENT_TYPE_MIDI_OUT;

			//if (!instrument) instrument = storageManager.createNewNonAudioInstrument(INSTRUMENT_TYPE_MIDI_OUT, 0, -1);
			instrumentPresetSlot = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "midiChannelSuffix")) {
			instrumentPresetSubSlot = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "cvChannel")) {
			instrumentTypeWhileLoading = INSTRUMENT_TYPE_CV;

			//if (!instrument) instrument = storageManager.createNewNonAudioInstrument(INSTRUMENT_TYPE_CV, 0, -1);
			instrumentPresetSlot = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "midiBank")) {
			midiBank = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "midiSub")) {
			midiSub = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "midiPGM")) {
			midiPGM = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "yScroll")) {
			yScroll = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "yScrollKeyboard")) {
			yScrollKeyboardScreen = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "crossScreenEditLevel")) {
			wrapEditLevel = storageManager.readTagOrAttributeValueInt();
			wrapEditing = true;
		}

		else if (!strcmp(tagName, "onKeyboardScreen")) {
			onKeyboardScreen = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "affectEntire")) {
			affectEntire = storageManager.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "soundMidiCommand")) { // Only for pre V2.0 song files
			soundMidiCommand.readChannelFromFile();
		}

		else if (!strcmp(tagName, "modKnobs")) { // Pre V2.0 only - for compatibility

			instrumentTypeWhileLoading = INSTRUMENT_TYPE_MIDI_OUT;

			output = song->getInstrumentFromPresetSlot(INSTRUMENT_TYPE_MIDI_OUT, instrumentPresetSlot,
			                                           instrumentPresetSubSlot, NULL, NULL, false);
			if (!output) {
				output = storageManager.createNewNonAudioInstrument(INSTRUMENT_TYPE_MIDI_OUT, instrumentPresetSlot,
				                                                    instrumentPresetSubSlot);

				if (!output) goto ramError;
				song->addOutput(output);
			}

			int error = paramManager.setupMIDI();
			if (error) return error;

			error = ((MIDIInstrument*)output)->readModKnobAssignmentsFromFile(readAutomationUpToPos, &paramManager);
			if (error) return error;

			if (loopLength) paramManager.getMIDIParamCollection()->makeInterpolatedCCsGoodAgain(loopLength);
		}

		else if (!strcmp(tagName, "arpeggiator")) {
			while (*(tagName = storageManager.readNextTagOrAttributeName())) {

				if (!strcmp(tagName, "rate")) {
					arpeggiatorRate = storageManager.readTagOrAttributeValueInt();
					storageManager.exitTag("rate");
				}
				else if (!strcmp(tagName, "numOctaves")) {
					arpSettings.numOctaves = storageManager.readTagOrAttributeValueInt();
					storageManager.exitTag("numOctaves");
				}
				else if (!strcmp(tagName, "syncLevel")) {
					arpSettings.syncLevel = (SyncLevel)storageManager.readTagOrAttributeValueInt();
					storageManager.exitTag("syncLevel");
				}
				else if (!strcmp(tagName, "mode")) {
					arpSettings.mode = stringToArpMode(storageManager.readTagOrAttributeValue());
					storageManager.exitTag("mode");
				}
				else if (!strcmp(tagName, "gate")) {
					arpeggiatorGate = storageManager.readTagOrAttributeValueInt();
					storageManager.exitTag("gate");
				}
				else {
					storageManager.exitTag(tagName);
				}
			}
		}

		// For song files from before V2.0, where Instruments were stored within the Clip.
		// Loading Instrument from another Clip.
		else if (!strcmp(tagName, "instrument")) {
			if (*(tagName = storageManager.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "referToTrackId")) {
					if (!output) {
						int clipId = storageManager.readTagOrAttributeValueInt();
						clipId = getMax((int)0, clipId);
						if (clipId >= song->sessionClips.getNumElements()) {
							error = ERROR_FILE_CORRUPTED;
							goto someError;
						}
						instrumentWasLoadedByReferenceFromClip =
						    (InstrumentClip*)song->sessionClips.getClipAtIndex(clipId);
						output = instrumentWasLoadedByReferenceFromClip->output;
						if (!output) {
							error = ERROR_FILE_CORRUPTED;
							goto someError;
						}
						instrumentTypeWhileLoading = output->type;
						if (instrumentTypeWhileLoading == INSTRUMENT_TYPE_SYNTH) {
							arpSettings.cloneFrom(&((SoundInstrument*)output)->defaultArpSettings);
						}
					}
					storageManager.exitTag("referToTrackId");
				}
			}
		}

		// For song files from before V2.0, where Instruments were stored within the Clip
		else if (!strcmp(tagName, "sound") || !strcmp(tagName, "synth")) {
			if (!output) {
				{
					void* instrumentMemory = generalMemoryAllocator.alloc(sizeof(SoundInstrument), NULL, false, true);
					if (!instrumentMemory) goto ramError;

					instrumentTypeWhileLoading = INSTRUMENT_TYPE_SYNTH;

					SoundInstrument* soundInstrument = new (instrumentMemory) SoundInstrument();
					error = soundInstrument->dirPath.set("SYNTHS");
					if (error) goto someError; // Default, in case not included in file.
					output = soundInstrument;
				}

loadInstrument:
				error = output->readFromFile(song, this, readAutomationUpToPos);
				if (error) goto someError;

				if (instrumentTypeWhileLoading == INSTRUMENT_TYPE_SYNTH) {
					arpSettings.cloneFrom(&((SoundInstrument*)output)->defaultArpSettings);
				}

				// Add the Instrument to the Song
				song->addOutput(output);
			}
		}

		// For song files from before V2.0, where Instruments were stored within the Clip
		else if (!strcmp(tagName, "kit")) {
			if (!output) {
				void* instrumentMemory = generalMemoryAllocator.alloc(sizeof(Kit), NULL, false, true);
				if (!instrumentMemory) goto ramError;

				instrumentTypeWhileLoading = INSTRUMENT_TYPE_KIT;
				Kit* kit = new (instrumentMemory) Kit();
				error = kit->dirPath.set("KITS");
				if (error) goto someError; // Default, in case not included in file.
				output = kit;
				goto loadInstrument;
			}
		}

		else if (!strcmp(tagName, "soundParams")) {
			instrumentTypeWhileLoading = INSTRUMENT_TYPE_SYNTH;

			// Normal case - load in brand new ParamManager
			if (storageManager.firmwareVersionOfFileBeingRead >= FIRMWARE_1P2P0 || !output) {
createNewParamManager:
				error = paramManager.setupWithPatching();
				if (error) goto someError;
				Sound::initParams(&paramManager);
			}

			// Slight hack to fix crash with late-2016-ish songs
			else {

				ParamManager* otherParamManager = song->getBackedUpParamManagerPreferablyWithClip(
				    (ModControllableAudio*)output->toModControllable(), this);
				if (!otherParamManager) goto createNewParamManager;
				error = paramManager.cloneParamCollectionsFrom(otherParamManager, false);
				if (error) goto someError;
			}
			Sound::readParamsFromFile(&paramManager, readAutomationUpToPos);
		}

		else if (!strcmp(tagName, "kitParams")) {
			instrumentTypeWhileLoading = INSTRUMENT_TYPE_KIT;
			error = paramManager.setupUnpatched();
			if (error) goto someError;

			GlobalEffectableForClip::initParams(&paramManager);
			GlobalEffectableForClip::readParamsFromFile(&paramManager, readAutomationUpToPos);
		}

		else if (!strcmp(tagName, "midiParams")) {
			instrumentTypeWhileLoading = INSTRUMENT_TYPE_MIDI_OUT;
			error = paramManager.setupMIDI();
			if (error) goto someError;

			error = readMIDIParamsFromFile(readAutomationUpToPos);
			if (error) goto someError;
		}

		else if (!strcmp(tagName, "noteRows")) {
			int minY = -32768;
			while (*(tagName = storageManager.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "noteRow")) {
					NoteRow* newNoteRow = noteRows.insertNoteRowAtIndex(noteRows.getNumElements());
					if (!newNoteRow) goto ramError;
					error = newNoteRow->readFromFile(&minY, this, song, readAutomationUpToPos);
					if (error) goto someError;
				}
				storageManager.exitTag();
			}
		}

		// These next 3 - only created by alpha testers for a few weeks. Could eventually remove.
		else if (!strcmp(tagName, "pitchBend")) {
			temp = 0;
doReadExpressionParam:
			paramManager.ensureExpressionParamSetExists();
			ParamCollectionSummary* summary = paramManager.getExpressionParamSetSummary();
			ExpressionParamSet* expressionParams = (ExpressionParamSet*)summary->paramCollection;
			if (expressionParams) {
				expressionParams->readParam(summary, temp, readAutomationUpToPos);
			}
		}

		else if (!strcmp(tagName, "yExpression")) {
			temp = 1;
			goto doReadExpressionParam;
		}

		else if (!strcmp(tagName, "channelPressure")) {
			temp = 2;
			goto doReadExpressionParam;
		} // -----------------------------------------------------------------------------------

		else if (!strcmp(tagName, "expressionData")) {
			paramManager.ensureExpressionParamSetExists();
			ParamCollectionSummary* summary = paramManager.getExpressionParamSetSummary();
			ExpressionParamSet* expressionParams = (ExpressionParamSet*)summary->paramCollection;
			if (expressionParams) {
				expressionParams->readFromFile(summary, readAutomationUpToPos);
			}
		}

		else if (!strcmp(tagName, "bendRange")) {
			temp = BEND_RANGE_MAIN;
doReadBendRange:
			ExpressionParamSet* expressionParams = paramManager.getOrCreateExpressionParamSet();
			if (expressionParams) {
				expressionParams->bendRanges[temp] = storageManager.readTagOrAttributeValueInt();
			}
		}

		else if (!strcmp(tagName, "bendRangeMPE")) {
			temp = BEND_RANGE_FINGER_LEVEL;
			goto doReadBendRange;
		}

		else readTagFromFile(tagName, song, &readAutomationUpToPos);

		storageManager.exitTag();
	}

	// Some stuff for song files before V2.0, where the Instrument would have been loaded at this point

	// For song files from before V2.0, where Instruments were stored within the Clip (which was called a Track back then)
	if (output) {
		if (!instrumentWasLoadedByReferenceFromClip) {
			switch (output->type) {
			case INSTRUMENT_TYPE_MIDI_OUT:
				((MIDIInstrument*)output)->channelSuffix = getMin(25, getMax(-1, instrumentPresetSubSlot));
				// No break

			case INSTRUMENT_TYPE_CV:
				((NonAudioInstrument*)output)->channel = getMin(numInstrumentSlots, getMax(0, instrumentPresetSlot));
				break;

			case INSTRUMENT_TYPE_SYNTH:
			case INSTRUMENT_TYPE_KIT:
				((Instrument*)output)->name.set(&instrumentPresetName);
				break;

			default:
				__builtin_unreachable();
			}
		}

		// If we loaded an audio Instrument (with a file from before V2.0)
		if (output->type != INSTRUMENT_TYPE_MIDI_OUT && output->type != INSTRUMENT_TYPE_CV) {

			// If we didn't get a paramManager (means pre-September-2016 song)
			if (!paramManager.containsAnyMainParamCollections()) {

				// Try grabbing the Instrument's "backed up" one
				ModControllable* modControllable = output->toModControllable();
				bool success = song->getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)modControllable,
				                                                               this, &paramManager);
				if (success) {
					char modelStackMemory[MODEL_STACK_MAX_SIZE];
					ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
					    setupModelStackWithThreeMainThingsButNoNoteRow(modelStackMemory, song, modControllable, this,
					                                                   &paramManager);

					paramManager.trimToLength(loopLength, modelStackWithThreeMainThings, NULL,
					                          false); // oldLength actually has no consequence anyway
				}

				// If there wasn't one, that's because another Clip already took it. Clone it from that Clip.
				else {

					// It can happen that a ParamManager was never created for a Kit (pre V2.0, or perhaps only in 1.0?). Just create one now.
					if (!instrumentWasLoadedByReferenceFromClip && output->type == INSTRUMENT_TYPE_KIT) {

						error = paramManager.setupUnpatched();
						if (error) goto someError;

						GlobalEffectableForClip::initParams(&paramManager);
					}

					else {
						if (!instrumentWasLoadedByReferenceFromClip
						    || !instrumentWasLoadedByReferenceFromClip->paramManager
						            .containsAnyMainParamCollections()) {
							error = ERROR_FILE_CORRUPTED;
							goto someError;
						}
						error = paramManager.cloneParamCollectionsFrom(
						    &instrumentWasLoadedByReferenceFromClip->paramManager,
						    false); // No need to trim - param automation didn't exist back then
						if (error) goto someError;
					}
				}
			}
		}
	}

	// Pre V3.2.0 (and also for some of 3.2's alpha phase), bend range wasn't adjustable, wasn't written in the file, and was always 12.
	if (storageManager.firmwareVersionOfFileBeingRead <= FIRMWARE_3P2P0_ALPHA
	    && !paramManager.getExpressionParamSet()) {
		ExpressionParamSet* expressionParams = paramManager.getOrCreateExpressionParamSet();
		if (expressionParams) {
			expressionParams->bendRanges[BEND_RANGE_MAIN] = 12;
		}
	}

	switch (instrumentTypeWhileLoading) {
	case INSTRUMENT_TYPE_SYNTH:
	case INSTRUMENT_TYPE_KIT:
		backedUpInstrumentName[instrumentTypeWhileLoading].set(&instrumentPresetName);
		if (dirPathHasBeenSpecified) {
			backedUpInstrumentDirPath[instrumentTypeWhileLoading].set(&instrumentPresetDirPath);
		}
		else {
			// Where dir path has not been specified (i.e. before V4.0.0), go with the default. The same has been done to the Instruments which this Clip will get matched against.
			error = backedUpInstrumentDirPath[instrumentTypeWhileLoading].set(
			    getInstrumentFolder(instrumentTypeWhileLoading));
			if (error) return error;
		}
		break;

	case INSTRUMENT_TYPE_MIDI_OUT:
	case INSTRUMENT_TYPE_CV:
		backedUpInstrumentSlot[instrumentTypeWhileLoading] = instrumentPresetSlot;
		backedUpInstrumentSubSlot[instrumentTypeWhileLoading] = instrumentPresetSubSlot;
		break;

	default:
		__builtin_unreachable();
	}

	return NO_ERROR;
}

int InstrumentClip::readMIDIParamsFromFile(int32_t readAutomationUpToPos) {

	char const* tagName;

	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "param")) {
			//int error = storageManager.readMIDIParamFromFile(readAutomationUpToPos, this);
			//if (error) return error;

			char const* tagName;
			int paramId = CC_NUMBER_NONE;
			AutoParam* param = NULL;
			ParamCollectionSummary* summary;
			ExpressionParamSet* expressionParams = NULL;

			while (*(tagName = storageManager.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "cc")) {
					char const* contents = storageManager.readTagOrAttributeValue();
					if (!strcasecmp(contents, "bend")) {
						paramId = 0;
expressionParam:
						// If we're here, we're reading a pre-V3.2 file, and need to read what we're now regarding as "expression".
						if (!paramManager.ensureExpressionParamSetExists()) return ERROR_INSUFFICIENT_RAM;
						summary = paramManager.getExpressionParamSetSummary();
						expressionParams = (ExpressionParamSet*)summary->paramCollection;
						param = &expressionParams->params[paramId];
					}
					else if (!strcasecmp(contents, "aftertouch")) {
						paramId = 2;
						goto expressionParam;
					}
					else if (!strcasecmp(contents, "none")
					         || !strcmp(contents, "120")) { // We used to write 120 for "none", pre V2.0
						paramId = CC_NUMBER_NONE;
					}
					else {
						paramId = stringToInt(contents);
						if (paramId < NUM_REAL_CC_NUMBERS) {
							if (paramId == 74) {
								paramId = 1;
								goto expressionParam;
							}
							MIDIParam* midiParam =
							    paramManager.getMIDIParamCollection()->params.getOrCreateParamFromCC(paramId, 0);
							if (!midiParam) return ERROR_INSUFFICIENT_RAM;
							param = &midiParam->param;
						}
					}
					storageManager.exitTag("cc");
				}
				else if (!strcmp(tagName, "value")) {
					if (param) {

						int error = param->readFromFile(readAutomationUpToPos);
						if (error) return error;

						if (expressionParams) {
							// Most other times you don't have to think about calling this. It's just because we didn't
							// know which ParamCollection we were gonna load into, and MIDIParamCollection doesn't keep
							// track of automation.
							if (param->isAutomated()) expressionParams->paramHasAutomationNow(summary, paramId);

							// If channel pressure, gotta move and scale the values from how they were in the pre-V3.2 firmware
							if (paramId) {
								param->transposeCCValuesToChannelPressureValues();
							}

							// Or if pitch bend, it'll no longer interpolate, so go place some new nodes. Actually even without this step, you can only just tell there's any problem.
							else {
								param->makeInterpolationGoodAgain(
								    loopLength,
								    22); // 22 is picked somewhat arbitrarily - see comment for function itself.
							}
						}
					}
					storageManager.exitTag("value");
				}
				else storageManager.exitTag(tagName);
			}

			storageManager.exitTag("param");
		}
		else storageManager.exitTag(tagName);
	}

	return NO_ERROR;
}

// This function also unassigns individual NoteRows from their "sound" MIDI commands
void InstrumentClip::prepNoteRowsForExitingKitMode(Song* song) {

	// If for some reason no NoteRows, just return. This shouldn't ever happen
	if (noteRows.getNumElements() == 0) return;

	// We want to select one NoteRow, pinned to a yNote

	NoteRow* chosenNoteRow = NULL;
	int chosenNoteRowIndex;

	// If we're in scale mode...
	if (inScaleMode) {

		// See if any NoteRows are a root note
		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			if (thisNoteRow->y != -32768 && song->getYNoteWithinOctaveFromYNote(thisNoteRow->y) == 0) {
				chosenNoteRow = thisNoteRow;
				chosenNoteRowIndex = i;
				break;
			}
		}
	}

	// If none found yet, just grab the first one with a "valid" yNote
	if (!chosenNoteRow) {

		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			if (thisNoteRow->y != -32768) {

				// But, if we're in key-mode, make sure this yNote fits within the scale!
				if (inScaleMode) {
					uint8_t yNoteWithinOctave = song->getYNoteWithinOctaveFromYNote(thisNoteRow->y);

					// Make sure this yNote fits the scale/mode
					if (!song->modeContainsYNoteWithinOctave(yNoteWithinOctave)) goto noteRowFailed;
				}

				chosenNoteRow = thisNoteRow;
				chosenNoteRowIndex = i;
				break;
			}
noteRowFailed : {}
		}
	}

	// Occasionally we get a crazy scroll value. Not sure how. It happened to Jon Hutton
	if (chosenNoteRow) {
		if (chosenNoteRow->y < -256 || chosenNoteRow->y >= 256)
			goto useRootNote; // Can't use isScrollWithinRange, cos that relies on existing note positions, which are messed up
	}

	// If still none, just pick the first one
	else {
		chosenNoteRow = noteRows.getElement(0);
		chosenNoteRowIndex = 0;
useRootNote:
		chosenNoteRow->y = (song->rootNote % 12) + 60; // Just do this even if we're not in key-mode
	}

	// Now, give all the other NoteRows yNotes
	int chosenNoteRowYVisual = song->getYVisualFromYNote(chosenNoteRow->y, inScaleMode);

	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (i != chosenNoteRowIndex) {
			thisNoteRow->y = song->getYNoteFromYVisual(chosenNoteRowYVisual - chosenNoteRowIndex + i, inScaleMode);
		}
	}
}

// Returns whether whole Clip should be deleted
bool InstrumentClip::deleteSoundsWhichWontSound(Song* song) {

	deleteBackedUpParamManagerMIDI();

	if (output->type == INSTRUMENT_TYPE_KIT) {
		Kit* kit = (Kit*)output;

		bool clipIsActive = song->isClipActive(this);

		for (int i = 0; i < noteRows.getNumElements();) {
			NoteRow* noteRow = noteRows.getElement(i);

			// If the NoteRow isn't gonna make any more sound...
			if ((!clipIsActive || noteRow->muted || noteRow->hasNoNotes())
			    // ...and it doesn't have a currently still-rendering Drum Sound
			    && (!noteRow->drum || noteRow->drum->type != DRUM_TYPE_SOUND
			        || ((SoundDrum*)noteRow->drum)->skippingRendering)
			    && (!noteRow->drum || noteRow->drum->type != DRUM_TYPE_SOUND
			        || (SoundDrum*)noteRow->drum != view.activeModControllableModelStack.modControllable)) {

				// OI!! Don't nest any of those conditions inside other if statements. We need the "else" below to take effect. Thanks

				// We'd ultimately love to just delete the Drum. But beware that multiple NoteRows in different Clips may have the same Drum. We used to just delete it, leading to a crash
				// sometimes! Now, if we just do this for the active Clip, it should be ok right, cos no other Clip is going to be doing anything on its NoteRow?
				if (clipIsActive && noteRow->drum) {

					if (ALPHA_OR_BETA_VERSION && noteRow->drum->type == DRUM_TYPE_SOUND
					    && ((SoundDrum*)noteRow->drum)->hasAnyVoices()) {
						numericDriver.freezeWithError("E176");
					}

					Drum* drum = noteRow->drum;
					kit->removeDrum(drum);

					void* toDealloc = dynamic_cast<void*>(drum);
					drum->~Drum();
					generalMemoryAllocator.dealloc(toDealloc);
				}

				noteRows.deleteNoteRowAtIndex(i);

				AudioEngine::routineWithClusterLoading(); // -----------------------------------
			}
			else {
				i++;
			}
		}

		return false;
	}

	// For MelodicInstruments, we can delete the Clip (which we know is active on the Instrument) if the Clip is inactive in the Song and the Instrument isn't still rendering anything
	else {
		return Clip::deleteSoundsWhichWontSound(song);
	}
}

// Will cause serious problems if the NoteRow doesn't exist in here
void InstrumentClip::deleteNoteRow(ModelStackWithTimelineCounter* modelStack, int noteRowIndex) {

	NoteRow* noteRow = noteRows.getElement(noteRowIndex);

	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(getNoteRowId(noteRow, noteRowIndex), noteRow);

	noteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow);

	noteRow->setDrum(NULL, (Kit*)output, modelStackWithNoteRow);
	noteRows.deleteNoteRowAtIndex(noteRowIndex);
}

void InstrumentClip::stopAllNotesForMIDIOrCV(ModelStackWithTimelineCounter* modelStack) {

	// This didn't use to be here but seems a good idea. Call this so that any drone notes will restart next loop around. Also, not all synths necessarily support the all-notes-off message, which the further below sends.
	stopAllNotesPlaying(modelStack);

	// And then we still need this but in case any notes have been sent out via audition, or I guess being echoed thru

	// CV - easy
	if (output->type == INSTRUMENT_TYPE_CV) {
		cvEngine.sendNote(false, ((CVInstrument*)output)->channel);
	}

	// MIDI - hard
	else if (output->type == INSTRUMENT_TYPE_MIDI_OUT) {
		((MIDIInstrument*)output)->allNotesOff();
	}
}

int16_t InstrumentClip::getTopYNote() {
	if (noteRows.getNumElements() == 0) return 64;
	return noteRows.getElement(noteRows.getNumElements() - 1)->y;
}

int16_t InstrumentClip::getBottomYNote() {
	if (noteRows.getNumElements() == 0) return 64;
	return noteRows.getElement(0)->y;
}

uint32_t InstrumentClip::getWrapEditLevel() {
	return wrapEditing
	           ? wrapEditLevel
	           : MAX_SEQUENCE_LENGTH; // Used to return the Clip length in this case, but that causes problems now that NoteRows may be longer.
}

bool InstrumentClip::hasSameInstrument(InstrumentClip* otherClip) {
	return (output == otherClip->output);
}

bool InstrumentClip::isScaleModeClip() {
	return (inScaleMode && output->type != INSTRUMENT_TYPE_KIT);
}

// TODO: this should be a virtual function in Instrument
// modelStack could contain a NULL noteRow if there isn't one - e.g. in a Synth Clip
bool InstrumentClip::allowNoteTails(ModelStackWithNoteRow* modelStack) {
	if (output->type == INSTRUMENT_TYPE_MIDI_OUT || output->type == INSTRUMENT_TYPE_CV) return true;

	if (output->type == INSTRUMENT_TYPE_SYNTH) {
		ModelStackWithSoundFlags* modelStackWithSoundFlags =
		    modelStack->addOtherTwoThings((SoundInstrument*)output, &paramManager)->addSoundFlags();
		return ((SoundInstrument*)output)->allowNoteTails(modelStackWithSoundFlags);
	}

	// Or if kit...
	NoteRow* noteRow = modelStack->getNoteRowAllowNull();
	if (!noteRow || !noteRow->drum) return true;
	ModelStackWithSoundFlags* modelStackWithSoundFlags =
	    modelStack->addOtherTwoThings(noteRow->drum->toModControllable(), &noteRow->paramManager)->addSoundFlags();
	return noteRow->drum->allowNoteTails(
	    modelStackWithSoundFlags); // Needs to survive a NULL noteRow, even if this generally wouldn't happen (it might if auditioning a Drum via MIDI or arranger audition pad which doesn't have one)
}

// What does this do exactly, again?
void InstrumentClip::ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(ModelStackWithTimelineCounter* modelStack,
                                                                            Sound* sound) {
	if (output) {
		if (output->type == INSTRUMENT_TYPE_SYNTH) {
			if ((SoundInstrument*)output == sound) {

				ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
				    modelStack->addOtherTwoThingsButNoNoteRow(sound, &paramManager);

				sound->ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(modelStackWithThreeMainThings);
			}
		}
		else { // KIT

			SoundDrum* soundDrum = (SoundDrum*)sound;
			for (int i = 0; i < noteRows.getNumElements(); i++) {
				NoteRow* thisNoteRow = noteRows.getElement(i);
				if (thisNoteRow->drum == soundDrum) {

					ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
					    modelStack->addNoteRow(i, thisNoteRow)->addOtherTwoThings(sound, &thisNoteRow->paramManager);

					sound->ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(modelStackWithThreeMainThings);
				}
			}
		}
	}
}

// For the purpose of deciding a region length for parameter automation manual editing
int32_t InstrumentClip::getDistanceToNextNote(Note* givenNote, ModelStackWithNoteRow* modelStack) {

	int32_t distance;

	// If non-affect-entire Kit, only think about one NoteRow
	if (output->type == INSTRUMENT_TYPE_KIT && !affectEntire) {
		distance = modelStack->getNoteRow()->getDistanceToNextNote(givenNote->pos, modelStack);
	}

	// Otherwise, take all NoteRows into account
	else {
		distance = 2147483647;

		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			int32_t earliestThisRow = thisNoteRow->getDistanceToNextNote(givenNote->pos, modelStack);
			distance = getMin(earliestThisRow, distance);
		}
	}

	return getMax(distance, givenNote->length);
}

// Make sure noteRow not NULL before you call!
int InstrumentClip::getNoteRowId(NoteRow* noteRow, int noteRowIndex) {
#if ALPHA_OR_BETA_VERSION
	if (!noteRow) numericDriver.freezeWithError("E380");
#endif
	if (output->type == INSTRUMENT_TYPE_KIT) {
		return noteRowIndex;
	}
	else {
		return noteRow->y;
	}
}

NoteRow* InstrumentClip::getNoteRowFromId(int id) {
	if (output->type == INSTRUMENT_TYPE_KIT) {
		if (id < 0 || id >= noteRows.getNumElements()) numericDriver.freezeWithError("E177");
		return noteRows.getElement(id);
	}

	else {
		NoteRow* noteRow = getNoteRowForYNote(id);

		// Might need to create, possibly if scale/mode changed
		if (!noteRow) {
			noteRow = noteRows.insertNoteRowAtY(id);
		}
		return noteRow;
	}
}

void InstrumentClip::shiftHorizontally(ModelStackWithTimelineCounter* modelStack, int amount) {

	if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
		paramManager.shiftHorizontally(
		    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager), amount, loopLength);
	}

	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		int noteRowId = getNoteRowId(thisNoteRow, i);
		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);
		thisNoteRow->shiftHorizontally(amount, modelStackWithNoteRow); // Shifts NoteRow-level param automation too
	}

	if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(this)) {
		expectEvent();
		reGetParameterAutomation(modelStack); // Re-gets all NoteRow-level param automation too
	}
}

void InstrumentClip::shiftOnlyOneNoteRowHorizontally(ModelStackWithNoteRow* modelStack, int shiftAmount) {
	NoteRow* noteRow = modelStack->getNoteRow();

	noteRow->shiftHorizontally(shiftAmount, modelStack);

	if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(this)) {
		expectEvent();

		if (noteRow->paramManager.mightContainAutomation()) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow();
			noteRow->paramManager.setPlayPos(getLivePos(), modelStackWithThreeMainThings,
			                                 modelStackWithThreeMainThings->isCurrentlyPlayingReversed());
		}
	}
}

void InstrumentClip::sendMIDIPGM() {
	MIDIInstrument* midiInstrument = (MIDIInstrument*)output;

	int outputFilter = midiInstrument->channel;
	int masterChannel = midiInstrument->getOutputMasterChannel();

	// Send MIDI PGM if there is one...
	if (midiBank != 128) {
		midiEngine.sendBank(masterChannel, midiBank, outputFilter);
	}
	if (midiSub != 128) {
		midiEngine.sendSubBank(masterChannel, midiSub, outputFilter);
	}
	if (midiPGM != 128) {
		midiEngine.sendPGMChange(masterChannel, midiPGM, outputFilter);
	}
}

void InstrumentClip::clear(Action* action, ModelStackWithTimelineCounter* modelStack) {
	Clip::clear(action, modelStack);

	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    modelStack->addNoteRow(getNoteRowId(thisNoteRow, i), thisNoteRow);
		thisNoteRow->clear(action, modelStackWithNoteRow);
	}
}

bool InstrumentClip::doesProbabilityExist(int32_t apartFromPos, int probability, int secondProbability) {

	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->doesProbabilityExist(apartFromPos, probability, secondProbability)) return true;
	}
	return false;
}

void InstrumentClip::clearArea(ModelStackWithTimelineCounter* modelStack, int32_t startPos, int32_t endPos,
                               Action* action) {
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		int noteRowId = getNoteRowId(thisNoteRow, i);
		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);

		thisNoteRow->clearArea(startPos, endPos - startPos, modelStackWithNoteRow, action,
		                       loopLength); // No cross-screen
	}
}

int InstrumentClip::getScaleType() {

	if (output->type == INSTRUMENT_TYPE_KIT) return SCALE_TYPE_KIT;
	else {
		if (inScaleMode) return SCALE_TYPE_SCALE;
		else return SCALE_TYPE_CHROMATIC;
	}
}

void InstrumentClip::backupPresetSlot() {

	switch (output->type) {
	case INSTRUMENT_TYPE_MIDI_OUT:
		backedUpInstrumentSubSlot[output->type] = ((MIDIInstrument*)output)->channelSuffix;
		// No break

	case INSTRUMENT_TYPE_CV:
		backedUpInstrumentSlot[output->type] = ((NonAudioInstrument*)output)->channel;
		break;

	case INSTRUMENT_TYPE_SYNTH:
	case INSTRUMENT_TYPE_KIT:
		backedUpInstrumentName[output->type].set(&output->name);
		backedUpInstrumentDirPath[output->type].set(&((Instrument*)output)->dirPath);
		break;

	default:
		__builtin_unreachable();
	}
}

void InstrumentClip::compensateVolumeForResonance(ModelStackWithTimelineCounter* modelStack) {
	((Instrument*)output)
	    ->compensateInstrumentVolumeForResonance(
	        modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager));

	if (output->type == INSTRUMENT_TYPE_KIT) {

		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			if (thisNoteRow->drum && thisNoteRow->paramManager.containsAnyMainParamCollections()
			    && thisNoteRow->drum->type == DRUM_TYPE_SOUND) {
				SoundDrum* thisDrum = (SoundDrum*)thisNoteRow->drum;
				ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
				    modelStack->addNoteRow(i, thisNoteRow)->addOtherTwoThings(thisDrum, &thisNoteRow->paramManager);
				thisDrum->compensateVolumeForResonance(modelStackWithThreeMainThings);
			}
		}
	}
}

void InstrumentClip::deleteOldDrumNames() {
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		thisNoteRow->deleteOldDrumNames();
	}
}

void InstrumentClip::ensureScrollWithinKitBounds() {
	if (yScroll < 1 - displayHeight) yScroll = 1 - displayHeight;
	else {
		int maxYScroll = getNumNoteRows() - 1;
		if (yScroll > maxYScroll) yScroll = maxYScroll;
	}
}

// Make sure not a Kit before calling this
bool InstrumentClip::isScrollWithinRange(int scrollAmount, int newYNote) {

	if (output->type == INSTRUMENT_TYPE_SYNTH) {

		if (scrollAmount >= 0) {
			int transposedNewYNote = newYNote + ((SoundInstrument*)output)->getMinOscTranspose();
			if (transposedNewYNote > 127 && newYNote > getTopYNote()) return false;
		}

		if (scrollAmount <= 0) {
			int transposedNewYNote = newYNote + ((SoundInstrument*)output)->getMaxOscTranspose(this);
			if (transposedNewYNote < 0 && newYNote < getBottomYNote()) return false;
		}
	}

	else if (output->type == INSTRUMENT_TYPE_CV) {
		int32_t newVoltage = cvEngine.calculateVoltage(newYNote, ((CVInstrument*)output)->channel);
		if (scrollAmount >= 0) {
			if (newVoltage >= 65536 && newYNote > getTopYNote()) return false;
		}
		if (scrollAmount <= 0) {
			if (newVoltage < 0 && newYNote < getBottomYNote()) return false;
		}
	}

	else { // INSTRUMENT_TYPE_MIDI_OUT
		if (scrollAmount >= 0) {
			if (newYNote > 127 && newYNote > getTopYNote()) return false;
		}
		if (scrollAmount <= 0) {
			if (newYNote < 0 && newYNote < getBottomYNote()) return false;
		}
	}
	return true;
}

bool InstrumentClip::containsAnyNotes() {
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (!thisNoteRow->hasNoNotes()) return true;
	}
	return false;
}

int InstrumentClip::getYNoteFromYDisplay(int yDisplay, Song* song) {
	return getYNoteFromYVisual(yDisplay + yScroll, song);
}

// Called when the user presses one of the instrument-type buttons (synth/kit/MIDI/CV). This function takes care of deciding what Instrument / preset to switch to.
Instrument* InstrumentClip::changeInstrumentType(ModelStackWithTimelineCounter* modelStack, int newInstrumentType) {
	int oldInstrumentType = output->type;

	if (oldInstrumentType == newInstrumentType) return NULL;

	actionLogger.deleteAllLogs(); // Can't undo past this!

	int availabilityRequirement;
	bool canReplaceWholeInstrument = modelStack->song->canOldOutputBeReplaced(this, &availabilityRequirement);

	bool shouldReplaceWholeInstrument;

	modelStack->song->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E061", "H061");

	backupPresetSlot();

	// Retrieve backed up slot numbers
	int16_t newSlot = backedUpInstrumentSlot[newInstrumentType];
	int8_t newSubSlot = backedUpInstrumentSubSlot[newInstrumentType];

	Instrument* newInstrument = NULL;

	bool instrumentAlreadyInSong = false;

	// MIDI / CV
	if (newInstrumentType == INSTRUMENT_TYPE_MIDI_OUT || newInstrumentType == INSTRUMENT_TYPE_CV) {
		newInstrument = modelStack->song->getNonAudioInstrumentToSwitchTo(
		    newInstrumentType, availabilityRequirement, newSlot, newSubSlot, &instrumentAlreadyInSong);
		if (!newInstrument) return NULL;
	}

	// Synth / Kit
	else {
		String newName;
		ReturnOfConfirmPresetOrNextUnlaunchedOne result;

		newName.set(&backedUpInstrumentName[newInstrumentType]);
		Browser::currentDir.set(&backedUpInstrumentDirPath[newInstrumentType]);

		if (Browser::currentDir.isEmpty()) {
			result.error = Browser::currentDir.set(getInstrumentFolder(newInstrumentType));
			if (result.error) {
displayError:
				numericDriver.displayError(result.error);
				return NULL;
			}
		}

		result = Browser::confirmPresetOrNextUnlaunchedOne(newInstrumentType, &newName, availabilityRequirement);
		if (result.error) goto displayError;

		newInstrument = result.fileItem->instrument;
		bool isHibernating = newInstrument && !result.fileItem->instrumentAlreadyInSong;
		instrumentAlreadyInSong = newInstrument && result.fileItem->instrumentAlreadyInSong;

		if (!newInstrument) {
			String newPresetName;
			result.fileItem->getDisplayNameWithoutExtension(&newPresetName);
			result.error = storageManager.loadInstrumentFromFile(modelStack->song, NULL, newInstrumentType, false,
			                                                     &newInstrument, &result.fileItem->filePointer,
			                                                     &newPresetName, &Browser::currentDir);
		}

		Browser::emptyFileItems();

		if (result.error) goto displayError;

		if (isHibernating) modelStack->song->removeInstrumentFromHibernationList(newInstrument);

#if HAVE_OLED
		OLED::displayWorkingAnimation("Loading");
#else
		numericDriver.displayLoadingAnimation();
#endif
		newInstrument->loadAllAudioFiles(true);
	}

	shouldReplaceWholeInstrument = (canReplaceWholeInstrument && !instrumentAlreadyInSong);

	// If replacing whole Instrument
	if (shouldReplaceWholeInstrument) {
		modelStack->song->replaceInstrument((Instrument*)output, newInstrument);
	}

	else {
		int error = changeInstrument(modelStack, newInstrument, NULL, INSTRUMENT_REMOVAL_DELETE_OR_HIBERNATE_IF_UNUSED,
		                             NULL, true);
		// TODO: deal with errors

		if (!instrumentAlreadyInSong) modelStack->song->addOutput(newInstrument);
	}

	// Turning into Kit
	if (newInstrumentType == INSTRUMENT_TYPE_KIT) {
		// Make sure we're not scrolled too far up (this has to happen amongst this code down here - NoteRows are deleted in the functions called above)
		int maxScroll = (int)getNumNoteRows() - displayHeight;
		maxScroll = getMax(0, maxScroll);
		yScroll = getMin(yScroll, maxScroll);
		((Kit*)newInstrument)->selectedDrum = NULL;
	}

	outputChanged(modelStack, newInstrument);
	modelStack->song->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E062", "H062");

#if HAVE_OLED
	OLED::removeWorkingAnimation();
#endif

	return newInstrument;
}

void InstrumentClip::getSuggestedParamManager(Clip* newClip, ParamManagerForTimeline** suggestedParamManager,
                                              Sound* sound) {
	if (&paramManager == *suggestedParamManager) {
		Clip::getSuggestedParamManager(newClip, suggestedParamManager, sound);
	}
	else {
		InstrumentClip* newInstrumentClip = (InstrumentClip*)newClip;
		for (int i = 0; i < newInstrumentClip->noteRows.getNumElements(); i++) {
			NoteRow* noteRow = newInstrumentClip->noteRows.getElement(i);
			if (noteRow->drum && noteRow->drum->type == DRUM_TYPE_SOUND && (SoundDrum*)noteRow->drum == sound) {
				*suggestedParamManager = &noteRow->paramManager;
				break;
			}
		}
	}
}

int InstrumentClip::claimOutput(ModelStackWithTimelineCounter* modelStack) {

	if (!output) { // Would only have an output already if file from before V2.0.0 I think? So, this block normally does apply.
		int instrumentType = instrumentTypeWhileLoading;

		char const* instrumentName = (instrumentType < 2) ? backedUpInstrumentName[instrumentType].get() : NULL;
		char const* dirPath = (instrumentType < 2) ? backedUpInstrumentDirPath[instrumentType].get() : NULL;

		output = modelStack->song->getInstrumentFromPresetSlot(instrumentType, backedUpInstrumentSlot[instrumentType],
		                                                       backedUpInstrumentSubSlot[instrumentType],
		                                                       instrumentName, dirPath, false);

		if (!output) {
			if (instrumentType == INSTRUMENT_TYPE_MIDI_OUT || instrumentType == INSTRUMENT_TYPE_CV) {
				output = storageManager.createNewNonAudioInstrument(
				    instrumentType, backedUpInstrumentSlot[instrumentType], backedUpInstrumentSubSlot[instrumentType]);

				if (!output) {
					return ERROR_INSUFFICIENT_RAM;
				}

				modelStack->song->addOutput(output);
			}
			else {
				return ERROR_FILE_CORRUPTED;
			}
		}
	}

	// If Instrument is a Kit, match each NoteRow to its Drum
	if (output->type == INSTRUMENT_TYPE_KIT) {
		Kit* kit = (Kit*)output;

		int noteRowCount = 0;

		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);

			if (!(noteRowCount & 15)) {
				AudioEngine::routineWithClusterLoading(); // -----------------------------------
				AudioEngine::logAction("nlkr");
			}

			bool paramManagerCloned = false;

			// Maybe we (cryptically) marked it as "no drum".
			if ((uint32_t)thisNoteRow->drum == (uint32_t)0xFFFFFFFF) {
				thisNoteRow->drum = NULL;
			}

			// Or a gate drum from a pre-V2.0 Song file...
			else if ((uint32_t)thisNoteRow->drum > (uint32_t)(0xFFFFFFFE - NUM_GATE_CHANNELS)) {

				int gateChannel = 0xFFFFFFFE - (uint32_t)thisNoteRow->drum;

				thisNoteRow->drum = kit->getGateDrumForChannel(gateChannel);

				if (!thisNoteRow->drum) {
					void* drumMemory = generalMemoryAllocator.alloc(sizeof(GateDrum), NULL, true);
					if (!drumMemory) return ERROR_INSUFFICIENT_RAM;
					GateDrum* newDrum = new (drumMemory) GateDrum();
					newDrum->channel = gateChannel;

					kit->addDrum(newDrum);
					thisNoteRow->drum = newDrum;
				}
				thisNoteRow->giveMidiCommandsToDrum();
			}

			// Otherwise, we know we've sneakily put an integer index in place of the pointer, so convert that back to an actual pointer now
			else {

				// Don't call setDrum(), because that would overwrite the NoteRow's paramManager. It already has the right one, loaded from file
				Drum* drumFromIndex = kit->getDrumFromIndex((uint32_t)thisNoteRow->drum);

				ParamManagerForTimeline* otherParamManager;

				ModelStackWithNoteRow*
				    modelStackWithNoteRow; // Defined up here so we can do the jump to haveNoDrum, below.

				int error; // These declared here, to allow gotos
				bool success;

				// We need to see whether any other NoteRows *that we've assigned drums so far* had this same drum. TODO: this could be waaaay more efficient!
				for (int j = 0; j < i; j++) {
					NoteRow* thatNoteRow = noteRows.getElement(j);
					if (thatNoteRow->drum == drumFromIndex) {
						// Oh no! That drum already has a NoteRow!

						// If any ParamManager, discard it
						thisNoteRow->deleteParamManager();

						goto haveNoDrum;
					}
				}

				// Cool ok, we found our Drum!
				thisNoteRow->drum = drumFromIndex;
				thisNoteRow->giveMidiCommandsToDrum();

				// If we didn't get a paramManager (means pre-September-2016 song). TODO: this whole section would lead to an ugly mess if the right stuff wasn't in the file. Or if not enough RAM
				if (!thisNoteRow->paramManager.containsAnyMainParamCollections()
				    && thisNoteRow->drum->type == DRUM_TYPE_SOUND) {

					modelStackWithNoteRow = modelStack->addNoteRow(i, thisNoteRow);

					// Try grabbing the Drum's "backed up" one
					success = modelStackWithNoteRow->song->getBackedUpParamManagerPreferablyWithClip(
					    (SoundDrum*)thisNoteRow->drum, this, &thisNoteRow->paramManager);
					if (success) {
						thisNoteRow->trimParamManager(modelStackWithNoteRow);
					}

					// If there wasn't one there, it means another Clip's NoteRow already claimed it
					else {
						otherParamManager =
						    modelStackWithNoteRow->song->findParamManagerForDrum(kit, thisNoteRow->drum, this);
						if (!otherParamManager) {
							return ERROR_UNSPECIFIED;
						}
						error = thisNoteRow->paramManager.cloneParamCollectionsFrom(otherParamManager, false);
						paramManagerCloned = true;

						// If wasn't enough RAM, we're really in trouble
						if (error) {
							numericDriver.freezeWithError("E011");
haveNoDrum:
							thisNoteRow->drum = NULL;
						}
					}
				}

				// If we've now got a paramManager and Drum...
				if (thisNoteRow->drum) {

					// If saved before V2.1, see if we need linear interpolation
					if (storageManager.firmwareVersionOfFileBeingRead < FIRMWARE_2P1P0_BETA) {
						if (thisNoteRow->drum->type == DRUM_TYPE_SOUND) {
							SoundDrum* sound = (SoundDrum*)thisNoteRow->drum;

							PatchedParamSet* patchedParams = thisNoteRow->paramManager.getPatchedParamSet();

							for (int s = 0; s < NUM_SOURCES; s++) {
								Source* source = &sound->sources[s];
								if (source->oscType == OSC_TYPE_SAMPLE) {
									if (sound->transpose || source->transpose || source->cents
									    || patchedParams->params[PARAM_LOCAL_PITCH_ADJUST].containsSomething(0)
									    //|| thisNoteRow->paramManager->patchCableSet.doesParamHaveSomethingPatchedToIt(PARAM_LOCAL_PITCH_ADJUST) // No, can't call these cos patching isn't set up yet. Oh well
									    //|| thisNoteRow->paramManager->patchCableSet.doesParamHaveSomethingPatchedToIt(PARAM_LOCAL_OSC_A_PITCH_ADJUST + s)
									    || patchedParams->params[PARAM_LOCAL_OSC_A_PITCH_ADJUST + s].containsSomething(
									        0)) {

										source->sampleControls.interpolationMode = INTERPOLATION_MODE_LINEAR;
									}
								}
							}
						}
					}
				}
			}
			noteRowCount++;
		}

		// Check scroll is within range
		if (yScroll < 1 - displayHeight) yScroll = 1 - displayHeight;
		else if (yScroll > noteRowCount - 1) yScroll = noteRowCount - 1;
	}

	// Otherwise, if not a Kit...
	else {

		// If we had a MIDI input channel for this Clip, as was the format pre V2.0, move this to the Instrument
		if (soundMidiCommand.containsSomething()) {
			((MelodicInstrument*)output)->midiInput = soundMidiCommand;
		}

		// Ensure all NoteRows have a NULL Drum pointer
		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			thisNoteRow->drum = NULL;
		}

		// And...
		if (output->type == INSTRUMENT_TYPE_MIDI_OUT) {
			if (!paramManager.containsAnyMainParamCollections()) {
				int error = paramManager.setupMIDI();
				if (error) return error;
			}
		}
		else if (output->type == INSTRUMENT_TYPE_SYNTH) {
			SoundInstrument* sound = (SoundInstrument*)output;
			sound->possiblySetupDefaultExpressionPatching(&paramManager);
		}

		// Occasionally we get a song file with a crazy scroll value. Not sure how. It happened to Tia
		if (!isScrollWithinRange(0, yScroll)) {
			yScroll = 60;
		}
	}

	// Now the Instrument (and all Drums) are matched up, we can do the resonance compensation crap.
	compensateVolumeForResonance(modelStack);

	// If saved before V2.1....
	if (storageManager.firmwareVersionOfFileBeingRead < FIRMWARE_2P1P0_BETA) {

		if (output->type == INSTRUMENT_TYPE_SYNTH) {
			SoundInstrument* sound = (SoundInstrument*)output;

			for (int s = 0; s < NUM_SOURCES; s++) {
				Source* source = &sound->sources[s];
				if (source->oscType == OSC_TYPE_SAMPLE) {
					source->sampleControls.interpolationMode = INTERPOLATION_MODE_LINEAR;
				}
			}
		}

		// For songs saved before V2.0, ensure that non-square oscillators have PW set to 0 (cos PW in this case didn't have an effect then but it will now)
		if (storageManager.firmwareVersionOfFileBeingRead < FIRMWARE_2P0P0_BETA) {
			if (output->type == INSTRUMENT_TYPE_SYNTH) {
				SoundInstrument* sound = (SoundInstrument*)output;

				ParamCollectionSummary* patchedParamsSummary = paramManager.getPatchedParamSetSummary();
				PatchedParamSet* patchedParams = (PatchedParamSet*)patchedParamsSummary->paramCollection;

				PatchCableSet* patchedCables = paramManager.getPatchCableSet();

				ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
				    modelStack->addOtherTwoThingsButNoNoteRow(sound, &paramManager);

				for (int s = 0; s < NUM_SOURCES; s++) {
					if (sound->sources[s].oscType != OSC_TYPE_SQUARE) {

						ModelStackWithParamCollection* modelStackWithParamCollection =
						    modelStackWithThreeMainThings->addParamCollection(patchedParams, patchedParamsSummary);

						patchedParams->deleteAutomationForParamBasicForSetup(modelStackWithParamCollection,
						                                                     PARAM_LOCAL_OSC_A_PHASE_WIDTH + s);
						patchedParams->params[PARAM_LOCAL_OSC_A_PHASE_WIDTH + s].setCurrentValueBasicForSetup(0);
						patchedCables->removeAllPatchingToParam(modelStackWithParamCollection,
						                                        PARAM_LOCAL_OSC_A_PHASE_WIDTH + s);
					}
				}
			}
		}
	}

	return NO_ERROR;
}

void InstrumentClip::finishLinearRecording(ModelStackWithTimelineCounter* modelStack, Clip* nextPendingLoop,
                                           int buttonLatencyForTempolessRecord) {

	if (getRootUI() == &arrangerView) arrangerView.clipNeedsReRendering(this);

	InstrumentClip* newInstrumentClip = NULL;
	if (nextPendingLoop) newInstrumentClip = (InstrumentClip*)nextPendingLoop;

	Action* action = NULL;

	// Notes may have been placed right at/past the end of the Clip, usually because one was quantized forwards - and set to the exact
	// end position - and it wasn't yet known whether to extend the length of the Clip in case the user cancelled linear recording.
	// Trim them off, and move them to the new Clip if there is one.
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		thisNoteRow->skipNextNote = false;

		bool mayStillLengthen = true;

		while (
		    thisNoteRow->notes.getNumElements()) { // There's most likely only one offender, but you never really know
			Note* lastNote = thisNoteRow->notes.getLast();

			// If Note is past new end-point that we're setting now, then delete / move the Note
			if (lastNote->pos >= loopLength) {

				mayStillLengthen = false;

				// If there's a newInstrumentClip, then put the Note in it
				if (newInstrumentClip) {
					ModelStackWithNoteRow* modelStackWithNoteRow;
					if (output->type == INSTRUMENT_TYPE_KIT) {
						modelStackWithNoteRow = newInstrumentClip->getNoteRowForDrum(modelStack, thisNoteRow->drum);
					}
					else {
						modelStackWithNoteRow =
						    newInstrumentClip->getOrCreateNoteRowForYNote(thisNoteRow->y, modelStack);
					}

					NoteRow* newNoteRow = modelStackWithNoteRow->getNoteRowAllowNull();
					if (newNoteRow) {
						newNoteRow->attemptNoteAdd(
						    0, lastNote->length, lastNote->velocity, lastNote->probability, modelStackWithNoteRow,
						    NULL); // I'm guessing I deliberately didn't send the Action in here, cos didn't want to make this Note on the new InstrumentClip undoable?
						newNoteRow->skipNextNote = true;
					}
				}

				// Delete the Note
				thisNoteRow->deleteNoteByIndex(thisNoteRow->notes.getNumElements() - 1, NULL,
				                               getNoteRowId(thisNoteRow, i), this);
			}

			// Or if Note not past end-point...
			else {

				// Extend length right to end-point
				if (mayStillLengthen
				    && ((Instrument*)output)->isNoteRowStillAuditioningAsLinearRecordingEnded(thisNoteRow)) {

					if (!action) action = actionLogger.getNewAction(ACTION_RECORD, true);
					int noteRowId = getNoteRowId(thisNoteRow, i);

					if (action)
						action->recordNoteArrayChangeIfNotAlreadySnapshotted(
						    this, noteRowId, &thisNoteRow->notes, false, true); // This has probably already been done
					// moveToFrontIfAlreadySnapshotted = true because we need to make the Consequence closer to the front than any previous Clip-lengthening that took place.

					lastNote->setLength(loopLength - lastNote->pos);
				}

				// And, that'll be the last Note we need to deal with
				break;
			}
		}
	}

	// If we did create a new Clip, we want to leave currentlyRecordingLinearly true just a bit longer so that when expectNoFurtherTicks() gets called as the new Clip
	// begins playing, it knows not to switch our currently sounding/auditioning notes off.
	// Otherwise, since that won't be happening, we just want to ensure that recording stops now.
	currentlyRecordingLinearly = newInstrumentClip;

	if (isUnfinishedAutoOverdub) {
		isUnfinishedAutoOverdub = false;
	}

	if (getRootUI())
		getRootUI()->clipNeedsReRendering(this); // Notes might have been lengthened - we'd better render it.
}

Clip* InstrumentClip::cloneAsNewOverdub(ModelStackWithTimelineCounter* modelStack, int newOverdubNature) {

	// Allocate memory for Clip
	void* clipMemory = generalMemoryAllocator.alloc(sizeof(InstrumentClip), NULL, false, true);
	if (!clipMemory) {
ramError:
		numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
		return NULL;
	}

	ParamManagerForTimeline newParamManager;

	int error = newParamManager.cloneParamCollectionsFrom(&paramManager, false, true);
	if (error) {
		numericDriver.displayError(error);
		return NULL;
	}

	InstrumentClip* newInstrumentClip = new (clipMemory) InstrumentClip(modelStack->song);
	newInstrumentClip->setInstrument((Instrument*)output, modelStack->song, &newParamManager);

	newInstrumentClip->setupForRecordingAsAutoOverdub(
	    this, modelStack->song,
	    newOverdubNature); // Hopefully fine - I've moved this to after setInstrument in March 2021, so we can override the new affectEntire default value set there.

	char modelStackMemoryNewClip[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackNewClip =
	    setupModelStackWithTimelineCounter(modelStackMemoryNewClip, modelStack->song, newInstrumentClip);

	newInstrumentClip->setupAsNewKitClipIfNecessary(modelStackNewClip);

	// If Kit, copy NoteRow colours
	if (output->type == INSTRUMENT_TYPE_KIT
	    && noteRows.getNumElements() == newInstrumentClip->noteRows.getNumElements()) {
		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* oldNoteRow = noteRows.getElement(i);
			NoteRow* newNoteRow = newInstrumentClip->noteRows.getElement(i);

			newNoteRow->colourOffset = oldNoteRow->colourOffset;
		}
	}

	return newInstrumentClip;
}

bool InstrumentClip::cloneOutput(ModelStackWithTimelineCounter* modelStack) {
	return false;
}

bool InstrumentClip::isAbandonedOverdub() {
	return (isUnfinishedAutoOverdub && !containsAnyNotes());
}

void InstrumentClip::quantizeLengthForArrangementRecording(ModelStackWithTimelineCounter* modelStack,
                                                           int32_t lengthSoFar, uint32_t timeRemainder,
                                                           int32_t suggestedLength, int32_t alternativeLongerLength) {

	if (alternativeLongerLength) {
		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			int numNotes = thisNoteRow->notes.getNumElements();
			if (numNotes) {
				Note* lastNote = thisNoteRow->notes.getElement(numNotes - 1);
				if (lastNote->pos + lastNote->length > suggestedLength) {
					goto useAlternativeLength;
				}
			}
		}

		if (false) {
useAlternativeLength:
			suggestedLength = alternativeLongerLength;
		}
	}

	int32_t oldLength = loopLength;
	loopLength = suggestedLength;
	lengthChanged(modelStack, oldLength);
}

bool InstrumentClip::currentlyScrollableAndZoomable() {
	return !onKeyboardScreen || (getRootUI() == &sessionView && containsAnyNotes()); // Cheating a bit!
}

// Call this after setInstrument() / setAudioInstrument(). I forget exactly where setupPatching() fits into this picture... Arranger view calls that before this...
void InstrumentClip::setupAsNewKitClipIfNecessary(ModelStackWithTimelineCounter* modelStack) {
	if (output->type == INSTRUMENT_TYPE_KIT) {
		((Kit*)output)->resetDrumTempValues();
		assignDrumsToNoteRows(modelStack);
		yScroll = 0;
	}
}

bool InstrumentClip::getCurrentlyRecordingLinearly() {
	return currentlyRecordingLinearly;
}

void InstrumentClip::abortRecording() {
	currentlyRecordingLinearly = false;
}

// ----- PlayPositionCounter implementation -------

void InstrumentClip::getActiveModControllable(ModelStackWithTimelineCounter* modelStack) {
	if (output->type == INSTRUMENT_TYPE_KIT && !affectEntire && getRootUI() != &sessionView
	    && getRootUI() != &arrangerView) {
		Kit* kit = (Kit*)output;

		if (!kit->selectedDrum || kit->selectedDrum->type != DRUM_TYPE_SOUND) {
returnNull:
			modelStack->setTimelineCounter(NULL);
			modelStack->addOtherTwoThingsButNoNoteRow(NULL, NULL);
		}
		else {
			int noteRowIndex;
			NoteRow* noteRow = getNoteRowForDrum(kit->selectedDrum, &noteRowIndex);

			// Ensure that the selected drum in fact has a NoteRow in this Clip. It may have been deleted.
			if (!noteRow) goto returnNull;

			modelStack->addNoteRow(noteRowIndex, noteRow)
			    ->addOtherTwoThings((SoundDrum*)kit->selectedDrum, &noteRow->paramManager);
		}
	}

	else {
		Clip::getActiveModControllable(modelStack);
	}
}

void InstrumentClip::expectEvent() {
	ticksTilNextNoteRowEvent = 0;
	Clip::expectEvent();
}

void InstrumentClip::instrumentBeenEdited() {
	((Instrument*)output)->beenEdited();
}

// May return NULL NoteRow - you must check for that.
ModelStackWithNoteRow* InstrumentClip::duplicateModelStackForClipBeingRecordedFrom(ModelStackWithNoteRow* modelStack,
                                                                                   char* otherModelStackMemory) {
	copyModelStack(otherModelStackMemory, modelStack, sizeof(ModelStackWithNoteRowId));
	ModelStackWithNoteRowId* otherModelStack = (ModelStackWithNoteRowId*)otherModelStackMemory;
	otherModelStack->setTimelineCounter(beingRecordedFromClip);
	ModelStackWithNoteRow* otherModelStackWithNoteRow = otherModelStack->automaticallyAddNoteRowFromId();
	return otherModelStackWithNoteRow;
}

extern int16_t zeroMPEValues[NUM_EXPRESSION_DIMENSIONS];

void InstrumentClip::recordNoteOn(ModelStackWithNoteRow* modelStack, int velocity, bool forcePos0,
                                  int16_t const* mpeValuesOrNull, int fromMIDIChannel) {

	NoteRow* noteRow = modelStack->getNoteRow();

	int32_t quantizedPos = 0;

	bool reversed = modelStack->isCurrentlyPlayingReversed();
	int32_t effectiveLength = modelStack->getLoopLength();

	if (forcePos0) {
		noteRow->skipNextNote = true;
	}
	else {
		uint32_t unquantizedPos = modelStack->getLivePos();

		bool quantizedLater = false;

		if (FlashStorage::recordQuantizeLevel) {
			unsigned int baseThing = modelStack->song->tripletsOn ? 4 : 3;
			uint16_t quantizeInterval = baseThing << (8 + modelStack->song->insideWorldTickMagnitude
			                                          + modelStack->song->insideWorldTickMagnitudeOffsetFromBPM
			                                          - FlashStorage::recordQuantizeLevel);
			quantizedPos = unquantizedPos / quantizeInterval * quantizeInterval;
			int offset = unquantizedPos - quantizedPos;

			int32_t amountLaterThanMiddle = (offset - (quantizeInterval >> 1));
			if (reversed) amountLaterThanMiddle = 1 - amountLaterThanMiddle;
			quantizedLater = (amountLaterThanMiddle >= 0);

			// If quantizing to the right...
			if (quantizedLater != reversed) { // If need to quantize forwards (to later in time)...
				quantizedPos += quantizeInterval;

				// If that's quantized it right to the end of the loop-length or maybe beyond...
				if (quantizedPos >= effectiveLength) {

					// If recording to arrangement, go and extend the Clip/NoteRow early, to create the place where we'll put the Note.
					if (playbackHandler.recording == RECORDING_ARRANGEMENT && isArrangementOnlyClip()) {

						int error;

						// If the NoteRow has independent *length* (not just independent play-pos), then it needs to be treated individually.
						if (noteRow->loopLengthIfIndependent) {
							if (output->type == INSTRUMENT_TYPE_KIT
							    && noteRows.getNumElements()
							           != ((InstrumentClip*)beingRecordedFromClip)->noteRows.getNumElements()) {
								error = ERROR_UNSPECIFIED;
							}

							else {
								char otherModelStackMemory[MODEL_STACK_MAX_SIZE];
								ModelStackWithNoteRow* otherModelStackWithNoteRow =
								    duplicateModelStackForClipBeingRecordedFrom(modelStack, otherModelStackMemory);

								NoteRow* otherNoteRow = otherModelStackWithNoteRow->getNoteRowAllowNull();
								if (otherNoteRow) { // It "should" always have it...

									int whichRepeatThisIs = (uint32_t)noteRow->loopLengthIfIndependent
									                        / (uint32_t)otherNoteRow->loopLengthIfIndependent;
									noteRow->appendNoteRow(modelStack, otherModelStackWithNoteRow,
									                       noteRow->loopLengthIfIndependent, whichRepeatThisIs,
									                       otherNoteRow->loopLengthIfIndependent);
									noteRow->loopLengthIfIndependent += otherNoteRow->loopLengthIfIndependent;
								}
							}
						}

						// Otherwise, just extend the whole Clip.
						else {
							char thisModelStackMemory[MODEL_STACK_MAX_SIZE];
							copyModelStack(thisModelStackMemory, modelStack, sizeof(ModelStackWithTimelineCounter));
							ModelStackWithTimelineCounter* thisModelStack =
							    (ModelStackWithTimelineCounter*)thisModelStackMemory;

							char otherModelStackMemory[MODEL_STACK_MAX_SIZE];
							ModelStackWithTimelineCounter* otherModelStack =
							    setupModelStackWithSong(otherModelStackMemory, modelStack->song)
							        ->addTimelineCounter(beingRecordedFromClip);

							error = appendClip(thisModelStack, otherModelStack);
						}

						if (error) goto doNormal;
					}

					// If recording linearly...
					else if (getCurrentlyRecordingLinearly()) {
						// Don't do anything - let the note begin at or past (?) the Clip length
					}
					else {
doNormal: // Wrap it back to the start.
						quantizedPos = 0;
					}
				}
			}

			// If we're quantized later to a pingpong-point, we have to consider the play-direction to have changed.
			if (quantizedLater && !quantizedPos) {
				if (noteRow->getEffectiveSequenceDirectionMode(modelStack) == SEQUENCE_DIRECTION_PINGPONG) {
					reversed = !reversed;
				}
			}
		}

		else {
			quantizedPos = unquantizedPos;
		}

		// If we quantized later, make sure that that note doesn't get played really soon when the play-pos reaches it
		if (quantizedLater || playbackHandler.ticksLeftInCountIn) {
			noteRow->skipNextNote = true;
			expectEvent();
		}
	}

	// Since recording usually involves creating lots of notes overall, we'll just snapshot all the notes in bulk
	Action* action = actionLogger.getNewAction(ACTION_RECORD, true);
	if (action)
		action->recordNoteArrayChangeIfNotAlreadySnapshotted(this, modelStack->noteRowId, &noteRow->notes, false, true);
	// moveToFrontIfAlreadySnapshotted = true because we need to make the Consequence closer to the front than any previous Clip-lengthening that took place.

	int32_t distanceToNextNote;

	// Add the actual note
	if (reversed) {
		bool allowingNoteTails = allowNoteTails(modelStack);
		distanceToNextNote = noteRow->attemptNoteAddReversed(modelStack, quantizedPos, velocity, allowingNoteTails);
	}

	else {
		distanceToNextNote = noteRow->attemptNoteAdd(quantizedPos, 1, velocity, NUM_PROBABILITY_VALUES, modelStack,
		                                             NULL); // Don't supply Action, cos we've done our own thing, above
	}

	// If that didn't work, get out - but not in the special case for linear recording, discussed below.
	if (!distanceToNextNote && quantizedPos < effectiveLength) return;

	// If we're doing MPE, we'll want to place a node here at the Note's start, so it's got the correct stuff
	// to sound during its note-on when we play back.

	// If we've been supplied MPE values, we definitely want to record these.
	if (mpeValuesOrNull) {
		noteRow->paramManager.ensureExpressionParamSetExists(); // If that fails, we'll return below.
	}

	// Or if we haven't been supplied MPE values, just check if this NoteRow already has MPE data, and only
	// if so, go and overwrite it here.
	else {
		mpeValuesOrNull = zeroMPEValues;
	}

	ParamCollectionSummary* mpeParamsSummary = noteRow->paramManager.getExpressionParamSetSummary();
	ExpressionParamSet* mpeParams = (ExpressionParamSet*)mpeParamsSummary->paramCollection;
	if (!mpeParams) return;

	int32_t posAtWhichClipWillCut = modelStack->getPosAtWhichPlaybackWillCut();

	ModelStackWithParamCollection* modelStackWithParamCollection =
	    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(mpeParams, mpeParamsSummary);

	for (int m = 0; m < NUM_EXPRESSION_DIMENSIONS; m++) {
		AutoParam* param = &mpeParams->params[m];
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStackWithParamCollection->addAutoParam(m, param);

		Action* action = actionLogger.getNewAction(ACTION_RECORD, true);
		if (action) action->recordParamChangeIfNotAlreadySnapshotted(modelStackWithAutoParam);

		int32_t value = (int32_t)mpeValuesOrNull[m] << 16;

		// Special case for MPE - in this case where we're setting the value for the whole length, we still do want to place one - just one - node at pos. It'll be for the start of a note.
		if (effectiveLength == distanceToNextNote) {
			param->deleteAutomation(NULL, modelStackWithAutoParam, false);

			int error = param->nodes.insertAtIndex(0);
			if (!error) {
				ParamNode* firstNode = param->nodes.getElement(0);
				firstNode->pos = quantizedPos;
				firstNode->value = value;
				firstNode->interpolated = reversed;
			}
		}

		else if (reversed) {
doHomogenize:
#if ALPHA_OR_BETA_VERSION
			param->nodes.testSequentiality(
			    "E442"); // drbourbon got, when check was inside homogenizeRegion(). Now trying to work out where that came from. March 2022.
#endif

			param->homogenizeRegion(modelStackWithAutoParam, quantizedPos, distanceToNextNote, value, reversed,
			                        reversed, effectiveLength, reversed, posAtWhichClipWillCut);
		}

		else {

			// Special case for if linear recording, quantized later, right to end of effectiveLength.
			if (quantizedPos >= effectiveLength) {
				param->setNodeAtPos(quantizedPos, value, false);
			}

			// Or, normal case
			else {
				goto doHomogenize;
			}
		}

		mpeParams->paramHasAutomationNow(mpeParamsSummary, m);

		// These manual sets are in case we quantized forwards and the region we just created actually begins after "now"-time.
		param->currentValue = value;
		param->valueIncrementPerHalfTick = 0;
		// TODO: and to make it perfect, we'd also want to ignore any further nodes between now and the start of the region. Or, could probably get away with just deleting them.
	}
}

void InstrumentClip::recordNoteOff(ModelStackWithNoteRow* modelStack, int velocity) {

	if (!allowNoteTails(modelStack)) return;

	Action* action = actionLogger.getNewAction(ACTION_RECORD, true);

	modelStack->getNoteRow()->recordNoteOff(getLivePos(), modelStack, action, velocity);
}

// This function looks a bit weird... probably old... should it maybe instead call a function on the MelodicInstrument / Kit?
void InstrumentClip::yDisplayNoLongerAuditioning(int yDisplay, Song* song) {
	if (output->type == INSTRUMENT_TYPE_KIT) {
		int noteRowIndex = yDisplay + yScroll;
		if (noteRowIndex >= 0 && noteRowIndex <= noteRows.getNumElements()) {
			NoteRow* noteRow = noteRows.getElement(noteRowIndex);
			if (noteRow->drum) {
				noteRow->drum->auditioned = false;
				noteRow->drum->lastMIDIChannelAuditioned = MIDI_CHANNEL_NONE; // So it won't record any more MPE
			}
		}
	}

	else {
		int yNote = getYNoteFromYDisplay(yDisplay, song);
		((MelodicInstrument*)output)->notesAuditioned.deleteAtKey(yNote);
	}

	expectEvent();
}

int32_t InstrumentClip::getMaxLength() {
	int32_t maxLength = loopLength;

	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->loopLengthIfIndependent > maxLength) maxLength = thisNoteRow->loopLengthIfIndependent;
	}

	return maxLength;
}

bool InstrumentClip::hasAnyPitchExpressionAutomationOnNoteRows() {
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		ExpressionParamSet* expressionParams = thisNoteRow->paramManager.getExpressionParamSet();
		if (expressionParams && expressionParams->params[0].isAutomated()) return true;
	}
	return false;
}

void InstrumentClip::incrementPos(ModelStackWithTimelineCounter* modelStack, int32_t numTicks) {
	Clip::incrementPos(modelStack, numTicks);

	ticksTilNextNoteRowEvent -= numTicks; // We're one tick closer to the next event...
	noteRowsNumTicksBehindClip += numTicks;

	if (ticksTilNextNoteRowEvent <= 0) {

		for (int i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			if (thisNoteRow->hasIndependentPlayPos()) {
				int32_t movement = noteRowsNumTicksBehindClip;

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    modelStack->addNoteRow(getNoteRowId(thisNoteRow, i), thisNoteRow);
				if (modelStackWithNoteRow->isCurrentlyPlayingReversed()) {
					movement = -movement;
				}
				thisNoteRow->lastProcessedPosIfIndependent += movement;
			}
		}
	}
}

/*
	for (int i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
*/
