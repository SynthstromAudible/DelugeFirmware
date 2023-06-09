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

#include <AudioEngine.h>
#include <AudioFileManager.h>
#include <InstrumentClip.h>
#include <InstrumentClipMinder.h>
#include <InstrumentClipView.h>
#include <ParamManager.h>
#include <sounddrum.h>
#include "kit.h"
#include "storagemanager.h"
#include "drum.h"
#include "functions.h"
#include "View.h"
#include <string.h>
#include <new>
#include "GeneralMemoryAllocator.h"
#include "GateDrum.h"
#include "NoteRow.h"
#include "song.h"
#include "playbackhandler.h"
#include "PlaybackMode.h"
#include "UI.h"
#include "Session.h"
#include "MIDIDrum.h"
#include "uart.h"
#include "numericdriver.h"
#include "ModelStack.h"
#include "MIDIDeviceManager.h"
#include "MIDIDevice.h"
#include "ParamSet.h"
#include "PatchCableSet.h"

Kit::Kit() : Instrument(INSTRUMENT_TYPE_KIT), drumsWithRenderingActive(sizeof(Drum*)) {
	firstDrum = NULL;
	selectedDrum = NULL;
}

Kit::~Kit() {
	// Delete all Drums
	while (firstDrum) {
		AudioEngine::logAction("~Kit");
		AudioEngine::routineWithClusterLoading(); // -----------------------------------
		Drum* toDelete = firstDrum;
		firstDrum = firstDrum->next;

		void* toDealloc = dynamic_cast<void*>(toDelete);

		toDelete->~Drum();
		generalMemoryAllocator.dealloc(toDealloc);
	}
}

Drum* Kit::getNextDrum(Drum* fromDrum) {
	if (fromDrum == NULL) return firstDrum;
	else return fromDrum->next;
}

Drum* Kit::getPrevDrum(Drum* fromDrum) {
	if (fromDrum == firstDrum) return NULL;

	Drum* thisDrum = firstDrum;
	while (thisDrum->next != fromDrum)
		thisDrum = thisDrum->next;
	return thisDrum;
}

bool Kit::writeDataToFile(Clip* clipForSavingOutputOnly, Song* song) {

	Instrument::writeDataToFile(clipForSavingOutputOnly, song);

	ParamManager* paramManager;
	if (clipForSavingOutputOnly) paramManager = &clipForSavingOutputOnly->paramManager;
	else {
		paramManager = NULL;

		// If no activeClip, that means no Clip has this Instrument, so there should be a backedUpParamManager that we should use
		if (!activeClip) paramManager = song->getBackedUpParamManagerPreferablyWithClip(this, NULL);
	}

	GlobalEffectableForClip::writeAttributesToFile(clipForSavingOutputOnly == NULL);

	storageManager
	    .writeOpeningTagEnd(); // --------------------------------------------------------------------------- Attributes end

	GlobalEffectableForClip::writeTagsToFile(paramManager, clipForSavingOutputOnly == NULL);

	storageManager.writeOpeningTag("soundSources"); // TODO: change this?
	int selectedDrumIndex = -1;
	int drumIndex = 0;

	Drum* newFirstDrum = NULL;
	Drum** newLastDrum = &newFirstDrum;

	Clip* clipToTakeDrumOrderFrom = clipForSavingOutputOnly;
	if (!clipToTakeDrumOrderFrom) {
		clipToTakeDrumOrderFrom = song->getClipWithOutput(this, false, NULL);
	}

	// If we have a Clip to take the Drum order from...
	if (clipToTakeDrumOrderFrom) {
		// First, write Drums in the order of their NoteRows. Remove these drums from our list - we'll re-add them in a moment, at the start, i.e. in the same order they appear in the file
		for (int i = 0; i < ((InstrumentClip*)clipToTakeDrumOrderFrom)->noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = ((InstrumentClip*)clipToTakeDrumOrderFrom)->noteRows.getElement(i);
			if (thisNoteRow->drum) {
				Drum* drum = thisNoteRow->drum;

				ParamManagerForTimeline* paramManagerForDrum = NULL;

				// If saving Kit (not Song)
				if (clipForSavingOutputOnly) {
					paramManagerForDrum = &thisNoteRow->paramManager;
				}
				// Or if saving Song, we know there's a NoteRow, so no need to save the ParamManager

				writeDrumToFile(drum, paramManagerForDrum, (clipForSavingOutputOnly == NULL), &selectedDrumIndex,
				                &drumIndex, song);

				removeDrumFromLinkedList(drum);
				drum->next = NULL;
				*newLastDrum = drum;
				newLastDrum = &drum->next;
			}
		}
	}

	// Then, write remaining Drums (or all Drums in the case of saving Song) whose order we didn't take from a NoteRow
	Drum** prevPointer = &firstDrum;
	while (true) {

		Drum* thisDrum = *prevPointer;
		if (!thisDrum) break;

		ParamManager* paramManagerForDrum = NULL;

		// If saving Kit (not song), only save Drums if some other NoteRow in the song has it - in which case, save as "default" the params from that NoteRow
		if (clipForSavingOutputOnly) {
			Uart::println("yup, clipForSavingOutputOnly");
			NoteRow* noteRow = song->findNoteRowForDrum(this, thisDrum);
			if (!noteRow) goto moveOn;
			paramManagerForDrum =
			    &noteRow->paramManager; // Of course there won't be one if it's a NonAudioDrum, but that's fine
		}

		// Or if saving song...
		else {
			// If no activeClip, this means we want to store all Drums
			// - and for SoundDrums, save as "default" any backedUpParamManagers (if none for a SoundDrum, definitely skip it)
			if (!activeClip) {
				Uart::println("nah, !activeClip");
				if (thisDrum->type == DRUM_TYPE_SOUND) {
					paramManagerForDrum = song->getBackedUpParamManagerPreferablyWithClip((SoundDrum*)thisDrum, NULL);
					if (!paramManagerForDrum) goto moveOn;
				}
			}

			// Otherwise, if some Clip does have this Kit, then yes do save this Drum - with no ParamManager though
			else {

				// ... but, if no NoteRow has this Drum, we actually want to delete it now, so that its existence doesn't affect drumIndexes!
				if (!song->findNoteRowForDrum(this, thisDrum)) {

					*prevPointer = thisDrum->next;

					if (thisDrum->type == DRUM_TYPE_SOUND) {
						song->deleteBackedUpParamManagersForModControllable((SoundDrum*)thisDrum);
					}

					drumRemoved(thisDrum);

					void* toDealloc = dynamic_cast<void*>(thisDrum);
					thisDrum->~Drum();
					generalMemoryAllocator.dealloc(toDealloc);

					continue;
				}
			}
		}

		writeDrumToFile(thisDrum, paramManagerForDrum, clipForSavingOutputOnly == NULL, &selectedDrumIndex, &drumIndex,
		                song);

moveOn:
		prevPointer = &thisDrum->next;
	}

	storageManager.writeClosingTag("soundSources");

	*newLastDrum = firstDrum;
	firstDrum = newFirstDrum;

	if (selectedDrumIndex != -1) {
		storageManager.writeTag((char*)"selectedDrumIndex", selectedDrumIndex);
	}

	return true;
}

