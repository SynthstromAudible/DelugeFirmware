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
#include <soundinstrument.h>
#include "uart.h"
#include "song.h"
#include "playbackhandler.h"
#include "View.h"
#include "storagemanager.h"
#include "numericdriver.h"
#include "ModelStack.h"
#include "NoteRow.h"
#include "VoiceVector.h"
#include "voice.h"
#include "ParamSet.h"
#include "PatchCableSet.h"

SoundInstrument::SoundInstrument() : MelodicInstrument(INSTRUMENT_TYPE_SYNTH) {
}

bool SoundInstrument::writeDataToFile(Clip* clipForSavingOutputOnly, Song* song) {

	// MelodicInstrument::writeDataToFile(clipForSavingOutputOnly, song); // Nope, this gets called within the below call
	writeMelodicInstrumentAttributesToFile(clipForSavingOutputOnly, song);

	ParamManager* paramManager;

	// If saving Output only...
	if (clipForSavingOutputOnly) paramManager = &clipForSavingOutputOnly->paramManager;

	// Or if saving Song...
	else {

		// If no activeClip, that means no Clip has this Output, so there should be a backedUpParamManager that we should use
		if (!activeClip) paramManager = song->getBackedUpParamManagerPreferablyWithClip(this, NULL);

		else paramManager = NULL;
	}

	Sound::writeToFile(clipForSavingOutputOnly == NULL, paramManager,
	                   clipForSavingOutputOnly ? &((InstrumentClip*)clipForSavingOutputOnly)->arpSettings : NULL);

	MelodicInstrument::writeMelodicInstrumentTagsToFile(clipForSavingOutputOnly, song);

	return true;
}

// arpSettings optional - no need if you're loading a new V2.0 song where Instruments are all separate from Clips and won't store any arp stuff
int SoundInstrument::readFromFile(Song* song, Clip* clip, int32_t readAutomationUpToPos) {

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithModControllable* modelStack =
	    setupModelStackWithSong(modelStackMemory, song)->addTimelineCounter(clip)->addModControllableButNoNoteRow(this);

	return Sound::readFromFile(modelStack, readAutomationUpToPos, &defaultArpSettings);
}

void SoundInstrument::cutAllSound() {
	Sound::unassignAllVoices();
}

void SoundInstrument::renderOutput(ModelStack* modelStack, StereoSample* startPos, StereoSample* endPos, int numSamples,
                                   int32_t* reverbBuffer, int32_t reverbAmountAdjust, int32_t sideChainHitPending,
                                   bool shouldLimitDelayFeedback, bool isClipActive) {

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addTimelineCounter(activeClip)
	        ->addOtherTwoThingsButNoNoteRow(this, getParamManager(modelStack->song));

	if (!skippingRendering) {
		Sound::render(modelStackWithThreeMainThings, startPos, numSamples, reverbBuffer, sideChainHitPending,
		              reverbAmountAdjust, shouldLimitDelayFeedback);
	}

	if (playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn && isClipActive) {

		ParamCollectionSummary* patchedParamsSummary =
		    &modelStackWithThreeMainThings->paramManager
		         ->summaries[1]; // No time to call the proper function and do error checking, sorry.
		if (patchedParamsSummary->whichParamsAreInterpolating[0] || patchedParamsSummary->whichParamsAreInterpolating[1]
#if NUM_PARAMS > 64
		    || patchedParamsSummary->whichParamsAreInterpolating[2]
#endif
		) {
yesTickParamManagerForClip:
			modelStackWithThreeMainThings->paramManager->toForTimeline()->tickSamples(numSamples,
			                                                                          modelStackWithThreeMainThings);
		}
		else {

			// Try other options too.

			ParamCollectionSummary* unpatchedParamsSummary =
			    &modelStackWithThreeMainThings->paramManager
			         ->summaries[0]; // No time to call the proper function and do error checking, sorry.
			if (unpatchedParamsSummary->whichParamsAreInterpolating[0]
#if MAX_NUM_UNPATCHED_PARAM_FOR_SOUNDS > 32
			    || unpatchedParamsSummary->whichParamsAreInterpolating[1]
#endif
			) {
				goto yesTickParamManagerForClip;
			}

			ParamCollectionSummary* patchCablesSummary =
			    &modelStackWithThreeMainThings->paramManager
			         ->summaries[2]; // No time to call the proper function and do error checking, sorry.
			if (patchCablesSummary->whichParamsAreInterpolating[0]
#if MAX_NUM_PATCH_CABLES > 32
			    || patchCablesSummary->whichParamsAreInterpolating[1]
#endif
			) {
				goto yesTickParamManagerForClip;
			}

			ParamCollectionSummary* expressionParamsSummary =
			    &modelStackWithThreeMainThings->paramManager
			         ->summaries[3]; // No time to call the proper function and do error checking, sorry.
			if (expressionParamsSummary->whichParamsAreInterpolating[0]
#if NUM_EXPRESSION_DIMENSIONS > 32
			    || expressionParamsSummary->whichParamsAreInterpolating[1]
#endif
			) {
				goto yesTickParamManagerForClip;
			}
		}

		// Do the ParamManagers of each NoteRow, too
		for (int i = 0; i < ((InstrumentClip*)activeClip)->noteRows.getNumElements(); i++) {
			NoteRow* thisNoteRow = ((InstrumentClip*)activeClip)->noteRows.getElement(i);
			ParamCollectionSummary* expressionParamsSummary =
			    &thisNoteRow->paramManager
			         .summaries[0]; // No time to call the proper function and do error checking, sorry.
			if (expressionParamsSummary->whichParamsAreInterpolating[0]
#if NUM_EXPRESSION_DIMENSIONS > 32
			    || expressionParamsSummary->whichParamsAreInterpolating[1]
#endif
			) {
				modelStackWithThreeMainThings->setNoteRow(thisNoteRow, thisNoteRow->y);
				modelStackWithThreeMainThings->paramManager = &thisNoteRow->paramManager;
				thisNoteRow->paramManager.tickSamples(numSamples, modelStackWithThreeMainThings);
			}
		}
	}
}

