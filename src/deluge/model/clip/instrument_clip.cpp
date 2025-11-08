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

#include "model/clip/instrument_clip.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/browser/browser.h"
#include "gui/ui/load/load_instrument_preset_ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/clip_instance.h"
#include "model/clip/sequencer/modes/step_sequencer_mode.h"
#include "model/clip/sequencer/sequencer_mode_manager.h"
#include "model/consequence/consequence_note_row_mute.h"
#include "model/consequence/consequence_scale_add_note.h"
#include "model/drum/drum_name.h"
#include "model/instrument/cv_instrument.h"
#include "model/instrument/midi_instrument.h"
#include "model/iterance/iterance.h"
#include "model/note/note.h"
#include "model/output.h"
#include "model/scale/note_set.h"
#include "model/scale/preset_scales.h"
#include "model/scale/scale_change.h"
#include "model/scale/utils.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/engines/cv_engine.h"
#include "processing/sound/sound_instrument.h"
#include "storage/storage_manager.h"
#include "util/firmware_version.h"
#include "util/try.h"
#include <cmath>
#include <new>
#include <ranges>

namespace params = deluge::modulation::params;

// Supplying song is optional, and basically only for the purpose of setting yScroll according to root note
InstrumentClip::InstrumentClip(Song* song) : Clip(ClipType::INSTRUMENT), noteRows() {
	midiBank = 128; // Means none
	midiSub = 128;  // Means none
	midiPGM = 128;  // Means none

	currentlyRecordingLinearly = false;

	if (song) {
		colourOffset -= song->key.rootNote;
	}

	wrapEditing = false;
	for (int32_t i = 0; i < 4; i++) {
		backedUpInstrumentSlot[i] = 0;
		backedUpInstrumentSubSlot[i] = -1;
	}

	affectEntire = true;

	inScaleMode = flashStorageCodeToScale(FlashStorage::defaultScale) != NO_SCALE;
	onKeyboardScreen = false;

	if (song) {
		int32_t yNote = ((uint16_t)(song->key.rootNote + 120) % 12) + 60;
		if (yNote > 66) {
			yNote -= 12;
		}
		yScroll = getYVisualFromYNote(yNote,
		                              song); // This takes into account the rootNote, which could be anything. Must be
		                                     // called after the above stuff is set up
	}
	else {
		yScroll =
		    0; // Only for safety. Shouldn't actually get here if we're not going to overwrite this elsewhere I think...
	}

	outputTypeWhileLoading = OutputType::SYNTH; // NOTE: (Kate) was 0, should probably be NONE
}

// You must call prepareForDestruction() before this, preferably by calling Song::deleteClipObject()
// Will call audio routine!!! Necessary to avoid voice cuts, especially when switching song
InstrumentClip::~InstrumentClip() {

	// Note: it's possible that we might be currentlyRecordingLinearly if we're being destructed because of a song-swap.
	// That's ok. Whereas, for AudioClips, it's made sure that all linear recording is stopped first

	deleteBackedUpParamManagerMIDI();
}

void InstrumentClip::deleteBackedUpParamManagerMIDI() {
	if (backedUpParamManagerMIDI.containsAnyMainParamCollections()) {
		backedUpParamManagerMIDI.destructAndForgetParamCollections();
	}
}

void InstrumentClip::copyBasicsFrom(Clip const* otherClip) {
	Clip::copyBasicsFrom(otherClip);

	auto otherInstrumentClip = static_cast<InstrumentClip const*>(otherClip);

	midiBank = otherInstrumentClip->midiBank;
	midiSub = otherInstrumentClip->midiSub;
	midiPGM = otherInstrumentClip->midiPGM;

	onKeyboardScreen = otherInstrumentClip->onKeyboardScreen;
	inScaleMode = otherInstrumentClip->inScaleMode;
	wrapEditing = otherInstrumentClip->wrapEditing;
	wrapEditLevel = otherInstrumentClip->wrapEditLevel;
	yScroll = otherInstrumentClip->yScroll;
	keyboardState = otherInstrumentClip->keyboardState;
	sequenceDirectionMode = otherInstrumentClip->sequenceDirectionMode;

	affectEntire = otherInstrumentClip->affectEntire;

	memcpy(backedUpInstrumentSlot, otherInstrumentClip->backedUpInstrumentSlot, sizeof(backedUpInstrumentSlot));
	memcpy(backedUpInstrumentSubSlot, otherInstrumentClip->backedUpInstrumentSubSlot,
	       sizeof(backedUpInstrumentSubSlot));
	for (int32_t i = 0; i < 2; i++) {
		backedUpInstrumentName[i].set(&otherInstrumentClip->backedUpInstrumentName[i]);
	}
	for (int32_t i = 0; i < 2; i++) {
		backedUpInstrumentDirPath[i].set(&otherInstrumentClip->backedUpInstrumentDirPath[i]);
	}

	arpSettings.cloneFrom(&otherInstrumentClip->arpSettings);

	// Copy sequencer mode data
	sequencerModeName_ = otherInstrumentClip->sequencerModeName_;

	// Clone the active sequencer mode if it exists
	if (otherInstrumentClip->sequencerMode_) {
		auto& manager = deluge::model::clip::sequencer::SequencerModeManager::instance();
		sequencerMode_ = manager.createMode(sequencerModeName_);
		if (sequencerMode_) {
			// TODO: We should implement a proper clone method for sequencer modes
			// For now, the new clip will get default sequencer mode data
			sequencerMode_->initialize();
		}
	}

	// Clone cached sequencer modes
	for (const auto& [modeName, mode] : otherInstrumentClip->cachedSequencerModes_) {
		if (mode) {
			auto& manager = deluge::model::clip::sequencer::SequencerModeManager::instance();
			auto clonedMode = manager.createMode(modeName);
			if (clonedMode) {
				// TODO: We should implement a proper clone method for sequencer modes
				// For now, the cached modes will have default data
				clonedMode->initialize();
				cachedSequencerModes_[modeName] = std::move(clonedMode);
			}
		}
	}
}

// Will replace the Clip in the modelStack, if success.
Error InstrumentClip::clone(ModelStackWithTimelineCounter* modelStack, bool shouldFlattenReversing) const {

	void* clipMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(InstrumentClip));
	if (!clipMemory) {
		return Error::INSUFFICIENT_RAM;
	}

	auto newClip = new (clipMemory) InstrumentClip(); // Don't supply Song. yScroll will get set in copyBasicsFrom()

	newClip->copyBasicsFrom(this);

	int32_t reverseWithLength = 0;
	if (shouldFlattenReversing && sequenceDirectionMode == SequenceDirection::REVERSE) {
		reverseWithLength = loopLength;
	}

	Error error = newClip->paramManager.cloneParamCollectionsFrom(&paramManager, true, true, reverseWithLength);
	if (error != Error::NONE) {
deleteClipAndGetOut:
		newClip->~InstrumentClip();
		delugeDealloc(clipMemory);
		return error;
	}

	newClip->activeIfNoSolo = false;
	newClip->soloingInSessionMode = false;
	newClip->output = output;

	if (!newClip->noteRows.cloneFrom(&noteRows)) {
		error = Error::INSUFFICIENT_RAM;
		goto deleteClipAndGetOut;
	}

	modelStack->setTimelineCounter(newClip);

	for (int32_t i = 0; i < newClip->noteRows.getNumElements(); i++) {
		NoteRow* noteRow = newClip->noteRows.getElement(i);
		int32_t noteRowId = newClip->getNoteRowId(noteRow, i);
		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);
		Error error = noteRow->beenCloned(modelStackWithNoteRow, shouldFlattenReversing);

		// If that fails, we have to keep going, cos otherwise some NoteRows' NoteVector will be left pointing to stuff
		// it shouldn't be
	}

	if (shouldFlattenReversing && newClip->sequenceDirectionMode == SequenceDirection::REVERSE) {
		newClip->sequenceDirectionMode = SequenceDirection::FORWARD;
	}
	// Leave PINGPONG as it is, because we haven't actually flattened that - its effect wouldn't be seen until a repeat
	// happened. And we may be about to flatten it with a increaseLengthWithRepeats(), so need to keep this designation
	// for now.

	return Error::NONE;
}

// newLength might not be any longer than we already were - but this function still gets called in case any shorter
// NoteRows need lengthening. So, this function must allow for that case (Clip length staying the same).
void InstrumentClip::increaseLengthWithRepeats(ModelStackWithTimelineCounter* modelStack, int32_t newLength,
                                               IndependentNoteRowLengthIncrease independentNoteRowInstruction,
                                               bool completelyRenderOutIterationDependence, Action* action) {

	int32_t numRepeatsRounded =
	    completelyRenderOutIterationDependence ? 0 : (uint32_t)(newLength + (loopLength >> 1)) / (uint32_t)loopLength;

	// Tell all the noteRows
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		int32_t noteRowId = getNoteRowId(thisNoteRow, i);

		int32_t numRepeatsRoundedHere = numRepeatsRounded;
		int32_t oldLengthHere = loopLength;
		int32_t newLengthHere = newLength;

		// Deal specially with NoteRows with independent length.
		if (thisNoteRow->loopLengthIfIndependent) {

			switch (independentNoteRowInstruction) {
			case IndependentNoteRowLengthIncrease::DOUBLE:
				newLengthHere = thisNoteRow->loopLengthIfIndependent << 1;
				break;

			case IndependentNoteRowLengthIncrease::ROUND_UP:
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

	bool pingponging = (sequenceDirectionMode == SequenceDirection::PINGPONG);

	if (newLength > loopLength) {
		ModelStackWithThreeMainThings* modelStackWithParamManager =
		    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);
		paramManager.generateRepeats(modelStackWithParamManager, loopLength, newLength, pingponging);
	}

	if (pingponging) {
		sequenceDirectionMode =
		    SequenceDirection::FORWARD; // Pingponging has been flattened out, and although there are arguments either
		                                // way, I think removing that setting now is best.
	}

	loopLength = newLength;
}

// If action is NULL, that means this is being called as part of an undo
// Call this *after* you've set length to its new value (why did I do it this way?)
void InstrumentClip::lengthChanged(ModelStackWithTimelineCounter* modelStack, int32_t oldLength, Action* action) {

	if (loopLength < oldLength) {
		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
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

// Does this individually for each NoteRow, because they might be different lengths, and some might need repeating while
// others need chopping.
void InstrumentClip::repeatOrChopToExactLength(ModelStackWithTimelineCounter* modelStack, int32_t newLength) {
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		int32_t oldLengthHere = thisNoteRow->loopLengthIfIndependent;
		if (!oldLengthHere) {
			oldLengthHere = loopLength;
		}

		if (oldLengthHere != newLength) {
			int32_t noteRowId = getNoteRowId(thisNoteRow, i);
			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);

			if (newLength > oldLengthHere) {
				int32_t numRepeatsRounded = (uint32_t)(newLength + (oldLengthHere >> 1)) / (uint32_t)oldLengthHere;
				thisNoteRow->generateRepeats(modelStackWithNoteRow, oldLengthHere, newLength, numRepeatsRounded,
				                             nullptr);
			}

			else {
				thisNoteRow->trimToLength(newLength, modelStackWithNoteRow, nullptr);
			}
		}

		thisNoteRow->loopLengthIfIndependent = 0; // It doesn't need to be independent anymore.
	}

	if (newLength > loopLength) {
		bool pingponging = (sequenceDirectionMode == SequenceDirection::PINGPONG);

		ModelStackWithThreeMainThings* modelStackWithParamManager =
		    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);

		paramManager.generateRepeats(modelStackWithParamManager, loopLength, newLength, pingponging);

		if (pingponging) {
			sequenceDirectionMode =
			    SequenceDirection::FORWARD; // Pingponging has been flattened out, and although there are arguments
			                                // either way, I think removing that setting now is best.
		}
	}

	int32_t oldLength = loopLength;

	loopLength = newLength;

	Clip::lengthChanged(modelStack, oldLength,
	                    nullptr); // Call this on Clip::, not us InstrumentClip, because we've done our own version
	                              // above of what that call would do.

	if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(this)) {
		resumePlayback(modelStack);
	}
}

// This only gets called when undoing a "multiply Clip".
void InstrumentClip::halveNoteRowsWithIndependentLength(ModelStackWithTimelineCounter* modelStack) {
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* noteRow = noteRows.getElement(i);

		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(getNoteRowId(noteRow, i), noteRow);

		if (noteRow->loopLengthIfIndependent) {
			noteRow->setLength(modelStackWithNoteRow, noteRow->loopLengthIfIndependent >> 1, nullptr,
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

	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		// This function is "supposed" to call setPosForParamManagers() on this InstrumentClip, but instead, we'll do
		// our own thing here, so we only have to iterate through NoteRows once.
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

			SequenceDirection effectiveSequenceDirectionMode =
			    thisNoteRow->getEffectiveSequenceDirectionMode(modelStackWithNoteRow);

			// Syncing pingponging with repeatCount is particularly important for when resuming after
			// recording a clone of this Clip from session to arranger.
			thisNoteRow->currentlyPlayingReversedIfIndependent =
			    (effectiveSequenceDirectionMode == SequenceDirection::REVERSE
			     || (effectiveSequenceDirectionMode == SequenceDirection::PINGPONG
			         && (thisNoteRow->repeatCountIfIndependent & 1)));

			thisNoteRow->lastProcessedPosIfIndependent =
			    newPos - thisNoteRow->repeatCountIfIndependent * effectiveLoopLength;
			if (thisNoteRow->currentlyPlayingReversedIfIndependent) {
				if (thisNoteRow->lastProcessedPosIfIndependent) {
					thisNoteRow->lastProcessedPosIfIndependent =
					    effectiveLoopLength - thisNoteRow->lastProcessedPosIfIndependent;
				}
				else {
					thisNoteRow->repeatCountIfIndependent--;
				}
			}
		}
	}
}

Error InstrumentClip::beginLinearRecording(ModelStackWithTimelineCounter* modelStack, int32_t buttonPressLatency) {
	currentlyRecordingLinearly = true;

	if (output->type == OutputType::KIT) {
		Kit* kit = (Kit*)output;

		Action* action = nullptr;

		for (Drum* thisDrum = kit->firstDrum; thisDrum; thisDrum = thisDrum->next) {

			int32_t velocity = thisDrum->earlyNoteVelocity;

			if (velocity != 0) {
				thisDrum->earlyNoteVelocity = 0;

				int32_t noteRowIndex;
				NoteRow* noteRow = getNoteRowForDrum(
				    thisDrum, &noteRowIndex); // Remember, I'm planning to introduce a faster search/index for this
				if (noteRow) {

					if (!action) {
						action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);
					}

					ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowIndex, noteRow);
					int32_t probability = noteRow->getDefaultProbability();
					Iterance iterance = noteRow->getDefaultIterance();
					int32_t fill = noteRow->getDefaultFill(modelStackWithNoteRow);
					noteRow->attemptNoteAdd(0, 1, velocity, probability, iterance, fill, modelStackWithNoteRow, action);
					if (!thisDrum->earlyNoteStillActive) {
						D_PRINTLN("skipping next note");

						// We just inserted a note-on for an "early" note that is still sounding at time 0, so ignore
						// note-ons until at least tick 1 to avoid double-playing that note
						noteRow->ignoreNoteOnsBefore_ = 1;
					}
				}
			}
		}
	}

	else {
		MelodicInstrument* melodicInstrument = (MelodicInstrument*)output;
		if (!melodicInstrument->earlyNotes.empty()) {

			Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);
			bool scaleAltered = false;

			for (auto [note, noteInfo] : melodicInstrument->earlyNotes) {
				const auto [velocity, still_active] = noteInfo;

				ModelStackWithNoteRow* modelStackWithNoteRow =
				    getOrCreateNoteRowForYNote(note, modelStack, action, &scaleAltered);
				NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
				if (noteRow != nullptr) {
					int32_t probability = noteRow->getDefaultProbability();
					Iterance iterance = noteRow->getDefaultIterance();
					int32_t fill = noteRow->getDefaultFill(modelStackWithNoteRow);
					noteRow->attemptNoteAdd(0, 1, velocity, probability, iterance, fill, modelStackWithNoteRow, action);
					if (!still_active) {
						// We just inserted a note-on for an "early" note that is still sounding at time 0, so ignore
						// note-ons until at least tick 1 to avoid double-playing that note
						noteRow->ignoreNoteOnsBefore_ = 1;
					}
				}
			}

			// If this caused the scale to change, update scroll
			if (action != nullptr && scaleAltered) {
				action->updateYScrollClipViewAfter();
			}
			melodicInstrument->earlyNotes.clear();
		}
	}

	return Clip::beginLinearRecording(modelStack, buttonPressLatency);
}

// Gets called by Clip::setPos()
void InstrumentClip::setPosForParamManagers(ModelStackWithTimelineCounter* modelStack, bool useActualPos) {

	uint32_t pos = useActualPos ? getLivePos() : lastProcessedPos;
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
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

// Grabs automated values from current play-pos. To be called after a possible big change made to automation data, e.g.
// after an undo. This is only to be called if playbackHandler.isEitherClockActive().
void InstrumentClip::reGetParameterAutomation(ModelStackWithTimelineCounter* modelStack) {

	if (!isActiveOnOutput()) {
		return; // Definitely don't do this if we're not an active Clip!
	}

	Clip::reGetParameterAutomation(modelStack);

	uint32_t actualPos = getLivePos();
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->paramManager.mightContainAutomation()) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addNoteRowAndExtraStuff(i, thisNoteRow);
			thisNoteRow->paramManager.setPlayPos(actualPos, modelStackWithThreeMainThings,
			                                     modelStackWithThreeMainThings->isCurrentlyPlayingReversed());
		}
	}
}

Error InstrumentClip::transferVoicesToOriginalClipFromThisClone(ModelStackWithTimelineCounter* modelStackOriginal,
                                                                ModelStackWithTimelineCounter* modelStackClone) {
	InstrumentClip* originalClip = (InstrumentClip*)modelStackOriginal->getTimelineCounter();

	if (output->type == OutputType::KIT) {
		if (noteRows.getNumElements() != originalClip->noteRows.getNumElements()) {
			return Error::UNSPECIFIED;
		}

		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* clonedNoteRow = noteRows.getElement(i);
			NoteRow* originalNoteRow = originalClip->noteRows.getElement(i);

			ModelStackWithNoteRow* modelStackWithNoteRowClone = modelStackClone->addNoteRow(i, clonedNoteRow);
			ModelStackWithNoteRow* modelStackWithNoteRowOriginal = modelStackOriginal->addNoteRow(i, originalNoteRow);

			clonedNoteRow->resumeOriginalNoteRowFromThisClone(modelStackWithNoteRowOriginal,
			                                                  modelStackWithNoteRowClone);
		}
	}

	else {
		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
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

	return Error::NONE;
}