void Kit::writeDrumToFile(Drum* thisDrum, ParamManager* paramManagerForDrum, bool savingSong, int* selectedDrumIndex,
                          int* drumIndex, Song* song) {
	if (thisDrum == selectedDrum) *selectedDrumIndex = *drumIndex;

	thisDrum->writeToFile(savingSong, paramManagerForDrum);
	(*drumIndex)++;
}

int Kit::readFromFile(Song* song, Clip* clip, int32_t readAutomationUpToPos) {

	int selectedDrumIndex = -1;

	ParamManagerForTimeline paramManager;

	char const* tagName;
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {

		if (!strcmp(tagName, "soundSources")) {
			while (*(tagName = storageManager.readNextTagOrAttributeName())) {
				int drumType;

				if (!strcmp(tagName, "sample") || !strcmp(tagName, "synth") || !strcmp(tagName, "sound")) {
					drumType = DRUM_TYPE_SOUND;
doReadDrum:
					int error = readDrumFromFile(song, clip, drumType, readAutomationUpToPos);
					if (error) return error;
					storageManager.exitTag();
				}
				else if (!strcmp(tagName, "midiOutput")) {
					drumType = DRUM_TYPE_MIDI;
					goto doReadDrum;
				}
				else if (!strcmp(tagName, "gateOutput")) {
					drumType = DRUM_TYPE_GATE;
					goto doReadDrum;
				}
				else storageManager.exitTag(tagName);
			}
			storageManager.exitTag("soundSources");
		}
		else if (!strcmp(tagName, "selectedDrumIndex")) {
			selectedDrumIndex = storageManager.readTagOrAttributeValueInt();
			storageManager.exitTag("selectedDrumIndex");
		}
		else {
			int result = GlobalEffectableForClip::readTagFromFile(tagName, &paramManager, readAutomationUpToPos, song);
			if (result == NO_ERROR) {}
			else if (result != RESULT_TAG_UNUSED) return result;
			else {
				if (Instrument::readTagFromFile(tagName)) {}
				else {
					int result = storageManager.tryReadingFirmwareTagFromFile(tagName);
					if (result && result != RESULT_TAG_UNUSED) return result;
					storageManager.exitTag(tagName);
				}
			}
		}
	}

	if (selectedDrumIndex != -1) {
		selectedDrum = getDrumFromIndex(selectedDrumIndex);
	}

	if (paramManager.containsAnyMainParamCollections()) {
		compensateInstrumentVolumeForResonance(&paramManager, song);
		song->backUpParamManager(this, clip, &paramManager, true);
	}

	return NO_ERROR;
}

int Kit::readDrumFromFile(Song* song, Clip* clip, int drumType, int32_t readAutomationUpToPos) {

	Drum* newDrum = storageManager.createNewDrum(drumType);
	if (!newDrum) return ERROR_INSUFFICIENT_RAM;

	int error = newDrum->readFromFile(
	    song, clip, readAutomationUpToPos); // Will create and "back up" a new ParamManager if anything to read into it
	if (error) {
		void* toDealloc = dynamic_cast<void*>(newDrum);
		newDrum->~Drum();
		generalMemoryAllocator.dealloc(toDealloc);
		return error;
	}
	addDrum(newDrum);

	return NO_ERROR;
}

// Returns true if more loading needed later
int Kit::loadAllAudioFiles(bool mayActuallyReadFiles) {

	int error = NO_ERROR;

	bool doingAlternatePath =
	    mayActuallyReadFiles && (audioFileManager.alternateLoadDirStatus == ALTERNATE_LOAD_DIR_NONE_SET);
	if (doingAlternatePath) {
		error = setupDefaultAudioFileDir();
		if (error) return error;
	}

	AudioEngine::logAction("Kit::loadAllSamples");
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (mayActuallyReadFiles && shouldAbortLoading()) {
			error = ERROR_ABORTED_BY_USER;
			goto getOut;
		}
		error = thisDrum->loadAllSamples(mayActuallyReadFiles);
		if (error) goto getOut;
	}

getOut:
	if (doingAlternatePath) {
		audioFileManager.thingFinishedLoading();
	}

	return error;
}

// Caller must check that there is an activeClip.
void Kit::loadCrucialAudioFilesOnly() {

	bool doingAlternatePath = (audioFileManager.alternateLoadDirStatus == ALTERNATE_LOAD_DIR_NONE_SET);
	if (doingAlternatePath) {
		int error = setupDefaultAudioFileDir();
		if (error) return;
	}

	AudioEngine::logAction("Kit::loadCrucialSamplesOnly");
	for (int i = 0; i < ((InstrumentClip*)activeClip)->noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = ((InstrumentClip*)activeClip)->noteRows.getElement(i);
		if (!thisNoteRow->muted && !thisNoteRow->hasNoNotes() && thisNoteRow->drum) {
			thisNoteRow->drum->loadAllSamples(true); // Why don't we deal with the error?
		}
	}

	if (doingAlternatePath) {
		audioFileManager.thingFinishedLoading();
	}
}

void Kit::addDrum(Drum* newDrum) {
	Drum** prevPointer = &firstDrum;
	while (*prevPointer != NULL)
		prevPointer = &((*prevPointer)->next);
	*prevPointer = newDrum;

	newDrum->kit = this;
}

void Kit::removeDrum(Drum* drum) {

	removeDrumFromLinkedList(drum);
	drumRemoved(drum);
}

void Kit::removeDrumFromLinkedList(Drum* drum) {
	Drum** prevPointer = &firstDrum;
	while (*prevPointer) {
		if (*prevPointer == drum) {
			*prevPointer = drum->next;
			return;
		}
		prevPointer = &((*prevPointer)->next);
	}
}

