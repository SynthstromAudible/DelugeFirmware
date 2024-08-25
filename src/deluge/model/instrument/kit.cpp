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

#include "model/instrument/kit.h"
#include "definitions_cxx.hpp"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "io/debug/log.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/drum/gate_drum.h"
#include "model/drum/midi_drum.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "playback/mode/playback_mode.h"
#include "playback/mode/session.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"
#include "processing/stem_export/stem_export.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/storage_manager.h"
#include <cstring>

namespace params = deluge::modulation::params;

Kit::Kit() : Instrument(OutputType::KIT), drumsWithRenderingActive(sizeof(Drum*)) {
	firstDrum = nullptr;
	selectedDrum = nullptr;
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
		delugeDealloc(toDealloc);
	}
}

Drum* Kit::getNextDrum(Drum* fromDrum) {
	if (fromDrum == NULL) {
		return firstDrum;
	}
	else {
		return fromDrum->next;
	}
}

Drum* Kit::getPrevDrum(Drum* fromDrum) {
	if (fromDrum == firstDrum) {
		return NULL;
	}

	Drum* thisDrum = firstDrum;
	while (thisDrum->next != fromDrum) {
		thisDrum = thisDrum->next;
	}
	return thisDrum;
}

bool Kit::writeDataToFile(Serializer& writer, Clip* clipForSavingOutputOnly, Song* song) {

	Instrument::writeDataToFile(writer, clipForSavingOutputOnly, song);

	ParamManager* paramManager;
	// saving preset
	if (clipForSavingOutputOnly) {

		paramManager = &clipForSavingOutputOnly->paramManager;
	}
	// saving song
	else {
		paramManager = NULL;

		// If no activeClip, that means no Clip has this Instrument, so there should be a backedUpParamManager that we
		// should use
		if (!activeClip) {
			paramManager = song->getBackedUpParamManagerPreferablyWithClip(this, NULL);
		}
	}

	GlobalEffectableForClip::writeAttributesToFile(writer, clipForSavingOutputOnly == NULL);

	writer.writeOpeningTagEnd(); // ---------------------------------------------------------------------------
	                             // Attributes end
	// saving song
	if (!clipForSavingOutputOnly) {
		if (midiInput.containsSomething()) {
			midiInput.writeNoteToFile(writer, "MIDIInput");
		}
	}
	GlobalEffectableForClip::writeTagsToFile(writer, paramManager, clipForSavingOutputOnly == NULL);

	writer.writeArrayStart("soundSources"); // TODO: change this?
	int32_t selectedDrumIndex = -1;
	int32_t drumIndex = 0;

	Drum* newFirstDrum = NULL;
	Drum** newLastDrum = &newFirstDrum;

	Clip* clipToTakeDrumOrderFrom = clipForSavingOutputOnly;
	if (!clipToTakeDrumOrderFrom) {
		clipToTakeDrumOrderFrom = song->getClipWithOutput(this, false, NULL);
	}

	// If we have a Clip to take the Drum order from...
	if (clipToTakeDrumOrderFrom) {
		// First, write Drums in the order of their NoteRows. Remove these drums from our list - we'll re-add them in a
		// moment, at the start, i.e. in the same order they appear in the file
		for (int32_t i = 0; i < ((InstrumentClip*)clipToTakeDrumOrderFrom)->noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = ((InstrumentClip*)clipToTakeDrumOrderFrom)->noteRows.getElement(i);
			if (thisNoteRow->drum) {
				Drum* drum = thisNoteRow->drum;

				ParamManagerForTimeline* paramManagerForDrum = NULL;

				// If saving Kit (not Song)
				if (clipForSavingOutputOnly) {
					paramManagerForDrum = &thisNoteRow->paramManager;
				}
				// Or if saving Song, we know there's a NoteRow, so no need to save the ParamManager

				writeDrumToFile(writer, drum, paramManagerForDrum, (clipForSavingOutputOnly == NULL),
				                &selectedDrumIndex, &drumIndex, song);

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
		if (!thisDrum) {
			break;
		}

		ParamManager* paramManagerForDrum = NULL;

		// If saving Kit (not song), only save Drums if some other NoteRow in the song has it - in which case, save as
		// "default" the params from that NoteRow
		if (clipForSavingOutputOnly) {
			D_PRINTLN("yup, clipForSavingOutputOnly");
			NoteRow* noteRow = song->findNoteRowForDrum(this, thisDrum);
			if (!noteRow) {
				goto moveOn;
			}
			paramManagerForDrum =
			    &noteRow->paramManager; // Of course there won't be one if it's a NonAudioDrum, but that's fine
		}

		// Or if saving song...
		else {
			// If no activeClip, this means we want to store all Drums
			// - and for SoundDrums, save as "default" any backedUpParamManagers (if none for a SoundDrum, definitely
			// skip it)
			if (!activeClip) {
				D_PRINTLN("nah, !activeClip");
				if (thisDrum->type == DrumType::SOUND) {
					paramManagerForDrum = song->getBackedUpParamManagerPreferablyWithClip((SoundDrum*)thisDrum, NULL);
					if (!paramManagerForDrum) {
						goto moveOn;
					}
				}
			}

			// Otherwise, if some Clip does have this Kit, then yes do save this Drum - with no ParamManager though
			else {

				// ... but, if no NoteRow has this Drum, we actually want to delete it now, so that its existence
				// doesn't affect drumIndexes!
				if (!song->findNoteRowForDrum(this, thisDrum)) {

					*prevPointer = thisDrum->next;

					if (thisDrum->type == DrumType::SOUND) {
						song->deleteBackedUpParamManagersForModControllable((SoundDrum*)thisDrum);
					}

					drumRemoved(thisDrum);

					void* toDealloc = dynamic_cast<void*>(thisDrum);
					thisDrum->~Drum();
					delugeDealloc(toDealloc);

					continue;
				}
			}
		}

		writeDrumToFile(writer, thisDrum, paramManagerForDrum, clipForSavingOutputOnly == NULL, &selectedDrumIndex,
		                &drumIndex, song);

moveOn:
		prevPointer = &thisDrum->next;
	}

	writer.writeArrayEnding("soundSources");

	*newLastDrum = firstDrum;
	firstDrum = newFirstDrum;

	if (selectedDrumIndex != -1) {
		writer.writeTag((char*)"selectedDrumIndex", selectedDrumIndex);
	}

	return true;
}

void Kit::writeDrumToFile(Serializer& writer, Drum* thisDrum, ParamManager* paramManagerForDrum, bool savingSong,
                          int32_t* selectedDrumIndex, int32_t* drumIndex, Song* song) {
	if (thisDrum == selectedDrum) {
		*selectedDrumIndex = *drumIndex;
	}

	thisDrum->writeToFile(writer, savingSong, paramManagerForDrum);
	(*drumIndex)++;
}

Error Kit::readFromFile(Deserializer& reader, Song* song, Clip* clip, int32_t readAutomationUpToPos) {

	int32_t selectedDrumIndex = -1;

	ParamManagerForTimeline paramManager;

	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {

		if (!strcmp(tagName, "soundSources")) {
			reader.match('[');
			while (reader.match('{') && *(tagName = reader.readNextTagOrAttributeName())) {
				DrumType drumType;

				if (!strcmp(tagName, "sample") || !strcmp(tagName, "synth") || !strcmp(tagName, "sound")) {
					drumType = DrumType::SOUND;
doReadDrum:
					reader.match('{');
					Error error = readDrumFromFile(reader, song, clip, drumType, readAutomationUpToPos);
					if (error != Error::NONE) {
						return error;
					}
					reader.match('}');          // Exit value.
					reader.exitTag(NULL, true); // Exit box.
				}
				else if (!strcmp(tagName, "midiOutput")) {
					drumType = DrumType::MIDI;
					goto doReadDrum;
				}
				else if (!strcmp(tagName, "gateOutput")) {
					drumType = DrumType::GATE;
					goto doReadDrum;
				}
				else {
					reader.exitTag(tagName);
				}
			}
			reader.match(']');
			reader.exitTag("soundSources");
		}
		else if (!strcmp(tagName, "selectedDrumIndex")) {
			selectedDrumIndex = reader.readTagOrAttributeValueInt();
			reader.exitTag("selectedDrumIndex");
		}
		else if (!strcmp(tagName, "MIDIInput")) {
			midiInput.readNoteFromFile(reader);
			reader.exitTag();
		}
		else {
			Error result =
			    GlobalEffectableForClip::readTagFromFile(reader, tagName, &paramManager, readAutomationUpToPos, song);
			if (result == Error::NONE) {}
			else if (result != Error::RESULT_TAG_UNUSED) {
				return result;
			}
			else {
				if (Instrument::readTagFromFile(reader, tagName)) {}
				else {
					Error result = reader.tryReadingFirmwareTagFromFile(tagName, false);
					if (result != Error::NONE && result != Error::RESULT_TAG_UNUSED) {
						return result;
					}
					reader.exitTag(tagName);
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

	return Error::NONE;
}

Error Kit::readDrumFromFile(Deserializer& reader, Song* song, Clip* clip, DrumType drumType,
                            int32_t readAutomationUpToPos) {

	Drum* newDrum = StorageManager::createNewDrum(drumType);
	if (!newDrum) {
		return Error::INSUFFICIENT_RAM;
	}

	Error error = newDrum->readFromFile(
	    reader, song, clip,
	    readAutomationUpToPos); // Will create and "back up" a new ParamManager if anything to read into it
	if (error != Error::NONE) {
		void* toDealloc = dynamic_cast<void*>(newDrum);
		newDrum->~Drum();
		delugeDealloc(toDealloc);
		return error;
	}
	addDrum(newDrum);

	return Error::NONE;
}

// Returns true if more loading needed later
Error Kit::loadAllAudioFiles(bool mayActuallyReadFiles) {

	Error error = Error::NONE;

	bool doingAlternatePath =
	    mayActuallyReadFiles && (audioFileManager.alternateLoadDirStatus == AlternateLoadDirStatus::NONE_SET);
	if (doingAlternatePath) {
		error = setupDefaultAudioFileDir();
		if (error != Error::NONE) {
			return error;
		}
	}

	AudioEngine::logAction("Kit::loadAllSamples");
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (mayActuallyReadFiles && shouldAbortLoading()) {
			error = Error::ABORTED_BY_USER;
			goto getOut;
		}
		error = thisDrum->loadAllSamples(mayActuallyReadFiles);
		if (error != Error::NONE) {
			goto getOut;
		}
	}

getOut:
	if (doingAlternatePath) {
		audioFileManager.thingFinishedLoading();
	}

	return error;
}

// Caller must check that there is an activeClip.
void Kit::loadCrucialAudioFilesOnly() {

	bool doingAlternatePath = (audioFileManager.alternateLoadDirStatus == AlternateLoadDirStatus::NONE_SET);
	if (doingAlternatePath) {
		Error error = setupDefaultAudioFileDir();
		if (error != Error::NONE) {
			return;
		}
	}

	AudioEngine::logAction("Kit::loadCrucialSamplesOnly");
	for (int32_t i = 0; i < ((InstrumentClip*)activeClip)->noteRows.getNumElements(); i++) {
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
	while (*prevPointer != NULL) {
		prevPointer = &((*prevPointer)->next);
	}
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
	if (selectedDrum == drum) {
		selectedDrum = NULL;
	}

#if ALPHA_OR_BETA_VERSION
	int32_t i = drumsWithRenderingActive.searchExact((int32_t)drum);
	if (i != -1) {
		FREEZE_WITH_ERROR("E321");
	}
#endif
}

Drum* Kit::getFirstUnassignedDrum(InstrumentClip* clip) {
	for (Drum* thisDrum = firstDrum; thisDrum != NULL; thisDrum = thisDrum->next) {
		if (!clip->getNoteRowForDrum(thisDrum)) {
			return thisDrum;
		}
	}

	return NULL;
}

int32_t Kit::getDrumIndex(Drum* drum) {
	int32_t index = 0;
	Drum* thisDrum;
	for (thisDrum = firstDrum; thisDrum && thisDrum != drum; thisDrum = thisDrum->next) {
		index++;
	}
	return thisDrum ? index : -1;
}

Drum* Kit::getDrumFromIndex(int32_t index) {
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (index == 0) {
			return thisDrum;
		}
		index--;
	}

	// Drum not found. Just return the first one
	return firstDrum;
}

Drum* Kit::getDrumFromIndexAllowNull(int32_t index) {
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (index == 0) {
			return thisDrum;
		}
		index--;
	}

	// Drum not found, return nullptr
	return nullptr;
}

SoundDrum* Kit::getDrumFromName(char const* name, bool onlyIfNoNoteRow) {
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {

		if (onlyIfNoNoteRow && thisDrum->noteRowAssignedTemp) {
			continue;
		}

		if (thisDrum->type == DrumType::SOUND && ((SoundDrum*)thisDrum)->name.equalsCaseIrrespective(name)) {
			return (SoundDrum*)thisDrum;
		}
	}

	return NULL;
}

void Kit::cutAllSound() {
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		thisDrum->unassignAllVoices();
	}
}

// Beware - unlike usual, modelStack, a ModelStackWithThreeMainThings*,  might have a NULL timelineCounter
bool Kit::renderGlobalEffectableForClip(ModelStackWithTimelineCounter* modelStack, StereoSample* globalEffectableBuffer,
                                        int32_t* bufferToTransferTo, int32_t numSamples, int32_t* reverbBuffer,
                                        int32_t reverbAmountAdjust, int32_t sideChainHitPending,
                                        bool shouldLimitDelayFeedback, bool isClipActive, int32_t pitchAdjust,
                                        int32_t amplitudeAtStart, int32_t amplitudeAtEnd) {
	bool rendered = false;
	// Render Drums. Traverse backwards, in case one stops rendering (removing itself from the list) as we render it
	for (int32_t d = drumsWithRenderingActive.getNumElements() - 1; d >= 0; d--) {
		Drum* thisDrum = (Drum*)drumsWithRenderingActive.getKeyAtIndex(d);

		if (ALPHA_OR_BETA_VERSION && thisDrum->type != DrumType::SOUND) {
			FREEZE_WITH_ERROR("E253");
		}

		SoundDrum* soundDrum = (SoundDrum*)thisDrum;

		if (ALPHA_OR_BETA_VERSION && soundDrum->skippingRendering) {
			FREEZE_WITH_ERROR("E254");
		}

		ParamManager* drumParamManager;
		NoteRow* thisNoteRow = NULL;
		int32_t noteRowIndex;

		if (activeClip) {
			thisNoteRow = ((InstrumentClip*)activeClip)->getNoteRowForDrum(thisDrum, &noteRowIndex);

			// If a new Clip had just launched on this Kit, but an old Drum was still sounding which isn't present in
			// the new Clip. In a perfect world, maybe we'd instead have it check and cut the voice / Drum on switch.
			// This used to be E255
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
		                  sideChainHitPending, reverbAmountAdjust, shouldLimitDelayFeedback, pitchAdjust,
		                  nullptr); // According to our volume, we tell Drums to send less reverb
		rendered = true;
	}

	// Tick ParamManagers
	if (playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn && isClipActive) {

		NoteRowVector* noteRows = &((InstrumentClip*)activeClip)->noteRows;

		for (int32_t i = 0; i < noteRows->getNumElements(); i++) {
			NoteRow* thisNoteRow = noteRows->getElement(i);

			// Just don't bother ticking other ones for now - their MPE doesn't need to interpolate.
			if (thisNoteRow->drum && thisNoteRow->drum->type == DrumType::SOUND) {

				// No time to call the proper function and do error checking, sorry.
				ParamCollectionSummary* patchedParamsSummary = &thisNoteRow->paramManager.summaries[1];

				bool anyInterpolating = false;
				if constexpr (deluge::modulation::params::kNumParams > 64) {
					anyInterpolating = patchedParamsSummary->whichParamsAreInterpolating[0]
					                   || patchedParamsSummary->whichParamsAreInterpolating[1]
					                   || patchedParamsSummary->whichParamsAreInterpolating[2];
				}
				else {
					anyInterpolating = patchedParamsSummary->whichParamsAreInterpolating[0]
					                   || patchedParamsSummary->whichParamsAreInterpolating[1];
				}
				if (anyInterpolating) {
yesTickParamManager:
					ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
					    modelStack->addNoteRow(i, thisNoteRow)
					        ->addOtherTwoThings((SoundDrum*)thisNoteRow->drum, &thisNoteRow->paramManager);
					thisNoteRow->paramManager.tickSamples(numSamples, modelStackWithThreeMainThings);
					continue;
				}

				// Try other options too.

				// No time to call the proper function and do error checking, sorry.
				ParamCollectionSummary* unpatchedParamsSummary = &thisNoteRow->paramManager.summaries[0];
				if constexpr (params::UNPATCHED_SOUND_MAX_NUM > 32) {
					if (unpatchedParamsSummary->whichParamsAreInterpolating[0]
					    || unpatchedParamsSummary->whichParamsAreInterpolating[1]) {
						goto yesTickParamManager;
					}
				}
				else {
					if (unpatchedParamsSummary->whichParamsAreInterpolating[0]) {
						goto yesTickParamManager;
					}
				}

				// No time to call the proper function and do error checking, sorry.
				ParamCollectionSummary* patchCablesSummary = &thisNoteRow->paramManager.summaries[2];
				if constexpr (kMaxNumPatchCables > 32) {
					if (patchCablesSummary->whichParamsAreInterpolating[0]
					    || patchCablesSummary->whichParamsAreInterpolating[1]) {
						goto yesTickParamManager;
					}
				}
				else {
					if (patchCablesSummary->whichParamsAreInterpolating[0]) {
						goto yesTickParamManager;
					}
				}

				// No time to call the proper function and do error checking, sorry.
				ParamCollectionSummary* expressionParamsSummary = &thisNoteRow->paramManager.summaries[3];
				if constexpr (kNumExpressionDimensions > 32) {
					if (expressionParamsSummary->whichParamsAreInterpolating[0]
					    || expressionParamsSummary->whichParamsAreInterpolating[1]) {
						goto yesTickParamManager;
					}
				}
				else {
					if (expressionParamsSummary->whichParamsAreInterpolating[0]) {
						goto yesTickParamManager;
					}
				}
				// Was that right? Until Jan 2022 I didn't have it checking for expression params automation here for
				// some reason...
			}
		}
	}
	return rendered;
}

void Kit::renderOutput(ModelStack* modelStack, StereoSample* outputBuffer, StereoSample* outputBufferEnd,
                       int32_t numSamples, int32_t* reverbBuffer, int32_t reverbAmountAdjust,
                       int32_t sideChainHitPending, bool shouldLimitDelayFeedback, bool isClipActive) {

	ParamManager* paramManager = getParamManager(modelStack->song);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(activeClip);
	// Beware - modelStackWithThreeMainThings might have a NULL timelineCounter

	GlobalEffectableForClip::renderOutput(modelStackWithTimelineCounter, paramManager, outputBuffer, numSamples,
	                                      reverbBuffer, reverbAmountAdjust, sideChainHitPending,
	                                      shouldLimitDelayFeedback, isClipActive, OutputType::KIT, recorder);
}

// offer the CC to kit gold knobs without also offering to all drums
void Kit::offerReceivedCCToModControllable(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
                                           ModelStackWithTimelineCounter* modelStack) {
	// NOTE: this call may change modelStack->timelineCounter etc!
	ModControllableAudio::offerReceivedCCToLearnedParamsForClip(fromDevice, channel, ccNumber, value, modelStack);
}
void Kit::offerReceivedCCToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t ccNumber, uint8_t value,
                                         ModelStackWithTimelineCounter* modelStack) {

	// Do it for this whole Kit
	// NOTE: this call may change modelStack->timelineCounter etc!
	offerReceivedCCToModControllable(fromDevice, channel, ccNumber, value, modelStack);

	// Now do it for each NoteRow / Drum
	// This is always actually true currently for calls to this function, but let's make this safe and future proof.
	if (modelStack->timelineCounterIsSet()) {
		InstrumentClip* clip =
		    (InstrumentClip*)modelStack->getTimelineCounter(); // May have been changed by call above!
		for (int32_t i = 0; i < clip->noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = clip->noteRows.getElement(i);
			Drum* thisDrum = thisNoteRow->drum;
			if (thisDrum && thisDrum->type == DrumType::SOUND) {
				((SoundDrum*)thisDrum)
				    ->offerReceivedCCToLearnedParamsForClip(fromDevice, channel, ccNumber, value, modelStack, i);
			}
		}
	}
}

// not updated for midi follow, this seems dumb and is just left for backwards compatibility
/// Pitch bend is available in the mod matrix as X and shouldn't be learned to params anymore (post 4.0)
bool Kit::offerReceivedPitchBendToLearnedParams(MIDIDevice* fromDevice, uint8_t channel, uint8_t data1, uint8_t data2,
                                                ModelStackWithTimelineCounter* modelStack) {

	bool messageUsed;

	// Do it for this whole Kit
	messageUsed = ModControllableAudio::offerReceivedPitchBendToLearnedParams(
	    fromDevice, channel, data1, data2, modelStack); // NOTE: this call may change modelStack->timelineCounter etc!

	if (modelStack->timelineCounterIsSet()) { // This is always actually true currently for calls to this function, but
		                                      // let's make this safe and future proof.
		InstrumentClip* clip =
		    (InstrumentClip*)modelStack->getTimelineCounter(); // May have been changed by call above!
		for (int32_t i = 0; i < clip->noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = clip->noteRows.getElement(i);
			Drum* thisDrum = thisNoteRow->drum;
			if (thisDrum && thisDrum->type == DrumType::SOUND) {
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
		if (thisDrum && thisDrum->type == DrumType::SOUND) {
			((SoundDrum*)thisDrum)->resyncGlobalLFO();
		}
	}
}

ModControllable* Kit::toModControllable() {
	return this;
}

// newName must be allowed to be edited by this function
Error Kit::makeDrumNameUnique(String* name, int32_t startAtNumber) {

	D_PRINTLN("making unique newName:");

	int32_t originalLength = name->getLength();

	do {
		char numberString[12];
		intToString(startAtNumber, numberString);
		Error error = name->concatenateAtPos(numberString, originalLength);
		if (error != Error::NONE) {
			return error;
		}
		startAtNumber++;
	} while (getDrumFromName(name->get()));

	return Error::NONE;
}

void Kit::setupWithoutActiveClip(ModelStack* modelStack) {

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(NULL);

	setupPatching(modelStackWithTimelineCounter);

	int32_t count = 0;
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (thisDrum->type == DrumType::SOUND) {

			if (!(count & 7)) {
				AudioEngine::routineWithClusterLoading(); // -----------------------------------
			}
			count++;

			SoundDrum* soundDrum = (SoundDrum*)thisDrum;

			ParamManager* paramManager = modelStackWithTimelineCounter->song->getBackedUpParamManagerPreferablyWithClip(
			    (ModControllableAudio*)soundDrum, NULL);
			if (!paramManager) {
				FREEZE_WITH_ERROR("E174");
			}

			soundDrum->patcher.performInitialPatching(soundDrum, (ParamManagerForTimeline*)paramManager);
		}
	}

	Instrument::setupWithoutActiveClip(modelStack);
}

// Accepts a ModelStack with NULL TimelineCounter
void Kit::setupPatching(ModelStackWithTimelineCounter* modelStack) {

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounterAllowNull();

	int32_t count = 0;

	if (clip) {
		for (int32_t i = 0; i < clip->noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = clip->noteRows.getElement(i);
			if (thisNoteRow->drum && thisNoteRow->drum->type == DrumType::SOUND) {

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
			if (thisDrum->type == DrumType::SOUND) {

				if (!(count & 7)) {
					AudioEngine::routineWithClusterLoading(); // -----------------------------------
				}
				count++;

				SoundDrum* soundDrum = (SoundDrum*)thisDrum;

				ParamManager* paramManager =
				    modelStack->song->getBackedUpParamManagerPreferablyWithClip((ModControllableAudio*)soundDrum, NULL);
				if (!paramManager) {
					FREEZE_WITH_ERROR("E172");
				}

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

bool Kit::setActiveClip(ModelStackWithTimelineCounter* modelStack, PgmChangeSend maySendMIDIPGMs) {
	bool clipChanged = Instrument::setActiveClip(modelStack, maySendMIDIPGMs);

	if (clipChanged) {

		resetDrumTempValues();
		if (modelStack) {
			int32_t count = 0;
			NoteRowVector* noteRows = &((InstrumentClip*)modelStack->getTimelineCounter())->noteRows;
			for (int32_t i = 0; i < noteRows->getNumElements(); i++) {
				NoteRow* thisNoteRow = noteRows->getElement(i);

				if (thisNoteRow->drum) { // In a perfect world we'd do this for every Drum, even any without NoteRows in
					                     // new Clip, but meh this'll be fine

					thisNoteRow->drum->noteRowAssignedTemp = true;
					thisNoteRow->drum->earlyNoteVelocity = 0;

					if (thisNoteRow->drum->type == DrumType::SOUND) {

						if (!(count & 7)) {
							AudioEngine::routineWithClusterLoading(); // ----------------------------------- I guess
							                                          // very often this wouldn't work cos the audio
							                                          // routine would be locked
						}
						count++;

						SoundDrum* soundDrum = (SoundDrum*)thisNoteRow->drum;

						soundDrum->patcher.performInitialPatching(soundDrum, &thisNoteRow->paramManager);
					}
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
	if (song_firmware_version < FirmwareVersion::official({1, 2, 0})
	    && !paramManager->resonanceBackwardsCompatibilityProcessed) {

		UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

		int32_t compensation = interpolateTableSigned(unpatchedParams->getValue(params::UNPATCHED_LPF_RES) + 2147483648,
		                                              32, oldResonanceCompensation, 3);
		float compensationDB = (float)compensation / (1024 << 16);

		if (compensationDB > 0.1) {
			unpatchedParams->shiftParamVolumeByDB(params::UNPATCHED_VOLUME, compensationDB);
		}

		// The SoundDrums, like all Sounds, will have already had resonance compensation done on their default
		// ParamManagers if and when any were in fact loaded. Or, if we're going through a Song doing this to all
		// ParamManagers within Clips, the Clip will automatically do all NoteRows / Drums next.

		GlobalEffectableForClip::compensateVolumeForResonance(paramManager);
	}
}

void Kit::deleteBackedUpParamManagers(Song* song) {
	song->deleteBackedUpParamManagersForModControllable(this);

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (thisDrum->type == DrumType::SOUND) {
			AudioEngine::routineWithClusterLoading(); // -----------------------------------
			song->deleteBackedUpParamManagersForModControllable((SoundDrum*)thisDrum);
		}
	}
}

// Returns num ticks til next arp event
int32_t Kit::doTickForwardForArp(ModelStack* modelStack, int32_t currentPos) {
	if (!activeClip) {
		return 2147483647;
	}

	bool clipIsActive = modelStack->song->isClipActive(activeClip);

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(activeClip);

	int32_t ticksTilNextArpEvent = 2147483647;
	for (int32_t i = 0; i < ((InstrumentClip*)activeClip)->noteRows.getNumElements(); i++) {
		NoteRow* thisNoteRow = ((InstrumentClip*)activeClip)->noteRows.getElement(i);
		if (thisNoteRow->drum
		    && thisNoteRow->drum->type == DrumType::SOUND) { // For now, only SoundDrums have Arps, but that's actually
			                                                 // a kinda pointless restriction...
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
				    modelStackWithSoundFlags,
				    instruction.arpNoteOn->inputCharacteristics[util::to_underlying(MIDICharacteristic::NOTE)],
				    instruction.noteCodeOnPostArp, instruction.arpNoteOn->velocity, instruction.arpNoteOn->mpeValues,
				    instruction.sampleSyncLengthOn, 0, 0);
			}

			ticksTilNextArpEvent = std::min(ticksTilNextArpEvent, ticksTilNextArpEventThisDrum);
		}
	}

	return ticksTilNextArpEvent;
}

GateDrum* Kit::getGateDrumForChannel(int32_t gateChannel) {
	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		if (thisDrum->type == DrumType::GATE) {
			GateDrum* gateDrum = (GateDrum*)thisDrum;
			if (gateDrum->channel == gateChannel) {
				return gateDrum;
			}
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

		for (int32_t i = 0; i < ((InstrumentClip*)activeClip)->noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = ((InstrumentClip*)activeClip)->noteRows.getElement(i);
			if (!thisNoteRow->drum || thisNoteRow->drum->type != DrumType::SOUND) {
				continue;
			}
			((SoundDrum*)thisNoteRow->drum)
			    ->getThingWithMostReverb(soundWithMostReverb, paramManagerWithMostReverb,
			                             globalEffectableWithMostReverb, highestReverbAmountFound,
			                             &thisNoteRow->paramManager);
		}
	}
}

void Kit::receivedNoteForDrum(ModelStackWithTimelineCounter* modelStack, MIDIDevice* fromDevice, bool on,
                              int32_t channel, int32_t note, int32_t velocity, bool shouldRecordNotes,
                              bool* doingMidiThru, Drum* thisDrum) {
	InstrumentClip* instrumentClip = (InstrumentClip*)modelStack->getTimelineCounterAllowNull(); // Yup it might be NULL

	// do we need to update the selectedDrum?
	possiblySetSelectedDrumAndRefreshUI(thisDrum);

	bool recordingNoteOnEarly = false;

	bool shouldRecordNoteOn = shouldRecordNotes && instrumentClip && currentSong->isClipActive(instrumentClip)
	                          && instrumentClip->armedForRecording; // Even if this comes out as false here,
	                                                                // there are some special cases below where
	                                                                // we might insist on making it true
	// If MIDIDrum, outputting same note, then don't additionally do thru
	if (doingMidiThru && thisDrum->type == DrumType::MIDI && ((MIDIDrum*)thisDrum)->channel == channel
	    && ((MIDIDrum*)thisDrum)->note == note) {
		*doingMidiThru = false;
	}

	// Just once, for first Drum we're doing a note-on on, see if we want to switch to a different InstrumentClip, for a
	// couple of reasons For simplicity we can do this every time, it only matters if you have multiple drums mapped to
	// the same note
	if (on && instrumentClip && shouldRecordNotes) {

		// Firstly, if recording session to arranger...
		if (playbackHandler.recording == RecordingMode::ARRANGEMENT) {

			instrumentClip->possiblyCloneForArrangementRecording(modelStack);

			instrumentClip = (InstrumentClip*)modelStack->getTimelineCounter(); // Re-get it, cos it might have changed

			if (instrumentClip->isArrangementOnlyClip()) {
				shouldRecordNoteOn = true;
			}
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
			int32_t ticksTilLaunch = session.launchEventAtSwungTickCount - playbackHandler.getActualSwungTickCount();
			int32_t samplesTilLaunch = ticksTilLaunch * playbackHandler.getTimePerInternalTick();
			if (samplesTilLaunch <= kLinearRecordingEarlyFirstNoteAllowance) {
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
		if (!thisNoteRow) {
			return; // Yeah, we won't even let them sound one with no NoteRow
		}
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

		// If input is MPE, we need to give the Drum the most recent MPE expression values received on the channel on
		// the Device. It doesn't keep track of these when a note isn't on, and even if it did this new note might be on
		// a different channel (just same notecode).
		if (fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].isChannelPartOfAnMPEZone(channel)) {
			for (int32_t i = 0; i < kNumExpressionDimensions; i++) {
				thisDrum->lastExpressionInputsReceived[BEND_RANGE_FINGER_LEVEL][i] =
				    fromDevice->defaultInputMPEValuesPerMIDIChannel[channel][i] >> 8;
			}
		}

		// And if non-MPE input, just set those finger-level MPE values to 0. If an MPE instrument had been used just
		// before, it could have left them set to something.
		else {
			for (int32_t i = 0; i < kNumExpressionDimensions; i++) {
				thisDrum->lastExpressionInputsReceived[BEND_RANGE_FINGER_LEVEL][i] = 0;
			}
		}

		int16_t mpeValues[kNumExpressionDimensions];
		thisDrum->getCombinedExpressionInputs(mpeValues);

		// MPE stuff - if editing note, we need to take note of the initial values which might have been sent before
		// this note-on.
		instrumentClipView.reportMPEInitialValuesForNoteEditing(modelStackWithNoteRow, mpeValues);

		if (!thisNoteRow || !thisNoteRow->soundingStatus) {

			if (thisNoteRow && shouldRecordNoteOn) {

				int16_t const* mpeValuesOrNull = NULL;

				if (fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].isChannelPartOfAnMPEZone(channel)) {
					mpeValuesOrNull = mpeValues;
				}

				instrumentClip->recordNoteOn(modelStackWithNoteRow, velocity, false, mpeValuesOrNull);
				if (getRootUI()) {
					getRootUI()->noteRowChanged(instrumentClip, thisNoteRow);
				}
			}
			// TODO: possibly should change the MPE params' currentValue to the initial values, since that usually does
			// get updated by the subsequent MPE that will come in. Or does that not matter?

			if (thisNoteRow && thisDrum->type == DrumType::SOUND
			    && !thisNoteRow->paramManager.containsAnyMainParamCollections()) {
				FREEZE_WITH_ERROR("E326"); // Trying to catch an E313 that Vinz got
			}

			beginAuditioningforDrum(modelStackWithNoteRow, thisDrum, velocity, mpeValues, channel);
		}
	}

	// Note-off
	else {
		if (thisNoteRow) {
			if (shouldRecordNotes && thisDrum->auditioned
			    && ((playbackHandler.recording == RecordingMode::ARRANGEMENT && instrumentClip->isArrangementOnlyClip())
			        || currentSong->isClipActive(instrumentClip))) {

				if (playbackHandler.recording == RecordingMode::ARRANGEMENT
				    && !instrumentClip->isArrangementOnlyClip()) {}
				else {
					instrumentClip->recordNoteOff(modelStackWithNoteRow, velocity);
					if (getRootUI()) {
						getRootUI()->noteRowChanged(instrumentClip, thisNoteRow);
					}
				}
			}
			instrumentClipView.reportNoteOffForMPEEditing(modelStackWithNoteRow);

			// MPE-controlled params are a bit special in that we can see (via this note-off) when the user has removed
			// their finger and won't be sending more values. So, let's unlatch those params now.
			ExpressionParamSet* mpeParams = thisNoteRow->paramManager.getExpressionParamSet();
			if (mpeParams) {
				mpeParams->cancelAllOverriding();
			}
		}
		endAuditioningForDrum(modelStackWithNoteRow, thisDrum,
		                      velocity); // Do this even if not marked as auditioned, to avoid stuck notes in cases like
		                                 // if two note-ons were sent
	}
}

void Kit::possiblySetSelectedDrumAndRefreshUI(Drum* thisDrum) {
	if (midiEngine.midiSelectKitRow) {
		instrumentClipView.setSelectedDrum(thisDrum, true, this);
	}
}

void Kit::offerReceivedNote(ModelStackWithTimelineCounter* modelStack, MIDIDevice* fromDevice, bool on, int32_t channel,
                            int32_t note, int32_t velocity, bool shouldRecordNotes, bool* doingMidiThru) {
	InstrumentClip* instrumentClip = (InstrumentClip*)modelStack->getTimelineCounterAllowNull(); // Yup it might be NULL
	MIDIMatchType match = midiInput.checkMatch(fromDevice, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		auto rootNote = midiInput.noteOrCC == 255 ? 0 : midiInput.noteOrCC;
		receivedNoteForKit(modelStack, fromDevice, on, channel, note - rootNote, velocity, shouldRecordNotes,
		                   doingMidiThru, instrumentClip);
		return;
	}

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {

		// If this is the "input" command, to sound / audition the Drum...
		// Returns true if midi channel and note match the learned midi note
		// We don't need the MPE match because all types of matches should sound the drum
		if (thisDrum->midiInput.equalsNoteOrCCAllowMPE(fromDevice, channel, note)) {
			receivedNoteForDrum(modelStack, fromDevice, on, channel, note, velocity, shouldRecordNotes, doingMidiThru,
			                    thisDrum);
		}
		// Or if this is the Drum's mute command...
		// changed to else if dec 2023 because the same note should never be both mute and sound, this will save
		// potential confusion if someone accidentally learns a note to mute as well as audition
		else if (instrumentClip && on && thisDrum->muteMIDICommand.equalsNoteOrCC(fromDevice, channel, note)) {

			ModelStackWithNoteRow* modelStackWithNoteRow = instrumentClip->getNoteRowForDrum(modelStack, thisDrum);

			NoteRow* thisNoteRow = modelStackWithNoteRow->getNoteRowAllowNull();

			if (thisNoteRow) {
				instrumentClip->toggleNoteRowMute(modelStackWithNoteRow);

				if (getCurrentUI() == &automationView
				    && automationView.getAutomationSubType() == AutomationSubType::INSTRUMENT) {
					uiNeedsRendering(&automationView, 0, 0xFFFFFFFF);
				}
				else {
					uiNeedsRendering(&instrumentClipView, 0, 0xFFFFFFFF);
				}
			}
		}
	}
}

void Kit::receivedPitchBendForDrum(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Drum* thisDrum,
                                   uint8_t data1, uint8_t data2, MIDIMatchType match, uint8_t channel,
                                   bool* doingMidiThru) {
	int32_t level;
	switch (match) {
		using enum MIDIMatchType;
	case NO_MATCH:
		return;
	case MPE_MEMBER:
		if (channel != thisDrum->lastMIDIChannelAuditioned) {
			return;
		}
		level = BEND_RANGE_FINGER_LEVEL;
		break;
	case MPE_MASTER:
		[[fallthrough]];
	case CHANNEL:
		level = BEND_RANGE_MAIN;
	}
	int16_t value16 = (((uint32_t)data1 | ((uint32_t)data2 << 7)) - 8192) << 2;
	thisDrum->expressionEventPossiblyToRecord(modelStackWithTimelineCounter, value16, X_PITCH_BEND, level);
}

void Kit::offerReceivedPitchBend(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
                                 uint8_t channel, uint8_t data1, uint8_t data2, bool* doingMidiThru) {
	InstrumentClip* instrumentClip =
	    (InstrumentClip*)modelStackWithTimelineCounter->getTimelineCounterAllowNull(); // Yup it might be NULL
	MIDIMatchType match = midiInput.checkMatch(fromDevice, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		receivedPitchBendForKit(modelStackWithTimelineCounter, fromDevice, match, channel, data1, data2, doingMidiThru);
		return;
	}

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		MIDIMatchType match = thisDrum->midiInput.checkMatch(fromDevice, channel);
		receivedPitchBendForDrum(modelStackWithTimelineCounter, thisDrum, data1, data2, match, channel, doingMidiThru);
	}
}

void Kit::receivedMPEYForDrum(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Drum* thisDrum,
                              MIDIMatchType match, uint8_t channel, uint8_t value) {

	int32_t level;
	switch (match) {
		using enum MIDIMatchType;
	case NO_MATCH:
		return;
	// note that in melodic instruments channel matches send Y on channel 1 but
	// with the different data model it doesn't make sense for kit rows, all kit rows
	// on that channel would send it out again later
	case CHANNEL:
		return;
	case MPE_MEMBER:
		if (channel != thisDrum->lastMIDIChannelAuditioned) {
			return;
		}
		level = BEND_RANGE_FINGER_LEVEL;
		break;
	case MPE_MASTER:
		level = BEND_RANGE_MAIN;
		break;
	}
	int16_t value16 = (value - 64) << 9;
	thisDrum->expressionEventPossiblyToRecord(modelStackWithTimelineCounter, value16, Y_SLIDE_TIMBRE, level);
}
void Kit::offerReceivedCC(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
                          uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru) {
	InstrumentClip* instrumentClip =
	    (InstrumentClip*)modelStackWithTimelineCounter->getTimelineCounterAllowNull(); // Yup it might be NULL
	MIDIMatchType match = midiInput.checkMatch(fromDevice, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		receivedCCForKit(modelStackWithTimelineCounter, fromDevice, match, channel, ccNumber, value, doingMidiThru,
		                 instrumentClip);
		return;
	}
	if (ccNumber != 74) {
		return;
	}
	if (!fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].isChannelPartOfAnMPEZone(channel)) {
		return;
	}

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		MIDIMatchType match = thisDrum->midiInput.checkMatch(fromDevice, channel);
		if (match == MIDIMatchType::MPE_MASTER || match == MIDIMatchType::MPE_MEMBER) {
			// this will make sure that the channel matches the drums last received one
			receivedMPEYForDrum(modelStackWithTimelineCounter, thisDrum, match, channel, value);
		}
	}
}
/// find the drum matching the noteCode, counting up from 0
Drum* Kit::getDrumFromNoteCode(InstrumentClip* clip, int32_t noteCode) {
	Drum* thisDrum = nullptr;
	// bottom kit noteRowId = 0
	// default middle C1 note number = 36
	// noteRowId + 36 = C1 up for kit sounds
	// this is configurable through the default menu
	if (noteCode >= 0) {
		int32_t index = noteCode;
		if (index < clip->noteRows.getNumElements()) {
			NoteRow* noteRow = clip->noteRows.getElement(index);
			if (noteRow) {
				thisDrum = noteRow->drum;
			}
		}
	}
	return thisDrum;
}

/// for pitch bend received on a channel learnt to a whole clip
void Kit::receivedPitchBendForKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
                                  MIDIMatchType match, uint8_t channel, uint8_t data1, uint8_t data2,
                                  bool* doingMidiThru) {

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		receivedPitchBendForDrum(modelStackWithTimelineCounter, thisDrum, data1, data2, match, channel, doingMidiThru);
	}
}

/// maps a note received on kit input channel to a drum. Note is zero indexed to first drum
void Kit::receivedNoteForKit(ModelStackWithTimelineCounter* modelStack, MIDIDevice* fromDevice, bool on,
                             int32_t channel, int32_t note, int32_t velocity, bool shouldRecordNotes,
                             bool* doingMidiThru, InstrumentClip* clip) {
	Kit* kit = (Kit*)clip->output;
	Drum* thisDrum = getDrumFromNoteCode(clip, note);

	if (thisDrum) {
		kit->receivedNoteForDrum(modelStack, fromDevice, on, channel, note, velocity, shouldRecordNotes, doingMidiThru,
		                         thisDrum);
	}
}

/// for learning a whole kit to a single channel, offer cc to all drums
void Kit::receivedCCForKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
                           MIDIMatchType match, uint8_t channel, uint8_t ccNumber, uint8_t value, bool* doingMidiThru,
                           Clip* clip) {
	if (match != MIDIMatchType::MPE_MASTER && match != MIDIMatchType::MPE_MEMBER) {
		return;
	}
	if (ccNumber != 74) {
		return;
	}
	if (!fromDevice->ports[MIDI_DIRECTION_INPUT_TO_DELUGE].isChannelPartOfAnMPEZone(channel)) {
		return;
	}

	Kit* kit = (Kit*)clip->output;
	Drum* firstDrum = kit->getDrumFromIndex(0);

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		kit->receivedMPEYForDrum(modelStackWithTimelineCounter, thisDrum, match, channel, value);
	}
}

void Kit::receivedAftertouchForKit(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
                                   MIDIMatchType match, int32_t channel, int32_t value, int32_t noteCode,
                                   bool* doingMidiThru) {
	// Channel pressure message...
	if (noteCode == -1) {
		Drum* firstDrum = getDrumFromIndex(0);
		for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
			int32_t level = BEND_RANGE_FINGER_LEVEL;
			receivedAftertouchForDrum(modelStackWithTimelineCounter, thisDrum, match, channel, value);
		}
	}
	// Or a polyphonic aftertouch message - these aren't allowed for MPE except on the "master" channel.
	else {
		Drum* thisDrum = getDrumFromNoteCode((InstrumentClip*)activeClip, noteCode);
		if ((thisDrum != nullptr) && (channel == thisDrum->lastMIDIChannelAuditioned)) {
			receivedAftertouchForDrum(modelStackWithTimelineCounter, thisDrum, MIDIMatchType::CHANNEL, channel, value);
		}
	}
}

void Kit::receivedAftertouchForDrum(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, Drum* thisDrum,
                                    MIDIMatchType match, uint8_t channel, uint8_t value) {
	int32_t level = BEND_RANGE_MAIN;
	switch (match) {
		using enum MIDIMatchType;
	case NO_MATCH:
		return;
	case MPE_MEMBER:
		if (channel != thisDrum->lastMIDIChannelAuditioned) {
			return;
		}
		level = BEND_RANGE_FINGER_LEVEL;
		[[fallthrough]];
	case MPE_MASTER:
		[[fallthrough]];
	case CHANNEL:
		int32_t value15 = value << 8;
		thisDrum->expressionEventPossiblyToRecord(modelStackWithTimelineCounter, value15, Z_PRESSURE, level);
		break;
	}
}

// noteCode -1 means channel-wide, including for MPE input (which then means it could still then just apply to one
// note). This function could be optimized a bit better, there are lots of calls to similar functions.
void Kit::offerReceivedAftertouch(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, MIDIDevice* fromDevice,
                                  int32_t channel, int32_t value, int32_t noteCode, bool* doingMidiThru) {

	InstrumentClip* instrumentClip =
	    (InstrumentClip*)modelStackWithTimelineCounter->getTimelineCounterAllowNull(); // Yup it might be NULL
	MIDIMatchType match = midiInput.checkMatch(fromDevice, channel);
	if (match != MIDIMatchType::NO_MATCH) {
		receivedAftertouchForKit(modelStackWithTimelineCounter, fromDevice, match, channel, value, noteCode,
		                         doingMidiThru);
		return;
	}

	for (Drum* thisDrum = firstDrum; thisDrum; thisDrum = thisDrum->next) {
		int32_t level = BEND_RANGE_FINGER_LEVEL;
		if (noteCode == -1) { // Channel pressure message...
			MIDIMatchType match = thisDrum->midiInput.checkMatch(fromDevice, channel);
			if (match != MIDIMatchType::NO_MATCH) {
				receivedAftertouchForDrum(modelStackWithTimelineCounter, thisDrum, match, channel, value);
			}
		}

		// Or a polyphonic aftertouch message - these aren't allowed for MPE except on the "master" channel.
		else {
			if (thisDrum->midiInput.equalsNoteOrCCAllowMPEMasterChannels(fromDevice, channel, noteCode)
			    && channel == thisDrum->lastMIDIChannelAuditioned) {
				receivedAftertouchForDrum(modelStackWithTimelineCounter, thisDrum, MIDIMatchType::CHANNEL, channel,
				                          value);
			}
		}
	}
}

void Kit::offerBendRangeUpdate(ModelStack* modelStack, MIDIDevice* device, int32_t channelOrZone,
                               int32_t whichBendRange, int32_t bendSemitones) {

	if (whichBendRange == BEND_RANGE_MAIN) {
		return; // This is not used in Kits for Drums. Drums use their BEND_RANGE_FINGER_LEVEL for both kinds of bend.
	}
	// TODO: Hmm, for non-MPE instruments we'd want to use this kind of bend range update and just paste it into
	// BEND_RANGE_FINGER_LEVEL though...

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
				// ParamManager* paramManager = modelStack->song->getBackedUpParamManagerPreferablyWithClip(thisDrum,
				// NULL); // TODO...
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

// You must supply noteRow if there is an activeClip with a NoteRow for that Drum. The TimelineCounter should be the
// activeClip. Drum must not be NULL - check first if not sure!
void Kit::beginAuditioningforDrum(ModelStackWithNoteRow* modelStack, Drum* drum, int32_t velocity,
                                  int16_t const* mpeValues, int32_t fromMIDIChannel) {
	if (!drum) {
		return;
	}
	ParamManager* paramManagerForDrum = NULL;

	if (modelStack->getNoteRowAllowNull()) {
		paramManagerForDrum = &modelStack->getNoteRow()->paramManager;
		if (!paramManagerForDrum->containsAnyMainParamCollections() && drum->type == DrumType::SOUND) {
			FREEZE_WITH_ERROR("E313"); // Vinz got this!
		}
	}
	else {
		if (drum->type == DrumType::SOUND) {
			paramManagerForDrum = modelStack->song->getBackedUpParamManagerPreferablyWithClip((SoundDrum*)drum, NULL);
			if (!paramManagerForDrum) {
				// Ron got this, June 2020, while "dragging" a row vertically in arranger
				FREEZE_WITH_ERROR("E314");
			}
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

// Check that it's auditioned before calling this if you don't want it potentially sending an extra note-off in some
// rare cases. You must supply noteRow if there is an activeClip with a NoteRow for that Drum. The TimelineCounter
// should be the activeClip.
void Kit::endAuditioningForDrum(ModelStackWithNoteRow* modelStack, Drum* drum, int32_t velocity) {

	drum->auditioned = false;
	drum->lastMIDIChannelAuditioned = MIDI_CHANNEL_NONE; // So it won't record any more MPE
	drum->earlyNoteStillActive = false;

	ParamManager* paramManagerForDrum = NULL;

	if (drum->type == DrumType::SOUND) {

		NoteRow* noteRow = modelStack->getNoteRowAllowNull();

		if (noteRow) {
			paramManagerForDrum = &noteRow->paramManager;
			goto gotParamManager;
		}

		// If still here, haven't found paramManager yet
		paramManagerForDrum = modelStack->song->getBackedUpParamManagerPreferablyWithClip((SoundDrum*)drum, NULL);
		if (!paramManagerForDrum) {
			FREEZE_WITH_ERROR("E312"); // Should make ALPHA_OR_BETA_VERSION after V3.0.0 release
		}
	}

gotParamManager:

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addOtherTwoThings(drum->toModControllable(), paramManagerForDrum);

	drum->noteOff(modelStackWithThreeMainThings);

	if (activeClip) {
		activeClip->expectEvent(); // Because the absence of auditioning here means sequenced notes may play
	}
}

// for (Drum* drum = firstDrum; drum; drum = drum->next) {

/// for a kit we have two types of automation: with Affect Entire and without Affect Entire
ModelStackWithAutoParam* Kit::getModelStackWithParam(ModelStackWithTimelineCounter* modelStack, Clip* clip,
                                                     int32_t paramID, params::Kind paramKind, bool affectEntire,
                                                     bool useMenuStack) {
	if (affectEntire) {
		return getModelStackWithParamForKit(modelStack, clip, paramID, paramKind, useMenuStack);
	}
	else {
		return getModelStackWithParamForKitRow(modelStack, clip, paramID, paramKind, useMenuStack);
	}
}

/// for a kit we have two types of automation: with Affect Entire and without Affect Entire
/// for a kit with affect entire on, we are automating information at the kit level
ModelStackWithAutoParam* Kit::getModelStackWithParamForKit(ModelStackWithTimelineCounter* modelStack, Clip* clip,
                                                           int32_t paramID, params::Kind paramKind, bool useMenuStack) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;

	if (useMenuStack) {
		modelStackWithThreeMainThings = modelStack->addOtherTwoThingsButNoNoteRow(soundEditor.currentModControllable,
		                                                                          soundEditor.currentParamManager);
	}
	else {
		modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(toModControllable(), &clip->paramManager);
	}

	if (modelStackWithThreeMainThings) {
		modelStackWithParam = modelStackWithThreeMainThings->getUnpatchedAutoParamFromId(paramID);
	}

	return modelStackWithParam;
}

/// for a kit we have two types of automation: with Affect Entire and without Affect Entire
/// for a kit with affect entire off, we are automating information at the noterow level
ModelStackWithAutoParam* Kit::getModelStackWithParamForKitRow(ModelStackWithTimelineCounter* modelStack, Clip* clip,
                                                              int32_t paramID, params::Kind paramKind,
                                                              bool useMenuStack) {
	ModelStackWithAutoParam* modelStackWithParam = nullptr;

	if (selectedDrum && selectedDrum->type == DrumType::SOUND) { // no automation for MIDI or CV kit drum types

		ModelStackWithNoteRow* modelStackWithNoteRow = ((InstrumentClip*)clip)->getNoteRowForSelectedDrum(modelStack);

		if (modelStackWithNoteRow->getNoteRowAllowNull()) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings = nullptr;

			if (useMenuStack) {
				modelStackWithThreeMainThings = modelStackWithNoteRow->addOtherTwoThings(
				    soundEditor.currentModControllable, soundEditor.currentParamManager);
			}
			else {
				modelStackWithThreeMainThings = modelStackWithNoteRow->addOtherTwoThingsAutomaticallyGivenNoteRow();
			}

			if (modelStackWithThreeMainThings) {
				if (paramKind == deluge::modulation::params::Kind::PATCHED) {
					modelStackWithParam = modelStackWithThreeMainThings->getPatchedAutoParamFromId(paramID);
				}

				else if (paramKind == deluge::modulation::params::Kind::UNPATCHED_SOUND) {
					modelStackWithParam = modelStackWithThreeMainThings->getUnpatchedAutoParamFromId(paramID);
				}

				else if (paramKind == deluge::modulation::params::Kind::PATCH_CABLE) {
					modelStackWithParam = modelStackWithThreeMainThings->getPatchCableAutoParamFromId(paramID);
				}
			}
		}
	}

	return modelStackWithParam;
}