// Returns error
Error InstrumentClip::appendClip(ModelStackWithTimelineCounter* thisModelStack,
                                 ModelStackWithTimelineCounter* otherModelStack) {

	InstrumentClip* otherInstrumentClip = (InstrumentClip*)otherModelStack->getTimelineCounter();

	int32_t whichRepeatThisIs = (uint32_t)loopLength / (uint32_t)otherInstrumentClip->loopLength;

	if (output->type == OutputType::KIT) {
		if (noteRows.getNumElements() != otherInstrumentClip->noteRows.getNumElements()) {
			return Error::UNSPECIFIED;
		}

		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* otherNoteRow = otherInstrumentClip->noteRows.getElement(i);
			if (otherNoteRow->loopLengthIfIndependent) {
				continue; // Skip NoteRows with independent length - they'll take care of themselves.
			}

			NoteRow* thisNoteRow = noteRows.getElement(i);

			ModelStackWithNoteRow* thisModelStackWithNoteRow = thisModelStack->addNoteRow(i, thisNoteRow);
			ModelStackWithNoteRow* otherModelStackWithNoteRow = otherModelStack->addNoteRow(i, otherNoteRow);

			Error error = thisNoteRow->appendNoteRow(thisModelStackWithNoteRow, otherModelStackWithNoteRow, loopLength,
			                                         whichRepeatThisIs, otherInstrumentClip->loopLength);
			if (error != Error::NONE) {
				return error;
			}
		}
	}

	else {
		for (int32_t i = 0; i < otherInstrumentClip->noteRows.getNumElements(); i++) {
			NoteRow* otherNoteRow = otherInstrumentClip->noteRows.getElement(i);
			if (otherNoteRow->loopLengthIfIndependent) {
				continue; // Skip NoteRows with independent length - they'll take care of themselves.
			}

			int32_t noteRowId = otherNoteRow->y;

			ModelStackWithNoteRow* thisModelStackWithNoteRow = getNoteRowForYNote(noteRowId, thisModelStack);
			NoteRow* thisNoteRow = thisModelStackWithNoteRow->getNoteRowAllowNull();
			if (thisNoteRow) {
				ModelStackWithNoteRow* otherModelStackWithNoteRow =
				    otherModelStack->addNoteRow(noteRowId, otherNoteRow);

				Error error =
				    thisNoteRow->appendNoteRow(thisModelStackWithNoteRow, otherModelStackWithNoteRow, loopLength,
				                               whichRepeatThisIs, otherInstrumentClip->loopLength);
				if (error != Error::NONE) {
					return error;
				}
			}
		}
	}

	return Clip::appendClip(thisModelStack, otherModelStack);
}

void InstrumentClip::posReachedEnd(ModelStackWithTimelineCounter* thisModelStack) {
	Clip::posReachedEnd(thisModelStack);

	if (playbackHandler.recording == RecordingMode::ARRANGEMENT && isArrangementOnlyClip()) {

		char otherModelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* otherModelStack =
		    setupModelStackWithSong(otherModelStackMemory, thisModelStack->song)
		        ->addTimelineCounter(beingRecordedFromClip);

		appendClip(thisModelStack, otherModelStack);
	}
}

bool InstrumentClip::wantsToBeginLinearRecording(Song* song) {
	if (!Clip::wantsToBeginLinearRecording(song)) {
		return false;
	}

	if (isPendingOverdub) {
		return true; // Must take precedence - because we may have already placed some new notes at 0 if user hit key
		             // just now
	}

	return !containsAnyNotes();
}

void InstrumentClip::pingpongOccurred(ModelStackWithTimelineCounter* modelStack) {
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
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
	if (modelStack->getTimelineCounter() != this) {
		return; // Is this in case it's created a new Clip or something?
	}

	// We already incremented / decremented noteRowsNumTicksBehindClip and ticksTilNextNoteRowEvent, in the call to
	// incrementPos().

	// If sequencer mode is active, skip normal note row processing
	// (sequencer processing is now handled in Session::doTickForward)
	if (sequencerMode_) {
		return;
	}

	if (ticksTilNextNoteRowEvent <= 0) {

		// Ok, time to do some ticks

		// We need to at least come back when the Clip wraps
#if HAVE_SEQUENCE_STEP_CONTROL
		if (lastProcessedPos && currentlyPlayingReversed) {
			ticksTilNextNoteRowEvent = lastProcessedPos;
		}
		else {
#endif
			ticksTilNextNoteRowEvent = loopLength - lastProcessedPos;
		}

		static PendingNoteOnList pendingNoteOnList; // Making this static, which it really should have always been,
		                                            // actually didn't help max stack usage at all somehow...
		pendingNoteOnList.count = 0;

		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);

			ModelStackWithNoteRow* modelStackWithNoteRow =
			    modelStack->addNoteRow(getNoteRowId(thisNoteRow, i), thisNoteRow);

			int32_t noteRowTicksTilNextEvent =
			    thisNoteRow->processCurrentPos(modelStackWithNoteRow, noteRowsNumTicksBehindClip, &pendingNoteOnList);
			if (noteRowTicksTilNextEvent < ticksTilNextNoteRowEvent) {
				ticksTilNextNoteRowEvent = noteRowTicksTilNextEvent;
			}
		}

		noteRowsNumTicksBehindClip = 0;

		// Count up how many of each probability there are
		uint8_t probabilityCount[kNumProbabilityValues];
		memset(probabilityCount, 0, sizeof(probabilityCount));

		// Check whether special case where all probability adds up to 100%
		int32_t probabilitySum = 0;

		bool doingSumTo100 = false;
		int32_t winningI;

		for (int32_t i = 0; i < pendingNoteOnList.count; i++) {

			// If we found a 100%, we know we're not doing sum-to-100
			if (pendingNoteOnList.pendingNoteOns[i].probability == kNumProbabilityValues) {
				goto skipDoingSumTo100;
			}

			// If any follow-previous-probability, skip this statistics-grabbing
			if (pendingNoteOnList.pendingNoteOns[i].probability & 128) {
				continue;
			}

			// Add to probability total sum - only if we hadn't already found a pending note-on with this probability
			// value
			// if (probabilityCount[pendingNoteOnList.pendingNoteOns[i].probability - 1] == 0)
			probabilitySum += pendingNoteOnList.pendingNoteOns[i].probability;

			probabilityCount[pendingNoteOnList.pendingNoteOns[i].probability - 1]++;
		}

		doingSumTo100 = (probabilitySum == kNumProbabilityValues);

		if (doingSumTo100) {
			int32_t probabilityValueForSummers = ((uint32_t)getRandom255() * kNumProbabilityValues) >> 8;

			int32_t probabilitySumSecondPass = 0;

			bool foundWinner = false;

			for (int32_t i = 0; i < pendingNoteOnList.count; i++) {

				// If any follow-previous-probability, skip this statistics-grabbing
				if (pendingNoteOnList.pendingNoteOns[i].probability & 128) {
					continue;
				}

				int32_t probability = pendingNoteOnList.pendingNoteOns[i].probability;

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
		for (int32_t i = 0; i < pendingNoteOnList.count; i++) {

			bool conditionPassed;

			// If it's a 100%, which usually will be the case...
			if (pendingNoteOnList.pendingNoteOns[i].probability == kNumProbabilityValues) {
				conditionPassed = true;
			}

			// Otherwise...
			else [[unlikely]] {
				int32_t probability = pendingNoteOnList.pendingNoteOns[i].probability & 127;

				// If based on a previous probability...
				if (pendingNoteOnList.pendingNoteOns[i].probability & 128) {

					// Check that that previous probability value is still valid. It normally should be, unless the
					// user has changed the probability of that "previous" note
					if (lastProbabiltyPos[probability] == -1 || lastProbabiltyPos[probability] == lastProcessedPos) {
						goto doNewProbability;
					}

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
						// If the outcome of this probability has already been decided (by another note with same
						// probability)
						if (probabilityCount[probability - 1] >= 254) {
							conditionPassed = probabilityCount[probability - 1] == 255;
						}

						// Otherwise, decide it now
						else {
							int32_t probabilityValue = ((uint32_t)getRandom255() * kNumProbabilityValues) >> 8;
							conditionPassed = (probabilityValue < probability);

							lastProbabilities[kNumProbabilityValues - probability] = !conditionPassed;
							lastProbabiltyPos[kNumProbabilityValues - probability] = lastProcessedPos;

							lastProbabilities[probability] = conditionPassed;
							lastProbabiltyPos[probability] = lastProcessedPos;

							// Store the outcome, for any neighbouring notes
							probabilityCount[probability - 1] = conditionPassed ? 255 : 254;
						}
					}
				}
			}

			// if probably setting has resulted in a note on
			if (conditionPassed) [[likely]] {
				// now we check if we should skip note based on iteration condition
				Iterance iterance = pendingNoteOnList.pendingNoteOns[i].iterance;

				// If it's an iteration dependence...
				if (iterance != kDefaultIteranceValue) [[unlikely]] {
					ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(
					    pendingNoteOnList.pendingNoteOns[i].noteRowId, pendingNoteOnList.pendingNoteOns[i].noteRow);

					conditionPassed = iterance.passesCheck(modelStackWithNoteRow->getRepeatCount());
				}

				// lastly, if after checking iteration we still have a note on
				// we'll check if that note should be sounded based on fill state
				if (conditionPassed) {
					// check if it's a FILL note and SYNC_SCALING is *not* pressed
					if (pendingNoteOnList.pendingNoteOns[i].fill == FillMode::FILL
					    && !currentSong->isFillModeActive()) {
						conditionPassed = false;
					}
					// check if it's a NOT FILL note and SYNC_SCALING is pressed
					else if (pendingNoteOnList.pendingNoteOns[i].fill == FillMode::NOT_FILL
					         && currentSong->isFillModeActive()) {
						conditionPassed = false;
					}
				}

				// probability, iterance and fill conditions have passed
				if (conditionPassed) {
					sendPendingNoteOn(modelStack, &pendingNoteOnList.pendingNoteOns[i]);
				}
				else {
					pendingNoteOnList.pendingNoteOns[i].noteRow->sequenced = false;
				}
			}
			else {
				pendingNoteOnList.pendingNoteOns[i].noteRow->sequenced = false;
			}
		}
	}

	if (ticksTilNextNoteRowEvent < playbackHandler.swungTicksTilNextEvent) {
		playbackHandler.swungTicksTilNextEvent = ticksTilNextNoteRowEvent;
	}
}

void InstrumentClip::sendPendingNoteOn(ModelStackWithTimelineCounter* modelStack, PendingNoteOn* pendingNoteOn) {

	ModelStackWithNoteRow* modelStackWithNoteRow =
	    modelStack->addNoteRow(pendingNoteOn->noteRowId, pendingNoteOn->noteRow);

	int16_t mpeValues[kNumExpressionDimensions];
	pendingNoteOn->noteRow->getMPEValues(modelStackWithNoteRow, mpeValues);

	if (output->type == OutputType::KIT) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings = modelStackWithNoteRow->addOtherTwoThings(
		    pendingNoteOn->noteRow->drum->toModControllable(), &pendingNoteOn->noteRow->paramManager);

		pendingNoteOn->noteRow->drum->kit->noteOnPreKitArp(modelStackWithThreeMainThings, pendingNoteOn->noteRow->drum,
		                                                   pendingNoteOn->velocity, mpeValues, MIDI_CHANNEL_NONE,
		                                                   pendingNoteOn->sampleSyncLength, pendingNoteOn->ticksLate);
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
	Action* action = actionLogger.getNewAction(ActionType::MISC);
	if (action) {
		void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceNoteRowMute));

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
ModelStackWithNoteRow* InstrumentClip::getNoteRowOnScreen(int32_t yDisplay, ModelStackWithTimelineCounter* modelStack) {
	int32_t noteRowIndex;
	NoteRow* noteRow = getNoteRowOnScreen(yDisplay, modelStack->song, &noteRowIndex);
	int32_t noteRowId;
	if (noteRow) {
		noteRowId = getNoteRowId(noteRow, noteRowIndex);
	}
	return modelStack->addNoteRow(noteRowId, noteRow);
}

/// can be called with null song if the clip is a kit
NoteRow* InstrumentClip::getNoteRowOnScreen(int32_t yDisplay, Song* song, int32_t* getIndex) {
	// Kit
	if (output->type == OutputType::KIT) {
		int32_t i = yDisplay + yScroll;
		if (i < 0 || i >= noteRows.getNumElements()) {
			return nullptr;
		}
		if (getIndex) {
			*getIndex = i;
		}
		return noteRows.getElement(i);
	}

	// Non-kit
	else {
		int32_t yNote = getYNoteFromYDisplay(yDisplay, song);
		return getNoteRowForYNote(yNote, getIndex);
	}
}

// Will set noteRow to NULL if one couldn't be found.
ModelStackWithNoteRow* InstrumentClip::getNoteRowForYNote(int32_t yNote, ModelStackWithTimelineCounter* modelStack) {
	int32_t noteRowIndex;
	NoteRow* noteRow = getNoteRowForYNote(yNote, &noteRowIndex);
	int32_t noteRowId;
	if (noteRow) {
		noteRowId = getNoteRowId(noteRow, noteRowIndex);
	}
	return modelStack->addNoteRow(noteRowId, noteRow);
}

NoteRow* InstrumentClip::getNoteRowForYNote(int32_t yNote, int32_t* getIndex) {

	int32_t i = noteRows.search(yNote, GREATER_OR_EQUAL);
	if (i < noteRows.getNumElements()) {
		NoteRow* noteRow = noteRows.getElement(i);
		if (noteRow->y == yNote) {
			if (getIndex) {
				*getIndex = i;
			}
			return noteRow;
		}
	}

	return nullptr;
}

// May set noteRow to NULL, of course.
// Will correctly do that if we're not a Kit Clip.
ModelStackWithNoteRow* InstrumentClip::getNoteRowForSelectedDrum(ModelStackWithTimelineCounter* modelStack) {
	int32_t noteRowId;
	NoteRow* noteRow = nullptr;
	if (output->type == OutputType::KIT) {
		Kit* kit = (Kit*)output;
		if (kit->selectedDrum) {
			noteRow = getNoteRowForDrum(kit->selectedDrum, &noteRowId);
		}
	}
	return modelStack->addNoteRow(noteRowId, noteRow);
}

ModelStackWithNoteRow* InstrumentClip::getNoteRowForDrum(ModelStackWithTimelineCounter* modelStack, Drum* drum) {
	int32_t noteRowId;
	NoteRow* noteRow = getNoteRowForDrum(drum, &noteRowId);
	return modelStack->addNoteRow(noteRowId, noteRow);
}

NoteRow* InstrumentClip::getNoteRowForDrum(Drum* drum, int32_t* getIndex) {

	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->drum == drum) {
			if (getIndex) {
				*getIndex = i;
			}
			return thisNoteRow;
		}
	}

	return nullptr;
}

// Should only be called for Kit Clips
ModelStackWithNoteRow* InstrumentClip::getNoteRowForDrumName(ModelStackWithTimelineCounter* modelStack,
                                                             char const* name) {

	int32_t i;
	NoteRow* thisNoteRow;

	for (i = 0; i < noteRows.getNumElements(); i++) {
		thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->drum && thisNoteRow->paramManager.containsAnyMainParamCollections()
		    && thisNoteRow->drum->type == DrumType::SOUND) {
			SoundDrum* thisDrum = (SoundDrum*)thisNoteRow->drum;

			if (thisDrum->name.equalsCaseIrrespective(name)) {
				goto foundIt;
			}
		}
	}

	thisNoteRow = nullptr;

foundIt:
	return modelStack->addNoteRow(i, thisNoteRow);
}

// Beware - this may change yScroll (via currentSong->setRootNote())
// *scaleAltered will not be set to false first - set it yourself. So that this can be called multiple times
ModelStackWithNoteRow* InstrumentClip::getOrCreateNoteRowForYNote(int32_t yNote,
                                                                  ModelStackWithTimelineCounter* modelStack,
                                                                  Action* action, bool* scaleAltered) {
	ModelStackWithNoteRow* modelStackWithNoteRow = getNoteRowForYNote(yNote, modelStack);

	// If one didn't already exist, create one
	if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
		int32_t noteRowIndex;
		NoteRow* thisNoteRow = noteRows.insertNoteRowAtY(yNote, &noteRowIndex);

		// If that created successfully (i.e. enough RAM)...
		if (thisNoteRow) {

			// Check that this yNote is allowed within our scale, if we have a scale. And if not allowed, then...
			if (!modelStack->song->isYNoteAllowed(yNote, inScaleMode)) {

				if (scaleAltered) {
					*scaleAltered = true;
				}

				// Recalculate the scale
				int32_t newI = thisNoteRow->notes.insertAtKey(
				    0); // Total hack - make it look like the NoteRow has a Note, so it doesn't get discarded during
				        // setRootNote(). We set it back (and then will soon give it a real note) really soon
				modelStack->song->setRootNote(modelStack->song->key.rootNote);

				thisNoteRow = getNoteRowForYNote(yNote); // Must re-get it
				if (ALPHA_OR_BETA_VERSION && !thisNoteRow) {
					FREEZE_WITH_ERROR("E -1");
				}

				thisNoteRow->notes.empty(); // Undo our "total hack", above

				if (action) {
					void* consMemory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceScaleAddNote));

					if (consMemory) {
						ConsequenceScaleAddNote* newConsequence =
						    new (consMemory) ConsequenceScaleAddNote((yNote + 120) % 12);
						action->addConsequence(newConsequence);
					}

					action->modeNotes[AFTER] = modelStack->song->key.modeNotes;
				}
			}

			modelStackWithNoteRow->setNoteRow(thisNoteRow, yNote);
		}
	}
	return modelStackWithNoteRow;
}

// I think you need to check (playbackHandler.isEitherClockActive() && song->isClipActive(thisClip)) before calling
// this.
void InstrumentClip::resumePlayback(ModelStackWithTimelineCounter* modelStack, bool mayMakeSound) {
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (!thisNoteRow->muted) {
			int32_t noteRowId = getNoteRowId(thisNoteRow, i);
			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);
			thisNoteRow->resumePlayback(modelStackWithNoteRow, mayMakeSound);
		}
	}
	expectEvent();
}

void InstrumentClip::expectNoFurtherTicks(Song* song, bool actuallySoundChange) {

	// If it's actually another Clip, that we're recording into the arranger...
	if (output->getActiveClip() && output->getActiveClip()->beingRecordedFromClip == this) {
		output->getActiveClip()->expectNoFurtherTicks(song, actuallySoundChange);
		return;
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack =
	    setupModelStackWithTimelineCounter(modelStackMemory, song, this); // TODO: make caller supply this

	stopAllNotesPlaying(modelStack, actuallySoundChange && !currentlyRecordingLinearly); // Stop all sound

	// Stop sequencer mode notes if active
	if (sequencerMode_) {
		sequencerMode_->stopAllNotes(modelStack);
	}

	// Also stop notes in cached sequencer modes
	for (auto& [name, mode] : cachedSequencerModes_) {
		if (mode) {
			mode->stopAllNotes(modelStack);
		}
	}

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);

	if (paramManager.containsAnyParamCollectionsIncludingExpression()) {
		paramManager.expectNoFurtherTicks(modelStackWithThreeMainThings);
	}

	if (output->type == OutputType::KIT) {
		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			if (thisNoteRow->drum && thisNoteRow->paramManager.containsAnyParamCollectionsIncludingExpression()) {
				ModelStackWithThreeMainThings* modelStackWithThreeMainThingsForNoteRow =
				    modelStack->addNoteRow(i, thisNoteRow)
				        ->addOtherTwoThings(thisNoteRow->drum->toModControllable(), &thisNoteRow->paramManager);
				thisNoteRow->paramManager.expectNoFurtherTicks(modelStackWithThreeMainThingsForNoteRow);
			}
		}
	}

	else if constexpr (PLAYBACK_STOP_SHOULD_CLEAR_MONO_EXPRESSION) {
		if (output->type == OutputType::SYNTH || output->type == OutputType::CV) {
			ParamCollectionSummary* expressionParamsSummary = paramManager.getExpressionParamSetSummary();
			if (expressionParamsSummary->paramCollection) {
				ModelStackWithParamCollection* modelStackWithParamCollection =
				    modelStackWithThreeMainThings->addParamCollectionSummary(expressionParamsSummary);

				((ExpressionParamSet*)modelStackWithParamCollection->paramCollection)
				    ->clearValues(modelStackWithParamCollection);
			}
		}
	}

	currentlyRecordingLinearly = false;
}