void Kit::drumRemoved(Drum* drum) {
	if (selectedDrum == drum) selectedDrum = NULL;

#if ALPHA_OR_BETA_VERSION
	int i = drumsWithRenderingActive.searchExact((int32_t)drum);
	if (i != -1) {
		numericDriver.freezeWithError("E321");
	}
#endif
}

Drum* Kit::getFirstUnassignedDrum(InstrumentClip* clip) {
	for (Drum* thisDrum = firstDrum; thisDrum != NULL; thisDrum = thisDrum->next) {
		if (!clip->getNoteRowForDrum(thisDrum)) return thisDrum;
	}

	return NULL;
}

int Kit::getDrumIndex(Drum* drum) {
	int index = 0;
	for (Drum* thisDrum = firstDrum; thisDrum != drum; thisDrum = thisDrum->next) {
		index++;
	}
	return index;
}

Drum* Kit::getDrumFromIndex(int index) {
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (index == 0) return thisDrum;
		index--;
	}

	// Drum not found. Just return the first one
	return firstDrum;
}

SoundDrum* Kit::getDrumFromName(char const* name, bool onlyIfNoNoteRow) {
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {

		if (onlyIfNoNoteRow && thisDrum->noteRowAssignedTemp) continue;

		if (thisDrum->type == DRUM_TYPE_SOUND && ((SoundDrum*)thisDrum)->name.equalsCaseIrrespective(name))
			return (SoundDrum*)thisDrum;
	}

	return NULL;
}

void Kit::cutAllSound() {
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		thisDrum->unassignAllVoices();
	}
}

// Beware - unlike usual, modelStack, a ModelStackWithThreeMainThings*,  might have a NULL timelineCounter
void Kit::renderGlobalEffectableForClip(ModelStackWithTimelineCounter* modelStack, StereoSample* globalEffectableBuffer,
                                        int32_t* bufferToTransferTo, int numSamples, int32_t* reverbBuffer,
                                        int32_t reverbAmountAdjust, int32_t sideChainHitPending,
                                        bool shouldLimitDelayFeedback, bool isClipActive, int32_t pitchAdjust,
                                        int32_t amplitudeAtStart, int32_t amplitudeAtEnd) {

	// Render Drums. Traverse backwards, in case one stops rendering (removing itself from the list) as we render it
	for (int d = drumsWithRenderingActive.getNumElements() - 1; d >= 0; d--) {
		Drum* thisDrum = (Drum*)drumsWithRenderingActive.getKeyAtIndex(d);

		if (ALPHA_OR_BETA_VERSION && thisDrum->type != DRUM_TYPE_SOUND) numericDriver.freezeWithError("E253");

		SoundDrum* soundDrum = (SoundDrum*)thisDrum;

		if (ALPHA_OR_BETA_VERSION && soundDrum->skippingRendering) numericDriver.freezeWithError("E254");

		ParamManager* drumParamManager;
		NoteRow* thisNoteRow = NULL;
		int noteRowIndex;

		if (activeClip) {
			thisNoteRow = ((InstrumentClip*)activeClip)->getNoteRowForDrum(thisDrum, &noteRowIndex);

			// If a new Clip had just launched on this Kit, but an old Drum was still sounding which isn't present in the new Clip.
			// In a perfect world, maybe we'd instead have it check and cut the voice / Drum on switch. This used to be E255
			if (!thisNoteRow) {
				soundDrum->unassignAllVoices();
				continue;
			}
			drumParamManager = &thisNoteRow->paramManager;
		}

		else {
			drumParamManager = modelStack->song->getBackedUpParamManagerPreferablyWithClip(soundDrum, NULL);
		}

		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addNoteRow(noteRowIndex, thisNoteRow)->addOtherTwoThings(soundDrum, drumParamManager);

		soundDrum->render(modelStackWithThreeMainThings, globalEffectableBuffer, numSamples, reverbBuffer,
		                  sideChainHitPending, reverbAmountAdjust, shouldLimitDelayFeedback,
		                  pitchAdjust); // According to our volume, we tell Drums to send less reverb
	}

	// Tick ParamManagers
	if (playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn && isClipActive) {

		NoteRowVector* noteRows = &((InstrumentClip*)activeClip)->noteRows;

		for (int i = 0; i < noteRows->getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows->getElement(i);
			if (thisNoteRow->drum
			    && thisNoteRow->drum->type
			           == DRUM_TYPE_SOUND) { // Just don't bother ticking other ones for now - their MPE doesn't need to interpolate.

				ParamCollectionSummary* patchedParamsSummary =
				    &thisNoteRow->paramManager
				         .summaries[1]; // No time to call the proper function and do error checking, sorry.
				if (patchedParamsSummary->whichParamsAreInterpolating[0]
				    || patchedParamsSummary->whichParamsAreInterpolating[1]
#if NUM_PARAMS > 64
				    || patchedParamsSummary->whichParamsAreInterpolating[2]
#endif
				) {
yesTickParamManager:
					ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
					    modelStack->addNoteRow(i, thisNoteRow)
					        ->addOtherTwoThings((SoundDrum*)thisNoteRow->drum, &thisNoteRow->paramManager);
					thisNoteRow->paramManager.tickSamples(numSamples, modelStackWithThreeMainThings);
					continue;
				}

				// Try other options too.

				ParamCollectionSummary* unpatchedParamsSummary =
				    &thisNoteRow->paramManager
				         .summaries[0]; // No time to call the proper function and do error checking, sorry.
				if (unpatchedParamsSummary->whichParamsAreInterpolating[0]
#if MAX_NUM_UNPATCHED_PARAM_FOR_SOUNDS > 32
				    || unpatchedParamsSummary->whichParamsAreInterpolating[1]
#endif
				) {
					goto yesTickParamManager;
				}

				ParamCollectionSummary* patchCablesSummary =
				    &thisNoteRow->paramManager
				         .summaries[2]; // No time to call the proper function and do error checking, sorry.
				if (patchCablesSummary->whichParamsAreInterpolating[0]
#if MAX_NUM_PATCH_CABLES > 32
				    || patchCablesSummary->whichParamsAreInterpolating[1]
#endif
				) {
					goto yesTickParamManager;
				}

				ParamCollectionSummary* expressionParamsSummary =
				    &thisNoteRow->paramManager
				         .summaries[3]; // No time to call the proper function and do error checking, sorry.
				if (expressionParamsSummary->whichParamsAreInterpolating[0]
#if NUM_EXPRESSION_DIMENSIONS > 32
				    || expressionParamsSummary->whichParamsAreInterpolating[1]
#endif
				) {
					goto yesTickParamManager;
				}
				// Was that right? Until Jan 2022 I didn't have it checking for expression params automation here for some reason...
			}
		}
	}
}