int SoundInstrument::loadAllAudioFiles(bool mayActuallyReadFiles) {

	bool doingAlternatePath =
	    mayActuallyReadFiles && (audioFileManager.alternateLoadDirStatus == ALTERNATE_LOAD_DIR_NONE_SET);
	if (doingAlternatePath) {
		int error = setupDefaultAudioFileDir();
		if (error) return error;
	}

	int error = Sound::loadAllAudioFiles(mayActuallyReadFiles);

	if (doingAlternatePath) {
		audioFileManager.thingFinishedLoading();
	}

	return error;
}

void SoundInstrument::resyncLFOs() {
	resyncGlobalLFO();
}

ModControllable* SoundInstrument::toModControllable() {
	return this;
}

void SoundInstrument::setupPatching(ModelStackWithTimelineCounter* modelStack) {

	InstrumentClip* clip = (InstrumentClip*)modelStack->getTimelineCounterAllowNull();
	ParamManagerForTimeline* paramManager;
	if (clip) {
		paramManager = &clip->paramManager;

		ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
		    modelStack->addOtherTwoThingsButNoNoteRow(this, paramManager);

		ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(modelStackWithThreeMainThings);
	}
	else {
		paramManager =
		    (ParamManagerForTimeline*)modelStack->song->getBackedUpParamManagerPreferablyWithClip(this, NULL);
		ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(paramManager);
	}

	ModelStackWithParamCollection* modelStackWithParamCollection =
	    paramManager->getPatchCableSet(modelStack->addOtherTwoThingsButNoNoteRow(this, paramManager));

	PatchCableSet* patchCableSet = (PatchCableSet*)modelStackWithParamCollection->paramCollection;

	patchCableSet->setupPatching(modelStackWithParamCollection);
}

bool SoundInstrument::setActiveClip(ModelStackWithTimelineCounter* modelStack, int maySendMIDIPGMs) {

	bool clipChanged = MelodicInstrument::setActiveClip(modelStack, maySendMIDIPGMs);

	if (clipChanged) {
		ParamManager* paramManager = &modelStack->getTimelineCounter()->paramManager;
		patcher.performInitialPatching(this, paramManager);
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;

		// Grab mono expression params
		ExpressionParamSet* expressionParams = paramManager->getExpressionParamSet();
		if (expressionParams) {
			for (int i = 0; i < NUM_EXPRESSION_DIMENSIONS; i++) {
				monophonicExpressionValues[i] = expressionParams->params[i].getCurrentValue();
			}
		}
		else {
			for (int i = 0; i < NUM_EXPRESSION_DIMENSIONS; i++) {
				monophonicExpressionValues[i] = 0;
			}
		}
		whichExpressionSourcesChangedAtSynthLevel = (1 << NUM_EXPRESSION_DIMENSIONS) - 1;
	}
	return clipChanged;
}