// Stops currently-playing notes by actually sending a note-off right now.
// Check that we're allowed to make sound before you call this (nowhere does, is that bad?)
void InstrumentClip::stopAllNotesPlaying(ModelStackWithTimelineCounter* modelStack, bool actuallySoundChange) {
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    modelStack->addNoteRow(getNoteRowId(thisNoteRow, i), thisNoteRow);
		thisNoteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow, actuallySoundChange);
	}
}

// Returns false in rare case that there wasn't enough ram to do this
NoteRow* InstrumentClip::createNewNoteRowForYVisual(int32_t yVisual, Song* song) {
	int32_t y = getYNoteFromYVisual(yVisual, song);
	return noteRows.insertNoteRowAtY(y);
}

// Returns false in rare case that there wasn't enough ram to do this
NoteRow* InstrumentClip::createNewNoteRowForKit(ModelStackWithTimelineCounter* modelStack, bool atStart,
                                                int32_t* getIndex) {

	int32_t index = atStart ? 0 : noteRows.getNumElements();

	Drum* newDrum = ((Kit*)output)->getFirstUnassignedDrum(this);

	NoteRow* newNoteRow = noteRows.insertNoteRowAtIndex(index);
	if (!newNoteRow) {
		return nullptr;
	}

	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(index, newNoteRow);

	newNoteRow->setDrum(newDrum, (Kit*)output, modelStackWithNoteRow); // It might end up NULL. That's fine

	if (atStart) {
		yScroll++;

		// Adjust colour offset, because colour offset is relative to the lowest NoteRow, and we just made a new lowest
		// one
		colourOffset--;
	}

	if (getIndex) {
		*getIndex = index;
	}
	return newNoteRow;
}

RGB InstrumentClip::getMainColourFromY(int32_t yNote, int8_t noteRowColourOffset) {
	return RGB::fromHue((yNote + colourOffset + noteRowColourOffset) * -8 / 3);
}

void InstrumentClip::replaceMusicalMode(const ScaleChange& changes, ModelStackWithTimelineCounter* modelStack) {
	if (!isScaleModeClip()) {
		return;
	}
	// Find all NoteRows which belong to this scale, and change their note
	//
	// TODO: There probably should not be _any_ rows which don't below to the
	// current scale? FREEZE_WITH_ERROR?
	MusicalKey key = modelStack->song->key;
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		int8_t degree = key.degreeOf(thisNoteRow->y);
		if (degree >= 0) {
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    modelStack->addNoteRow(getNoteRowId(thisNoteRow, i), thisNoteRow);

			thisNoteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow); // Otherwise we'd leave a MIDI note playing
			thisNoteRow->y += changes[degree];
		}
	}

	uint8_t oldSize = changes.source.scaleSize();
	uint8_t newSize = changes.target.scaleSize();

	// Which octave & scale degree was at the bottom of the view before scale change?
	int yOctave = yScroll / oldSize;
	int yDegree = yScroll - (yOctave * oldSize);
	// Take scale size changes into account and adjust yScroll to keep same octave visible
	yScroll = yOctave * newSize + yDegree;
}

void InstrumentClip::noteRemovedFromMode(int32_t yNoteWithinOctave, Song* song) {
	if (!isScaleModeClip()) {
		return;
	}

	for (int32_t i = 0; i < noteRows.getNumElements();) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		if ((thisNoteRow->y + 120) % 12 == yNoteWithinOctave) {
			noteRows.deleteNoteRowAtIndex(i);
		}
		else {
			i++;
		}
	}
}

void InstrumentClip::seeWhatNotesWithinOctaveArePresent(NoteSet& notesWithinOctavePresent, MusicalKey key) {
	for (int32_t i = 0; i < noteRows.getNumElements();) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		if (!thisNoteRow->hasNoNotes()) {
			notesWithinOctavePresent.add(key.intervalOf(thisNoteRow->getNoteCode()));
			i++;
		}

		// If this NoteRow has no notes, delete it, otherwise we'll have problems as the musical mode is changed
		else {
			noteRows.deleteNoteRowAtIndex(i);
		}
	}
}

/* Chromatic tranpose of all notes by fixed semitone amount */
void InstrumentClip::transpose(int32_t semitones, ModelStackWithTimelineCounter* modelStack) {
	// Make sure no notes sounding
	stopAllNotesPlaying(modelStack);

	// Must also do auditioned notes, since transpose can now be sequenced and change
	// noterows while we hold an audition pad.
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStack* modelStackWithSong = setupModelStackWithSong(modelStackMemory, currentSong);
	output->stopAnyAuditioning(modelStackWithSong);

	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		thisNoteRow->y += semitones;
	}

	yScroll += semitones;
	colourOffset -= semitones;
}

void InstrumentClip::nudgeNotesVertically(int32_t direction, VerticalNudgeType type,
                                          ModelStackWithTimelineCounter* modelStack) {
	// This method is limited to no more than an octave of "change", currently used
	// by the "hold and turn vertical encoder" and "shift + hold and turn vertical
	// encoder" shorcuts.

	if (!direction) {
		// It's not clear if we ever get "zero" as direction of change, but let's
		// make sure we behave sensibly in that case as well.
		return;
	}

	int32_t change = direction > 0 ? 1 : -1;
	if (type == VerticalNudgeType::OCTAVE) {
		if (isScaleModeClip()) {
			change *= modelStack->song->key.modeNotes.count();
		}
		else {
			change *= 12;
		}
	}

	// Make sure no notes sounding
	stopAllNotesPlaying(modelStack);

	if (!this->isScaleModeClip()) {
		// Non scale clip, transpose directly by semitone jumps
		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			// transpose by semitones or by octave
			thisNoteRow->y += change;
		}
	}
	else {
		// Scale clip, transpose by scale note jumps

		// wanting to change a full octave
		if (std::abs(change) == modelStack->song->key.modeNotes.count()) {
			int32_t changeInSemitones = (change > 0) ? 12 : -12;
			for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
				NoteRow* thisNoteRow = noteRows.getElement(i);
				// transpose by semitones or by octave
				thisNoteRow->y += changeInSemitones;
			}
		}

		// wanting to change less than an octave
		else {
			for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
				MusicalKey key = modelStack->song->key;
				NoteRow* thisNoteRow = noteRows.getElement(i);
				int32_t changeInSemitones = 0;
				int32_t yNoteWithinOctave = key.intervalOf(thisNoteRow->getNoteCode());
				int32_t oldModeNoteIndex = 0;
				for (; oldModeNoteIndex < key.modeNotes.count(); oldModeNoteIndex++) {
					if (key.modeNotes[oldModeNoteIndex] == yNoteWithinOctave) {
						break;
					}
				}
				int32_t newModeNoteIndex = (oldModeNoteIndex + change + modelStack->song->key.modeNotes.count())
				                           % modelStack->song->key.modeNotes.count();

				int32_t s = 0;
				if ((change > 0 && newModeNoteIndex > oldModeNoteIndex)
				    || (change < 0 && newModeNoteIndex < oldModeNoteIndex)) {
					// within the same octave
					changeInSemitones = modelStack->song->key.modeNotes[newModeNoteIndex]
					                    - modelStack->song->key.modeNotes[oldModeNoteIndex];
					s = 1;
				}
				else {
					if (change > 0) {
						// go up an octave
						changeInSemitones = modelStack->song->key.modeNotes[newModeNoteIndex]
						                    - modelStack->song->key.modeNotes[oldModeNoteIndex] + 12;
						s = 2;
					}
					else {
						// go down an octave
						changeInSemitones = modelStack->song->key.modeNotes[newModeNoteIndex]
						                    - modelStack->song->key.modeNotes[oldModeNoteIndex] - 12;
						s = 3;
					}
				}
				// transpose by semitones
				thisNoteRow->y += changeInSemitones;
			}
		}
	}
	yScroll += change;
}

// Lock rendering before calling this!
bool InstrumentClip::renderAsSingleRow(ModelStackWithTimelineCounter* modelStack, TimelineView* editorScreen,
                                       int32_t xScroll, uint32_t xZoom, RGB* image, uint8_t occupancyMask[],
                                       bool addUndefinedArea, int32_t noteRowIndexStart, int32_t noteRowIndexEnd,
                                       int32_t xStart, int32_t xEnd, bool allowBlur, bool drawRepeats) {

	AudioEngine::logAction("InstrumentClip::renderAsSingleRow");

	// Special case if we're a simple keyboard-mode Clip
	if (onKeyboardScreen && !containsAnyNotes()) {
		int32_t increment = (kDisplayWidth + (kDisplayHeight * keyboardState.isomorphic.rowInterval)) / kDisplayWidth;
		for (int32_t x = xStart; x < xEnd; x++) {
			image[x] = getMainColourFromY(keyboardState.isomorphic.scrollOffset + x * increment, 0);
		}
		return true;
	}

	Clip::renderAsSingleRow(modelStack, editorScreen, xScroll, xZoom, image, occupancyMask, addUndefinedArea,
	                        noteRowIndexStart, noteRowIndexEnd, xStart, xEnd, allowBlur, drawRepeats);

	noteRowIndexStart = std::max(noteRowIndexStart, 0_i32);
	noteRowIndexEnd = std::min(noteRowIndexEnd, noteRows.getNumElements());

	bool rowAllowsNoteTails;

	// Render every NoteRow into this, taking into account our search boundary
	for (int32_t i = noteRowIndexStart; i < noteRowIndexEnd; i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		if (!(i & 15)) {
			if (!AudioEngine::audioRoutineLocked) {
				// Sean: replace routineWithClusterLoading call, yield until AudioRoutine is called
				AudioEngine::routineBeenCalled = false;
				yield([]() { return (AudioEngine::routineBeenCalled == true); });
			}
			AudioEngine::logAction("renderAsSingleRow still");
		}

		int32_t yNote;

		if (output->type == OutputType::KIT) {
			yNote = i;
		}
		else {
			yNote = thisNoteRow->y;
		}

		RGB mainColour = getMainColourFromY(yNote, thisNoteRow->getColourOffset(this));
		RGB tailColour = mainColour.forTail();
		RGB blurColour = allowBlur ? mainColour.forBlur() : mainColour;
		if (i == noteRowIndexStart || output->type == OutputType::KIT) {
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    modelStack->addNoteRow(getNoteRowId(thisNoteRow, i), thisNoteRow);
			rowAllowsNoteTails = allowNoteTails(modelStackWithNoteRow);
		}

		thisNoteRow->renderRow(editorScreen, mainColour, tailColour, blurColour, image, occupancyMask, false,
		                       loopLength, rowAllowsNoteTails, kDisplayWidth, xScroll, xZoom, xStart, xEnd,
		                       drawRepeats);
	}
	if (addUndefinedArea) {
		drawUndefinedArea(xScroll, xZoom, loopLength, image, occupancyMask, kDisplayWidth, editorScreen,
		                  currentSong->tripletsOn);
	}

	return true;
}

int32_t InstrumentClip::getYVisualFromYNote(int32_t yNote, Song* song) { // TODO: this necessary?
	return song->getYVisualFromYNote(yNote, inScaleMode);
}

int32_t InstrumentClip::getYNoteFromYVisual(int32_t yVisual, Song* song) {
	if (output->type == OutputType::KIT) {
		return yVisual;
	}
	else {
		return song->getYNoteFromYVisual(yVisual, inScaleMode);
	}
}

int32_t InstrumentClip::guessRootNote(Song* song, int32_t previousRoot) {
	NoteSet notesPresent;

	// It's important this comes before noteRows.getNumElements(), since fetching
	// used notes also deletes empty note rows!
	seeWhatNotesWithinOctaveArePresent(notesPresent, song->key);

	// If no NoteRows, not much we can do
	if (noteRows.getNumElements() == 0) {
		return previousRoot;
	}

	previousRoot = previousRoot % 12;
	if (previousRoot < 0) {
		previousRoot += 12;
	}

	int32_t lowestNote = noteRows.getElement(0)->getNoteCode() % 12;
	if (lowestNote < 0) {
		lowestNote += 12;
	}

	uint8_t lowestIncompatibility = 255;
	uint8_t mostViableRoot = 0;

	// Go through each possible root note
	for (int32_t root = 0; root < 12; root++) {
		uint8_t incompatibility = 255;

		if (notesPresent.has(root)) { // || root == previousRoot) {
			// Assess viability of this being the root note
			uint8_t majorIncompatibility = 0;
			if (notesPresent.has((root + 1) % 12)) {
				majorIncompatibility++;
			}
			if (notesPresent.has((root + 3) % 12)) {
				majorIncompatibility += 2;
			}
			if (notesPresent.has((root + 6) % 12)) {
				majorIncompatibility++;
			}
			if (notesPresent.has((root + 8) % 12)) {
				majorIncompatibility++;
			}
			if (notesPresent.has((root + 10) % 12)) {
				majorIncompatibility++;
			}

			uint8_t minorIncompatibility = 0;
			if (notesPresent.has((root + 1) % 12)) {
				minorIncompatibility++;
			}
			if (notesPresent.has((root + 4) % 12)) {
				minorIncompatibility += 2;
			}
			if (notesPresent.has((root + 6) % 12)) {
				minorIncompatibility++;
			}
			if (notesPresent.has((root + 9) % 12)) {
				minorIncompatibility++;
			}
			if (notesPresent.has((root + 11) % 12)) {
				minorIncompatibility++;
			}

			incompatibility = std::min(majorIncompatibility, minorIncompatibility);
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

int32_t InstrumentClip::getNumNoteRows() {
	return noteRows.getNumElements();
}

Error InstrumentClip::setNonAudioInstrument(Instrument* newInstrument, Song* song, ParamManager* newParamManager) {

	// New addition - need expression params... hopefully fine?
	// Maybe this function should have the ability to do something equivalent to solicitParamManager(), for the purpose
	// of getting bend ranges from other Clips with same Instrument? Though it's an obscure requirement that's probably
	// hardly needed.
	if (newParamManager) {
		paramManager.stealParamCollectionsFrom(newParamManager, true);
	}

	if (newInstrument->type == OutputType::MIDI_OUT) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithModControllable* modelStack =
		    setupModelStackWithModControllable(modelStackMemory, song, this, newInstrument->toModControllable());
		restoreBackedUpParamManagerMIDI(modelStack);
		if (!paramManager.containsAnyMainParamCollections()) {
			Error error = paramManager.setupMIDI();
			if (error != Error::NONE) {
				if (ALPHA_OR_BETA_VERSION) {
					FREEZE_WITH_ERROR("E052");
				}
				return error;
			}
		}
	}
	output = newInstrument;
	affectEntire = true; // Moved here from changeInstrument, March 2021

	return Error::NONE;
}

// Does not set up patching!
Error InstrumentClip::setInstrument(Instrument* newInstrument, Song* song, ParamManager* newParamManager,
                                    InstrumentClip* favourClipForCloningParamManager) {

	// If MIDI or CV...
	if (newInstrument->type == OutputType::MIDI_OUT || newInstrument->type == OutputType::CV) {
		return setNonAudioInstrument(newInstrument, song, newParamManager);
	}

	// Or if Synth or Kit...
	return setAudioInstrument(
	    newInstrument, song, false, newParamManager,
	    favourClipForCloningParamManager); // Tell it not to setup patching - this will happen back here in
	                                       // changeInstrumentPreset() after all Drums matched up
}

void InstrumentClip::prepareToEnterKitMode(Song* song) {
	// Make sure all rows on screen have a NoteRow. Any RAM problems and we'll just quit
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		NoteRow* noteRow = getNoteRowOnScreen(yDisplay, song);
		if (!noteRow) {
			noteRow = createNewNoteRowForYVisual(yDisplay + yScroll, song);
			if (!noteRow) {
				return;
			}
		}
	}

	// Delete empty NoteRows that aren't onscreen
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		int32_t yDisplay = getYVisualFromYNote(thisNoteRow->y, song) - yScroll;

		if ((yDisplay < 0 || yDisplay >= kDisplayHeight) && thisNoteRow->hasNoNotes()) {
			noteRows.deleteNoteRowAtIndex(i);
		}
		else {
			i++;
		}
	}

	// Figure out the new scroll value
	if (noteRows.getNumElements()) {
		yScroll -= getYVisualFromYNote(noteRows.getElement(0)->y, song);
	}
	else {
		yScroll = 0;
	}
}