void Kit::renderOutput(ModelStack* modelStack, StereoSample* outputBuffer, StereoSample* outputBufferEnd,
                       int numSamples, int32_t* reverbBuffer, int32_t reverbAmountAdjust, int32_t sideChainHitPending,
                       bool shouldLimitDelayFeedback, bool isClipActive) {

	ParamManager* paramManager = getParamManager(modelStack->song);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(activeClip);
	// Beware - modelStackWithThreeMainThings might have a NULL timelineCounter

	GlobalEffectableForClip::renderOutput(modelStackWithTimelineCounter, paramManager, outputBuffer, numSamples,
	                                      reverbBuffer, reverbAmountAdjust, sideChainHitPending,
	                                      shouldLimitDelayFeedback, isClipActive, INSTRUMENT_TYPE_KIT, 8);
}

void Kit::offerReceivedCCToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
                                         ModelStackWithTimelineCounter* modelStack) {

	// Do it for this whole Kit
	ModControllableAudio::offerReceivedCCToLearnedParams(
	    fromDevice, channel, ccNumber, value,
	    modelStack); // NOTE: this call may change modelStack->timelineCounter etc!

	// Now do it for each NoteRow / Drum
	if (modelStack
	        ->timelineCounterIsSet()) { // This is always actually true currently for calls to this function, but let's make this safe and future proof.
		InstrumentClip* clip =
		    (InstrumentClip*)modelStack->getTimelineCounter(); // May have been changed by call above!
		for (int i = 0; i < clip->noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = clip->noteRows.getElement(i);
			Drum* thisDrum = thisNoteRow->drum;
			if (thisDrum && thisDrum->type == DRUM_TYPE_SOUND) {
				((SoundDrum*)thisDrum)
				    ->offerReceivedCCToLearnedParams(fromDevice, channel, ccNumber, value, modelStack, i);
			}
		}
	}
}

bool Kit::offerReceivedPitchBendToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2,
                                                ModelStackWithTimelineCounter* modelStack) {

	bool messageUsed;

	// Do it for this whole Kit
	messageUsed = ModControllableAudio::offerReceivedPitchBendToLearnedParams(
	    fromDevice, channel, data1, data2, modelStack); // NOTE: this call may change modelStack->timelineCounter etc!

	if (modelStack
	        ->timelineCounterIsSet()) { // This is always actually true currently for calls to this function, but let's make this safe and future proof.
		InstrumentClip* clip =
		    (InstrumentClip*)modelStack->getTimelineCounter(); // May have been changed by call above!
		for (int i = 0; i < clip->noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = clip->noteRows.getElement(i);
			Drum* thisDrum = thisNoteRow->drum;
			if (thisDrum && thisDrum->type == DRUM_TYPE_SOUND) {
				if (((SoundDrum*)thisDrum)
				        ->offerReceivedPitchBendToLearnedParams(fromDevice, channel, data1, data2, modelStack)) {
					messageUsed = true;
				}
			}
		}
	}

	return messageUsed;
}

void Kit::choke() {
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		thisDrum->choke(NULL);
	}
}

void Kit::resyncLFOs() {
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (thisDrum && thisDrum->type == DRUM_TYPE_SOUND) {
			((SoundDrum*)thisDrum)->resyncGlobalLFO();
		}
	}
}

ModControllable* Kit::toModControllable() {
	return this;
}

// newName must be allowed to be edited by this function
int Kit::makeDrumNameUnique(String* name, int startAtNumber) {

	Uart::println("making unique newName:");

	int originalLength = name->getLength();

	do {
		char numberString[12];
		intToString(startAtNumber, numberString);
		int error = name->concatenateAtPos(numberString, originalLength);
		if (error) return error;
		startAtNumber++;
	} while (getDrumFromName(name->get()));

	return NO_ERROR;
}

void Kit::setupWithoutActiveClip(ModelStack* modelStack) {

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(NULL);

	setupPatching(modelStackWithTimelineCounter);

	int count = 0;
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (thisDrum->type == DRUM_TYPE_SOUND) {

			if (!(count & 7)) {
				AudioEngine::routineWithClusterLoading(); // -----------------------------------
			}
			count++;

			SoundDrum* soundDrum = (SoundDrum*)thisDrum;

			ParamManager* paramManager = modelStackWithTimelineCounter->song->getBackedUpParamManagerPreferablyWithClip(
			    (ModControllableAudio*)soundDrum, NULL);
			if (!paramManager) numericDriver.freezeWithError("E174");

			soundDrum->patcher.performInitialPatching(soundDrum, (ParamManagerForTimeline*)paramManager);
		}
	}

	Instrument::setupWithoutActiveClip(modelStack);
}

// Accepts a ModelStack with NULL TimelineCounter
void Kit::setupPatching(ModelStackWithTimelineCounter* modelStack) {

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounterAllowNull();

	int count = 0;

	if (clip) {
		for (int i = 0; i < clip->noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = clip->noteRows.getElement(i);
			if (thisNoteRow->drum && thisNoteRow->drum->type == DRUM_TYPE_SOUND) {

				if (!(count & 7)) {
					AudioEngine::routineWithClusterLoading(); // -----------------------------------
				}
				count++;

				SoundDrum* soundDrum = (SoundDrum*)thisNoteRow->drum;

				ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
				    modelStack->addNoteRow(i, thisNoteRow)->addOtherTwoThings(soundDrum, &thisNoteRow->paramManager);

				soundDrum->ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(modelStackWithThreeMainThings);

				ModelStackWithParamCollection* modelStackWithParamCollection =
				    modelStackWithThreeMainThings->addParamCollectionSummary(
				        thisNoteRow->paramManager.getPatchCableSetSummary());
				((PatchCableSet*)modelStackWithParamCollection->paramCollection)
				    ->setupPatching(modelStackWithParamCollection);
			}
		}
	}

	else {
		for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
			if (thisDrum->type == DRUM_TYPE_SOUND) {

				if (!(count & 7)) {
					AudioEngine::routineWithClusterLoading(); // -----------------------------------
				}
				count++;

				SoundDrum* soundDrum = (SoundDrum*)thisDrum;

				ParamManager* paramManager =
				    modelStack->song->getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)soundDrum, NULL);
				if (!paramManager) numericDriver.freezeWithError("E172");

				soundDrum->ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(
				    (ParamManagerForTimeline*)paramManager);

				ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
				    modelStack->addOtherTwoThingsButNoNoteRow(soundDrum, paramManager);
				ModelStackWithParamCollection* modelStackWithParamCollection =
				    modelStackWithThreeMainThings->addParamCollectionSummary(paramManager->getPatchCableSetSummary());

				((PatchCableSet*)modelStackWithParamCollection->paramCollection)
				    ->setupPatching(modelStackWithParamCollection);
			}
		}
	}
}