void SoundInstrument::setupWithoutActiveClip(ModelStack* modelStack) {

	ModelStackWithTimelineCounter* modelStackWithTimelineCounter = modelStack->addTimelineCounter(NULL);

	setupPatching(modelStackWithTimelineCounter);

	ParamManager* paramManager =
	    modelStackWithTimelineCounter->song->getBackedUpParamManagerPreferablyWithClip(this, NULL);
	if (!paramManager) numericDriver.freezeWithError("E173");
	patcher.performInitialPatching(this, paramManager);

	// Clear mono expression params
	for (int i = 0; i < NUM_EXPRESSION_DIMENSIONS; i++) {
		monophonicExpressionValues[i] = 0;
	}
	whichExpressionSourcesChangedAtSynthLevel = (1 << NUM_EXPRESSION_DIMENSIONS) - 1;

	Instrument::setupWithoutActiveClip(modelStack);
}

void SoundInstrument::prepareForHibernationOrDeletion() {
	Sound::prepareForHibernation();
}

void SoundInstrument::setupPatchingForAllParamManagers(Song* song) {
	song->setupPatchingForAllParamManagersForInstrument(this);
}

void SoundInstrument::deleteBackedUpParamManagers(Song* song) {
	song->deleteBackedUpParamManagersForModControllable(this);
}

extern bool expressionValueChangesMustBeDoneSmoothly;

void SoundInstrument::monophonicExpressionEvent(int newValue, int whichExpressionDimension) {
	whichExpressionSourcesChangedAtSynthLevel |= 1 << whichExpressionDimension;
	monophonicExpressionValues[whichExpressionDimension] = newValue;
}

// Alternative to what's in the NonAudioInstrument:: implementation, which would almost work here, but we cut corner for Sound by avoiding going through the Arp and just talk directly to the Voices.
// (Despite my having made it now actually need to talk to the Arp too, as below...)
// Note, this virtual function actually overrides/implements from two base classes - MelodicInstrument and ModControllable.
void SoundInstrument::polyphonicExpressionEventOnChannelOrNote(int newValue, int whichExpressionDimension,
                                                               int channelOrNoteNumber, int whichCharacteristic) {
	int s = whichExpressionDimension + PATCH_SOURCE_X;

	//sourcesChanged |= 1 << s; // We'd ideally not want to apply this to all voices though...

	int ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		if (thisVoice->inputCharacteristics[whichCharacteristic] == channelOrNoteNumber) {
			if (expressionValueChangesMustBeDoneSmoothly) {
				thisVoice->expressionEventSmooth(newValue, s);
			}
			else {
				thisVoice->expressionEventImmediate(this, newValue, s);
			}
		}
	}

	// Must update MPE values in Arp too - useful either if it's on, or if we're in true monophonic mode - in either case, we could need to suddenly do a note-on for a different note that the Arp knows about, and need these MPE values.
	int n, nEnd;
	if (whichCharacteristic == MIDI_CHARACTERISTIC_NOTE) {
		n = arpeggiator.notes.search(channelOrNoteNumber, GREATER_OR_EQUAL);
		if (n < arpeggiator.notes.getNumElements()) {
			nEnd = 0;
			goto lookAtArpNote;
		}
		return;
	}
	nEnd = arpeggiator.notes.getNumElements();
	for (n = 0; n < nEnd; n++) {
lookAtArpNote:
		ArpNote* arpNote = (ArpNote*)arpeggiator.notes.getElementAddress(n);
		if (arpNote->inputCharacteristics[whichCharacteristic] == channelOrNoteNumber) {
			arpNote->mpeValues[whichExpressionDimension] = newValue >> 16;
		}
	}
}

void SoundInstrument::sendNote(ModelStackWithThreeMainThings* modelStack, bool isOn, int noteCode,
                               int16_t const* mpeValues, int fromMIDIChannel, uint8_t velocity,
                               uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate) {

	if (!inValidState) return;

	if (isOn) {
		noteOn(modelStack, &arpeggiator, noteCode, mpeValues, sampleSyncLength, ticksLate, samplesLate, velocity,
		       fromMIDIChannel);
	}
	else {
		ArpeggiatorSettings* arpSettings = getArpSettings();

		ArpReturnInstruction instruction;

		arpeggiator.noteOff(arpSettings, noteCode, &instruction);

		if (instruction.noteCodeOffPostArp != ARP_NOTE_NONE) {

#if ALPHA_OR_BETA_VERSION
			if (!modelStack->paramManager)
				numericDriver.freezeWithError(
				    "E402"); // Previously we were allowed to receive a NULL paramManager, then would just crudely do an unassignAllVoices(). But I'm pretty sure this doesn't exist anymore?
#endif
			ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();

			noteOffPostArpeggiator(modelStackWithSoundFlags, instruction.noteCodeOffPostArp);

			reassessRenderSkippingStatus(modelStackWithSoundFlags);
		}
	}
}