// Returns error code in theory - but in reality we're screwed if we get to that stage.
// newParamManager is optional - normally it's not supplied, and will be searched for
Error InstrumentClip::changeInstrument(ModelStackWithTimelineCounter* modelStack, Instrument* newInstrument,
                                       ParamManagerForTimeline* newParamManager,
                                       InstrumentRemoval instrumentRemovalInstruction,
                                       InstrumentClip* favourClipForCloningParamManager, bool keepNoteRowsWithMIDIInput,
                                       bool giveMidiAssignmentsToNewInstrument) {

	bool shouldBackUpExpressionParamsToo = false;

	// If switching to Kit
	if (newInstrument->type == OutputType::KIT) {

		// ... from non-Kit
		if (output->type != OutputType::KIT) {

			// Makes sure all NoteRows onscreen are populated, and deletes any empty NoteRows not onscreen.
			prepareToEnterKitMode(modelStack->song);

			shouldBackUpExpressionParamsToo =
			    true; // If switching from non-Kit to Kit, expression params won't get used, so store them with the
			          // backup in case the old MelodicInstrument gets used again later. Actually is this ideal?
		}
	}

	Instrument* oldInstrument = (Instrument*)output;
	int32_t oldYScroll = yScroll;

	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	AudioEngine::audioRoutineLocked = true;

	/* Further stuff to optimize in here:
	 * -- Delete surplus NoteRows in advance (must stop those Drums playing)
	 * -- Guess we could even search out the Drums with the names beforehand
	 * -- Allocate RAM beforehand
	 * -- Save ParamManagers to a quick list to properly back up later. And if we're deleting the Instrument, don't even
	 * end up doing that
	 */

	if (isActiveOnOutput() && playbackHandler.isEitherClockActive()) {
		expectNoFurtherTicks(modelStack->song); // Still necessary? Probably.
	}

	detachFromOutput(modelStack, true, (newInstrument->type == OutputType::KIT), false, keepNoteRowsWithMIDIInput,
	                 giveMidiAssignmentsToNewInstrument,
	                 shouldBackUpExpressionParamsToo); // Will unassignAllNoteRowsFromDrums(), and remember Drum names

	Error error =
	    setInstrument(newInstrument, modelStack->song, newParamManager,
	                  favourClipForCloningParamManager); // Tell it not to setup patching - this will happen back here
	                                                     // in changeInstrumentPreset() after all Drums matched up
	if (error != Error::NONE) {
		FREEZE_WITH_ERROR("E039");
		return error; // TODO: we'll need to get the old Instrument back...
	}

	// If a synth...
	if (newInstrument->type == OutputType::SYNTH) {

		SoundInstrument* synth = (SoundInstrument*)newInstrument;

		paramManager.getPatchCableSet()->grabVelocityToLevelFromMIDIInput(
		    &synth->midiInput); // Should happen before we call setupPatching().

		// Set up patching now. If a Kit, we do the drums individually below.
		synth->setupPatching(modelStack);
	}

	// If Clip (now) has a ParamManager (i.e. is not a CV Clip (wait, not anymore?)), set its pos now. Don't do it for
	// NoteRows yet - that happens as Drums are set, below
	if (playbackHandler.isEitherClockActive() && paramManager.mightContainAutomation()
	    && modelStack->song->isClipActive(this)) {
		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);
		paramManager.setPlayPos(getLivePos(), modelStackWithThreeMainThings, currentlyPlayingReversed);
	}

	// If newInstrument has no activeClip, we must set that right now before the audio routine is called - otherwise it
	// won't be able to find its ParamManager. This prevents a crash if we just navigated this Clip into this Instrument
	// and it already existed and had no Clips
	if (!newInstrument->getActiveClip()) {
		newInstrument->setActiveClip(modelStack, PgmChangeSend::NEVER);
	}

	// Can safely call audio routine again now
	AudioEngine::audioRoutineLocked = false;
	AudioEngine::bypassCulling = true;
	AudioEngine::logAction("bypassing culling in change instrument");
	AudioEngine::routineWithClusterLoading(); // -----------------------------------

	// If now a Kit, match NoteRows back up to Drums
	if (newInstrument->type == OutputType::KIT) {

		Kit* kit = (Kit*)newInstrument;
		kit->resetDrumTempValues();

		// For each NoteRow, see if one of the new Drums has the right name for it
		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
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
					if (giveMidiAssignmentsToNewInstrument) {
						thisNoteRow->giveMidiCommandsToDrum();
					}

					// And get out
					break;
				}
			}

			// TODO: we surely don't need to call this every time through
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
		}

		int32_t numNoteRowsDeletedFromBottom = (oldInstrument->type == OutputType::KIT) ? oldYScroll - yScroll : 0;

		assignDrumsToNoteRows(
		    modelStack, true,
		    numNoteRowsDeletedFromBottom); // If any unassigned Drums, give them to any NoteRows without a Drum - or
		                                   // create them a new NoteRow. Sets up patching

		// If changing from a kit to a kit, we may have ended up with 0 NoteRows. We do need to keep at least 1
		if (!noteRows.getNumElements()) {
			noteRows.insertNoteRowAtIndex(0);
		}
	}

	// Or if now a MelodicInstrument...
	else {

		// If the MelodicInstrument has an input MIDIDevice with bend range(s), we'll often want to grab those. The same
		// logic can be found in View::noteOnReceivedForMidiLearn().
		LearnedMIDI* midiInput = &((MelodicInstrument*)newInstrument)->midiInput;
		if (midiInput->containsSomething() && midiInput->cable) {
			MIDICable& cable = *midiInput->cable;

			int32_t zone = midiInput->channelOrZone - MIDI_CHANNEL_MPE_LOWER_ZONE;

			uint8_t newBendRanges[2];

			// MPE input
			if (zone >= 0) {
				newBendRanges[BEND_RANGE_MAIN] = cable.mpeZoneBendRanges[zone][BEND_RANGE_MAIN];
				newBendRanges[BEND_RANGE_FINGER_LEVEL] = cable.mpeZoneBendRanges[zone][BEND_RANGE_FINGER_LEVEL];

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
				newBendRanges[BEND_RANGE_MAIN] = cable.inputChannels[midiInput->channelOrZone].bendRange;
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
		if (oldInstrument->type == OutputType::KIT) {
			prepNoteRowsForExitingKitMode(modelStack->song);

			yScroll += getYVisualFromYNote(noteRows.getElement(0)->y, modelStack->song);
		}
	}

	// Dispose of old Instrument down here, now that we can breathe (we've done all the stuff above quickly because we
	// couldn't call the audio routine during it).
	if (instrumentRemovalInstruction == InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED) {
		modelStack->song->deleteOrHibernateOutputIfNoClips(oldInstrument);
	}
	else if (instrumentRemovalInstruction == InstrumentRemoval::DELETE) {
		modelStack->song->deleteOutputThatIsInMainList(oldInstrument);
	}

	return Error::NONE;
}

void InstrumentClip::deleteEmptyNoteRowsAtEitherEnd(bool onlyIfNoDrum, ModelStackWithTimelineCounter* modelStack,
                                                    bool mustKeepLastOne, bool keepOnesWithMIDIInput) {

	// Prioritize deleting from end of list first, cos this won't mess up scroll
	int32_t firstToDelete = noteRows.getNumElements();
	for (int32_t i = noteRows.getNumElements() - 1; i >= mustKeepLastOne; i--) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		// If we're keeping this one, stop searching
		if (!possiblyDeleteEmptyNoteRow(thisNoteRow, onlyIfNoDrum, modelStack->song, false, keepOnesWithMIDIInput)) {
			break;
		}

		firstToDelete = i;
	}

	int32_t numToDelete = noteRows.getNumElements() - firstToDelete;
	if (numToDelete > 0) {
		for (int32_t i = firstToDelete; i < noteRows.getNumElements(); i++) {
			NoteRow* noteRow = noteRows.getElement(i);
			if (noteRow->drum) {
				int32_t noteRowId = getNoteRowId(noteRow, i);
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);
				noteRow->setDrum(nullptr, (Kit*)output, modelStackWithNoteRow);
			}
		}
		noteRows.deleteNoteRowAtIndex(firstToDelete, numToDelete);
	}

	// Then try deleting from start
	int32_t firstToKeep = 0;
	for (int32_t i = 0; i < noteRows.getNumElements() - mustKeepLastOne; i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (!possiblyDeleteEmptyNoteRow(thisNoteRow, onlyIfNoDrum, modelStack->song, true, keepOnesWithMIDIInput)) {
			break;
		}

		firstToKeep = i + 1;
	}

	if (firstToKeep > 0) {
		for (int32_t i = 0; i < firstToKeep; i++) {
			NoteRow* noteRow = noteRows.getElement(i);
			if (noteRow->drum) {
				int32_t noteRowId = getNoteRowId(noteRow, i);
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, noteRow);
				noteRow->setDrum(nullptr, (Kit*)output, modelStackWithNoteRow);
			}
		}
		noteRows.deleteNoteRowAtIndex(0, firstToKeep);

		yScroll -= firstToKeep;
	}
}

void InstrumentClip::actuallyDeleteEmptyNoteRow(ModelStackWithNoteRow* modelStack) {
	NoteRow* noteRow = modelStack->getNoteRow();
	if (noteRow->drum) {
		noteRow->setDrum(nullptr, (Kit*)output, modelStack);
	}
	noteRow->~NoteRow();
	delugeDealloc(noteRow);
}

// Returns whether to delete it
bool InstrumentClip::possiblyDeleteEmptyNoteRow(NoteRow* noteRow, bool onlyIfNoDrum, Song* song, bool onlyIfNonNumeric,
                                                bool keepIfHasMIDIInput) {
	// If it has notes, our work is done
	if (!noteRow->hasNoNotes()) {
		return false;
	}

	// If MIDI assignment on NoteRow, keep it
	if (noteRow->midiInput.containsSomething() || noteRow->muteMIDICommand.containsSomething()) {
		return false;
	}

	Drum* drum = noteRow->drum;
	// If it has a drum, our work might be done, depending on what the caller wanted
	if (drum) {
		if (onlyIfNoDrum) {
			return false;
		}

		if (onlyIfNonNumeric && drum->type == DrumType::SOUND && stringIsNumericChars(((SoundDrum*)drum)->name.get())) {
			return false;
		}

		if (keepIfHasMIDIInput) {
			// If MIDI assignment on Drum, keep it
			if (drum->midiInput.containsSomething() || drum->muteMIDICommand.containsSomething()) {
				return false;
			}
		}
	}

	return true;
}

// Before calling this, you must ensure that each Drum's temp value represents whether it has a NoteRow assigned
void InstrumentClip::assignDrumsToNoteRows(ModelStackWithTimelineCounter* modelStack,
                                           bool shouldGiveMIDICommandsToDrums,
                                           int32_t numNoteRowsPreviouslyDeletedFromBottom) {

	Kit* kit = (Kit*)output;

	Drum* nextPotentiallyUnassignedDrum = kit->firstDrum;

	// We first need to know whether any NoteRows already have a Drum
	int32_t firstNoteRowToHaveADrum = -1;
	Drum* lowestDrumOnscreen = nullptr;
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->drum) {
			firstNoteRowToHaveADrum = i;
			lowestDrumOnscreen = thisNoteRow->drum;
			break;
		}
	}

	int32_t maxNumNoteRowsToInsertAtBottom;

	// If at least one NoteRow already did have a Drum, then we want to put the first unassigned drums (up til the first
	// assigned one) and their new NoteRows at the bottom of the screen
	if (firstNoteRowToHaveADrum >= 0) {

		// If first NoteRow already had a Drum, we can insert as many new ones below it as we want
		if (firstNoteRowToHaveADrum == 0) {
			maxNumNoteRowsToInsertAtBottom = 2147483647;

			// Otherwise, only allow enough new ones to be inserted that, combined with the drum-less ones at the
			// bottom, it'll take us up to the drum in question
		}
		else {
			maxNumNoteRowsToInsertAtBottom = kit->getDrumIndex(lowestDrumOnscreen) - firstNoteRowToHaveADrum;
		}

insertSomeAtBottom:
		int32_t numNoteRowsInsertedAtBottom = 0;

		while (nextPotentiallyUnassignedDrum && numNoteRowsInsertedAtBottom < maxNumNoteRowsToInsertAtBottom) {

			Drum* thisDrum = nextPotentiallyUnassignedDrum;
			nextPotentiallyUnassignedDrum = nextPotentiallyUnassignedDrum->next;

			// If this Drum is already assigned to a NoteRow...
			if (thisDrum->noteRowAssignedTemp) {
				break;
			}

			// Create the NoteRow
			NoteRow* newNoteRow = noteRows.insertNoteRowAtIndex(numNoteRowsInsertedAtBottom);
			if (!newNoteRow) {
				break;
			}

			int32_t noteRowId = getNoteRowId(newNoteRow, numNoteRowsInsertedAtBottom);
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
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (!thisNoteRow->drum) {

			if (!nextPotentiallyUnassignedDrum) {
noUnassignedDrumsLeft:
				anyNoteRowsRemainingWithoutDrum = true;
				continue;
			}

			while (nextPotentiallyUnassignedDrum->noteRowAssignedTemp) {
				nextPotentiallyUnassignedDrum = nextPotentiallyUnassignedDrum->next;
				if (!nextPotentiallyUnassignedDrum) {
					goto noUnassignedDrumsLeft;
				}
			}

			int32_t noteRowId = getNoteRowId(thisNoteRow, i);
			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);

			thisNoteRow->setDrum(nextPotentiallyUnassignedDrum, kit, modelStackWithNoteRow);
			nextPotentiallyUnassignedDrum = nextPotentiallyUnassignedDrum->next;

			if (shouldGiveMIDICommandsToDrums) {
				thisNoteRow->giveMidiCommandsToDrum();
			}
		}
	}

	// If any NoteRows with no Drum remain (which means more NoteRows than Drums), then delete them if they're at the
	// end of the list and are empty (but not if it's the last one left)
	if (anyNoteRowsRemainingWithoutDrum) {
		deleteEmptyNoteRowsAtEitherEnd(true, modelStack);
	}

	// Or, if all NoteRows which exist (possibly none) have a Drum, we'd better check if there are any Drums with no
	// NoteRow, and make them one
	else {

		for (; nextPotentiallyUnassignedDrum; nextPotentiallyUnassignedDrum = nextPotentiallyUnassignedDrum->next) {

			// If this Drum is already assigned to a NoteRow...
			if (nextPotentiallyUnassignedDrum->noteRowAssignedTemp) {
				continue;
			}

			// Create the NoteRow
			int32_t i = noteRows.getNumElements();
			NoteRow* newNoteRow = noteRows.insertNoteRowAtIndex(i);
			if (!newNoteRow) {
				break;
			}

			int32_t noteRowId = getNoteRowId(newNoteRow, i);
			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, newNoteRow);

			newNoteRow->setDrum(nextPotentiallyUnassignedDrum, kit, modelStackWithNoteRow);
		}
	}
}

void InstrumentClip::unassignAllNoteRowsFromDrums(ModelStackWithTimelineCounter* modelStack,
                                                  bool shouldRememberDrumNames, bool shouldRetainLinksToSounds,
                                                  bool shouldGrabMidiCommands, bool shouldBackUpExpressionParamsToo) {
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->drum) {
			if (shouldRememberDrumNames) {
				thisNoteRow->rememberDrumName();
			}
			AudioEngine::logAction("InstrumentClip::unassignAllNoteRowsFromDrums");
			AudioEngine::routineWithClusterLoading(); // -----------------------------------

			// If we're retaining links to Sounds, like if we're undo-ably "deleting" a Clip, just backup (and remove
			// link to) the paramManager
			if (shouldRetainLinksToSounds) {
				if (thisNoteRow->paramManager.containsAnyMainParamCollections()) {
					modelStack->song->backUpParamManager((SoundDrum*)thisNoteRow->drum, this,
					                                     &thisNoteRow->paramManager, shouldBackUpExpressionParamsToo);
				}
			}

			// Or, the more normal thing...
			else {
				if (shouldGrabMidiCommands) {
					thisNoteRow->grabMidiCommandsFromDrum();
				}

				int32_t noteRowId = getNoteRowId(thisNoteRow, i);
				ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);
				thisNoteRow->setDrum(nullptr, (Kit*)output, modelStackWithNoteRow);
			}
		}
	}
}

// Returns error code.
// Should only call for Kit Clips.
Error InstrumentClip::undoUnassignmentOfAllNoteRowsFromDrums(ModelStackWithTimelineCounter* modelStack) {
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* noteRow = noteRows.getElement(i);
		if (noteRow->drum && noteRow->drum->type == DrumType::SOUND) {

			bool success = modelStack->song->getBackedUpParamManagerPreferablyWithClip((SoundDrum*)noteRow->drum, this,
			                                                                           &noteRow->paramManager);

			if (!success) {
				if (ALPHA_OR_BETA_VERSION) {
					FREEZE_WITH_ERROR("E229");
				}
				return Error::BUG;
			}

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(i, noteRow);
			noteRow->trimParamManager(modelStackWithNoteRow);
		}
	}

	return Error::NONE;
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
	if (!backedUpParamManagerMIDI.containsAnyMainParamCollections()) {
		return;
	}

	paramManager.stealParamCollectionsFrom(&backedUpParamManagerMIDI);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = modelStack->addParamManager(&paramManager);

	paramManager.trimToLength(loopLength, modelStackWithThreeMainThings, nullptr,
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

	if (output->type == OutputType::MIDI_OUT) {
		if (paramManager.containsAnyMainParamCollections()) { // Wouldn't this always be? Or is there some case where we
			                                                  // might be calling this just after it's been created, and
			                                                  // no paramManager yet?
			setBackedUpParamManagerMIDI(&paramManager);
		}
	}
	else if (output->type != OutputType::CV) {

		if (output->type == OutputType::KIT) {

			if (shouldDeleteEmptyNoteRowsAtEitherEnd) { // Only true when called from changeInstrument()
				deleteEmptyNoteRowsAtEitherEnd(
				    false, modelStack, false,
				    keepNoteRowsWithMIDIInput); // Might call audio routine (?). Will back up ParamManagers for any
				                                // NoteRows deleted with Drums
				// That does not enforce keeping the last NoteRow. This is ok because we know if we're here that we're
				// remaining a Kit
			}
			unassignAllNoteRowsFromDrums(modelStack, shouldRememberDrumNames, shouldRetainLinksToOutput,
			                             shouldGrabMidiCommands, shouldBackUpExpressionParamsToo);
		}

		modelStack->song->backUpParamManager((ModControllableAudio*)output->toModControllable(), this, &paramManager,
		                                     shouldBackUpExpressionParamsToo);
	}

	if (!shouldRetainLinksToOutput) {
		output = nullptr;
	}
}

// Returns error code
Error InstrumentClip::undoDetachmentFromOutput(ModelStackWithTimelineCounter* modelStack) {

	// We really just need all our ParamManagers back

	if (output->type == OutputType::MIDI_OUT) {

		ModelStackWithModControllable* modelStackWithModControllable =
		    modelStack->addModControllableButNoNoteRow(output->toModControllable());
		restoreBackedUpParamManagerMIDI(modelStackWithModControllable);

		if (!paramManager.containsAnyMainParamCollections()) {
			if (ALPHA_OR_BETA_VERSION) {
				FREEZE_WITH_ERROR("E230");
			}
			return Error::BUG;
		}
	}
	else if (output->type != OutputType::CV) {

		if (output->type == OutputType::KIT) {
			Error error = undoUnassignmentOfAllNoteRowsFromDrums(modelStack);
			if (error != Error::NONE) {
				return error;
			}
		}

		return Clip::undoDetachmentFromOutput(modelStack);
	}

	return Error::NONE;
}

// If newInstrument is a Kit, you must call assignDrumsToNoteRows() after this
Error InstrumentClip::setAudioInstrument(Instrument* newInstrument, Song* song, bool shouldSetupPatching,
                                         ParamManager* newParamManager,
                                         InstrumentClip* favourClipForCloningParamManager) {

	output = newInstrument;
	affectEntire = (newInstrument->type != OutputType::KIT); // Moved here from changeInstrument, March 2021

	Error error = solicitParamManager(song, newParamManager, favourClipForCloningParamManager);
	if (error != Error::NONE) {
		return error;
	}

	// Arp stuff, so long as not a Kit (but remember, Sound/Synth is the only other option in this function)
	if (newInstrument->type == OutputType::SYNTH) {
		arpSettings.cloneFrom(&((SoundInstrument*)newInstrument)->defaultArpSettings);
	}
	else if (newInstrument->type == OutputType::KIT) {
		arpSettings.cloneFrom(&((Kit*)newInstrument)->defaultArpSettings);
	}

	if (shouldSetupPatching) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = setupModelStackWithTimelineCounter(modelStackMemory, song, this);
		((Instrument*)output)->setupPatching(modelStack);
	}

	return Error::NONE;
}