bool Kit::setActiveClip(ModelStackWithTimelineCounter* modelStack, int maySendMIDIPGMs) {
	bool clipChanged = Instrument::setActiveClip(modelStack, maySendMIDIPGMs);

	if (clipChanged) {

		resetDrumTempValues();

		int count = 0;
		NoteRowVector* noteRows = &((InstrumentClip*)modelStack->getTimelineCounter())->noteRows;
		for (int i = 0; i < noteRows->getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows->getElement(i);

			if (thisNoteRow
			        ->drum) { // In a perfect world we'd do this for every Drum, even any without NoteRows in new Clip, but meh this'll be fine

				thisNoteRow->drum->noteRowAssignedTemp = true;
				thisNoteRow->drum->earlyNoteVelocity = 0;

				if (thisNoteRow->drum->type == DRUM_TYPE_SOUND) {

					if (!(count & 7)) {
						AudioEngine::
						    routineWithClusterLoading(); // ----------------------------------- I guess very often this wouldn't work cos the audio routine would be locked
					}
					count++;

					SoundDrum* soundDrum = (SoundDrum*)thisNoteRow->drum;

					soundDrum->patcher.performInitialPatching(soundDrum, &thisNoteRow->paramManager);
				}
			}
		}

		for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
			if (!thisDrum->noteRowAssignedTemp) {
				thisDrum->drumWontBeRenderedForAWhile();
			}
		}

		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
	return clipChanged;
}

void Kit::prepareForHibernationOrDeletion() {
	ModControllableAudio::wontBeRenderedForAWhile();

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		thisDrum->prepareForHibernation();
	}
}

void Kit::compensateInstrumentVolumeForResonance(ParamManagerForTimeline* paramManager, Song* song) {

	// If it was a pre-V1.2.0 firmware file, we need to compensate for resonance
	if (storageManager.firmwareVersionOfFileBeingRead < FIRMWARE_1P2P0
	    && !paramManager->resonanceBackwardsCompatibilityProcessed) {

		UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

		int32_t compensation =
		    interpolateTableSigned(unpatchedParams->getValue(PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_RES) + 2147483648, 32,
		                           oldResonanceCompensation, 3);
		float compensationDB = (float)compensation / (1024 << 16);

		if (compensationDB > 0.1)
			unpatchedParams->shiftParamVolumeByDB(PARAM_UNPATCHED_GLOBALEFFECTABLE_VOLUME, compensationDB);

		// The SoundDrums, like all Sounds, will have already had resonance compensation done on their default ParamManagers if and when any were in fact loaded.
		// Or, if we're going through a Song doing this to all ParamManagers within Clips, the Clip will automatically do all NoteRows / Drums next.

		GlobalEffectableForClip::compensateVolumeForResonance(paramManager);
	}
}

void Kit::deleteBackedUpParamManagers(Song* song) {
	song->deleteBackedUpParamManagersForModControllable(this);

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (thisDrum->type == DRUM_TYPE_SOUND) {
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
			song->deleteBackedUpParamManagersForModControllable((SoundDrum*)thisDrum);
		}
	}
}

// Returns num ticks til next arp event
int32_t Kit::doTickForwardForArp(ModelStack* modelStack, int32_t currentPos) {
	if (!activeClip) return 2147483647;

	bool clipIsActive = modelStack->song->isClipActive(activeClip);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(activeClip);

	int32_t ticksTilNextArpEvent = 2147483647;
	for (int i = 0; i < ((InstrumentClip*)activeClip)->noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = ((InstrumentClip*)activeClip)->noteRows.getElement(i);
		if (thisNoteRow->drum
		    && thisNoteRow->drum->type
		           == DRUM_TYPE_SOUND) { // For now, only SoundDrums have Arps, but that's actually a kinda pointless restriction...
			SoundDrum* soundDrum = (SoundDrum*)thisNoteRow->drum;

			ArpReturnInstruction instruction;

			ModelStackWithNoteRow* modelStackWithNoteRow = modelStackWithTimelineCounter->addNoteRow(i, thisNoteRow);

			bool shouldUseIndependentPlayPos = (clipIsActive && thisNoteRow->hasIndependentPlayPos());
			int32_t currentPosThisRow =
			    shouldUseIndependentPlayPos ? thisNoteRow->lastProcessedPosIfIndependent : currentPos;

			bool reversed = (clipIsActive && modelStackWithNoteRow->isCurrentlyPlayingReversed());

			int32_t ticksTilNextArpEventThisDrum = soundDrum->arpeggiator.doTickForward(
			    &soundDrum->arpSettings, &instruction, currentPosThisRow, reversed);

			ModelStackWithSoundFlags* modelStackWithSoundFlags =
			    modelStackWithNoteRow->addOtherTwoThings(soundDrum, &thisNoteRow->paramManager)->addSoundFlags();

			if (instruction.noteCodeOffPostArp != ARP_NOTE_NONE) {
				soundDrum->noteOffPostArpeggiator(modelStackWithSoundFlags, instruction.noteCodeOffPostArp);
			}

			if (instruction.noteCodeOnPostArp != ARP_NOTE_NONE) {
				soundDrum->noteOnPostArpeggiator(
				    modelStackWithSoundFlags, instruction.arpNoteOn->inputCharacteristics[MIDI_CHARACTERISTIC_NOTE],
				    instruction.noteCodeOnPostArp, instruction.arpNoteOn->velocity, instruction.arpNoteOn->mpeValues,
				    instruction.sampleSyncLengthOn, 0, 0);
			}

			ticksTilNextArpEvent = getMin(ticksTilNextArpEvent, ticksTilNextArpEventThisDrum);
		}
	}

	return ticksTilNextArpEvent;
}