ArpeggiatorSettings* SoundInstrument::getArpSettings(InstrumentClip* clip) {
	return MelodicInstrument::getArpSettings(clip);
}

bool SoundInstrument::readTagFromFile(char const* tagName) {
	return MelodicInstrument::readTagFromFile(tagName);
}

void SoundInstrument::compensateInstrumentVolumeForResonance(ModelStackWithThreeMainThings* modelStack) {
	Sound::compensateVolumeForResonance(modelStack);
}

void SoundInstrument::loadCrucialAudioFilesOnly() {
	loadAllAudioFiles(true);
}

// Any time it gets edited, we want to grab the default arp settings from the activeClip
void SoundInstrument::beenEdited(bool shouldMoveToEmptySlot) {
	if (activeClip) defaultArpSettings.cloneFrom(&((InstrumentClip*)activeClip)->arpSettings);
	Instrument::beenEdited(shouldMoveToEmptySlot);
}

// Returns num ticks til next arp event
int32_t SoundInstrument::doTickForwardForArp(ModelStack* modelStack, int32_t currentPos) {
	if (!activeClip) return 2147483647;

	ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
	    modelStack->addTimelineCounter(activeClip)
	        ->addOtherTwoThingsButNoNoteRow(this, getParamManager(modelStack->song));

	ArpReturnInstruction instruction;

	int32_t ticksTilNextArpEvent = arpeggiator.doTickForward(&((InstrumentClip*)activeClip)->arpSettings, &instruction,
	                                                         currentPos, activeClip->currentlyPlayingReversed);

	ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStackWithThreeMainThings->addSoundFlags();

	if (instruction.noteCodeOffPostArp != ARP_NOTE_NONE) {
		noteOffPostArpeggiator(modelStackWithSoundFlags, instruction.noteCodeOffPostArp);
	}

	if (instruction.noteCodeOnPostArp != ARP_NOTE_NONE) {
		noteOnPostArpeggiator(modelStackWithSoundFlags,
		                      instruction.arpNoteOn->inputCharacteristics[MIDI_CHARACTERISTIC_NOTE],
		                      instruction.noteCodeOnPostArp, instruction.arpNoteOn->velocity,
		                      instruction.arpNoteOn->mpeValues, instruction.sampleSyncLengthOn, 0, 0,
		                      instruction.arpNoteOn->inputCharacteristics[MIDI_CHARACTERISTIC_CHANNEL]);
	}

	return ticksTilNextArpEvent;
}

void SoundInstrument::getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
                                             GlobalEffectableForClip** globalEffectableWithMostReverb,
                                             int32_t* highestReverbAmountFound) {
	if (activeClip) {
		Sound::getThingWithMostReverb(soundWithMostReverb, paramManagerWithMostReverb, globalEffectableWithMostReverb,
		                              highestReverbAmountFound, &activeClip->paramManager);
	}
}

ArpeggiatorBase* SoundInstrument::getArp() {
	return &arpeggiator;
}

bool SoundInstrument::noteIsOn(int noteCode) {

	ArpeggiatorSettings* arpSettings = getArpSettings();

	if (arpSettings) {
		if (arpSettings->mode != ARP_MODE_OFF || polyphonic == POLYPHONY_LEGATO || polyphonic == POLYPHONY_MONO) {

			int n = arpeggiator.notes.search(noteCode, GREATER_OR_EQUAL);
			if (n >= arpeggiator.notes.getNumElements()) return false;
			ArpNote* arpNote = (ArpNote*)arpeggiator.notes.getElementAddress(n);
			return (arpNote->inputCharacteristics[MIDI_CHARACTERISTIC_NOTE] == noteCode);
		}
	}

	if (!numVoicesAssigned) return false;

	int ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		if ((thisVoice->noteCodeAfterArpeggiation == noteCode)
		    && thisVoice->envelopes[0].state < ENVELOPE_STAGE_RELEASE) { // Ignore releasing notes. Is this right?
			return true;
		}
	}
	return false;
}