void InstrumentClip::writeDataToFile(Serializer& writer, Song* song) {

	writer.writeAttribute("clipName", name.get());
	writer.writeAttribute("inKeyMode", inScaleMode);
	writer.writeAttribute("yScroll", yScroll);
	writer.writeAttribute("yScrollKeyboard", keyboardState.isomorphic.scrollOffset);

	if (onKeyboardScreen) {
		writer.writeAttribute("onKeyboardScreen", 1);
	}
	if (onAutomationClipView) {
		writer.writeAttribute("onAutomationInstrumentClipView", 1);
	}
	if (lastSelectedParamID != kNoSelection) {
		writer.writeAttribute("lastSelectedParamID", lastSelectedParamID);
		writer.writeAttribute("lastSelectedParamKind", util::to_underlying(lastSelectedParamKind));
		writer.writeAttribute("lastSelectedParamShortcutX", lastSelectedParamShortcutX);
		writer.writeAttribute("lastSelectedParamShortcutY", lastSelectedParamShortcutY);
		writer.writeAttribute("lastSelectedInstrumentType", util::to_underlying(lastSelectedOutputType));
		writer.writeAttribute("lastSelectedPatchSource", util::to_underlying(lastSelectedPatchSource));
	}
	if (wrapEditing) {
		writer.writeAttribute("crossScreenEditLevel", wrapEditLevel);
	}
	if (output->type == OutputType::KIT) {
		writer.writeAttribute("affectEntire", affectEntire);
	}

	Instrument* instrument = (Instrument*)output;

	if (output->type == OutputType::MIDI_OUT) {
		writer.writeAttribute("midiChannel", ((MIDIInstrument*)instrument)->getChannel());

		if (((MIDIInstrument*)instrument)->channelSuffix != -1) {
			writer.writeAttribute("midiChannelSuffix", ((MIDIInstrument*)instrument)->channelSuffix);
		}

		// MIDI PGM
		if (midiBank != 128) {
			writer.writeAttribute("midiBank", midiBank);
		}
		if (midiSub != 128) {
			writer.writeAttribute("midiSub", midiSub);
		}
		if (midiPGM != 128) {
			writer.writeAttribute("midiPGM", midiPGM);
		}
	}
	else if (output->type == OutputType::CV) {
		writer.writeAttribute("cvChannel", ((CVInstrument*)instrument)->getChannel());
	}
	else {
		writer.writeAttribute("instrumentPresetName", output->name.get());

		if (!instrument->dirPath.isEmpty()) {
			writer.writeAttribute("instrumentPresetFolder", instrument->dirPath.get());
		}
	}

	Clip::writeDataToFile(writer, song);

	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	writer.writeAttribute("keyboardLayout", keyboardState.currentLayout);
	writer.writeAttribute("keyboardRowInterval", keyboardState.isomorphic.rowInterval);
	writer.writeAttribute("drumsScrollOffset", keyboardState.drums.scroll_offset);
	writer.writeAttribute("drumsZoomLevel", keyboardState.drums.zoom_level);
	writer.writeAttribute("inKeyScrollOffset", keyboardState.inKey.scrollOffset);
	writer.writeAttribute("inKeyRowInterval", keyboardState.inKey.rowInterval);

	writer.writeOpeningTagEnd();

	Clip::writeMidiCommandsToFile(writer, song);

	if (output->type == OutputType::MIDI_OUT) {
		paramManager.getMIDIParamCollection()->writeToFile(writer);
	}

	writer.writeOpeningTagBeginning("arpeggiator");

	arpSettings.writeCommonParamsToFile(writer, nullptr);

	if (output->type == OutputType::MIDI_OUT || output->type == OutputType::CV) {
		arpSettings.writeNonAudioParamsToFile(writer);
	}

	writer.closeTag();

	if (output->type == OutputType::KIT) {
		writer.writeOpeningTagBeginning("kitParams");
		GlobalEffectableForClip::writeParamAttributesToFile(writer, &paramManager, true);
		writer.writeOpeningTagEnd();
		GlobalEffectableForClip::writeParamTagsToFile(writer, &paramManager, true);
		writer.writeClosingTag("kitParams");
	}
	else if (output->type == OutputType::SYNTH) {
		writer.writeOpeningTagBeginning("soundParams");
		Sound::writeParamsToFile(writer, &paramManager, true);
		writer.writeClosingTag("soundParams", true);
	}

	if (output->type != OutputType::KIT) {
		ExpressionParamSet* expressionParams = paramManager.getExpressionParamSet();
		if (expressionParams) {
			expressionParams->writeToFile(writer);

			writer.writeTag("bendRange", expressionParams->bendRanges[BEND_RANGE_MAIN]);
			writer.writeTag("bendRangeMPE", expressionParams->bendRanges[BEND_RANGE_FINGER_LEVEL]);
		}
	}

	writer.writeOpeningTag("columnControls");
	keyboardState.columnControl.writeToFile(writer);
	writer.writeClosingTag("columnControls");

	if (noteRows.getNumElements()) {
		writer.writeArrayStart("noteRows");

		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			int32_t drumIndex = 65535;

			// If a Kit, and the drum isn't a GateDrum, see what Drum this NoteRow has
			if (output->type == OutputType::KIT && thisNoteRow->drum) {
				drumIndex = ((Kit*)output)->getDrumIndex(thisNoteRow->drum);
			}
			// no matching drum found
			if (drumIndex != -1) {
				thisNoteRow->writeToFile(writer, drumIndex, this);
			}
		}

		writer.writeArrayEnding("noteRows");
	}

	// Write sequencer mode data if active (for song save)
	if (hasSequencerMode()) {
		writer.writeOpeningTagBeginning("sequencerMode");
		writer.writeAttribute("mode", sequencerModeName_.c_str());
		writer.writeOpeningTagEnd();

		// Write the active sequencer mode's data
		sequencerMode_->writeToFile(writer, true); // Include scenes

		writer.writeClosingTag("sequencerMode");
	}
}

Error InstrumentClip::readFromFile(Deserializer& reader, Song* song) {

	Error error;

	if (false) {
ramError:

		error = Error::INSUFFICIENT_RAM;

someError:
		// Clear out all NoteRows of phony info stored where their drum pointer would normally go

		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			thisNoteRow->drum = nullptr;
		}

		return error;
	}
	reader.match('{');

	instrumentWasLoadedByReferenceFromClip = nullptr;

	char const* tagName;

	int16_t instrumentPresetSlot = 0;
	int8_t instrumentPresetSubSlot = -1;
	String instrumentPresetName;
	String instrumentPresetDirPath;
	bool dirPathHasBeenSpecified = false;

	int32_t readAutomationUpToPos = kMaxSequenceLength;

	while (*(tagName = reader.readNextTagOrAttributeName())) {
		// D_PRINTLN(tagName); delayMS(30);

		int32_t temp;

		if (!strcmp(tagName, "clipName")) {
			reader.readTagOrAttributeValueString(&name);
		}
		else if (!strcmp(tagName, "inKeyMode")) {
			inScaleMode = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "instrumentPresetSlot")) {
			int32_t slotHere = reader.readTagOrAttributeValueInt();
			String slotChars;
			slotChars.setInt(slotHere, 3);
			slotChars.concatenate(&instrumentPresetName);
			instrumentPresetName.set(&slotChars);
		}

		else if (!strcmp(tagName, "instrumentPresetSubSlot")) {
			int32_t subSlotHere = reader.readTagOrAttributeValueInt();
			if (subSlotHere >= 0 && subSlotHere < 26) {
				char buffer[2];
				buffer[0] = 'A' + subSlotHere;
				buffer[1] = 0;
				instrumentPresetName.concatenate(buffer);
			}
		}

		else if (!strcmp(tagName, "instrumentPresetName")) {
			reader.readTagOrAttributeValueString(&instrumentPresetName);
		}

		else if (!strcmp(tagName, "instrumentPresetFolder")) {
			reader.readTagOrAttributeValueString(&instrumentPresetDirPath);
			dirPathHasBeenSpecified = true;
		}

		else if (!strcmp(tagName, "midiChannel")) {
			outputTypeWhileLoading = OutputType::MIDI_OUT;

			// if (!instrument) instrument = reader.createNewNonAudioInstrument(OutputType::MIDI_OUT, 0, -1);
			instrumentPresetSlot = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "midiChannelSuffix")) {
			instrumentPresetSubSlot = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "cvChannel")) {
			outputTypeWhileLoading = OutputType::CV;

			// if (!instrument) instrument = reader.createNewNonAudioInstrument(OutputType::CV, 0, -1);
			instrumentPresetSlot = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "midiBank")) {
			midiBank = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "midiSub")) {
			midiSub = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "midiPGM")) {
			midiPGM = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "yScroll")) {
			yScroll = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "keyboardLayout")) {
			keyboardState.currentLayout = (KeyboardLayoutType)reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "yScrollKeyboard")) {
			keyboardState.isomorphic.scrollOffset = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "keyboardRowInterval")) {
			keyboardState.isomorphic.rowInterval = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "drumsScrollOffset")) {
			keyboardState.drums.scroll_offset = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "drumsZoomLevel")) {
			keyboardState.drums.zoom_level = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "inKeyScrollOffset")) {
			keyboardState.inKey.scrollOffset = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "inKeyRowInterval")) {
			keyboardState.inKey.rowInterval = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "crossScreenEditLevel")) {
			wrapEditLevel = reader.readTagOrAttributeValueInt();
			wrapEditing = true;
		}

		else if (!strcmp(tagName, "onKeyboardScreen")) {
			onKeyboardScreen = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "onAutomationInstrumentClipView")) {
			onAutomationClipView = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "lastSelectedParamID")) {
			lastSelectedParamID = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "lastSelectedParamKind")) {
			lastSelectedParamKind = static_cast<params::Kind>(reader.readTagOrAttributeValueInt());
		}

		else if (!strcmp(tagName, "lastSelectedParamShortcutX")) {
			lastSelectedParamShortcutX = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "lastSelectedParamShortcutY")) {
			lastSelectedParamShortcutY = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "lastSelectedParamArrayPosition")) {
			lastSelectedParamArrayPosition = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "lastSelectedInstrumentType")) {
			lastSelectedOutputType = static_cast<OutputType>(reader.readTagOrAttributeValueInt());
		}

		else if (!strcmp(tagName, "lastSelectedPatchSource")) {
			lastSelectedPatchSource = static_cast<PatchSource>(reader.readTagOrAttributeValueInt());
		}

		else if (!strcmp(tagName, "affectEntire")) {
			affectEntire = reader.readTagOrAttributeValueInt();
		}

		else if (!strcmp(tagName, "soundMidiCommand")) { // Only for pre V2.0 song files
			soundMidiCommand.readChannelFromFile(reader);
		}

		else if (!strcmp(tagName, "modKnobs")) { // Pre V2.0 only - for compatibility

			outputTypeWhileLoading = OutputType::MIDI_OUT;

			output = song->getInstrumentFromPresetSlot(OutputType::MIDI_OUT, instrumentPresetSlot,
			                                           instrumentPresetSubSlot, nullptr, nullptr, false);
			if (!output) {
				output = StorageManager::createNewNonAudioInstrument(OutputType::MIDI_OUT, instrumentPresetSlot,
				                                                     instrumentPresetSubSlot);

				if (!output) {
					goto ramError;
				}
				song->addOutput(output);
			}

			Error error = paramManager.setupMIDI();
			if (error != Error::NONE) {
				return error;
			}

			error = ((MIDIInstrument*)output)->readModKnobAssignmentsFromFile(readAutomationUpToPos, &paramManager);
			if (error != Error::NONE) {
				return error;
			}

			if (loopLength) {
				paramManager.getMIDIParamCollection()->makeInterpolatedCCsGoodAgain(loopLength);
			}
		}

		else if (!strcmp(tagName, "arpeggiator")) {
			reader.match('{');
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				bool readAndExited = arpSettings.readCommonTagsFromFile(reader, tagName, nullptr);
				if (!readAndExited
				    && (outputTypeWhileLoading == OutputType::MIDI_OUT || outputTypeWhileLoading == OutputType::CV)) {
					readAndExited = arpSettings.readNonAudioTagsFromFile(reader, tagName);
				}

				if (!readAndExited) {
					reader.exitTag(tagName);
				}
			}
			reader.match('}'); // End arpeggiator value object.
		}

		// For song files from before V2.0, where Instruments were stored within the Clip.
		// Loading Instrument from another Clip.
		else if (!strcmp(tagName, "instrument")) {
			reader.match('{');
			if (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "referToTrackId")) {
					if (!output) {
						int32_t clipId = reader.readTagOrAttributeValueInt();
						clipId = std::max((int32_t)0, clipId);
						if (clipId >= song->sessionClips.getNumElements()) {
							error = Error::FILE_CORRUPTED;
							goto someError;
						}
						instrumentWasLoadedByReferenceFromClip =
						    (InstrumentClip*)song->sessionClips.getClipAtIndex(clipId);
						output = instrumentWasLoadedByReferenceFromClip->output;
						if (!output) {
							error = Error::FILE_CORRUPTED;
							goto someError;
						}
						outputTypeWhileLoading = output->type;
						if (outputTypeWhileLoading == OutputType::SYNTH) {
							arpSettings.cloneFrom(&((SoundInstrument*)output)->defaultArpSettings);
						}
						else if (outputTypeWhileLoading == OutputType::KIT) {
							arpSettings.cloneFrom(&((Kit*)output)->defaultArpSettings);
						}
					}
					reader.exitTag("referToTrackId");
				}
			}
		}

		// For song files from before V2.0, where Instruments were stored within the Clip
		else if (!strcmp(tagName, "sound") || !strcmp(tagName, "synth")) {
			if (!output) {
				{
					void* instrumentMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(SoundInstrument));
					if (!instrumentMemory) {
						goto ramError;
					}

					outputTypeWhileLoading = OutputType::SYNTH;

					SoundInstrument* soundInstrument = new (instrumentMemory) SoundInstrument();
					error = soundInstrument->dirPath.set("SYNTHS");
					if (error != Error::NONE) {
						goto someError; // Default, in case not included in file.
					}
					output = soundInstrument;
				}

loadInstrument:
				error = output->readFromFile(reader, song, this, readAutomationUpToPos);
				if (error != Error::NONE) {
					goto someError;
				}

				if (outputTypeWhileLoading == OutputType::SYNTH) {
					arpSettings.cloneFrom(&((SoundInstrument*)output)->defaultArpSettings);
				}
				else if (outputTypeWhileLoading == OutputType::KIT) {
					arpSettings.cloneFrom(&((Kit*)output)->defaultArpSettings);
				}

				// Add the Instrument to the Song
				song->addOutput(output);
			}
		}

		// For song files from before V2.0, where Instruments were stored within the Clip
		else if (!strcmp(tagName, "kit")) {
			if (!output) {
				void* instrumentMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(Kit));
				if (!instrumentMemory) {
					goto ramError;
				}

				outputTypeWhileLoading = OutputType::KIT;
				Kit* kit = new (instrumentMemory) Kit();
				error = kit->dirPath.set("KITS");
				if (error != Error::NONE) {
					goto someError; // Default, in case not included in file.
				}
				output = kit;
				goto loadInstrument;
			}
		}

		else if (!strcmp(tagName, "soundParams")) {
			outputTypeWhileLoading = OutputType::SYNTH;

			// Normal case - load in brand new ParamManager

			if (song_firmware_version >= FirmwareVersion::official({1, 2, 0}) || !output) {
createNewParamManager:
				error = paramManager.setupWithPatching();
				if (error != Error::NONE) {
					goto someError;
				}
				Sound::initParams(&paramManager);
			}

			// Slight hack to fix crash with late-2016-ish songs
			else {

				ParamManager* otherParamManager = song->getBackedUpParamManagerPreferablyWithClip(
				    (ModControllableAudio*)output->toModControllable(), this);
				if (!otherParamManager) {
					goto createNewParamManager;
				}
				error = paramManager.cloneParamCollectionsFrom(otherParamManager, false);
				if (error != Error::NONE) {
					goto someError;
				}
			}
			Sound::readParamsFromFile(reader, &paramManager, readAutomationUpToPos);
		}

		else if (!strcmp(tagName, "kitParams")) {
			outputTypeWhileLoading = OutputType::KIT;
			error = paramManager.setupUnpatched();
			if (error != Error::NONE) {
				goto someError;
			}

			GlobalEffectableForClip::initParams(&paramManager);
			reader.match('{');
			GlobalEffectableForClip::readParamsFromFile(reader, &paramManager, readAutomationUpToPos);
			reader.match('}');
		}

		else if (!strcmp(tagName, "midiParams")) {
			outputTypeWhileLoading = OutputType::MIDI_OUT;
			error = paramManager.setupMIDI();
			if (error != Error::NONE) {
				goto someError;
			}

			error = readMIDIParamsFromFile(reader, readAutomationUpToPos);
			if (error != Error::NONE) {
				goto someError;
			}
		}

		else if (!strcmp(tagName, "noteRows")) {
			reader.match('[');
			int32_t minY = -32768;
			while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "noteRow")) {
					NoteRow* newNoteRow = noteRows.insertNoteRowAtIndex(noteRows.getNumElements());
					if (!newNoteRow) {
						goto ramError;
					}
					error = newNoteRow->readFromFile(reader, &minY, this, song, readAutomationUpToPos);
					if (error != Error::NONE) {
						goto someError;
					}
				}
				reader.match('}');             // leave value object.
				reader.exitTag(nullptr, true); // leave box.
			}
			reader.match(']');
		}

		// Sequencer mode data (for song loading)
		else if (!strcmp(tagName, "sequencerMode")) {
			char const* modeName = nullptr;

			// Read sequencer mode attributes first
			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "mode")) {
					modeName = reader.readTagOrAttributeValue();

					// Set the sequencer mode based on the mode name
					if (modeName) {
						if (!hasSequencerMode() || getSequencerModeName() != modeName) {
							setSequencerMode(modeName);
						}
					}
				}
				else if (!strcmp(tagName, "controlColumns")) {
					// Handle control columns directly (sequencer modes delegate this back)
					if (sequencerMode_) {
						error = sequencerMode_->getControlColumnState().readFromFile(reader);
						if (error != Error::NONE) {
							goto someError;
						}
					}
					else {
						reader.exitTag(tagName);
					}
				}
				else {
					// This is a child element, let the sequencer mode handle it
					if (sequencerMode_) {
						error = sequencerMode_->readFromFile(reader);
						if (error != Error::NONE) {
							goto someError;
						}
					}
					else {
						reader.exitTag(tagName);
					}
				}
			}
		}

		// These are the expression params for MPE
		else if (!strcmp(tagName, "pitchBend")) {
			temp = 0;
doReadExpressionParam:
			paramManager.ensureExpressionParamSetExists();
			ParamCollectionSummary* summary = paramManager.getExpressionParamSetSummary();
			ExpressionParamSet* expressionParams = (ExpressionParamSet*)summary->paramCollection;
			if (expressionParams) {
				expressionParams->readParam(reader, summary, temp, readAutomationUpToPos);
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
				expressionParams->readFromFile(reader, summary, readAutomationUpToPos);
			}
		}

		else if (!strcmp(tagName, "bendRange")) {
			temp = BEND_RANGE_MAIN;
doReadBendRange:
			ExpressionParamSet* expressionParams = paramManager.getOrCreateExpressionParamSet();
			if (expressionParams) {
				expressionParams->bendRanges[temp] = reader.readTagOrAttributeValueInt();
			}
		}

		else if (!strcmp(tagName, "bendRangeMPE")) {
			temp = BEND_RANGE_FINGER_LEVEL;
			goto doReadBendRange;
		}
		else if (!strcmp(tagName, "columnControls")) {
			keyboardState.columnControl.readFromFile(reader);
		}
		else {
			readTagFromFile(reader, tagName, song, &readAutomationUpToPos);
		}

		reader.exitTag();
	}
	reader.match('}'); // Close values object.
	// Some stuff for song files before V2.0, where the Instrument would have been loaded at this point

	// For song files from before V2.0, where Instruments were stored within the Clip (which was called a Track back
	// then)
	if (output) {
		if (!instrumentWasLoadedByReferenceFromClip) {
			switch (output->type) {
			case OutputType::MIDI_OUT:
				((MIDIInstrument*)output)->channelSuffix = std::clamp<int8_t>(instrumentPresetSubSlot, -1, 25);
				[[fallthrough]];
				// No break

			case OutputType::CV:
				((CVInstrument*)output)->setChannel(std::clamp<int32_t>(instrumentPresetSlot, 0, kNumInstrumentSlots));
				break;

			case OutputType::SYNTH:
			case OutputType::KIT:
				((Instrument*)output)->name.set(&instrumentPresetName);
				break;

			default:
				__builtin_unreachable();
			}
		}

		// If we loaded an audio Instrument (with a file from before V2.0)
		if (output->type != OutputType::MIDI_OUT && output->type != OutputType::CV) {

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

					paramManager.trimToLength(loopLength, modelStackWithThreeMainThings, nullptr,
					                          false); // oldLength actually has no consequence anyway
				}

				// If there wasn't one, that's because another Clip already took it. Clone it from that Clip.
				else {

					// It can happen that a ParamManager was never created for a Kit (pre V2.0, or perhaps only
					// in 1.0?). Just create one now.
					if (!instrumentWasLoadedByReferenceFromClip && output->type == OutputType::KIT) {

						error = paramManager.setupUnpatched();
						if (error != Error::NONE) {
							goto someError;
						}

						GlobalEffectableForClip::initParams(&paramManager);
					}

					else {
						if (!instrumentWasLoadedByReferenceFromClip
						    || !instrumentWasLoadedByReferenceFromClip->paramManager
						            .containsAnyMainParamCollections()) {
							error = Error::FILE_CORRUPTED;
							goto someError;
						}
						error = paramManager.cloneParamCollectionsFrom(
						    &instrumentWasLoadedByReferenceFromClip->paramManager,
						    false); // No need to trim - param automation didn't exist back then
						if (error != Error::NONE) {
							goto someError;
						}
					}
				}
			}
		}
	}

	// Pre V3.2.0 (and also for some of 3.2's alpha phase), bend range wasn't adjustable, wasn't written in the file,
	// and was always 12.
	if (song_firmware_version <= FirmwareVersion::official({3, 2, 0, "alpha"})
	    && !paramManager.getExpressionParamSet()) {
		ExpressionParamSet* expressionParams = paramManager.getOrCreateExpressionParamSet();
		if (expressionParams) {
			expressionParams->bendRanges[BEND_RANGE_MAIN] = 12;
		}
	}

	const size_t outputTypeWhileLoadingAsIdx = static_cast<size_t>(outputTypeWhileLoading);
	switch (outputTypeWhileLoading) {
	case OutputType::SYNTH:
	case OutputType::KIT:
		backedUpInstrumentName[outputTypeWhileLoadingAsIdx].set(&instrumentPresetName);
		if (dirPathHasBeenSpecified) {
			backedUpInstrumentDirPath[outputTypeWhileLoadingAsIdx].set(&instrumentPresetDirPath);
		}
		else {
			// Where dir path has not been specified (i.e. before V4.0.0), go with the default. The same has been done
			// to the Instruments which this Clip will get matched against.
			error =
			    backedUpInstrumentDirPath[outputTypeWhileLoadingAsIdx].set(getInstrumentFolder(outputTypeWhileLoading));
			if (error != Error::NONE) {
				return error;
			}
		}
		break;

	case OutputType::MIDI_OUT:
	case OutputType::CV:
		backedUpInstrumentSlot[outputTypeWhileLoadingAsIdx] = instrumentPresetSlot;
		backedUpInstrumentSubSlot[outputTypeWhileLoadingAsIdx] = instrumentPresetSubSlot;
		break;

	default:
		__builtin_unreachable();
	}

	return Error::NONE;
}