GateDrum* Kit::getGateDrumForChannel(int gateChannel) {
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (thisDrum->type == DRUM_TYPE_GATE) {
			GateDrum* gateDrum = (GateDrum*)thisDrum;
			if (gateDrum->channel == gateChannel) return gateDrum;
		}
	}
	return NULL;
}

void Kit::resetDrumTempValues() {
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		thisDrum->noteRowAssignedTemp = 0;
	}
}

void Kit::getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
                                 GlobalEffectableForClip** globalEffectableWithMostReverb,
                                 int32_t* highestReverbAmountFound) {

	GlobalEffectableForClip::getThingWithMostReverb(activeClip, soundWithMostReverb, paramManagerWithMostReverb,
	                                                globalEffectableWithMostReverb, highestReverbAmountFound);

	if (activeClip) {

		for (int i = 0; i < ((InstrumentClip*)activeClip)->noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = ((InstrumentClip*)activeClip)->noteRows.getElement(i);
			if (!thisNoteRow->drum || thisNoteRow->drum->type != DRUM_TYPE_SOUND) continue;
			((SoundDrum*)thisNoteRow->drum)
			    ->getThingWithMostReverb(soundWithMostReverb, paramManagerWithMostReverb,
			                             globalEffectableWithMostReverb, highestReverbAmountFound,
			                             &thisNoteRow->paramManager);
		}
	}
}

void Kit::offerReceivedNote(ModelStackWithTimelineCounter* modelStack, MIDIDevice* fromDevice, bool on, int channel,
                            int note, int velocity, bool shouldRecordNotes, bool* doingMidiThru) {

	InstrumentClip* instrumentClip = (InstrumentClip*)modelStack->getTimelineCounterAllowNull(); // Yup it might be NULL

	bool recordingNoteOnEarly = false;
	bool lookingForFirstDrumForNoteOn = on;

	bool shouldRecordNoteOn =
	    shouldRecordNotes && instrumentClip
	    && currentSong->isClipActive(instrumentClip); // Even if this comes out as false here, there are some
	                                                  // special cases below where we might insist on making
	                                                  // it true
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {

		// If this is the "input" command, to sound / audition the Drum...
		// Returns true if midi channel and note match the learned midi note
		// Calls equalsChannelAllowMPE to check channel equivalence
		// Convert channel+device into zone before comparison to stop crossover between MPE and non MPE channels
		int channelOrZone = fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].channelToZone(channel);
		if (thisDrum->midiInput.equalsNoteOrCCAllowMPE(fromDevice, channelOrZone, note)) {

			// If MIDIDrum, outputting same note, then don't additionally do thru
			if (doingMidiThru && thisDrum->type == DRUM_TYPE_MIDI && ((MIDIDrum*)thisDrum)->channel == channel
			    && ((MIDIDrum*)thisDrum)->note == note) {
				*doingMidiThru = false;
			}

			// Just once, for first Drum we're doing a note-on on, see if we want to switch to a different InstrumentClip, for a couple of reasons
			if (lookingForFirstDrumForNoteOn && instrumentClip && shouldRecordNotes) {
				lookingForFirstDrumForNoteOn = false;

				// Firstly, if recording session to arranger...
				if (playbackHandler.recording == RECORDING_ARRANGEMENT) {

					instrumentClip->possiblyCloneForArrangementRecording(modelStack);

					instrumentClip =
					    (InstrumentClip*)modelStack->getTimelineCounter(); // Re-get it, cos it might have changed

					if (instrumentClip->isArrangementOnlyClip()) shouldRecordNoteOn = true;
				}

				// If count-in is on, we only got here if it's very nearly finished
				else if (currentUIMode == UI_MODE_RECORD_COUNT_IN) {
goingToRecordNoteOnEarly:
					recordingNoteOnEarly = true;
					shouldRecordNoteOn = false;
				}

				// And another special case - if there's a pending overdub beginning really soon,
				// and activeClip is not linearly recording (and maybe not even active)...
				else if (currentPlaybackMode == &session && session.launchEventAtSwungTickCount
				         && !instrumentClip->getCurrentlyRecordingLinearly()) {
					int ticksTilLaunch =
					    session.launchEventAtSwungTickCount - playbackHandler.getActualSwungTickCount();
					int samplesTilLaunch = ticksTilLaunch * playbackHandler.getTimePerInternalTick();
					if (samplesTilLaunch <= LINEAR_RECORDING_EARLY_FIRST_NOTE_ALLOWANCE) {
						Clip* clipAboutToRecord = currentSong->getClipWithOutputAboutToBeginLinearRecording(this);
						if (clipAboutToRecord) {
							goto goingToRecordNoteOnEarly;
						}
					}
				}
			}

			ModelStackWithNoteRow* modelStackWithNoteRow;

			NoteRow* thisNoteRow = NULL; // Will only be set to true if there's a Clip / activeClip

			if (instrumentClip) {
				modelStackWithNoteRow = instrumentClip->getNoteRowForDrum(modelStack, thisDrum);
				thisNoteRow = modelStackWithNoteRow->getNoteRowAllowNull();
				if (!thisNoteRow) continue; // Yeah, we won't even let them sound one with no NoteRow
			}
			else {
				modelStackWithNoteRow = modelStack->addNoteRow(0, NULL);
			}

			if (recordingNoteOnEarly) {
				bool allowingNoteTails = instrumentClip && instrumentClip->allowNoteTails(modelStackWithNoteRow);
				thisDrum->recordNoteOnEarly(velocity, allowingNoteTails);
			}

			// Note-on
			if (on) {

				// If input is MPE, we need to give the Drum the most recent MPE expression values received on the channel on the Device. It doesn't keep track of these when a note isn't on, and
				// even if it did, this new note might be on a different channel (just same notecode).
				if (thisDrum->midiInput.isForMPEZone()) {
					for (int i = 0; i < NUM_EXPRESSION_DIMENSIONS; i++) {
						thisDrum->lastExpressionInputsReceived[BEND_RANGE_FINGER_LEVEL][i] =
						    fromDevice->defaultInputMPEValuesPerMIDIChannel[channel][i] >> 8;
					}
				}

				// And if non-MPE input, just set those finger-level MPE values to 0. If an MPE instrument had been used just before, it could have left them set to something.
				else {
					for (int i = 0; i < NUM_EXPRESSION_DIMENSIONS; i++) {
						thisDrum->lastExpressionInputsReceived[BEND_RANGE_FINGER_LEVEL][i] = 0;
					}
				}

				int16_t mpeValues[NUM_EXPRESSION_DIMENSIONS];
				thisDrum->getCombinedExpressionInputs(mpeValues);

				// MPE stuff - if editing note, we need to take note of the initial values which might have been sent before this note-on.
				instrumentClipView.reportMPEInitialValuesForNoteEditing(modelStackWithNoteRow, mpeValues);

				if (!thisNoteRow || !thisNoteRow->soundingStatus) {

					if (thisNoteRow && shouldRecordNoteOn) {

						int16_t const* mpeValuesOrNull = NULL;

						if (fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].isChannelPartOfAnMPEZone(channel)) {
							mpeValuesOrNull = mpeValues;
						}

						instrumentClip->recordNoteOn(modelStackWithNoteRow, velocity, false, mpeValuesOrNull);
						if (getRootUI()) getRootUI()->noteRowChanged(instrumentClip, thisNoteRow);
					}
					// TODO: possibly should change the MPE params' currentValue to the initial values, since that usually does get updated by the
					// subsequent MPE that will come in. Or does that not matter?

					if (thisNoteRow && thisDrum->type == DRUM_TYPE_SOUND
					    && !thisNoteRow->paramManager.containsAnyMainParamCollections())
						numericDriver.freezeWithError("E326"); // Trying to catch an E313 that Vinz got

					beginAuditioningforDrum(modelStackWithNoteRow, thisDrum, velocity, mpeValues, channel);
				}
			}

			// Note-off
			else {
				if (thisNoteRow) {
					if (shouldRecordNotes && thisDrum->auditioned
					    && ((playbackHandler.recording == RECORDING_ARRANGEMENT
					         && instrumentClip->isArrangementOnlyClip())
					        || currentSong->isClipActive(instrumentClip))) {

						if (playbackHandler.recording == RECORDING_ARRANGEMENT
						    && !instrumentClip->isArrangementOnlyClip()) {}
						else {
							instrumentClip->recordNoteOff(modelStackWithNoteRow, velocity);
							if (getRootUI()) getRootUI()->noteRowChanged(instrumentClip, thisNoteRow);
						}
					}
					instrumentClipView.reportNoteOffForMPEEditing(modelStackWithNoteRow);

					// MPE-controlled params are a bit special in that we can see (via this note-off) when the user has removed their finger and won't be sending more
					// values. So, let's unlatch those params now.
					ExpressionParamSet* mpeParams = thisNoteRow->paramManager.getExpressionParamSet();
					if (mpeParams) {
						mpeParams->cancelAllOverriding();
					}
				}
				endAuditioningForDrum(
				    modelStackWithNoteRow, thisDrum,
				    velocity); // Do this even if not marked as auditioned, to avoid stuck notes in cases like if two note-ons were sent
			}
		}

		// Or if this is the Drum's mute command...
		if (instrumentClip && on && thisDrum->muteMIDICommand.equalsNoteOrCC(fromDevice, channel, note)) {

			ModelStackWithNoteRow* modelStackWithNoteRow = instrumentClip->getNoteRowForDrum(modelStack, thisDrum);

			NoteRow* thisNoteRow = modelStackWithNoteRow->getNoteRowAllowNull();

			if (thisNoteRow) {
				instrumentClip->toggleNoteRowMute(modelStackWithNoteRow);
				uiNeedsRendering(&instrumentClipView, 0, 0xFFFFFFFF);
			}
		}
	}
}