Error InstrumentClip::readMIDIParamsFromFile(Deserializer& reader, int32_t readAutomationUpToPos) {

	char const* tagName;

	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "param")) {
			// Error error = reader.readMIDIParamFromFile(readAutomationUpToPos, this);
			// if (error != Error::NONE) return error;

			char const* tagName;
			int32_t paramId = CC_NUMBER_NONE;
			AutoParam* param = nullptr;
			ParamCollectionSummary* summary;
			ExpressionParamSet* expressionParams = nullptr;

			while (*(tagName = reader.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "cc")) {
					char const* contents = reader.readTagOrAttributeValue();
					if (!strcasecmp(contents, "bend")) {
						paramId = X_PITCH_BEND;
expressionParam:
						// If we're here, we're reading a pre-V3.2 file, and need to read what we're now regarding as
						// "expression".
						if (!paramManager.ensureExpressionParamSetExists()) {
							return Error::INSUFFICIENT_RAM;
						}
						summary = paramManager.getExpressionParamSetSummary();
						expressionParams = (ExpressionParamSet*)summary->paramCollection;
						param = &expressionParams->params[paramId];
					}
					else if (!strcasecmp(contents, "aftertouch")) {
						paramId = Z_PRESSURE;
						goto expressionParam;
					}
					else if (!strcasecmp(contents, "none")) {
						// We used to write 120 for "none", pre V2.0, but that's now bend
						paramId = CC_NUMBER_NONE;
					}
					else {
						paramId = stringToInt(contents);
						if (paramId < kNumRealCCNumbers) {
							if (paramId == CC_EXTERNAL_MOD_WHEEL) {
								// m-m-adams - used to convert CC74 to y-axis, and I don't think that would
								// ever have been desireable. Now convert mod wheel, as mono y axis outputs as mod wheel
								if (song_firmware_version < FirmwareVersion::community({1, 1, 0})) {
									paramId = Y_SLIDE_TIMBRE;
									goto expressionParam;
								}
							}
							MIDIParam* midiParam =
							    paramManager.getMIDIParamCollection()->params.getOrCreateParamFromCC(paramId, 0);
							if (!midiParam) {
								return Error::INSUFFICIENT_RAM;
							}
							param = &midiParam->param;
						}
					}
					reader.exitTag("cc");
				}
				else if (!strcmp(tagName, "value")) {
					if (param) {

						Error error = param->readFromFile(reader, readAutomationUpToPos);
						if (error != Error::NONE) {
							return error;
						}

						if (expressionParams) {
							// Most other times you don't have to think about calling this. It's just because we didn't
							// know which ParamCollection we were gonna load into, and MIDIParamCollection doesn't keep
							// track of automation.
							if (param->isAutomated()) {
								expressionParams->paramHasAutomationNow(summary, paramId);
							}

							// If channel pressure, gotta move and scale the values from how they were in the pre-V3.2
							// firmware
							if (paramId) {
								param->transposeCCValuesToChannelPressureValues();
							}

							// Or if pitch bend, it'll no longer interpolate, so go place some new nodes. Actually even
							// without this step, you can only just tell there's any problem.
							else {
								param->makeInterpolationGoodAgain(
								    loopLength,
								    22); // 22 is picked somewhat arbitrarily - see comment for function itself.
							}
						}
					}
					reader.exitTag("value");
				}
				else {
					reader.exitTag(tagName);
				}
			}

			reader.exitTag("param");
		}
		else {
			reader.exitTag(tagName);
		}
	}

	return Error::NONE;
}

// This function also unassigns individual NoteRows from their "sound" MIDI commands
void InstrumentClip::prepNoteRowsForExitingKitMode(Song* song) {

	// If for some reason no NoteRows, just return. This shouldn't ever happen
	if (noteRows.getNumElements() == 0) {
		return;
	}

	// We want to select one NoteRow, pinned to a yNote

	NoteRow* chosenNoteRow = nullptr;
	int32_t chosenNoteRowIndex;
	MusicalKey key = song->key;

	// If we're in scale mode...
	if (inScaleMode) {

		// See if any NoteRows are a root note
		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			if (thisNoteRow->y != -32768 && key.intervalOf(thisNoteRow->y) == 0) {
				chosenNoteRow = thisNoteRow;
				chosenNoteRowIndex = i;
				break;
			}
		}
	}

	// If none found yet, just grab the first one with a "valid" yNote
	if (!chosenNoteRow) {

		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			if (thisNoteRow->y != -32768) {

				// But, if we're in key-mode, make sure this yNote fits within the scale!
				if (inScaleMode) {
					uint8_t yNoteWithinOctave = key.intervalOf(thisNoteRow->y);

					// Make sure this yNote fits the scale/mode
					if (!song->key.modeNotes.has(yNoteWithinOctave)) {
						goto noteRowFailed;
					}
				}

				chosenNoteRow = thisNoteRow;
				chosenNoteRowIndex = i;
				break;
			}
noteRowFailed: {}
		}
	}

	// Occasionally we get a crazy scroll value. Not sure how. It happened to Jon Hutton
	if (chosenNoteRow) {
		if (chosenNoteRow->y < -256 || chosenNoteRow->y >= 256) {
			goto useRootNote; // Can't use isScrollWithinRange, cos that relies on existing note positions, which are
			                  // messed up
		}
	}

	// If still none, just pick the first one
	else {
		chosenNoteRow = noteRows.getElement(0);
		chosenNoteRowIndex = 0;
useRootNote:
		chosenNoteRow->y = (song->key.rootNote % 12) + 60; // Just do this even if we're not in key-mode
	}

	// Now, give all the other NoteRows yNotes
	int32_t chosenNoteRowYVisual = song->getYVisualFromYNote(chosenNoteRow->y, inScaleMode);

	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (i != chosenNoteRowIndex) {
			thisNoteRow->y = song->getYNoteFromYVisual(chosenNoteRowYVisual - chosenNoteRowIndex + i, inScaleMode);
		}
	}
}

// Returns whether whole Clip should be deleted
bool InstrumentClip::deleteSoundsWhichWontSound(Song* song) {

	deleteBackedUpParamManagerMIDI();

	if (output->type == OutputType::KIT) {
		Kit* kit = (Kit*)output;

		bool clipIsActive = song->isClipActive(this);

		for (int32_t i = 0; i < noteRows.getNumElements();) {
			NoteRow* noteRow = noteRows.getElement(i);

			// If the NoteRow isn't gonna make any more sound...
			if ((!clipIsActive || noteRow->muted || noteRow->hasNoNotes())
			    // ...and it doesn't have a currently still-rendering Drum Sound
			    && (!noteRow->drum || noteRow->drum->type != DrumType::SOUND
			        || ((SoundDrum*)noteRow->drum)->skippingRendering)
			    && (!noteRow->drum || noteRow->drum->type != DrumType::SOUND
			        || (SoundDrum*)noteRow->drum != view.activeModControllableModelStack.modControllable)) {

				// OI!! Don't nest any of those conditions inside other if statements. We need the "else" below to take
				// effect. Thanks

				// We'd ultimately love to just delete the Drum. But beware that multiple NoteRows in different Clips
				// may have the same Drum. We used to just delete it, leading to a crash sometimes! Now, if we just do
				// this for the active Clip, it should be ok right, cos no other Clip is going to be doing anything on
				// its NoteRow?
				if (clipIsActive && noteRow->drum) {

					if (ALPHA_OR_BETA_VERSION && noteRow->drum->type == DrumType::SOUND
					    && static_cast<SoundDrum*>(noteRow->drum)->hasActiveVoices()) {
						FREEZE_WITH_ERROR("E176");
					}

					Drum* drum = noteRow->drum;
					kit->removeDrum(drum);

					void* toDealloc = dynamic_cast<void*>(drum);
					drum->~Drum();
					delugeDealloc(toDealloc);
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

	// For MelodicInstruments, we can delete the Clip (which we know is active on the Instrument) if the Clip is
	// inactive in the Song and the Instrument isn't still rendering anything
	else {
		return Clip::deleteSoundsWhichWontSound(song);
	}
}

// Will cause serious problems if the NoteRow doesn't exist in here
void InstrumentClip::deleteNoteRow(ModelStackWithTimelineCounter* modelStack, int32_t noteRowIndex) {

	NoteRow* noteRow = noteRows.getElement(noteRowIndex);

	ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(getNoteRowId(noteRow, noteRowIndex), noteRow);

	noteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow);

	Kit* kit = (Kit*)output;
	kit->removeDrumFromKitArpeggiator(noteRowIndex);

	noteRow->setDrum(nullptr, (Kit*)output, modelStackWithNoteRow);
	noteRows.deleteNoteRowAtIndex(noteRowIndex);
}

void InstrumentClip::stopAllNotesForMIDIOrCV(ModelStackWithTimelineCounter* modelStack) {

	// This didn't use to be here but seems a good idea. Call this so that any drone notes will restart next loop
	// around. Also, not all synths necessarily support the all-notes-off message, which the further below sends.
	stopAllNotesPlaying(modelStack);

	// And then we still need this but in case any notes have been sent out via audition, or I guess being echoed thru

	// CV - easy
	if (output->type == OutputType::CV) {
		cvEngine.sendNote(false, ((CVInstrument*)output)->getChannel());
	}

	// MIDI - hard
	else if (output->type == OutputType::MIDI_OUT) {
		((MIDIInstrument*)output)->allNotesOff();
	}
}

int16_t InstrumentClip::getTopYNote() {
	if (noteRows.getNumElements() == 0) {
		return 64;
	}
	return noteRows.getElement(noteRows.getNumElements() - 1)->y;
}

int16_t InstrumentClip::getBottomYNote() {
	if (noteRows.getNumElements() == 0) {
		return 64;
	}
	return noteRows.getElement(0)->y;
}

uint32_t InstrumentClip::getWrapEditLevel() {
	return wrapEditing ? wrapEditLevel : kMaxSequenceLength; // Used to return the Clip length in this case, but that
	                                                         // causes problems now that NoteRows may be longer.
}

bool InstrumentClip::hasSameInstrument(InstrumentClip* otherClip) {
	return (output == otherClip->output);
}

bool InstrumentClip::isScaleModeClip() {
	return (inScaleMode && output->type != OutputType::KIT);
}

// TODO: this should be a virtual function in Instrument
// modelStack could contain a NULL noteRow if there isn't one - e.g. in a Synth Clip
bool InstrumentClip::allowNoteTails(ModelStackWithNoteRow* modelStack) {
	if (output->type == OutputType::MIDI_OUT || output->type == OutputType::CV) {
		return true;
	}

	if (output->type == OutputType::SYNTH) {
		ModelStackWithSoundFlags* modelStackWithSoundFlags =
		    modelStack->addOtherTwoThings((SoundInstrument*)output, &paramManager)->addSoundFlags();
		return ((SoundInstrument*)output)->allowNoteTails(modelStackWithSoundFlags);
	}

	// Or if kit...
	NoteRow* noteRow = modelStack->getNoteRowAllowNull();
	if (!noteRow || !noteRow->drum) {
		return true;
	}
	ModelStackWithSoundFlags* modelStackWithSoundFlags =
	    modelStack->addOtherTwoThings(noteRow->drum->toModControllable(), &noteRow->paramManager)->addSoundFlags();
	return noteRow->drum->allowNoteTails(
	    modelStackWithSoundFlags); // Needs to survive a NULL noteRow, even if this generally wouldn't happen (it might
	                               // if auditioning a Drum via MIDI or arranger audition pad which doesn't have one)
}

// What does this do exactly, again?
void InstrumentClip::ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(ModelStackWithTimelineCounter* modelStack,
                                                                            Sound* sound) {
	if (output) {
		if (output->type == OutputType::SYNTH) {
			if ((SoundInstrument*)output == sound) {

				ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
				    modelStack->addOtherTwoThingsButNoNoteRow(sound, &paramManager);

				sound->ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(modelStackWithThreeMainThings);
			}
		}
		else { // KIT

			SoundDrum* soundDrum = (SoundDrum*)sound;
			for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
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
	if (output->type == OutputType::KIT && !affectEntire) {
		distance = modelStack->getNoteRow()->getDistanceToNextNote(givenNote->pos, modelStack);
	}

	// Otherwise, take all NoteRows into account
	else {
		distance = 2147483647;

		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			int32_t earliestThisRow = thisNoteRow->getDistanceToNextNote(givenNote->pos, modelStack);
			distance = std::min(earliestThisRow, distance);
		}
	}

	return std::max(distance, givenNote->length);
}

// Make sure noteRow not NULL before you call!
int32_t InstrumentClip::getNoteRowId(NoteRow* noteRow, int32_t noteRowIndex) {
#if ALPHA_OR_BETA_VERSION
	if (!noteRow) {
		FREEZE_WITH_ERROR("E380");
	}
#endif
	if (output->type == OutputType::KIT) {
		return noteRowIndex;
	}
	else {
		return noteRow->y;
	}
}

NoteRow* InstrumentClip::getNoteRowFromId(int32_t id) {
	if (output->type == OutputType::KIT) {
		if (id < 0 || id >= noteRows.getNumElements()) {
			FREEZE_WITH_ERROR("E177");
		}
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

bool InstrumentClip::shiftHorizontally(ModelStackWithTimelineCounter* modelStack, int32_t amount, bool shiftAutomation,
                                       bool shiftSequenceAndMPE) {
	// the following code iterates through all param collections and shifts automation and MPE separately
	// automation only gets shifted if shiftAutomation is true
	// MPE only gets shifted if shiftSequenceAndMPE is true
	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager);

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
					    ->shiftHorizontally(modelStackWithParamCollection, amount, loopLength);
				}
			}

			// Normal case (non MPE automation)
			else {
				if (shiftAutomation) {
					summary->paramCollection->shiftHorizontally(modelStackWithParamCollection, amount, loopLength);
				}
			}
			summary++;
			i++;
		}
	}

	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		int32_t noteRowId = getNoteRowId(thisNoteRow, i);
		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);
		// Shifts NoteRow-level param automation too
		thisNoteRow->shiftHorizontally(amount, modelStackWithNoteRow, shiftAutomation, shiftSequenceAndMPE);
	}

	if (playbackHandler.isEitherClockActive() && modelStack->song->isClipActive(this)) {
		expectEvent();
		reGetParameterAutomation(modelStack); // Re-gets all NoteRow-level param automation too
	}
	return true;
}