void Kit::offerReceivedPitchBend(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
                                 uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru) {

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (thisDrum->midiInput.equalsChannelAllowMPE(fromDevice, channel)) {
			int level = BEND_RANGE_MAIN;
			if (thisDrum->midiInput.isForMPEZone()) { // If Drum has MPE input.
				if (channel
				    == thisDrum->midiInput
				           .getMasterChannel()) { // Message coming in on master channel - that's "main"/zone-level, too.
					goto yesThisDrum;
				}
				if (channel
				    == thisDrum
				           ->lastMIDIChannelAuditioned) { // Or if per-finger level, check the member channel of the message matches the one sounding on the Drum right now.
					level = BEND_RANGE_FINGER_LEVEL;
					goto yesThisDrum;
				}
			}
			else { // Or, if Drum does not have MPE input, then this is a channel-level message.
yesThisDrum:
				int16_t value16 = (((uint32_t)data1 | ((uint32_t)data2 << 7)) - 8192) << 2;
				thisDrum->expressionEventPossiblyToRecord(modelStackWithTimelineCounter, value16, 0, level);
			}
		}
	}
}

void Kit::offerReceivedCC(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
                          uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru) {

	if (ccNumber != 74) return;
	if (!fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].isChannelPartOfAnMPEZone(channel)) return;

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (thisDrum->midiInput.equalsChannelAllowMPE(fromDevice, channel)) {

			if (thisDrum->midiInput.isForMPEZone()) { // If Drum has MPE input.
				int level = BEND_RANGE_MAIN;
				if (channel
				    == thisDrum->midiInput
				           .getMasterChannel()) { // Message coming in on master channel - that's "main"/zone-level, too.
					goto yesThisDrum;
				}
				if (channel
				    == thisDrum
				           ->lastMIDIChannelAuditioned) { // Or if per-finger level, check the member channel of the message matches the one sounding on the Drum right now.
					level = BEND_RANGE_FINGER_LEVEL;
yesThisDrum:
					int16_t value16 = (value - 64) << 9;
					thisDrum->expressionEventPossiblyToRecord(modelStackWithTimelineCounter, value16, 1, level);
				}
			}

			// If not an MPE input, we don't want to respond to this CC74 at all (for this Drum).
		}
	}
}