void InstrumentClip::shiftOnlyOneNoteRowHorizontally(ModelStackWithNoteRow* modelStack, int32_t shiftAmount,
                                                     bool shiftAutomation, bool shiftSequenceAndMPE) {
	NoteRow* noteRow = modelStack->getNoteRow();

	noteRow->shiftHorizontally(shiftAmount, modelStack, shiftAutomation, shiftSequenceAndMPE);

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

	int32_t outputFilter = midiInstrument->getChannel();
	int32_t masterChannel = midiInstrument->getOutputMasterChannel();

	// Send MIDI PGM if there is one...
	if (midiBank != 128) {
		midiEngine.sendBank(midiInstrument, masterChannel, midiBank, outputFilter);
	}
	if (midiSub != 128) {
		midiEngine.sendSubBank(midiInstrument, masterChannel, midiSub, outputFilter);
	}
	if (midiPGM != 128) {
		midiEngine.sendPGMChange(midiInstrument, masterChannel, midiPGM, outputFilter);
	}
}

void InstrumentClip::clear(Action* action, ModelStackWithTimelineCounter* modelStack, bool clearAutomation,
                           bool clearSequenceAndMPE) {
	// this clears automations when "affectEntire" is enabled
	Clip::clear(action, modelStack, clearAutomation, clearSequenceAndMPE);

	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    modelStack->addNoteRow(getNoteRowId(thisNoteRow, i), thisNoteRow);
		thisNoteRow->clear(action, modelStackWithNoteRow, clearAutomation, clearSequenceAndMPE);
	}

	// Paul: Note rows were lingering, delete them immediately instead of relying they get deleted along the way
	// Mark: BayMud immediately had 2 crashes related to missing note rows - E105 and E177
	// noteRows.deleteNoteRowAtIndex(0, noteRows.getNumElements());
}

bool InstrumentClip::doesProbabilityExist(int32_t apartFromPos, int32_t probability, int32_t secondProbability) {

	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->doesProbabilityExist(apartFromPos, probability, secondProbability)) {
			return true;
		}
	}
	return false;
}

void InstrumentClip::clearArea(ModelStackWithTimelineCounter* modelStack, int32_t startPos, int32_t endPos,
                               Action* action) {
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		int32_t noteRowId = getNoteRowId(thisNoteRow, i);
		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(noteRowId, thisNoteRow);

		thisNoteRow->clearArea(startPos, endPos - startPos, modelStackWithNoteRow, action,
		                       loopLength); // No cross-screen
	}
}

ScaleType InstrumentClip::getScaleType() {

	if (output->type == OutputType::KIT) {
		return ScaleType::KIT;
	}
	else {
		if (inScaleMode) {
			return ScaleType::SCALE;
		}
		else {
			return ScaleType::CHROMATIC;
		}
	}
}

void InstrumentClip::backupPresetSlot() {
	const size_t outputTypeAsIdx = static_cast<size_t>(output->type);
	switch (output->type) {
	case OutputType::MIDI_OUT:
		backedUpInstrumentSubSlot[outputTypeAsIdx] = ((MIDIInstrument*)output)->channelSuffix;
		// No break

	case OutputType::CV:
		backedUpInstrumentSlot[outputTypeAsIdx] = ((NonAudioInstrument*)output)->getChannel();
		break;

	case OutputType::SYNTH:
	case OutputType::KIT:
		backedUpInstrumentName[outputTypeAsIdx].set(&output->name);
		backedUpInstrumentDirPath[outputTypeAsIdx].set(&((Instrument*)output)->dirPath);
		break;

	default:
		__builtin_unreachable();
	}
}

void InstrumentClip::compensateVolumeForResonance(ModelStackWithTimelineCounter* modelStack) {
	((Instrument*)output)
	    ->compensateInstrumentVolumeForResonance(
	        modelStack->addOtherTwoThingsButNoNoteRow(output->toModControllable(), &paramManager));

	if (output->type == OutputType::KIT) {

		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			if (thisNoteRow->drum && thisNoteRow->paramManager.containsAnyMainParamCollections()
			    && thisNoteRow->drum->type == DrumType::SOUND) {
				SoundDrum* thisDrum = (SoundDrum*)thisNoteRow->drum;
				ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
				    modelStack->addNoteRow(i, thisNoteRow)->addOtherTwoThings(thisDrum, &thisNoteRow->paramManager);
				thisDrum->compensateVolumeForResonance(modelStackWithThreeMainThings);
			}
		}
	}
}

void InstrumentClip::deleteOldDrumNames() {
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		thisNoteRow->deleteOldDrumNames();
	}
}

void InstrumentClip::ensureScrollWithinKitBounds() {
	if (yScroll < 1 - kDisplayHeight) {
		yScroll = 1 - kDisplayHeight;
	}
	else {
		int32_t maxYScroll = getNumNoteRows() - 1;
		if (yScroll > maxYScroll) {
			yScroll = maxYScroll;
		}
	}
}

// Make sure not a Kit before calling this
bool InstrumentClip::isScrollWithinRange(int32_t scrollAmount, int32_t newYNote) {

	if (output->type == OutputType::SYNTH) {

		if (scrollAmount >= 0) {
			int32_t transposedNewYNote = newYNote + ((SoundInstrument*)output)->getMinOscTranspose();
			if (transposedNewYNote > 127 && newYNote > getTopYNote()) {
				return false;
			}
		}

		if (scrollAmount <= 0) {
			int32_t transposedNewYNote = newYNote + ((SoundInstrument*)output)->getMaxOscTranspose(this);
			if (transposedNewYNote < 0 && newYNote < getBottomYNote()) {
				return false;
			}
		}
	}

	else if (output->type == OutputType::CV) {
		int32_t newVoltage = cvEngine.calculateVoltage(newYNote, ((CVInstrument*)output)->getChannel());
		if (scrollAmount >= 0) {
			if (newVoltage >= 65536 && newYNote > getTopYNote()) {
				return false;
			}
		}
		if (scrollAmount <= 0) {
			if (newVoltage < 0 && newYNote < getBottomYNote()) {
				return false;
			}
		}
	}

	else { // OutputType::MIDI_OUT
		if (scrollAmount >= 0) {
			if (newYNote > 127 && newYNote > getTopYNote()) {
				return false;
			}
		}
		if (scrollAmount <= 0) {
			if (newYNote < 0 && newYNote < getBottomYNote()) {
				return false;
			}
		}
	}
	return true;
}

bool InstrumentClip::isEmpty(bool displayPopup) {
	// does this clip have notes?
	if (containsAnyNotes()) {
		if (displayPopup) {
			display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CLIP_NOT_EMPTY));
		}
		return false;
	}
	return true;
}

bool InstrumentClip::containsAnyNotes() {
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (!thisNoteRow->hasNoNotes()) {
			return true;
		}
	}
	return false;
}

int32_t InstrumentClip::getYNoteFromYDisplay(int32_t yDisplay, Song* song) {
	return getYNoteFromYVisual(yDisplay + yScroll, song);
}

// Called when the user presses one of the instrument-type buttons (synth/kit/MIDI/CV). This function takes care of
// deciding what Instrument / preset to switch to.
Instrument* InstrumentClip::changeOutputType(ModelStackWithTimelineCounter* modelStack, OutputType newOutputType) {
	OutputType oldOutputType = output->type;

	if (oldOutputType == newOutputType) {
		return nullptr;
	}

	actionLogger.deleteAllLogs(); // Can't undo past this!

	Availability availabilityRequirement;
	bool canReplaceWholeInstrument = modelStack->song->shouldOldOutputBeReplaced(this, &availabilityRequirement);

	bool shouldReplaceWholeInstrument;

	modelStack->song->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E061", "H061");

	backupPresetSlot();

	// Retrieve backed up slot numbers
	const size_t newOutputTypeAsIdx = static_cast<size_t>(newOutputType);
	int16_t newSlot = backedUpInstrumentSlot[newOutputTypeAsIdx];
	int8_t newSubSlot = backedUpInstrumentSubSlot[newOutputTypeAsIdx];

	Instrument* newInstrument = nullptr;

	bool instrumentAlreadyInSong = false;

	// MIDI / CV
	if (newOutputType == OutputType::MIDI_OUT || newOutputType == OutputType::CV) {
		newInstrument = modelStack->song->getNonAudioInstrumentToSwitchTo(
		    newOutputType, availabilityRequirement, newSlot, newSubSlot, &instrumentAlreadyInSong);
		if (!newInstrument) {
			return nullptr;
		}
	}

	// Synth / Kit
	else {
		String newName;
		Error error;

		newName.set(&backedUpInstrumentName[newOutputTypeAsIdx]);
		Browser::currentDir.set(&backedUpInstrumentDirPath[newOutputTypeAsIdx]);

		if (Browser::currentDir.isEmpty()) {
			error = Browser::currentDir.set(getInstrumentFolder(newOutputType));
			if (error != Error::NONE) {
				display->displayError(error);
				return nullptr;
			}
		}

		FileItem* fileItem = D_TRY_CATCH(
		    loadInstrumentPresetUI.confirmPresetOrNextUnlaunchedOne(newOutputType, &newName, availabilityRequirement),
		    error, {
			    display->displayError(error);
			    return nullptr;
		    });

		newInstrument = fileItem->instrument;
		bool isHibernating = newInstrument && !fileItem->instrumentAlreadyInSong;
		instrumentAlreadyInSong = newInstrument && fileItem->instrumentAlreadyInSong;

		if (!newInstrument) {
			String newPresetName;
			fileItem->getDisplayNameWithoutExtension(&newPresetName);
			error =
			    StorageManager::loadInstrumentFromFile(modelStack->song, nullptr, newOutputType, false, &newInstrument,
			                                           &fileItem->filePointer, &newPresetName, &Browser::currentDir);
		}

		Browser::emptyFileItems();

		if (error != Error::NONE) {
			display->displayError(error);
			return nullptr;
		}

		if (isHibernating) {
			modelStack->song->removeInstrumentFromHibernationList(newInstrument);
		}

		display->displayLoadingAnimationText("Loading");
		newInstrument->loadAllAudioFiles(true);
	}

	shouldReplaceWholeInstrument = (canReplaceWholeInstrument && !instrumentAlreadyInSong);

	// If replacing whole Instrument
	if (shouldReplaceWholeInstrument) {
		modelStack->song->replaceInstrument((Instrument*)output, newInstrument);
	}

	else {
		Error error = changeInstrument(modelStack, newInstrument, nullptr,
		                               InstrumentRemoval::DELETE_OR_HIBERNATE_IF_UNUSED, nullptr, true);
		// TODO: deal with errors

		if (!instrumentAlreadyInSong) {
			modelStack->song->addOutput(newInstrument);
		}
	}

	// Turning into Kit
	if (newOutputType == OutputType::KIT) {
		// Make sure we're not scrolled too far up (this has to happen amongst this code down here - NoteRows are
		// deleted in the functions called above)
		int32_t maxScroll = (int32_t)getNumNoteRows() - kDisplayHeight;
		maxScroll = std::max(0_i32, maxScroll);
		yScroll = std::min(yScroll, maxScroll);
		((Kit*)newInstrument)->selectedDrum = nullptr;
	}

	outputChanged(modelStack, newInstrument);
	modelStack->song->ensureAllInstrumentsHaveAClipOrBackedUpParamManager("E062", "H062");

	display->removeWorkingAnimation();

	return newInstrument;
}

void InstrumentClip::getSuggestedParamManager(Clip* newClip, ParamManagerForTimeline** suggestedParamManager,
                                              Sound* sound) {
	if (&paramManager == *suggestedParamManager) {
		Clip::getSuggestedParamManager(newClip, suggestedParamManager, sound);
	}
	else {
		InstrumentClip* newInstrumentClip = (InstrumentClip*)newClip;
		for (int32_t i = 0; i < newInstrumentClip->noteRows.getNumElements(); i++) {
			NoteRow* noteRow = newInstrumentClip->noteRows.getElement(i);
			if (noteRow->drum && noteRow->drum->type == DrumType::SOUND && (SoundDrum*)noteRow->drum == sound) {
				*suggestedParamManager = &noteRow->paramManager;
				break;
			}
		}
	}
}

ParamManagerForTimeline* InstrumentClip::getCurrentParamManager() {
	ParamManagerForTimeline* currentParamManager = nullptr;

	if (output->type == OutputType::KIT && !affectEntire) {
		Drum* selectedDrum = ((Kit*)output)->selectedDrum;

		// If a SoundDrum is selected...
		if (selectedDrum) {
			if (selectedDrum->type == DrumType::SOUND) {
				NoteRow* noteRow = getNoteRowForDrum(selectedDrum);
				if (noteRow != nullptr) {
					currentParamManager = &noteRow->paramManager;
				}
			}
		}
	}
	else {
		currentParamManager = &paramManager;
	}

	return currentParamManager;
}

Error InstrumentClip::claimOutput(ModelStackWithTimelineCounter* modelStack) {

	if (!output) { // Would only have an output already if file from before V2.0.0 I think? So, this block normally does
		           // apply.
		const OutputType outputType = outputTypeWhileLoading;
		const size_t outputTypeAsIdx = static_cast<size_t>(outputType);

		char const* instrumentName = (outputTypeAsIdx < 2) ? backedUpInstrumentName[outputTypeAsIdx].get() : nullptr;
		char const* dirPath = (outputTypeAsIdx < 2) ? backedUpInstrumentDirPath[outputTypeAsIdx].get() : nullptr;

		output = modelStack->song->getInstrumentFromPresetSlot(outputType, backedUpInstrumentSlot[outputTypeAsIdx],
		                                                       backedUpInstrumentSubSlot[outputTypeAsIdx],
		                                                       instrumentName, dirPath, false);

		if (!output) {

			return Error::FILE_CORRUPTED;
		}
	}

	// If Instrument is a Kit, match each NoteRow to its Drum
	if (output->type == OutputType::KIT) {
		Kit* kit = (Kit*)output;

		int32_t noteRowCount = 0;

		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);

			if (!(noteRowCount & 15)) {
				AudioEngine::routineWithClusterLoading(); // -----------------------------------
				AudioEngine::logAction("nlkr");
			}

			bool paramManagerCloned = false;

			// Maybe we (cryptically) marked it as "no drum".
			if ((uint32_t)thisNoteRow->drum == (uint32_t)0xFFFFFFFF) {
				thisNoteRow->drum = nullptr;
			}

			// Or a gate drum from a pre-V2.0 Song file...
			else if ((uint32_t)thisNoteRow->drum > (uint32_t)(0xFFFFFFFE - NUM_GATE_CHANNELS)) {

				int32_t gateChannel = 0xFFFFFFFE - (uint32_t)thisNoteRow->drum;

				thisNoteRow->drum = kit->getGateDrumForChannel(gateChannel);

				if (!thisNoteRow->drum) {
					void* drumMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(GateDrum));
					if (!drumMemory) {
						return Error::INSUFFICIENT_RAM;
					}
					GateDrum* newDrum = new (drumMemory) GateDrum();
					newDrum->channel = gateChannel;

					kit->addDrum(newDrum);
					thisNoteRow->drum = newDrum;
				}
				thisNoteRow->giveMidiCommandsToDrum();
			}

			// Otherwise, we know we've sneakily put an integer index in place of the pointer, so convert that back to
			// an actual pointer now
			else {

				// Don't call setDrum(), because that would overwrite the NoteRow's paramManager. It already has the
				// right one, loaded from file
				Drum* drumFromIndex = kit->getDrumFromIndex((uint32_t)thisNoteRow->drum);

				ParamManagerForTimeline* otherParamManager;

				ModelStackWithNoteRow*
				    modelStackWithNoteRow; // Defined up here so we can do the jump to haveNoDrum, below.

				Error error; // These declared here, to allow gotos
				bool success;

				// We need to see whether any other NoteRows *that we've assigned drums so far* had this same drum.
				// TODO: this could be waaaay more efficient!
				for (int32_t j = 0; j < i; j++) {
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

				// If we didn't get a paramManager (means pre-September-2016 song). TODO: this whole section would lead
				// to an ugly mess if the right stuff wasn't in the file. Or if not enough RAM
				if (!thisNoteRow->paramManager.containsAnyMainParamCollections()
				    && thisNoteRow->drum->type == DrumType::SOUND) {

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
							return Error::UNSPECIFIED;
						}
						error = thisNoteRow->paramManager.cloneParamCollectionsFrom(otherParamManager, false);
						paramManagerCloned = true;

						// If wasn't enough RAM, we're really in trouble
						if (error != Error::NONE) {
							FREEZE_WITH_ERROR("E011");
haveNoDrum:
							thisNoteRow->drum = nullptr;
						}
					}
				}

				// If we've now got a paramManager and Drum...
				if (thisNoteRow->drum) {

					// If saved before V2.1, see if we need linear interpolation
					if (song_firmware_version < FirmwareVersion::official({2, 1, 0, "beta"})) {
						if (thisNoteRow->drum->type == DrumType::SOUND) {
							SoundDrum* sound = (SoundDrum*)thisNoteRow->drum;

							PatchedParamSet* patchedParams = thisNoteRow->paramManager.getPatchedParamSet();

							for (int32_t s = 0; s < kNumSources; s++) {
								Source* source = &sound->sources[s];
								if (source->oscType == OscType::SAMPLE) {
									if (sound->transpose || source->transpose || source->cents
									    || patchedParams->params[params::LOCAL_PITCH_ADJUST].containsSomething(0)
									    //||
									    // thisNoteRow->paramManager->patchCableSet.doesParamHaveSomethingPatchedToIt(params::LOCAL_PITCH_ADJUST)
									    //// No, can't call these cos patching isn't set up yet. Oh well
									    //||
									    // thisNoteRow->paramManager->patchCableSet.doesParamHaveSomethingPatchedToIt(params::LOCAL_OSC_A_PITCH_ADJUST
									    //+ s)
									    || patchedParams->params[params::LOCAL_OSC_A_PITCH_ADJUST + s]
									           .containsSomething(0)) {

										source->sampleControls.interpolationMode = InterpolationMode::LINEAR;
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
		if (yScroll < 1 - kDisplayHeight) {
			yScroll = 1 - kDisplayHeight;
		}
		else if (yScroll > noteRowCount - 1) {
			yScroll = noteRowCount - 1;
		}
	}

	// Otherwise, if not a Kit...
	else {

		// If we had a MIDI input channel for this Clip, as was the format pre V2.0, move this to the Instrument
		if (soundMidiCommand.containsSomething()) {
			((MelodicInstrument*)output)->midiInput = soundMidiCommand;
		}

		// Ensure all NoteRows have a NULL Drum pointer
		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			thisNoteRow->drum = nullptr;
		}

		// And...
		if (output->type == OutputType::MIDI_OUT) {
			if (!paramManager.containsAnyMainParamCollections()) {
				Error error = paramManager.setupMIDI();
				if (error != Error::NONE) {
					return error;
				}
			}
		}
		else if (output->type == OutputType::SYNTH) {
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
	if (song_firmware_version < FirmwareVersion::official({2, 1, 0, "beta"})) {

		if (output->type == OutputType::SYNTH) {
			SoundInstrument* sound = (SoundInstrument*)output;

			for (int32_t s = 0; s < kNumSources; s++) {
				Source* source = &sound->sources[s];
				if (source->oscType == OscType::SAMPLE) {
					source->sampleControls.interpolationMode = InterpolationMode::LINEAR;
				}
			}
		}

		// For songs saved before V2.0, ensure that non-square oscillators have PW set to 0 (cos PW in this case didn't
		// have an effect then but it will now)
		if (song_firmware_version < FirmwareVersion::official({2, 0, 0, "beta"})) {
			if (output->type == OutputType::SYNTH) {
				SoundInstrument* sound = (SoundInstrument*)output;

				ParamCollectionSummary* patchedParamsSummary = paramManager.getPatchedParamSetSummary();
				PatchedParamSet* patchedParams = (PatchedParamSet*)patchedParamsSummary->paramCollection;

				PatchCableSet* patchedCables = paramManager.getPatchCableSet();

				ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
				    modelStack->addOtherTwoThingsButNoNoteRow(sound, &paramManager);

				for (int32_t s = 0; s < kNumSources; s++) {
					if (sound->sources[s].oscType != OscType::SQUARE) {

						ModelStackWithParamCollection* modelStackWithParamCollection =
						    modelStackWithThreeMainThings->addParamCollection(patchedParams, patchedParamsSummary);

						patchedParams->deleteAutomationForParamBasicForSetup(modelStackWithParamCollection,
						                                                     params::LOCAL_OSC_A_PHASE_WIDTH + s);
						patchedParams->params[params::LOCAL_OSC_A_PHASE_WIDTH + s].setCurrentValueBasicForSetup(0);
						patchedCables->removeAllPatchingToParam(modelStackWithParamCollection,
						                                        params::LOCAL_OSC_A_PHASE_WIDTH + s);
					}
				}
			}
		}
	}

	return Error::NONE;
}

void InstrumentClip::finishLinearRecording(ModelStackWithTimelineCounter* modelStack, Clip* nextPendingLoop,
                                           int32_t buttonLatencyForTempolessRecord) {

	if (getRootUI() == &arrangerView) {
		arrangerView.clipNeedsReRendering(this);
	}

	InstrumentClip* newInstrumentClip = nullptr;
	if (nextPendingLoop) {
		newInstrumentClip = (InstrumentClip*)nextPendingLoop;
	}

	Action* action = nullptr;

	// Notes may have been placed right at/past the end of the Clip, usually because one was quantized forwards - and
	// set to the exact end position - and it wasn't yet known whether to extend the length of the Clip in case the user
	// cancelled linear recording. Trim them off, and move them to the new Clip if there is one.
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);

		// All notes should be recorded
		thisNoteRow->ignoreNoteOnsBefore_ = 0;

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
					if (output->type == OutputType::KIT) {
						modelStackWithNoteRow = newInstrumentClip->getNoteRowForDrum(modelStack, thisNoteRow->drum);
					}
					else {
						modelStackWithNoteRow =
						    newInstrumentClip->getOrCreateNoteRowForYNote(thisNoteRow->y, modelStack);
					}

					NoteRow* newNoteRow = modelStackWithNoteRow->getNoteRowAllowNull();
					if (newNoteRow) {
						// I'm guessing I deliberately didn't send the Action in here, cos didn't want to
						// make this Note on the new InstrumentClip undoable?
						newNoteRow->attemptNoteAdd(0, lastNote->length, lastNote->velocity, lastNote->probability,
						                           lastNote->iterance, lastNote->fill, modelStackWithNoteRow, nullptr);
						// Make sure we don't double-play the note
						newNoteRow->ignoreNoteOnsBefore_ = 1;
					}
				}

				// Delete the Note
				thisNoteRow->deleteNoteByIndex(thisNoteRow->notes.getNumElements() - 1, nullptr,
				                               getNoteRowId(thisNoteRow, i), this);
			}

			// Or if Note not past end-point...
			else {

				// Extend length right to end-point
				if (mayStillLengthen
				    && ((Instrument*)output)->isNoteRowStillAuditioningAsLinearRecordingEnded(thisNoteRow)) {

					if (!action) {
						action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);
					}
					int32_t noteRowId = getNoteRowId(thisNoteRow, i);

					if (action) {
						action->recordNoteArrayChangeIfNotAlreadySnapshotted(
						    this, noteRowId, &thisNoteRow->notes, false, true); // This has probably already been done
					}
					// moveToFrontIfAlreadySnapshotted = true because we need to make the Consequence closer to the
					// front than any previous Clip-lengthening that took place.

					lastNote->setLength(loopLength - lastNote->pos);

					// if we just recorded a drone note, transfer the note to the sequencer
					// so that we can stop auditioning / sending midi and note will continue sustaining
					if (thisNoteRow->isDroning(loopLength)) {
						thisNoteRow->sequenced = true;
					}
				}

				// And, that'll be the last Note we need to deal with
				break;
			}
		}
	}

	// If we did create a new Clip, we want to leave currentlyRecordingLinearly true just a bit longer so that when
	// expectNoFurtherTicks() gets called as the new Clip begins playing, it knows not to switch our currently
	// sounding/auditioning notes off. Otherwise, since that won't be happening, we just want to ensure that recording
	// stops now.
	currentlyRecordingLinearly = newInstrumentClip;

	if (isUnfinishedAutoOverdub) {
		isUnfinishedAutoOverdub = false;
	}

	if (getRootUI()) {
		getRootUI()->clipNeedsReRendering(this); // Notes might have been lengthened - we'd better render it.
	}
}