// noteCode -1 means channel-wide, including for MPE input (which then means it could still then just apply to one note).
// This function could be optimized a bit better, there are lots of calls to similar functions.
void Kit::offerReceivedAftertouch(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
                                  int channel, int value, int noteCode, bool* doingMidiThru) {

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		int level = BEND_RANGE_MAIN;
		if (noteCode == -1) { // Channel pressure message...
			if (thisDrum->midiInput.equalsChannelAllowMPE(fromDevice, channel)) {
				if (thisDrum->midiInput.isForMPEZone()) { // If Drum has MPE input.
					if (channel
					    == thisDrum->midiInput
					           .getMasterChannel()) { // Message coming in on master channel - that's "main"/zone-level, too.
						goto yesThisDrum;
					}
					if (channel
					    == thisDrum
					           ->lastMIDIChannelAuditioned) { // Or if per-finger level, check the member channel of the message matches the one sounding on the Drum right now.
						level = BEND_RANGE_FINGER_LEVEL;
						goto yesThisDrum;
					}
				}
				else { // Or, if Drum does not have MPE input, then this is a channel-level message.
yesThisDrum:
					int32_t value15 = value << 8;
					thisDrum->expressionEventPossiblyToRecord(modelStackWithTimelineCounter, value15, 2, level);
				}
			}
		}

		// Or a polyphonic aftertouch message - these aren't allowed for MPE except on the "master" channel.
		else {
			if (thisDrum->midiInput.equalsNoteOrCCAllowMPEMasterChannels(fromDevice, channel, noteCode)
			    && channel == thisDrum->lastMIDIChannelAuditioned) {
				level = BEND_RANGE_FINGER_LEVEL;
				goto yesThisDrum;
			}
		}
	}
}

void Kit::offerBendRangeUpdate(ModelStack* modelStack, MIDIDevice* device, int channelOrZone, int whichBendRange,
                               int bendSemitones) {

	if (whichBendRange == BEND_RANGE_MAIN)
		return; // This is not used in Kits for Drums. Drums use their BEND_RANGE_FINGER_LEVEL for both kinds of bend.
		    // TODO: Hmm, for non-MPE instruments we'd want to use this kind of bend range update and just paste it into BEND_RANGE_FINGER_LEVEL though...

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (thisDrum->midiInput.equalsChannelOrZone(device, channelOrZone)) {

			if (activeClip) {
				NoteRow* noteRow = ((InstrumentClip*)activeClip)->getNoteRowForDrum(thisDrum);
				if (noteRow) {
					ExpressionParamSet* expressionParams = noteRow->paramManager.getOrCreateExpressionParamSet();
					if (expressionParams) {
						if (!expressionParams->params[0].isAutomated()) {
							expressionParams->bendRanges[whichBendRange] = bendSemitones;
						}
					}
				}
			}
			else {
				//ParamManager* paramManager = modelStack->song->getBackedUpParamManagerPreferablyWithClip(thisDrum, NULL); // TODO...
			}
		}
	}
}

bool Kit::isNoteRowStillAuditioningAsLinearRecordingEnded(NoteRow* noteRow) {
	return (noteRow->drum && noteRow->drum->auditioned && !noteRow->drum->earlyNoteVelocity);
}

void Kit::stopAnyAuditioning(ModelStack* modelStack) {

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(activeClip);

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (thisDrum->auditioned) {
			ModelStackWithNoteRow* modelStackWithNoteRow =
			    activeClip ? ((InstrumentClip*)activeClip)->getNoteRowForDrum(modelStackWithTimelineCounter, thisDrum)
			               : modelStackWithTimelineCounter->addNoteRow(0, NULL);

			endAuditioningForDrum(modelStackWithNoteRow, thisDrum);
		}
	}
}

bool Kit::isAnyAuditioningHappening() {
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (thisDrum->auditioned) {
			return true;
		}
	}
	return false;
}

// You must supply noteRow if there is an activeClip with a NoteRow for that Drum. The TimelineCounter should be the activeClip.
// Drum must not be NULL - check first if not sure!
void Kit::beginAuditioningforDrum(ModelStackWithNoteRow* modelStack, Drum* drum, int velocity, int16_t const* mpeValues,
                                  int fromMIDIChannel) {

	ParamManager* paramManagerForDrum = NULL;

	if (modelStack->getNoteRowAllowNull()) {
		paramManagerForDrum = &modelStack->getNoteRow()->paramManager;
		if (!paramManagerForDrum->containsAnyMainParamCollections() && drum->type == DRUM_TYPE_SOUND)
			numericDriver.freezeWithError("E313"); // Vinz got this!
	}
	else {
		if (drum->type == DRUM_TYPE_SOUND) {
			paramManagerForDrum = modelStack->song->getBackedUpParamManagerPreferablyWithClip((SoundDrum*)drum, NULL);
			if (!paramManagerForDrum)
				numericDriver.freezeWithError(
				    "E314"); // Ron got this, June 2020, while "dragging" a row vertically in arranger
		}
	}

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThings(drum->toModControllable(), paramManagerForDrum);

	drum->noteOn(modelStackWithThreeMainThings, velocity, this, mpeValues, fromMIDIChannel);

	if (!activeClip || ((InstrumentClip*)activeClip)->allowNoteTails(modelStack)) {
		drum->auditioned = true;
	}

	drum->lastMIDIChannelAuditioned = fromMIDIChannel;
}

// Check that it's auditioned before calling this if you don't want it potentially sending an extra note-off in some rare cases.
// You must supply noteRow if there is an activeClip with a NoteRow for that Drum. The TimelineCounter should be the activeClip.
void Kit::endAuditioningForDrum(ModelStackWithNoteRow* modelStack, Drum* drum, int velocity) {

	drum->auditioned = false;
	drum->lastMIDIChannelAuditioned = MIDI_CHANNEL_NONE; // So it won't record any more MPE
	drum->earlyNoteStillActive = false;

	ParamManager* paramManagerForDrum = NULL;

	if (drum->type == DRUM_TYPE_SOUND) {

		NoteRow* noteRow = modelStack->getNoteRowAllowNull();

		if (noteRow) {
			paramManagerForDrum = &noteRow->paramManager;
			goto gotParamManager;
		}

		// If still here, haven't found paramManager yet
		paramManagerForDrum = modelStack->song->getBackedUpParamManagerPreferablyWithClip((SoundDrum*)drum, NULL);
		if (!paramManagerForDrum)
			numericDriver.freezeWithError("E312"); // Should make ALPHA_OR_BETA_VERSION after V3.0.0 release
	}

gotParamManager:

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThings(drum->toModControllable(), paramManagerForDrum);

	drum->noteOff(modelStackWithThreeMainThings);

	if (activeClip) activeClip->expectEvent(); // Because the absence of auditioning here means sequenced notes may play
}

// for (Drum* drum = firstDrum; drum; drum = drum->next) {