Clip* InstrumentClip::cloneAsNewOverdub(ModelStackWithTimelineCounter* modelStack, OverDubType newOverdubNature) {

	// Allocate memory for Clip
	void* clipMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(InstrumentClip));
	if (!clipMemory) {
ramError:
		display->displayError(Error::INSUFFICIENT_RAM);
		return nullptr;
	}

	ParamManagerForTimeline newParamManager;

	Error error = newParamManager.cloneParamCollectionsFrom(&paramManager, false, true);
	if (error != Error::NONE) {
		display->displayError(error);
		return nullptr;
	}

	InstrumentClip* newInstrumentClip = new (clipMemory) InstrumentClip(modelStack->song);
	newInstrumentClip->setInstrument((Instrument*)output, modelStack->song, &newParamManager);

	newInstrumentClip->setupForRecordingAsAutoOverdub(
	    this, modelStack->song,
	    newOverdubNature); // Hopefully fine - I've moved this to after setInstrument in March 2021, so we can override
	                       // the new affectEntire default value set there.

	char modelStackMemoryNewClip[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStackNewClip =
	    setupModelStackWithTimelineCounter(modelStackMemoryNewClip, modelStack->song, newInstrumentClip);

	newInstrumentClip->setupAsNewKitClipIfNecessary(modelStackNewClip);

	// If Kit, copy NoteRow colours
	if (output->type == OutputType::KIT && noteRows.getNumElements() == newInstrumentClip->noteRows.getNumElements()) {
		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
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
		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows.getElement(i);
			int32_t numNotes = thisNoteRow->notes.getNumElements();
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

// Call this after setInstrument() / setAudioInstrument(). I forget exactly where setupPatching() fits into this
// picture... Arranger view calls that before this...
void InstrumentClip::setupAsNewKitClipIfNecessary(ModelStackWithTimelineCounter* modelStack) {
	if (output->type == OutputType::KIT) {
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
	if (output->type == OutputType::KIT && !affectEntire && getRootUI() != &sessionView
	    && getRootUI() != &arrangerView) {
		Kit* kit = (Kit*)output;

		if (!kit->selectedDrum || kit->selectedDrum->type != DrumType::SOUND) {
returnNull:
			modelStack->setTimelineCounter(nullptr);
			modelStack->addOtherTwoThingsButNoNoteRow(nullptr, nullptr);
		}
		else {
			int32_t noteRowIndex;
			NoteRow* noteRow = getNoteRowForDrum(kit->selectedDrum, &noteRowIndex);

			// Ensure that the selected drum in fact has a NoteRow in this Clip. It may have been deleted.
			if (!noteRow) {
				goto returnNull;
			}

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

extern int16_t zeroMPEValues[kNumExpressionDimensions];

void InstrumentClip::recordNoteOn(ModelStackWithNoteRow* modelStack, int32_t velocity, bool forcePos0,
                                  int16_t const* mpeValuesOrNull, int32_t fromMIDIChannel) {

	NoteRow* noteRow = modelStack->getNoteRow();

	// Rounded position in sequencer ticks of the note-on event.
	int32_t quantizedPos = 0;

	bool reversed = modelStack->isCurrentlyPlayingReversed();
	int32_t effectiveLength = modelStack->getLoopLength();

	if (forcePos0) {
		noteRow->ignoreNoteOnsBefore_ = 1;
	}
	else {
		uint32_t unquantizedPos = modelStack->getLivePos();

		bool quantizedLater = false;

		if (FlashStorage::recordQuantizeLevel) {
			// If triplets are currently enabled in the song
			uint32_t baseThing = modelStack->song->tripletsOn ? 4 : 3;
			// Number of sequencer ticks we're quantizing to.
			//
			// If this is larger than 0x7fffffff we have significant problems down the line, so just cast here so we're
			// honest.
			auto quantizeInterval =
			    static_cast<int32_t>(baseThing << (8 + modelStack->song->insideWorldTickMagnitude
			                                       + modelStack->song->insideWorldTickMagnitudeOffsetFromBPM
			                                       - FlashStorage::recordQuantizeLevel));
			auto offset = static_cast<int32_t>(unquantizedPos % quantizeInterval);
			quantizedPos = static_cast<int32_t>(unquantizedPos - static_cast<uint32_t>(offset));

			int32_t amountLaterThanMiddle = offset - (quantizeInterval / 2);
			if (reversed) {
				// Invert the sense of "amountLaterThanMiddle", and offset by 1 to account for the reversed sense of
				// time.
				amountLaterThanMiddle = 1 - amountLaterThanMiddle;
			}
			quantizedLater = (amountLaterThanMiddle >= 0);

			// If quantizing to the right...
			if (quantizedLater != reversed) {
				quantizedPos += static_cast<int32_t>(quantizeInterval);

				// If that's quantized it right to the end of the loop-length or maybe beyond...
				if (quantizedPos >= effectiveLength) {

					// If recording to arrangement, go and extend the Clip/NoteRow early, to create the place where
					// we'll put the Note.
					if (playbackHandler.recording == RecordingMode::ARRANGEMENT && isArrangementOnlyClip()) {

						Error error{Error::NONE};

						// If the NoteRow has independent *length* (not just independent play-pos), then it needs to be
						// treated individually.
						if (noteRow->loopLengthIfIndependent != 0) {
							if (output->type == OutputType::KIT
							    && noteRows.getNumElements()
							           != (static_cast<InstrumentClip*>(beingRecordedFromClip))
							                  ->noteRows.getNumElements()) {
								error = Error::UNSPECIFIED;
							}

							else {
								char otherModelStackMemory[MODEL_STACK_MAX_SIZE];
								ModelStackWithNoteRow* otherModelStackWithNoteRow =
								    duplicateModelStackForClipBeingRecordedFrom(modelStack, otherModelStackMemory);

								NoteRow* otherNoteRow = otherModelStackWithNoteRow->getNoteRowAllowNull();
								if (otherNoteRow != nullptr) { // It "should" always have it...

									int32_t whichRepeatThisIs =
									    noteRow->loopLengthIfIndependent / otherNoteRow->loopLengthIfIndependent;
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

						if (error != Error::NONE) {
							goto doNormal;
						}
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
				if (noteRow->getEffectiveSequenceDirectionMode(modelStack) == SequenceDirection::PINGPONG) {
					reversed = !reversed;
				}
			}
		}

		else {
			quantizedPos = unquantizedPos;
		}

		// If we quantized later, make sure that that note doesn't get played really soon when the play-pos reaches it
		if (quantizedLater || playbackHandler.ticksLeftInCountIn) {
			noteRow->ignoreNoteOnsBefore_ = quantizedPos + 1;
			expectEvent();
		}
	}

	// Since recording usually involves creating lots of notes overall, we'll just snapshot all the notes in bulk
	Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);
	if (action) {
		action->recordNoteArrayChangeIfNotAlreadySnapshotted(this, modelStack->noteRowId, &noteRow->notes, false, true);
	}
	// moveToFrontIfAlreadySnapshotted = true because we need to make the Consequence closer to the front than any
	// previous Clip-lengthening that took place.

	int32_t distanceToNextNote;

	// Add the actual note
	if (reversed) {
		bool allowingNoteTails = allowNoteTails(modelStack);
		distanceToNextNote = noteRow->attemptNoteAddReversed(modelStack, quantizedPos, velocity, allowingNoteTails);
	}

	else {
		int32_t probability = noteRow->getDefaultProbability();
		Iterance iterance = noteRow->getDefaultIterance();
		int32_t fill = noteRow->getDefaultFill(modelStack);
		// Don't supply Action, cos we've done our own thing, above
		distanceToNextNote =
		    noteRow->attemptNoteAdd(quantizedPos, 1, velocity, probability, iterance, fill, modelStack, nullptr);
	}

	// If that didn't work, get out - but not in the special case for linear recording, discussed below.
	if (!distanceToNextNote && quantizedPos < effectiveLength) {
		return;
	}

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
	if (!mpeParams) {
		return;
	}

	int32_t posAtWhichClipWillCut = modelStack->getPosAtWhichPlaybackWillCut();

	ModelStackWithParamCollection* modelStackWithParamCollection =
	    modelStack->addOtherTwoThingsAutomaticallyGivenNoteRow()->addParamCollection(mpeParams, mpeParamsSummary);

	for (int32_t m = 0; m < kNumExpressionDimensions; m++) {
		AutoParam* param = &mpeParams->params[m];
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStackWithParamCollection->addAutoParam(m, param);

		Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);
		if (action) {
			action->recordParamChangeIfNotAlreadySnapshotted(modelStackWithAutoParam);
		}

		int32_t value = (int32_t)mpeValuesOrNull[m] << 16;

		// Special case for MPE - in this case where we're setting the value for the whole length, we still do want to
		// place one - just one - node at pos. It'll be for the start of a note.
		if (effectiveLength == distanceToNextNote) {
			param->deleteAutomation(nullptr, modelStackWithAutoParam, false);

			Error error = param->nodes.insertAtIndex(0);
			if (error == Error::NONE) {
				ParamNode* firstNode = param->nodes.getElement(0);
				firstNode->pos = quantizedPos;
				firstNode->value = value;
				firstNode->interpolated = reversed;
			}
		}

		else if (reversed) {
doHomogenize:
#if ENABLE_SEQUENTIALITY_TESTS
			param->nodes.testSequentiality("E442"); // drbourbon got, when check was inside homogenizeRegion(). Now
			                                        // trying to work out where that came from. March 2022.
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

		// These manual sets are in case we quantized forwards and the region we just created actually begins after
		// "now"-time.
		param->currentValue = value;
		param->valueIncrementPerHalfTick = 0;
		// TODO: and to make it perfect, we'd also want to ignore any further nodes between now and the start of the
		// region. Or, could probably get away with just deleting them.
	}
}

void InstrumentClip::recordNoteOff(ModelStackWithNoteRow* modelStack, int32_t velocity) {

	if (!allowNoteTails(modelStack)) {
		return;
	}

	Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);

	modelStack->getNoteRow()->recordNoteOff(getLivePos(), modelStack, action, velocity);
}

// This function looks a bit weird... probably old... should it maybe instead call a function on the MelodicInstrument /
// Kit?
void InstrumentClip::yDisplayNoLongerAuditioning(int32_t yDisplay, Song* song) {
	if (output->type == OutputType::KIT) {
		int32_t noteRowIndex = yDisplay + yScroll;
		if (noteRowIndex >= 0 && noteRowIndex <= noteRows.getNumElements()) {
			NoteRow* noteRow = noteRows.getElement(noteRowIndex);
			if (noteRow->drum) {
				noteRow->drum->auditioned = false;
				noteRow->drum->lastMIDIChannelAuditioned = MIDI_CHANNEL_NONE; // So it won't record any more MPE
			}
		}
	}

	else {
		int32_t yNote = getYNoteFromYDisplay(yDisplay, song);
		static_cast<MelodicInstrument*>(output)->notesAuditioned.erase(yNote);
	}

	expectEvent();
}

int32_t InstrumentClip::getMaxLength() {
	int32_t maxLength = loopLength;

	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		if (thisNoteRow->loopLengthIfIndependent > maxLength) {
			maxLength = thisNoteRow->loopLengthIfIndependent;
		}
	}

	return maxLength;
}

bool InstrumentClip::hasAnyPitchExpressionAutomationOnNoteRows() {
	for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = noteRows.getElement(i);
		ExpressionParamSet* expressionParams = thisNoteRow->paramManager.getExpressionParamSet();
		if (expressionParams && expressionParams->params[0].isAutomated()) {
			return true;
		}
	}
	return false;
}

void InstrumentClip::incrementPos(ModelStackWithTimelineCounter* modelStack, int32_t numTicks) {
	Clip::incrementPos(modelStack, numTicks);

	ticksTilNextNoteRowEvent -= numTicks; // We're one tick closer to the next event...
	noteRowsNumTicksBehindClip += numTicks;

	if (ticksTilNextNoteRowEvent <= 0) {

		for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
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

// SEQUENCER MODE MANAGEMENT

void InstrumentClip::setSequencerMode(const std::string& modeName) {
	// Handle piano roll mode (empty string)
	if (modeName.empty()) {
		// Cache current sequencer mode if we have one
		if (sequencerMode_ && !sequencerModeName_.empty()) {
			cachedSequencerModes_[sequencerModeName_] = std::move(sequencerMode_);
		}

		// Clear active mode but keep cache
		sequencerMode_.reset();
		sequencerModeName_ = "";
		return;
	}

	// If we're already in this mode, don't recreate it
	if (hasSequencerMode() && sequencerModeName_ == modeName) {
		return;
	}

	// Cache current mode if switching to a different mode
	if (sequencerMode_ && !sequencerModeName_.empty() && sequencerModeName_ != modeName) {
		cachedSequencerModes_[sequencerModeName_] = std::move(sequencerMode_);
	}

	// Try to restore from cache first
	if (cachedSequencerModes_.count(modeName)) {
		sequencerMode_ = std::move(cachedSequencerModes_[modeName]);
		cachedSequencerModes_.erase(modeName);
	}
	else {
		// Create new mode
		auto& manager = deluge::model::clip::sequencer::SequencerModeManager::instance();
		sequencerMode_ = manager.createMode(modeName);

		if (sequencerMode_) {
			sequencerMode_->initialize();
		}
	}

	// Set active mode
	if (sequencerMode_) {
		sequencerModeName_ = modeName;
		expectEvent();
	}
}

void InstrumentClip::clearSequencerMode() {
	if (sequencerMode_) {
		sequencerMode_->cleanup();
		sequencerMode_.reset();
		sequencerModeName_.clear();
	}

	// Clear cache as well
	for (auto& [name, mode] : cachedSequencerModes_) {
		if (mode) {
			mode->cleanup();
		}
	}
	cachedSequencerModes_.clear();
}

/*
    for (int32_t i = 0; i < noteRows.getNumElements(); i++) {
        NoteRow* thisNoteRow = noteRows.getElement(i);
*/
