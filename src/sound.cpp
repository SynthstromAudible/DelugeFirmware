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
#include "functions.h"
#include "storagemanager.h"
#include <math.h>
#include <patcher.h>
#include <ParamManager.h>
#include <sound.h>
#include <soundinstrument.h>
#include "soundeditor.h"
#include "FilterSetConfig.h"
#include "kit.h"
#include "numericdriver.h"
#include "View.h"
#include "Action.h"
#include "ActionLogger.h"
#include <string.h>
#include <TimelineCounter.h>
#include "GeneralMemoryAllocator.h"
#include "MultisampleRange.h"
#include "MultiWaveTableRange.h"
#include "VoiceSample.h"
#include <new>
#include "matrixdriver.h"
#include "playbackhandler.h"
#include "ParamSet.h"
#include "song.h"
#include "RootUI.h"
#include "Buttons.h"
#include "voice.h"
#include "Sample.h"
#include "VoiceVector.h"
#include "ModelStack.h"
#include "IndicatorLEDs.h"
#include "FlashStorage.h"
#include "PatchCableSet.h"

extern "C" {
#include "mtu.h"
#include "uart_all_cpus.h"
}

const PatchableInfo patchableInfoForSound = {
    (int32_t)(offsetof(Sound, paramFinalValues) - offsetof(Sound, patcher) - (FIRST_GLOBAL_PARAM * sizeof(int32_t))),
    offsetof(Sound, globalSourceValues) - offsetof(Sound, patcher),
    FIRST_GLOBAL_PARAM,
    FIRST_GLOBAL_NON_VOLUME_PARAM,
    FIRST_GLOBAL_HYBRID_PARAM,
    FIRST_GLOBAL_EXP_PARAM,
    NUM_PARAMS,
    GLOBALITY_GLOBAL};

Sound::Sound() : patcher(&patchableInfoForSound) {

	for (int s = 0; s < NUM_SOURCES; s++) {
		oscRetriggerPhase[s] = 0xFFFFFFFF;
	}
	for (int m = 0; m < numModulators; m++) {
		modulatorRetriggerPhase[m] = 0;
	}

	for (int i = 0; i < NUM_EXPRESSION_DIMENSIONS; i++) {
		monophonicExpressionValues[i] = 0;
	}

	numVoicesAssigned = 0;

	sideChainSendLevel = 0;
	polyphonic = POLYPHONY_POLY;
	lastNoteCode = -2147483648;

	modulatorTranspose[0] = 0;
	modulatorCents[0] = 0;
	modulatorTranspose[1] = -12;
	modulatorCents[1] = 0;

	transpose = 0;
	modFXType = MOD_FX_TYPE_NONE;

	oscillatorSync = false;

	numUnison = 1;
	unisonDetune = 8;

	synthMode = SYNTH_MODE_SUBTRACTIVE;
	modulator1ToModulator0 = false;

	lpfMode = LPF_MODE_TRANSISTOR_24DB; // Good for samples, I think

	postReverbVolumeLastTime = -1; // Special state to make it grab the actual value the first time it's rendered

	// LFO
	lfoGlobalWaveType = LFO_TYPE_TRIANGLE;
	lfoLocalWaveType = LFO_TYPE_TRIANGLE;
	lfoGlobalSyncType =
	    SYNC_TYPE_EVEN; // This may be set without calling the setter function, because we're setting it to 0
	lfoGlobalSyncLevel =
	    SYNC_LEVEL_NONE; // This may be set without calling the setter function, because we're setting it to 0

	modKnobs[0][1].paramDescriptor.setToHaveParamOnly(PARAM_GLOBAL_VOLUME_POST_FX);
	modKnobs[0][0].paramDescriptor.setToHaveParamOnly(PARAM_LOCAL_PAN);

	modKnobs[1][1].paramDescriptor.setToHaveParamOnly(PARAM_LOCAL_LPF_FREQ);
	modKnobs[1][0].paramDescriptor.setToHaveParamOnly(PARAM_LOCAL_LPF_RESONANCE);

	modKnobs[2][1].paramDescriptor.setToHaveParamOnly(PARAM_LOCAL_ENV_0_ATTACK);
	modKnobs[2][0].paramDescriptor.setToHaveParamOnly(PARAM_LOCAL_ENV_0_RELEASE);

	modKnobs[3][1].paramDescriptor.setToHaveParamOnly(PARAM_GLOBAL_DELAY_RATE);
	modKnobs[3][0].paramDescriptor.setToHaveParamOnly(PARAM_GLOBAL_DELAY_FEEDBACK);

	modKnobs[4][0].paramDescriptor.setToHaveParamOnly(PARAM_GLOBAL_REVERB_AMOUNT);

	modKnobs[5][1].paramDescriptor.setToHaveParamOnly(PARAM_GLOBAL_LFO_FREQ);

	modKnobs[4][1].paramDescriptor.setToHaveParamAndSource(PARAM_GLOBAL_VOLUME_POST_REVERB_SEND,
	                                                       PATCH_SOURCE_COMPRESSOR);
	modKnobs[5][0].paramDescriptor.setToHaveParamAndSource(PARAM_LOCAL_PITCH_ADJUST, PATCH_SOURCE_LFO_GLOBAL);

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	modKnobs[6][1].paramDescriptor.setToHaveParamOnly(PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_STUTTER_RATE);
	modKnobs[6][0].paramDescriptor.setToHaveParamOnly(PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_SOUND_PORTA);

	modKnobs[7][1].paramDescriptor.setToHaveParamOnly(PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_SAMPLE_RATE_REDUCTION);
	modKnobs[7][0].paramDescriptor.setToHaveParamOnly(PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_BITCRUSHING);
#endif

	voicePriority = 1;
	whichExpressionSourcesChangedAtSynthLevel = 0;

	skippingRendering = true;
	startSkippingRenderingAtTime = 0;

	paramLPF.p = PARAM_LPF_OFF;

	doneReadingFromFile();
}

void Sound::initParams(ParamManager* paramManager) {

	ModControllableAudio::initParams(paramManager);

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->params[PARAM_UNPATCHED_SOUND_ARP_GATE].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[PARAM_UNPATCHED_MOD_FX_FEEDBACK].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[PARAM_UNPATCHED_SOUND_PORTA].setCurrentValueBasicForSetup(-2147483648);

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	patchedParams->params[PARAM_LOCAL_VOLUME].setCurrentValueBasicForSetup(0);
	patchedParams->params[PARAM_LOCAL_OSC_A_VOLUME].setCurrentValueBasicForSetup(2147483647);
	patchedParams->params[PARAM_LOCAL_OSC_B_VOLUME].setCurrentValueBasicForSetup(2147483647);
	patchedParams->params[PARAM_GLOBAL_VOLUME_POST_FX].setCurrentValueBasicForSetup(
	    getParamFromUserValue(PARAM_GLOBAL_VOLUME_POST_FX, 40));
	patchedParams->params[PARAM_GLOBAL_VOLUME_POST_REVERB_SEND].setCurrentValueBasicForSetup(0);
	patchedParams->params[PARAM_LOCAL_HPF_RESONANCE].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_LOCAL_HPF_FREQ].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_LOCAL_PITCH_ADJUST].setCurrentValueBasicForSetup(0);
	patchedParams->params[PARAM_GLOBAL_REVERB_AMOUNT].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_GLOBAL_DELAY_RATE].setCurrentValueBasicForSetup(0);
	patchedParams->params[PARAM_GLOBAL_ARP_RATE].setCurrentValueBasicForSetup(0);
	patchedParams->params[PARAM_GLOBAL_DELAY_FEEDBACK].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_LOCAL_CARRIER_0_FEEDBACK].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_LOCAL_CARRIER_1_FEEDBACK].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_LOCAL_MODULATOR_0_FEEDBACK].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_LOCAL_MODULATOR_1_FEEDBACK].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_LOCAL_MODULATOR_0_VOLUME].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_LOCAL_MODULATOR_1_VOLUME].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_LOCAL_OSC_A_PHASE_WIDTH].setCurrentValueBasicForSetup(0);
	patchedParams->params[PARAM_LOCAL_OSC_B_PHASE_WIDTH].setCurrentValueBasicForSetup(0);
	patchedParams->params[PARAM_LOCAL_ENV_1_ATTACK].setCurrentValueBasicForSetup(
	    getParamFromUserValue(PARAM_LOCAL_ENV_1_ATTACK, 20));
	patchedParams->params[PARAM_LOCAL_ENV_1_DECAY].setCurrentValueBasicForSetup(
	    getParamFromUserValue(PARAM_LOCAL_ENV_1_DECAY, 20));
	patchedParams->params[PARAM_LOCAL_ENV_1_SUSTAIN].setCurrentValueBasicForSetup(
	    getParamFromUserValue(PARAM_LOCAL_ENV_1_SUSTAIN, 25));
	patchedParams->params[PARAM_LOCAL_ENV_1_RELEASE].setCurrentValueBasicForSetup(
	    getParamFromUserValue(PARAM_LOCAL_ENV_1_RELEASE, 20));
	patchedParams->params[PARAM_LOCAL_LFO_LOCAL_FREQ].setCurrentValueBasicForSetup(0);
	patchedParams->params[PARAM_GLOBAL_LFO_FREQ].setCurrentValueBasicForSetup(
	    getParamFromUserValue(PARAM_GLOBAL_LFO_FREQ, 30));
	patchedParams->params[PARAM_LOCAL_PAN].setCurrentValueBasicForSetup(0);
	patchedParams->params[PARAM_LOCAL_NOISE_VOLUME].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_GLOBAL_MOD_FX_DEPTH].setCurrentValueBasicForSetup(0);
	patchedParams->params[PARAM_GLOBAL_MOD_FX_RATE].setCurrentValueBasicForSetup(0);
	patchedParams->params[PARAM_LOCAL_OSC_A_PITCH_ADJUST].setCurrentValueBasicForSetup(0);       // Don't change
	patchedParams->params[PARAM_LOCAL_OSC_B_PITCH_ADJUST].setCurrentValueBasicForSetup(0);       // Don't change
	patchedParams->params[PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST].setCurrentValueBasicForSetup(0); // Don't change
	patchedParams->params[PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST].setCurrentValueBasicForSetup(0); // Don't change
}

void Sound::setupAsSample(ParamManagerForTimeline* paramManager) {

	polyphonic = POLYPHONY_AUTO;
	lpfMode = LPF_MODE_TRANSISTOR_24DB;

	sources[0].oscType = OSC_TYPE_SAMPLE;
	sources[1].oscType = OSC_TYPE_SAMPLE;

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	patchedParams->params[PARAM_LOCAL_OSC_B_VOLUME].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(
	    getParamFromUserValue(PARAM_LOCAL_ENV_0_ATTACK, 0));
	patchedParams->params[PARAM_LOCAL_ENV_0_DECAY].setCurrentValueBasicForSetup(
	    getParamFromUserValue(PARAM_LOCAL_ENV_0_DECAY, 20));
	patchedParams->params[PARAM_LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(
	    getParamFromUserValue(PARAM_LOCAL_ENV_0_SUSTAIN, 50));
	patchedParams->params[PARAM_LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
	    getParamFromUserValue(PARAM_LOCAL_ENV_0_RELEASE, 0));

	patchedParams->params[PARAM_LOCAL_LPF_RESONANCE].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_LOCAL_LPF_FREQ].setCurrentValueBasicForSetup(2147483647);

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	modKnobs[6][0].paramDescriptor.setToHaveParamOnly(PARAM_LOCAL_PITCH_ADJUST);
#endif

	paramManager->getPatchCableSet()->numPatchCables = 1;
	paramManager->getPatchCableSet()->patchCables[0].setup(PATCH_SOURCE_VELOCITY, PARAM_LOCAL_VOLUME,
	                                                       getParamFromUserValue(PARAM_STATIC_PATCH_CABLE, 50));

	setupDefaultExpressionPatching(paramManager);

	doneReadingFromFile();
}

void Sound::setupAsDefaultSynth(ParamManager* paramManager) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	patchedParams->params[PARAM_LOCAL_OSC_B_VOLUME].setCurrentValueBasicForSetup(0x47AE1457);
	patchedParams->params[PARAM_LOCAL_LPF_RESONANCE].setCurrentValueBasicForSetup(0xA2000000);
	patchedParams->params[PARAM_LOCAL_LPF_FREQ].setCurrentValueBasicForSetup(0x10000000);
	patchedParams->params[PARAM_LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(0x80000000);
	patchedParams->params[PARAM_LOCAL_ENV_0_DECAY].setCurrentValueBasicForSetup(0xE6666654);
	patchedParams->params[PARAM_LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(0x7FFFFFFF);
	patchedParams->params[PARAM_LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(0x851EB851);
	patchedParams->params[PARAM_LOCAL_ENV_1_ATTACK].setCurrentValueBasicForSetup(0xA3D70A37);
	patchedParams->params[PARAM_LOCAL_ENV_1_DECAY].setCurrentValueBasicForSetup(0xA3D70A37);
	patchedParams->params[PARAM_LOCAL_ENV_1_SUSTAIN].setCurrentValueBasicForSetup(0xFFFFFFE9);
	patchedParams->params[PARAM_LOCAL_ENV_1_RELEASE].setCurrentValueBasicForSetup(0xE6666654);
	patchedParams->params[PARAM_GLOBAL_VOLUME_POST_FX].setCurrentValueBasicForSetup(0x50000000);

	paramManager->getPatchCableSet()->patchCables[0].setup(PATCH_SOURCE_NOTE, PARAM_LOCAL_LPF_FREQ, 0x08F5C28C);
	paramManager->getPatchCableSet()->patchCables[1].setup(PATCH_SOURCE_ENVELOPE_1, PARAM_LOCAL_LPF_FREQ, 0x1C28F5B8);
	paramManager->getPatchCableSet()->patchCables[2].setup(PATCH_SOURCE_VELOCITY, PARAM_LOCAL_LPF_FREQ, 0x0F5C28F0);
	paramManager->getPatchCableSet()->patchCables[3].setup(PATCH_SOURCE_VELOCITY, PARAM_LOCAL_VOLUME, 0x3FFFFFE8);

	paramManager->getPatchCableSet()->numPatchCables = 4;

	setupDefaultExpressionPatching(paramManager);

	lpfMode = LPF_MODE_TRANSISTOR_24DB; // Good for samples, I think

	sources[0].oscType = OSC_TYPE_SAW;
	sources[1].transpose = -12;

	numUnison = 4;
	unisonDetune = 10;

	transpose = -12;

	doneReadingFromFile();
}

void Sound::possiblySetupDefaultExpressionPatching(ParamManager* paramManager) {

	if (storageManager.firmwareVersionOfFileBeingRead < FIRMWARE_4P0P0_BETA) {

		if (!paramManager->getPatchCableSet()->isSourcePatchedToSomethingManuallyCheckCables(PATCH_SOURCE_AFTERTOUCH)
		    && !paramManager->getPatchCableSet()->isSourcePatchedToSomethingManuallyCheckCables(PATCH_SOURCE_X)
		    && !paramManager->getPatchCableSet()->isSourcePatchedToSomethingManuallyCheckCables(PATCH_SOURCE_Y)) {

			setupDefaultExpressionPatching(paramManager);
		}
	}
}

void Sound::setupDefaultExpressionPatching(ParamManager* paramManager) {
	PatchCableSet* patchCableSet = paramManager->getPatchCableSet();

	if (patchCableSet->numPatchCables >= MAX_NUM_PATCH_CABLES) return;
	patchCableSet->patchCables[patchCableSet->numPatchCables++].setup(
	    PATCH_SOURCE_AFTERTOUCH, PARAM_LOCAL_VOLUME, getParamFromUserValue(PARAM_STATIC_PATCH_CABLE, 33));

	if (patchCableSet->numPatchCables >= MAX_NUM_PATCH_CABLES) return;

	if (synthMode == SYNTH_MODE_FM) {
		patchCableSet->patchCables[patchCableSet->numPatchCables++].setup(
		    PATCH_SOURCE_Y, PARAM_LOCAL_MODULATOR_0_VOLUME, getParamFromUserValue(PARAM_STATIC_PATCH_CABLE, 15));
	}
	else {
		patchCableSet->patchCables[patchCableSet->numPatchCables++].setup(
		    PATCH_SOURCE_Y, PARAM_LOCAL_LPF_FREQ, getParamFromUserValue(PARAM_STATIC_PATCH_CABLE, 20));
	}
}

void Sound::setupAsBlankSynth(ParamManager* paramManager) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	patchedParams->params[PARAM_LOCAL_OSC_B_VOLUME].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_LOCAL_LPF_FREQ].setCurrentValueBasicForSetup(2147483647);
	patchedParams->params[PARAM_LOCAL_LPF_RESONANCE].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(-2147483648);
	patchedParams->params[PARAM_LOCAL_ENV_0_DECAY].setCurrentValueBasicForSetup(
	    getParamFromUserValue(PARAM_LOCAL_ENV_0_DECAY, 20));
	patchedParams->params[PARAM_LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(2147483647);
	patchedParams->params[PARAM_LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(-2147483648);

	paramManager->getPatchCableSet()->numPatchCables = 1;
	paramManager->getPatchCableSet()->patchCables[0].setup(PATCH_SOURCE_VELOCITY, PARAM_LOCAL_VOLUME,
	                                                       getParamFromUserValue(PARAM_STATIC_PATCH_CABLE, 50));

	setupDefaultExpressionPatching(paramManager);

	doneReadingFromFile();
}

// Returns false if not enough ram
bool Sound::setModFXType(int newType) {
	if (newType == MOD_FX_TYPE_FLANGER || newType == MOD_FX_TYPE_CHORUS) {
		if (!modFXBuffer) {
			// TODO: should give an error here if no free ram
			modFXBuffer =
			    (StereoSample*)generalMemoryAllocator.alloc(modFXBufferSize * sizeof(StereoSample), NULL, false, true);
			if (!modFXBuffer) return false;
		}
	}
	else {
		if (modFXBuffer) {
			generalMemoryAllocator.dealloc(modFXBuffer);
			modFXBuffer = NULL;
		}
	}

	modFXType = newType;
	clearModFXMemory();
	return true;
}

void Sound::patchedParamPresetValueChanged(uint8_t p, ModelStackWithSoundFlags* modelStack, int32_t oldValue,
                                           int32_t newValue) {

	recalculatePatchingToParam(p, (ParamManagerForTimeline*)modelStack->paramManager);

	// If we just enabled an oscillator, we need to calculate voices' phase increments
	if (oldValue == -2147483648 && newValue != -2147483648) {

		// This will make inactive any voiceSources which currently have no volume. Ideally we'd only tell it to do the consideration for the oscillator in question, but oh well

		switch (p) {
		case PARAM_LOCAL_OSC_A_VOLUME:
		case PARAM_LOCAL_OSC_B_VOLUME:
		case PARAM_LOCAL_MODULATOR_0_VOLUME:
		case PARAM_LOCAL_MODULATOR_1_VOLUME:
			recalculateAllVoicePhaseIncrements(modelStack);
		}
	}
}

void Sound::recalculatePatchingToParam(uint8_t p, ParamManagerForTimeline* paramManager) {

	Destination* destination = paramManager->getPatchCableSet()->getDestinationForParam(p);
	if (destination) {
		sourcesChanged |=
		    destination
		        ->sources; // Pretend those sources have changed, and the param will update - for each Voice too if local.
	}

	// Otherwise, if nothing patched there...
	else {

		// Whether global...
		if (p >= FIRST_GLOBAL_PARAM) {
			patcher.recalculateFinalValueForParamWithNoCables(p, this, paramManager);
		}

		// Or local (do to each voice)...
		else {
			if (numVoicesAssigned) {
				int ends[2];
				AudioEngine::activeVoices.getRangeForSound(this, ends);
				for (int v = ends[0]; v < ends[1]; v++) {
					Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
					thisVoice->patcher.recalculateFinalValueForParamWithNoCables(p, this, paramManager);
				}
			}
		}
	}
}

#define ENSURE_PARAM_MANAGER_EXISTS                                                                                    \
	if (!paramManager->containsAnyMainParamCollections()) {                                                            \
		int error = createParamManagerForLoading(paramManager);                                                        \
		if (error) return error;                                                                                       \
	}                                                                                                                  \
	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();                      \
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;                  \
	ParamCollectionSummary* patchedParamsSummary = paramManager->getPatchedParamSetSummary();                          \
	PatchedParamSet* patchedParams = (PatchedParamSet*)patchedParamsSummary->paramCollection;

// paramManager only required for old old song files, or for presets (because you'd be wanting to extract the defaultParams into it).
// arpSettings optional - no need if you're loading a new V2.0 song where Instruments are all separate from Clips and won't store any arp stuff.
int Sound::readTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
                           ArpeggiatorSettings* arpSettings, Song* song) {

	if (!strcmp(tagName, "osc1")) {
		int error = readSourceFromFile(0, paramManager, readAutomationUpToPos);
		if (error) return error;
		storageManager.exitTag("osc1");
	}

	else if (!strcmp(tagName, "osc2")) {
		int error = readSourceFromFile(1, paramManager, readAutomationUpToPos);
		if (error) return error;
		storageManager.exitTag("osc2");
	}

	else if (!strcmp(tagName, "mode")) {
		char const* contents = storageManager.readTagOrAttributeValue();
		if (synthMode != SYNTH_MODE_RINGMOD) { // Compatibility with old XML files
			synthMode = stringToSynthMode(contents);
		}
		//Uart::print("synth mode set to: ");
		//Uart::println(synthMode);
		storageManager.exitTag("mode");
	}

	// Backwards-compatible reading of old-style oscs, from pre-mid-2016 files
	else if (!strcmp(tagName, "oscillatorA")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {

			if (!strcmp(tagName, "type")) {
				sources[0].oscType = stringToOscType(storageManager.readTagOrAttributeValue());
				storageManager.exitTag("type");
			}
			else if (!strcmp(tagName, "volume")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_A_VOLUME, readAutomationUpToPos);
				storageManager.exitTag("volume");
			}
			else if (!strcmp(tagName, "phaseWidth")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_A_PHASE_WIDTH, readAutomationUpToPos);
				storageManager.exitTag("phaseWidth");
			}
			else if (!strcmp(tagName, "note")) {
				int presetNote = storageManager.readTagOrAttributeValueInt();
				presetNote = getMax(0, getMin(127, presetNote));

				sources[0].transpose += presetNote - 60;
				sources[1].transpose += presetNote - 60;
				modulatorTranspose[0] += presetNote - 60;
				modulatorTranspose[1] += presetNote - 60;
				storageManager.exitTag("note");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}
		storageManager.exitTag("oscillatorA");
	}

	else if (!strcmp(tagName, "oscillatorB")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "type")) {
				sources[1].oscType = stringToOscType(storageManager.readTagOrAttributeValue());
				storageManager.exitTag("type");
			}
			else if (!strcmp(tagName, "volume")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_B_VOLUME, readAutomationUpToPos);
				storageManager.exitTag("volume");
			}
			else if (!strcmp(tagName, "phaseWidth")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_B_PHASE_WIDTH, readAutomationUpToPos);
				//setParamPresetValue(PARAM_LOCAL_OSC_B_PHASE_WIDTH, stringToInt(storageManager.readTagContents()) << 1); // Special case - pw values are stored at half size in file
				storageManager.exitTag("phaseWidth");
			}
			else if (!strcmp(tagName, "transpose")) {
				sources[1].transpose += storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("transpose");
			}
			else if (!strcmp(tagName, "cents")) {
				sources[1].cents = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("cents");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}
		storageManager.exitTag("oscillatorB");
	}

	else if (!strcmp(tagName, "modulator1")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "volume")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_MODULATOR_0_VOLUME, readAutomationUpToPos);
				storageManager.exitTag("volume");
			}
			else if (!strcmp(tagName, "transpose")) {
				modulatorTranspose[0] += storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("transpose");
			}
			else if (!strcmp(tagName, "cents")) {
				modulatorCents[0] = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("cents");
			}
			else if (!strcmp(tagName, "retrigPhase")) {
				modulatorRetriggerPhase[0] = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("retrigPhase");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}
		storageManager.exitTag("modulator1");
	}

	else if (!strcmp(tagName, "modulator2")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "volume")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_MODULATOR_1_VOLUME, readAutomationUpToPos);
				storageManager.exitTag("volume");
			}
			else if (!strcmp(tagName, "transpose")) {
				modulatorTranspose[1] += storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("transpose");
			}
			else if (!strcmp(tagName, "cents")) {
				modulatorCents[1] = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("cents");
			}
			else if (!strcmp(tagName, "retrigPhase")) {
				modulatorRetriggerPhase[1] = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("retrigPhase");
			}
			else if (!strcmp(tagName, "toModulator1")) {
				modulator1ToModulator0 = storageManager.readTagOrAttributeValueInt();
				if (modulator1ToModulator0 != 0) modulator1ToModulator0 = 1;
				storageManager.exitTag("toModulator1");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}
		storageManager.exitTag("modulator2");
	}

	else if (!strcmp(tagName, "arpeggiator")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {

			if (!strcmp(
			        tagName,
			        "rate")) { // This is here for compatibility only for people (Lou and Ian) who saved songs with firmware in September 2016
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_ARP_RATE, readAutomationUpToPos);
				storageManager.exitTag("rate");
			}
			else if (!strcmp(tagName, "numOctaves")) {
				if (arpSettings) arpSettings->numOctaves = storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("numOctaves");
			}
			else if (!strcmp(tagName, "syncLevel")) {
				if (arpSettings) arpSettings->syncLevel = storageManager.readAbsoluteSyncLevelFromFile(song);
				storageManager.exitTag("syncLevel");
			}
			else if (!strcmp(tagName, "mode")) {
				if (arpSettings) arpSettings->mode = stringToArpMode(storageManager.readTagOrAttributeValue());
				storageManager.exitTag("mode");
			}
			else if (
			    !strcmp(
			        tagName,
			        "gate")) { // This is here for compatibility only for people (Lou and Ian) who saved songs with firmware in September 2016
				ENSURE_PARAM_MANAGER_EXISTS
				unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_SOUND_ARP_GATE,
				                           readAutomationUpToPos);
				storageManager.exitTag("gate");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}

		storageManager.exitTag("arpeggiator");
	}

	else if (!strcmp(tagName, "transpose")) {
		transpose = storageManager.readTagOrAttributeValueInt();
		storageManager.exitTag("transpose");
	}

	else if (!strcmp(tagName, "noiseVolume")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_NOISE_VOLUME, readAutomationUpToPos);
		storageManager.exitTag("noiseVolume");
	}

	else if (
	    !strcmp(
	        tagName,
	        "portamento")) { // This is here for compatibility only for people (Lou and Ian) who saved songs with firmware in September 2016
		ENSURE_PARAM_MANAGER_EXISTS
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_SOUND_PORTA, readAutomationUpToPos);
		storageManager.exitTag("portamento");
	}

	// For backwards compatibility. If off, switch off for all operators
	else if (!strcmp(tagName, "oscillatorReset")) {
		int value = storageManager.readTagOrAttributeValueInt();
		if (!value) {
			for (int s = 0; s < NUM_SOURCES; s++) {
				oscRetriggerPhase[s] = 0xFFFFFFFF;
			}
			for (int m = 0; m < numModulators; m++) {
				modulatorRetriggerPhase[m] = 0xFFFFFFFF;
			}
		}
		storageManager.exitTag("oscillatorReset");
	}

	else if (!strcmp(tagName, "unison")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "num")) {
				int32_t contents = storageManager.readTagOrAttributeValueInt();
				numUnison = getMax((int32_t)0, getMin((int32_t)maxNumUnison, contents));
				storageManager.exitTag("num");
			}
			else if (!strcmp(tagName, "detune")) {
				int contents = storageManager.readTagOrAttributeValueInt();
				unisonDetune = getMax(0, getMin(MAX_UNISON_DETUNE, contents));
				storageManager.exitTag("detune");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}
		storageManager.exitTag("unison");
	}

	else if (!strcmp(tagName, "oscAPitchAdjust")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_A_PITCH_ADJUST, readAutomationUpToPos);
		storageManager.exitTag("oscAPitchAdjust");
	}

	else if (!strcmp(tagName, "oscBPitchAdjust")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_B_PITCH_ADJUST, readAutomationUpToPos);
		storageManager.exitTag("oscBPitchAdjust");
	}

	else if (!strcmp(tagName, "mod1PitchAdjust")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST, readAutomationUpToPos);
		storageManager.exitTag("mod1PitchAdjust");
	}

	else if (!strcmp(tagName, "mod2PitchAdjust")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST, readAutomationUpToPos);
		storageManager.exitTag("mod2PitchAdjust");
	}

	// Stuff from the early-2016 format, for compatibility
	else if (!strcmp(tagName, "fileName")) {
		ENSURE_PARAM_MANAGER_EXISTS

		MultisampleRange* range = (MultisampleRange*)sources[0].getOrCreateFirstRange();
		if (!range) return ERROR_INSUFFICIENT_RAM;

		range->getAudioFileHolder()->filePath.set(storageManager.readTagOrAttributeValue());
		sources[0].oscType = OSC_TYPE_SAMPLE;
		paramManager->getPatchedParamSet()->params[PARAM_LOCAL_ENV_0_ATTACK].setCurrentValueBasicForSetup(
		    getParamFromUserValue(PARAM_LOCAL_ENV_0_ATTACK, 0));
		paramManager->getPatchedParamSet()->params[PARAM_LOCAL_ENV_0_DECAY].setCurrentValueBasicForSetup(
		    getParamFromUserValue(PARAM_LOCAL_ENV_0_DECAY, 20));
		paramManager->getPatchedParamSet()->params[PARAM_LOCAL_ENV_0_SUSTAIN].setCurrentValueBasicForSetup(
		    getParamFromUserValue(PARAM_LOCAL_ENV_0_SUSTAIN, 50));
		paramManager->getPatchedParamSet()->params[PARAM_LOCAL_ENV_0_RELEASE].setCurrentValueBasicForSetup(
		    getParamFromUserValue(PARAM_LOCAL_ENV_0_RELEASE, 0));
		paramManager->getPatchedParamSet()->params[PARAM_LOCAL_OSC_A_VOLUME].setCurrentValueBasicForSetup(
		    getParamFromUserValue(PARAM_LOCAL_OSC_B_VOLUME, 50));
		paramManager->getPatchedParamSet()->params[PARAM_LOCAL_OSC_B_VOLUME].setCurrentValueBasicForSetup(
		    getParamFromUserValue(PARAM_LOCAL_OSC_B_VOLUME, 0));

		storageManager.exitTag("fileName");
	}

	else if (!strcmp(tagName, "cents")) {
		int8_t newCents = storageManager.readTagOrAttributeValueInt();
		sources[0].cents = (getMax(
		    (int8_t)-50,
		    getMin(
		        (int8_t)50,
		        newCents))); // We don't need to call the setTranspose method here, because this will get called soon anyway, once the sample rate is known
		storageManager.exitTag("cents");
	}
	else if (!strcmp(tagName, "continuous")) {
		sources[0].repeatMode = storageManager.readTagOrAttributeValueInt();
		sources[0].repeatMode = getMin(sources[0].repeatMode, (uint8_t)(NUM_REPEAT_MODES - 1));
		storageManager.exitTag("continuous");
	}
	else if (!strcmp(tagName, "reversed")) {
		sources[0].sampleControls.reversed = storageManager.readTagOrAttributeValueInt();
		storageManager.exitTag("reversed");
	}
	else if (!strcmp(tagName, "zone")) {

		MultisampleRange* range = (MultisampleRange*)sources[0].getOrCreateFirstRange();
		if (!range) return ERROR_INSUFFICIENT_RAM;

		range->sampleHolder.startMSec = 0;
		range->sampleHolder.endMSec = 0;
		range->sampleHolder.startPos = 0;
		range->sampleHolder.endPos = 0;
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			// Because this is for old, early-2016 format, there'll only be seconds and milliseconds in here, not samples
			if (!strcmp(tagName, "startSeconds")) {
				range->sampleHolder.startMSec += storageManager.readTagOrAttributeValueInt() * 1000;
				storageManager.exitTag("startSeconds");
			}
			else if (!strcmp(tagName, "startMilliseconds")) {
				range->sampleHolder.startMSec += storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("startMilliseconds");
			}
			else if (!strcmp(tagName, "endSeconds")) {
				range->sampleHolder.endMSec += storageManager.readTagOrAttributeValueInt() * 1000;
				storageManager.exitTag("endSeconds");
			}
			else if (!strcmp(tagName, "endMilliseconds")) {
				range->sampleHolder.endMSec += storageManager.readTagOrAttributeValueInt();
				storageManager.exitTag("endMilliseconds");
			}
		}
		storageManager.exitTag("zone");
	}

	else if (!strcmp(tagName, "ringMod")) {
		int32_t contents = storageManager.readTagOrAttributeValueInt();
		if (contents == 1) synthMode = SYNTH_MODE_RINGMOD;
		storageManager.exitTag("ringMod");
	}

	else if (!strcmp(tagName, "modKnobs")) {

		int k = 0;
		int w = 0;

		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "modKnob")) {

				uint8_t p = PARAM_NONE;
				uint8_t s = 255;
				uint8_t s2 = 255;

				while (*(tagName = storageManager.readNextTagOrAttributeName())) {
					if (!strcmp(tagName, "controlsParam")) {
						p = stringToParam(storageManager.readTagOrAttributeValue());
					}
					else if (!strcmp(tagName, "patchAmountFromSource")) {
						s = stringToSource(storageManager.readTagOrAttributeValue());
					}
					else if (!strcmp(tagName, "patchAmountFromSecondSource")) {
						s2 = stringToSource(storageManager.readTagOrAttributeValue());
					}
					storageManager.exitTag(tagName);
				}

				if (k < NUM_MOD_BUTTONS) { // Ensure we're not loading more than actually fit in our array
					if (p != PARAM_NONE
					    && p != PARAM_PLACEHOLDER_RANGE) { // Discard any unlikely "range" ones from before V3.2.0, for complex reasons
						ModKnob* newKnob = &modKnobs[k][w];

						if (s == 255) {
							newKnob->paramDescriptor.setToHaveParamOnly(p);
						}
						else if (s2 == 255) {
							newKnob->paramDescriptor.setToHaveParamAndSource(p, s);
						}
						else {
							newKnob->paramDescriptor.setToHaveParamAndTwoSources(p, s, s2);
						}

						ensureKnobReferencesCorrectVolume(newKnob);
					}
				}

				w++;
				if (w == NUM_PHYSICAL_MOD_KNOBS) {
					w = 0;
					k++;

					// If this is a 40-pad Deluge reading a drum preset made for the 144-pad, make "custom 1" be pitch
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
					if (k == NUM_MOD_BUTTONS && isDrum()) {
						modKnobs[5][1].paramDescriptor.setToHaveParamOnly(PARAM_LOCAL_PITCH_ADJUST);
					}
#endif
				}
			}
			storageManager.exitTag();
		}
		storageManager.exitTag("modKnobs");
	}

	else if (!strcmp(tagName, "patchCables")) {
		ENSURE_PARAM_MANAGER_EXISTS
		paramManager->getPatchCableSet()->readPatchCablesFromFile(readAutomationUpToPos);
		storageManager.exitTag("patchCables");
	}

	else if (!strcmp(tagName, "volume")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_VOLUME_POST_FX, readAutomationUpToPos);
		storageManager.exitTag("volume");
	}

	else if (!strcmp(tagName, "pan")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_PAN, readAutomationUpToPos);
		storageManager.exitTag("pan");
	}

	else if (!strcmp(tagName, "pitchAdjust")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_PITCH_ADJUST, readAutomationUpToPos);
		storageManager.exitTag("pitchAdjust");
	}

	else if (!strcmp(tagName, "modFXType")) {
		bool result = setModFXType(
		    stringToFXType(storageManager.readTagOrAttributeValue())); // This might not work if not enough RAM
		if (!result) numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
		storageManager.exitTag("modFXType");
	}

	else if (!strcmp(tagName, "fx")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {

			if (!strcmp(tagName, "type")) {
				bool result = setModFXType(
				    stringToFXType(storageManager.readTagOrAttributeValue())); // This might not work if not enough RAM
				if (!result) numericDriver.displayError(ERROR_INSUFFICIENT_RAM);
				storageManager.exitTag("type");
			}
			else if (!strcmp(tagName, "rate")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_MOD_FX_RATE, readAutomationUpToPos);
				storageManager.exitTag("rate");
			}
			else if (!strcmp(tagName, "feedback")) {
				// This is for compatibility with old files. Some reverse calculation needs to be done.
				int finalValue = storageManager.readTagOrAttributeValueInt();
				int32_t i =
				    (1 - pow(1 - ((float)finalValue / 2147483648), (float)1 / 3)) / 0.74 * 4294967296 - 2147483648;
				ENSURE_PARAM_MANAGER_EXISTS
				paramManager->getUnpatchedParamSet()
				    ->params[PARAM_UNPATCHED_MOD_FX_FEEDBACK]
				    .setCurrentValueBasicForSetup(i);
				storageManager.exitTag("feedback");
			}
			else if (!strcmp(tagName, "offset")) {
				// This is for compatibility with old files. Some reverse calculation needs to be done.
				int32_t contents = storageManager.readTagOrAttributeValueInt();
				int32_t value = ((int64_t)contents << 8) - 2147483648;
				ENSURE_PARAM_MANAGER_EXISTS
				paramManager->getUnpatchedParamSet()
				    ->params[PARAM_UNPATCHED_MOD_FX_OFFSET]
				    .setCurrentValueBasicForSetup(value);
				storageManager.exitTag("offset");
			}
			else if (!strcmp(tagName, "depth")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_MOD_FX_DEPTH, readAutomationUpToPos);
				storageManager.exitTag("depth");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}
		storageManager.exitTag("fx");
	}

	else if (!strcmp(tagName, "lfo1")) {
		// Set default values in case they are not configured.
		// setLFOGlobalSyncLevel will also set type based on value.
		setLFOGlobalSyncLevel(SYNC_LEVEL_NONE);

		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "type")) {
				setLFOGlobalWave(stringToLFOType(storageManager.readTagOrAttributeValue()));
				storageManager.exitTag("type");
			}
			else if (!strcmp(tagName, "rate")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_LFO_FREQ, readAutomationUpToPos);
				storageManager.exitTag("rate");
			}
			else if (!strcmp(tagName, "syncType")) {
				setLFOGlobalSyncType(storageManager.readSyncTypeFromFile(song));
				storageManager.exitTag("syncType");
			}
			else if (!strcmp(tagName, "syncLevel")) {
				setLFOGlobalSyncLevel(storageManager.readAbsoluteSyncLevelFromFile(song));
				storageManager.exitTag("syncLevel");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}
		storageManager.exitTag("lfo1");
	}

	else if (!strcmp(tagName, "lfo2")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "type")) {
				lfoLocalWaveType = stringToLFOType(storageManager.readTagOrAttributeValue());
				storageManager.exitTag("type");
			}
			else if (!strcmp(tagName, "rate")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_LFO_LOCAL_FREQ, readAutomationUpToPos);
				storageManager.exitTag("rate");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}
		storageManager.exitTag("lfo2");
	}

	else if (!strcmp(tagName, "sideChainSend")) {
		sideChainSendLevel = storageManager.readTagOrAttributeValueInt();
		storageManager.exitTag("sideChainSend");
	}

	else if (!strcmp(tagName, "lpf")) {
		bool switchedOn = true; // For backwards compatibility with pre November 2015 files
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "status")) {
				int32_t contents = storageManager.readTagOrAttributeValueInt();
				switchedOn = getMax((int32_t)0, getMin((int32_t)1, contents));
				storageManager.exitTag("status");
			}
			else if (!strcmp(tagName, "frequency")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_LPF_FREQ, readAutomationUpToPos);
				storageManager.exitTag("frequency");
			}
			else if (!strcmp(tagName, "resonance")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_LPF_RESONANCE, readAutomationUpToPos);
				storageManager.exitTag("resonance");
			}
			else if (!strcmp(tagName, "mode")) { // For old, pre- October 2016 files
				lpfMode = stringToLPFType(storageManager.readTagOrAttributeValue());
				storageManager.exitTag("mode");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}
		if (!switchedOn) {
			ENSURE_PARAM_MANAGER_EXISTS
			paramManager->getPatchedParamSet()->params[PARAM_LOCAL_LPF_FREQ].setCurrentValueBasicForSetup(
			    getParamFromUserValue(PARAM_LOCAL_LPF_FREQ, 50));
		}

		storageManager.exitTag("lpf");
	}

	else if (!strcmp(tagName, "hpf")) {
		bool switchedOn = true; // For backwards compatibility with pre November 2015 files
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "status")) {
				int32_t contents = storageManager.readTagOrAttributeValueInt();
				switchedOn = getMax((int32_t)0, getMin((int32_t)1, contents));
				storageManager.exitTag("status");
			}
			else if (!strcmp(tagName, "frequency")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_HPF_FREQ, readAutomationUpToPos);
				storageManager.exitTag("frequency");
			}
			else if (!strcmp(tagName, "resonance")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_HPF_RESONANCE, readAutomationUpToPos);
				storageManager.exitTag("resonance");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}
		if (!switchedOn) {
			ENSURE_PARAM_MANAGER_EXISTS
			paramManager->getPatchedParamSet()->params[PARAM_LOCAL_HPF_FREQ].setCurrentValueBasicForSetup(
			    getParamFromUserValue(PARAM_LOCAL_HPF_FREQ, 50));
		}

		storageManager.exitTag("hpf");
	}

	else if (!strcmp(tagName, "envelope1")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "attack")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_0_ATTACK, readAutomationUpToPos);
				storageManager.exitTag("attack");
			}
			else if (!strcmp(tagName, "decay")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_0_DECAY, readAutomationUpToPos);
				storageManager.exitTag("decay");
			}
			else if (!strcmp(tagName, "sustain")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_0_SUSTAIN, readAutomationUpToPos);
				storageManager.exitTag("sustain");
			}
			else if (!strcmp(tagName, "release")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_0_RELEASE, readAutomationUpToPos);
				storageManager.exitTag("release");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}

		storageManager.exitTag("envelope1");
	}

	else if (!strcmp(tagName, "envelope2")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "attack")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_1_ATTACK, readAutomationUpToPos);
				storageManager.exitTag("attack");
			}
			else if (!strcmp(tagName, "decay")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_1_DECAY, readAutomationUpToPos);
				storageManager.exitTag("decay");
			}
			else if (!strcmp(tagName, "sustain")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_1_SUSTAIN, readAutomationUpToPos);
				storageManager.exitTag("sustain");
			}
			else if (!strcmp(tagName, "release")) {
				ENSURE_PARAM_MANAGER_EXISTS
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_1_RELEASE, readAutomationUpToPos);
				storageManager.exitTag("release");
			}
			else {
				storageManager.exitTag(tagName);
			}
		}

		storageManager.exitTag("envelope2");
	}

	else if (!strcmp(tagName, "polyphonic")) {
		polyphonic = stringToPolyphonyMode(storageManager.readTagOrAttributeValue());
		storageManager.exitTag("polyphonic");
	}

	else if (!strcmp(tagName, "voicePriority")) {
		voicePriority = storageManager.readTagOrAttributeValueInt();
		storageManager.exitTag("voicePriority");
	}

	else if (!strcmp(tagName, "reverbAmount")) {
		ENSURE_PARAM_MANAGER_EXISTS
		patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_REVERB_AMOUNT, readAutomationUpToPos);
		storageManager.exitTag("reverbAmount");
	}

	else if (!strcmp(tagName, "defaultParams")) {
		ENSURE_PARAM_MANAGER_EXISTS
		Sound::readParamsFromFile(paramManager, readAutomationUpToPos);
		storageManager.exitTag("defaultParams");
	}

	else {
		int result = ModControllableAudio::readTagFromFile(tagName, paramManager, readAutomationUpToPos, song);
		if (result == NO_ERROR) {}
		else if (result != RESULT_TAG_UNUSED) return result;
		else if (readTagFromFile(tagName)) {}
		else {
			result = storageManager.tryReadingFirmwareTagFromFile(tagName);
			if (result && result != RESULT_TAG_UNUSED) return result;
			storageManager.exitTag();
		}
	}

	return NO_ERROR;
}

// Exists for the purpose of potentially correcting an incorrect file as it's loaded
void Sound::ensureKnobReferencesCorrectVolume(Knob* knob) {
	int p = knob->paramDescriptor.getJustTheParam();

	if (p == PARAM_GLOBAL_VOLUME_POST_REVERB_SEND || p == PARAM_GLOBAL_VOLUME_POST_FX || p == PARAM_LOCAL_VOLUME) {
		if (knob->paramDescriptor.isJustAParam()) knob->paramDescriptor.setToHaveParamOnly(PARAM_GLOBAL_VOLUME_POST_FX);
		else if (knob->paramDescriptor.getTopLevelSource() == PATCH_SOURCE_COMPRESSOR)
			knob->paramDescriptor.changeParam(PARAM_GLOBAL_VOLUME_POST_REVERB_SEND);
		else knob->paramDescriptor.changeParam(PARAM_LOCAL_VOLUME);
	}
}

// p=255 means we're just querying the source to see if it can be patched to anything
uint8_t Sound::maySourcePatchToParam(uint8_t s, uint8_t p, ParamManager* paramManager) {

	if (s == PATCH_SOURCE_NOTE && isDrum()) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;

	if (p != 255 && s != 255 && s >= FIRST_LOCAL_SOURCE && p >= FIRST_GLOBAL_PARAM)
		return PATCH_CABLE_ACCEPTANCE_DISALLOWED; // Can't patch local source to global param

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	switch (p) {
	case PARAM_NONE:
		return PATCH_CABLE_ACCEPTANCE_DISALLOWED;

	case PARAM_LOCAL_VOLUME:
		return (s != PATCH_SOURCE_ENVELOPE_0
		        && s != PATCH_SOURCE_ENVELOPE_1 // No envelopes allowed to be patched to volume - this is hardcoded elsewhere
		        && s != PATCH_SOURCE_COMPRESSOR) // Don't let the compressor patch to local volume - it's supposed to go to global volume
		           ? PATCH_CABLE_ACCEPTANCE_ALLOWED
		           : PATCH_CABLE_ACCEPTANCE_DISALLOWED;

	case PARAM_LOCAL_OSC_A_PHASE_WIDTH:
		if (getSynthMode() == SYNTH_MODE_FM) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
		//if (getSynthMode() == SYNTH_MODE_FM || (sources[0].oscType != OSC_TYPE_SQUARE && sources[0].oscType != OSC_TYPE_JUNO60_SUBOSC)) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
		break;
	case PARAM_LOCAL_OSC_A_VOLUME:
		if (getSynthMode() == SYNTH_MODE_RINGMOD) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
	case PARAM_LOCAL_OSC_A_PITCH_ADJUST:
		return (isSourceActiveEverDisregardingMissingSample(0, paramManager)) ? PATCH_CABLE_ACCEPTANCE_ALLOWED
		                                                                      : PATCH_CABLE_ACCEPTANCE_EDITABLE;

	case PARAM_LOCAL_CARRIER_0_FEEDBACK:
		if (synthMode != SYNTH_MODE_FM) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
		return (isSourceActiveEver(0, paramManager)
		        && patchedParams->params[PARAM_LOCAL_CARRIER_0_FEEDBACK].containsSomething(-2147483648))
		           ? PATCH_CABLE_ACCEPTANCE_ALLOWED
		           : PATCH_CABLE_ACCEPTANCE_EDITABLE;

	case PARAM_LOCAL_OSC_B_PHASE_WIDTH:
		if (getSynthMode() == SYNTH_MODE_FM) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
		//if (getSynthMode() == SYNTH_MODE_FM || (sources[1].oscType != OSC_TYPE_SQUARE && sources[1].oscType != OSC_TYPE_JUNO60_SUBOSC)) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
		break;
	case PARAM_LOCAL_OSC_B_VOLUME:
		if (getSynthMode() == SYNTH_MODE_RINGMOD) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
	case PARAM_LOCAL_OSC_B_PITCH_ADJUST:
		return (isSourceActiveEverDisregardingMissingSample(1, paramManager)) ? PATCH_CABLE_ACCEPTANCE_ALLOWED
		                                                                      : PATCH_CABLE_ACCEPTANCE_EDITABLE;

	case PARAM_LOCAL_CARRIER_1_FEEDBACK:
		if (synthMode != SYNTH_MODE_FM) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
		return (isSourceActiveEver(1, paramManager)
		        && patchedParams->params[PARAM_LOCAL_CARRIER_1_FEEDBACK].containsSomething(-2147483648))
		           ? PATCH_CABLE_ACCEPTANCE_ALLOWED
		           : PATCH_CABLE_ACCEPTANCE_EDITABLE;

	case PARAM_LOCAL_NOISE_VOLUME:
		if (synthMode == SYNTH_MODE_FM) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
		return (patchedParams->params[PARAM_LOCAL_NOISE_VOLUME].containsSomething(-2147483648))
		           ? PATCH_CABLE_ACCEPTANCE_ALLOWED
		           : PATCH_CABLE_ACCEPTANCE_EDITABLE;

		//case PARAM_LOCAL_RANGE:
		//return (rangeAdjustedParam != PARAM_NONE); // Ideally we'd also check whether the range-adjusted param actually has something patched to it

	case PARAM_LOCAL_LPF_FREQ:
	case PARAM_LOCAL_LPF_RESONANCE:
		if (synthMode == SYNTH_MODE_FM) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
		break;

	case PARAM_LOCAL_HPF_FREQ:
	case PARAM_LOCAL_HPF_RESONANCE:
		if (synthMode == SYNTH_MODE_FM) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
		break;

	case PARAM_LOCAL_MODULATOR_0_VOLUME:
	case PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST:
		if (synthMode != SYNTH_MODE_FM) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
		return (patchedParams->params[PARAM_LOCAL_MODULATOR_0_VOLUME].containsSomething(-2147483648))
		           ? PATCH_CABLE_ACCEPTANCE_ALLOWED
		           : PATCH_CABLE_ACCEPTANCE_EDITABLE;

	case PARAM_LOCAL_MODULATOR_0_FEEDBACK:
		if (synthMode != SYNTH_MODE_FM) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
		return (patchedParams->params[PARAM_LOCAL_MODULATOR_0_VOLUME].containsSomething(-2147483648)
		        && patchedParams->params[PARAM_LOCAL_MODULATOR_0_FEEDBACK].containsSomething(-2147483648))
		           ? PATCH_CABLE_ACCEPTANCE_ALLOWED
		           : PATCH_CABLE_ACCEPTANCE_EDITABLE;

	case PARAM_LOCAL_MODULATOR_1_VOLUME:
	case PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST:
		if (synthMode != SYNTH_MODE_FM) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
		return (patchedParams->params[PARAM_LOCAL_MODULATOR_1_VOLUME].containsSomething(-2147483648))
		           ? PATCH_CABLE_ACCEPTANCE_ALLOWED
		           : PATCH_CABLE_ACCEPTANCE_EDITABLE;

	case PARAM_LOCAL_MODULATOR_1_FEEDBACK:
		if (synthMode != SYNTH_MODE_FM) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
		return (patchedParams->params[PARAM_LOCAL_MODULATOR_1_VOLUME].containsSomething(-2147483648)
		        && patchedParams->params[PARAM_LOCAL_MODULATOR_1_FEEDBACK].containsSomething(-2147483648))
		           ? PATCH_CABLE_ACCEPTANCE_ALLOWED
		           : PATCH_CABLE_ACCEPTANCE_EDITABLE;

	case PARAM_GLOBAL_LFO_FREQ:
		return (lfoGlobalSyncLevel == SYNC_LEVEL_NONE) ? PATCH_CABLE_ACCEPTANCE_ALLOWED
		                                               : PATCH_CABLE_ACCEPTANCE_DISALLOWED;

		// Nothing may patch to post-fx volume. This is for manual control only. The compressor patches to post-reverb volume, and everything else patches to per-voice, "local" volume
	case PARAM_GLOBAL_VOLUME_POST_FX:
		return PATCH_CABLE_ACCEPTANCE_DISALLOWED;

	case PARAM_LOCAL_PITCH_ADJUST:
		if (s == PATCH_SOURCE_X)
			return PATCH_CABLE_ACCEPTANCE_DISALLOWED; // No patching X to pitch. This happens automatically.
		break;

	case PARAM_GLOBAL_VOLUME_POST_REVERB_SEND: // Only the compressor can patch to here
		if (s != PATCH_SOURCE_COMPRESSOR) return PATCH_CABLE_ACCEPTANCE_DISALLOWED;
		break;

		// In a perfect world, we'd only allow patching to LFO rates if the LFO as a source is itself patched somewhere usable
	}

	return PATCH_CABLE_ACCEPTANCE_ALLOWED;
}

void Sound::noteOn(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator, int noteCodePreArp,
                   int16_t const* mpeValues, uint32_t sampleSyncLength, int32_t ticksLate, uint32_t samplesLate,
                   int velocity, int fromMIDIChannel) {

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;

	ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();

	if (synthMode == SYNTH_MODE_RINGMOD) goto allFine;
	if (modelStackWithSoundFlags->checkSourceEverActive(0)) goto allFine;
	if (modelStackWithSoundFlags->checkSourceEverActive(1)) goto allFine;
	if (paramManager->getPatchedParamSet()->params[PARAM_LOCAL_NOISE_VOLUME].containsSomething(-2147483648))
		goto allFine;

	return;

allFine:

	ArpeggiatorSettings* arpSettings = getArpSettings();
	getArpBackInTimeAfterSkippingRendering(arpSettings); // Have to do this before telling the arp to noteOn()

	ArpReturnInstruction instruction;
	instruction.sampleSyncLengthOn = sampleSyncLength;

	// We used to not have to worry about the arpeggiator if one-shot samples etc. But now that we support MPE, we do need to keep track of all sounding notes,
	// even one-shot ones, and the "arpeggiator" is where this is stored. These will get left here even after the note has long gone (for sequenced notes anyway),
	// but I can't actually find any negative consequence of this, or need to ever remove them en masse.
	arpeggiator->noteOn(arpSettings, noteCodePreArp, velocity, &instruction, fromMIDIChannel, mpeValues);

	if (instruction.noteCodeOnPostArp != ARP_NOTE_NONE) {
		noteOnPostArpeggiator(modelStackWithSoundFlags, noteCodePreArp, instruction.noteCodeOnPostArp, velocity,
		                      mpeValues, instruction.sampleSyncLengthOn, ticksLate, samplesLate, fromMIDIChannel);
	}
}

void Sound::noteOnPostArpeggiator(ModelStackWithSoundFlags* modelStack, int noteCodePreArp, int noteCodePostArp,
                                  int velocity, int16_t const* mpeValues, uint32_t sampleSyncLength, int32_t ticksLate,
                                  uint32_t samplesLate, int fromMIDIChannel) {

	Voice* voiceToReuse = NULL;

	Voice* voiceForLegato = NULL;

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;

	// If not polyphonic, stop any notes which are releasing, now
	if (numVoicesAssigned && polyphonic != POLYPHONY_POLY) {

		int ends[2];
		AudioEngine::activeVoices.getRangeForSound(this, ends);
		for (int v = ends[0]; v < ends[1]; v++) {
			Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);

			// If we're proper-MONO, or it's releasing OR has no sustain / note tails
			if (polyphonic == POLYPHONY_MONO || thisVoice->envelopes[0].state >= ENVELOPE_STAGE_RELEASE
			    || !allowNoteTails(
			        modelStack,
			        true)) { // allowNoteTails() is very nearly exactly what we want to be calling here, though not named after the thing we're looking for here

				// If non-FM and all active sources are samples, do a fast-release (if not already fast-releasing). Otherwise, just unassign (cut instantly)
				if (synthMode == SYNTH_MODE_FM) {
justUnassign:
					// Ideally, we want to save this voice to reuse. But we can only do that for the first such one
					if (!voiceToReuse) {
						voiceToReuse = thisVoice;
						thisVoice->unassignStuff();
					}

					// Or if we'd already found one, have to just unassign this new one
					else {
						if (ALPHA_OR_BETA_VERSION) AudioEngine::activeVoices.checkVoiceExists(thisVoice, this, "E198");
						AudioEngine::unassignVoice(thisVoice, this, modelStack);
						v--;
						ends[1]--;
					}
				}
				else {
					for (int s = 0; s < NUM_SOURCES; s++) {
						if (isSourceActiveCurrently(s, paramManager) && sources[s].oscType != OSC_TYPE_SAMPLE) {
							goto justUnassign;
						}
					}

					if (thisVoice->envelopes[0].state != ENVELOPE_STAGE_FAST_RELEASE) {
						bool stillGoing = thisVoice->doFastRelease();

						if (!stillGoing) goto justUnassign;
					}
				}
			}

			// Otherwise...
			else {
				voiceForLegato = thisVoice;
				break;
			}
		}
	}

	if (polyphonic == POLYPHONY_LEGATO && voiceForLegato) {
		ModelStackWithVoice* modelStackWithVoice = modelStack->addVoice(voiceForLegato);
		voiceForLegato->changeNoteCode(modelStackWithVoice, noteCodePreArp, noteCodePostArp, fromMIDIChannel,
		                               mpeValues);
	}
	else {

		Voice* newVoice;
		int32_t envelopePositions[numEnvelopes];

		if (voiceToReuse) {
			newVoice = voiceToReuse;

			// The osc phases and stuff will remain

			for (int e = 0; e < numEnvelopes; e++) {
				envelopePositions[e] = voiceToReuse->envelopes[e].lastValue;
			}
		}

		else {
			newVoice = AudioEngine::solicitVoice(this);
			if (!newVoice) return; // Should basically never happen
			numVoicesAssigned++;
			reassessRenderSkippingStatus(
			    modelStack); // Since we potentially just changed numVoicesAssigned from 0 to 1.

			newVoice->randomizeOscPhases(this);
		}

		if (sideChainSendLevel != 0) AudioEngine::registerSideChainHit(sideChainSendLevel);

		ModelStackWithVoice* modelStackWithVoice = modelStack->addVoice(newVoice);

		bool success =
		    newVoice->noteOn(modelStackWithVoice, noteCodePreArp, noteCodePostArp, velocity, sampleSyncLength,
		                     ticksLate, samplesLate, voiceToReuse == NULL, fromMIDIChannel, mpeValues);
		if (success) {
			if (voiceToReuse) {
				for (int e = 0; e < numEnvelopes; e++) {
					newVoice->envelopes[e].resumeAttack(envelopePositions[e]);
				}
			}
		}

		else {
			AudioEngine::activeVoices.checkVoiceExists(newVoice, this, "E199");
			AudioEngine::unassignVoice(newVoice, this, modelStack);
		}
	}

	lastNoteCode = noteCodePostArp; // Store for porta. We store that at both note-on and note-off.
}

void Sound::allNotesOff(ModelStackWithThreeMainThings* modelStack, ArpeggiatorBase* arpeggiator) {

	arpeggiator->reset();

#if ALPHA_OR_BETA_VERSION
	if (!modelStack->paramManager)
		numericDriver.freezeWithError(
		    "E403"); // Previously we were allowed to receive a NULL paramManager, then would just crudely do an unassignAllVoices(). But I'm pretty sure this doesn't exist anymore?
#endif

	ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();

	noteOffPostArpeggiator(modelStackWithSoundFlags, -32768);
}

// noteCode = -32768 (default) means stop *any* voice, regardless of noteCode
void Sound::noteOffPostArpeggiator(ModelStackWithSoundFlags* modelStack, int noteCode) {
	if (!numVoicesAssigned) return;

	int ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		if ((thisVoice->noteCodeAfterArpeggiation == noteCode || noteCode == -32768)
		    && thisVoice->envelopes[0].state < ENVELOPE_STAGE_RELEASE) { // Don't bother if it's already "releasing"

			ArpeggiatorSettings* arpSettings = getArpSettings();

			ModelStackWithVoice* modelStackWithVoice = modelStack->addVoice(thisVoice);

			// If we have actual arpeggiation, just switch off.
			if (arpSettings && arpSettings->mode) {
				goto justSwitchOff;
			}

			// If we're in LEGATO or true-MONO mode and there's another note we can switch back to...
			if ((polyphonic == POLYPHONY_LEGATO || polyphonic == POLYPHONY_MONO) && !isDrum()
			    && allowNoteTails(
			        modelStackWithVoice)) { // If no note-tails (i.e. yes one-shot samples etc.), the Arpeggiator will be full of notes which
				// might not be active anymore, cos we were keeping track of them for MPE purposes.
				Arpeggiator* arpeggiator = &((SoundInstrument*)this)->arpeggiator;
				if (arpeggiator->hasAnyInputNotesActive()) {
					ArpNote* arpNote =
					    (ArpNote*)arpeggiator->notes.getElementAddress(arpeggiator->notes.getNumElements() - 1);
					int newNoteCode = arpNote->inputCharacteristics[MIDI_CHARACTERISTIC_NOTE];

					if (polyphonic == POLYPHONY_LEGATO) {
						thisVoice->changeNoteCode(modelStackWithVoice, newNoteCode, newNoteCode,
						                          arpNote->inputCharacteristics[MIDI_CHARACTERISTIC_CHANNEL],
						                          arpNote->mpeValues);
						lastNoteCode = newNoteCode;
						// I think we could just return here, too?
					}
					else { // POLYPHONY_MONO
						noteOnPostArpeggiator(
						    modelStack, newNoteCode, newNoteCode,
						    arpeggiator
						        ->lastVelocity, // Interesting - I've made it keep the velocity of presumably the note we just switched off. I must have decided that sounded best? I think I vaguely remember.
						    arpNote
						        ->mpeValues, // ... We take the MPE values from the "keypress" associated with the new note we'll sound, though.
						    0, 0, 0, arpNote->inputCharacteristics[MIDI_CHARACTERISTIC_CHANNEL]);
						return;
					}
				}
				else goto justSwitchOff;
			}

			else {
justSwitchOff:
				thisVoice->noteOff(modelStackWithVoice);
			}
		}
	}
}

bool Sound::allowNoteTails(ModelStackWithSoundFlags* modelStack, bool disregardSampleLoop) {
	// Return yes unless all active sources are play-once samples, or envelope 0 has no sustain

	// If arp on, then definitely yes
	ArpeggiatorSettings* arpSettings = getArpSettings((InstrumentClip*)modelStack->getTimelineCounterAllowNull());
	if (arpSettings && arpSettings->mode) return true;

	// If no sustain ever, we definitely can't have tails
	if (!envelopeHasSustainEver(0, (ParamManagerForTimeline*)modelStack->paramManager)) return false;

	// After that if not subtractive (so no samples) or there's some noise, we definitely can have tails
	if (synthMode != SYNTH_MODE_SUBTRACTIVE
	    || modelStack->paramManager->getPatchedParamSet()->params[PARAM_LOCAL_NOISE_VOLUME].containsSomething(
	        -2147483648))
		return true;

	// If we still don't know, just check there's at least one active oscillator that isn't a one-shot sample without a loop-end point
	bool anyActiveSources = false;
	for (int s = 0; s < NUM_SOURCES; s++) {
		bool sourceEverActive = modelStack->checkSourceEverActiveDisregardingMissingSample(s);

		anyActiveSources = sourceEverActive || anyActiveSources;

		if (sourceEverActive
		    && (sources[s].oscType != OSC_TYPE_SAMPLE || sources[s].repeatMode != SAMPLE_REPEAT_ONCE
		        || (!disregardSampleLoop && sources[s].hasAnyLoopEndPoint()))) {
			return true;
		}
	}

	return !anyActiveSources;
}

int32_t Sound::hasAnyTimeStretchSyncing(ParamManagerForTimeline* paramManager, bool getSampleLength, int note) {

	if (synthMode == SYNTH_MODE_FM) return 0;

	for (int s = 0; s < NUM_SOURCES; s++) {

		bool sourceEverActive = s ? isSourceActiveEver(1, paramManager) : isSourceActiveEver(0, paramManager);

		if (sourceEverActive && sources[s].oscType == OSC_TYPE_SAMPLE
		    && sources[s].repeatMode == SAMPLE_REPEAT_STRETCH) {
			if (getSampleLength) {
				return sources[s].getLengthInSamplesAtSystemSampleRate(note + transpose, true);
			}
			return 1;
		}
	}

	return 0;
}

// Returns sample length in samples
int32_t Sound::hasCutOrLoopModeSamples(ParamManagerForTimeline* paramManager, int note, bool* anyLooping) {

	if (synthMode == SYNTH_MODE_FM) return 0;

	if (isNoiseActiveEver(paramManager)) return 0;

	int32_t maxLength = 0;
	if (anyLooping) *anyLooping = false;

	for (int s = 0; s < NUM_SOURCES; s++) {
		bool sourceEverActive = s ? isSourceActiveEver(1, paramManager) : isSourceActiveEver(0, paramManager);
		if (!sourceEverActive) continue;

		if (sources[s].oscType != OSC_TYPE_SAMPLE) {
			return 0;
		}
		else if (sources[s].repeatMode == SAMPLE_REPEAT_CUT || sources[s].repeatMode == SAMPLE_REPEAT_LOOP) {

			if (anyLooping && sources[s].repeatMode == SAMPLE_REPEAT_LOOP) *anyLooping = true;
			int32_t length = sources[s].getLengthInSamplesAtSystemSampleRate(note);

			// TODO: need a bit here to take into account the fact that the note pitch may well have lengthened or shortened the sample

			maxLength = getMax(maxLength, length);
		}
	}

	return maxLength;
}

bool Sound::hasCutModeSamples(ParamManagerForTimeline* paramManager) {

	if (synthMode == SYNTH_MODE_FM) return false;

	if (isNoiseActiveEver(paramManager)) return false;

	for (int s = 0; s < NUM_SOURCES; s++) {
		bool sourceEverActive = s ? isSourceActiveEver(1, paramManager) : isSourceActiveEver(0, paramManager);
		if (!sourceEverActive) continue;

		if (sources[s].oscType != OSC_TYPE_SAMPLE || !sources[s].hasAtLeastOneAudioFileLoaded()
		    || sources[s].repeatMode != SAMPLE_REPEAT_CUT) {
			return false;
		}
	}

	return true;
}

bool Sound::allowsVeryLateNoteStart(InstrumentClip* clip, ParamManagerForTimeline* paramManager) {

	// If arpeggiator, we can always start very late
	ArpeggiatorSettings* arpSettings = getArpSettings(clip);
	if (arpSettings && arpSettings->mode) return true;

	if (synthMode == SYNTH_MODE_FM) return false;

	// Basically, if any wave-based oscillators active, or one-shot samples, that means no not allowed
	for (int s = 0; s < NUM_SOURCES; s++) {

		bool sourceEverActive = s ? isSourceActiveEver(1, paramManager) : isSourceActiveEver(0, paramManager);
		if (!sourceEverActive) continue;

		switch (sources[s].oscType) {

		// Sample - generally ok, but not if one-shot
		case OSC_TYPE_SAMPLE:
			if (sources[s].repeatMode == SAMPLE_REPEAT_ONCE || !sources[s].hasAtLeastOneAudioFileLoaded())
				return false; // Not quite sure why the must-be-loaded requirement - maybe something would break if it tried to do a late start otherwise?
			break;

		// Input - ok
		case OSC_TYPE_INPUT_L:
		case OSC_TYPE_INPUT_R:
		case OSC_TYPE_INPUT_STEREO:
			break;

		// Wave-based - instant fail!
		default:
			return false;
		}
	}

	return true;
}

bool Sound::isSourceActiveCurrently(int s, ParamManagerForTimeline* paramManager) {
	return (synthMode == SYNTH_MODE_RINGMOD
	        || getSmoothedPatchedParamValue(PARAM_LOCAL_OSC_A_VOLUME + s, paramManager) != -2147483648)
	       && (synthMode == SYNTH_MODE_FM || sources[s].oscType != OSC_TYPE_SAMPLE
	           || sources[s].hasAtLeastOneAudioFileLoaded());
}

bool Sound::isSourceActiveEverDisregardingMissingSample(int s, ParamManager* paramManager) {
	return (synthMode == SYNTH_MODE_RINGMOD
	        || paramManager->getPatchedParamSet()->params[PARAM_LOCAL_OSC_A_VOLUME + s].containsSomething(-2147483648)
	        || renderingOscillatorSyncEver(paramManager));
}

bool Sound::isSourceActiveEver(int s, ParamManager* paramManager) {
	return isSourceActiveEverDisregardingMissingSample(s, paramManager)
	       && (synthMode == SYNTH_MODE_FM || sources[s].oscType != OSC_TYPE_SAMPLE
	           || sources[s].hasAtLeastOneAudioFileLoaded());
}

bool Sound::isNoiseActiveEver(ParamManagerForTimeline* paramManager) {
	return (synthMode != SYNTH_MODE_FM
	        && paramManager->getPatchedParamSet()->params[PARAM_LOCAL_NOISE_VOLUME].containsSomething(-2147483648));
}

bool Sound::renderingOscillatorSyncCurrently(ParamManagerForTimeline* paramManager) {
	if (!oscillatorSync) return false;
	if (synthMode == SYNTH_MODE_FM) return false;
	return (getSmoothedPatchedParamValue(PARAM_LOCAL_OSC_B_VOLUME, paramManager) != -2147483648
	        || synthMode == SYNTH_MODE_RINGMOD);
}

bool Sound::renderingOscillatorSyncEver(ParamManager* paramManager) {
	if (!oscillatorSync) return false;
	if (synthMode == SYNTH_MODE_FM) return false;
	return (paramManager->getPatchedParamSet()->params[PARAM_LOCAL_OSC_B_VOLUME].containsSomething(-2147483648)
	        || synthMode == SYNTH_MODE_RINGMOD);
}

void Sound::sampleZoneChanged(int markerType, int s, ModelStackWithSoundFlags* modelStack) {
	if (!numVoicesAssigned) return;

	if (sources[s].sampleControls.reversed) markerType = NUM_MARKER_TYPES - 1 - markerType;

	int ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		ModelStackWithVoice* modelStackWithVoice = modelStack->addVoice(thisVoice);
		bool stillGoing = thisVoice->sampleZoneChanged(modelStackWithVoice, s, markerType);
		if (!stillGoing) {
			AudioEngine::activeVoices.checkVoiceExists(thisVoice, this, "E200");
			AudioEngine::unassignVoice(thisVoice, this, modelStack);
			v--;
			ends[1]--;
		}
	}
}

// Unlike most functions, this one accepts modelStack as NULL, because when unassigning all voices e.g. on song swap, we won't have it.
void Sound::reassessRenderSkippingStatus(ModelStackWithSoundFlags* modelStack, bool shouldJustCutModFX) {

	// TODO: should get the caller to provide this, cos they usually already have it. In fact, should put this on the
	// ModelStack, cos many deeper-nested functions called by this one need it too!
	ArpeggiatorSettings* arpSettings = getArpSettings();

	bool skippingStatusNow = (!numVoicesAssigned && !delay.repeatsUntilAbandon && !stutterer.status
	                          && (!arpSettings || !getArp()->hasAnyInputNotesActive() || !arpSettings->mode));

	if (skippingStatusNow != skippingRendering) {

		if (skippingStatusNow) {

			// We wanna start, skipping, but if MOD fx are on...
			if (modFXType) {

				// If we didn't start the wait-time yet, start it now
				if (!startSkippingRenderingAtTime) {

					// But wait, first, maybe we actually have just been instructed to cut the MODFX tail
					if (shouldJustCutModFX) {
doCutModFXTail:
						clearModFXMemory();
						goto yupStartSkipping;
					}

					int waitSamples =
					    (modFXType == MOD_FX_TYPE_CHORUS)
					        ? (20 * 44)
					        : (90
					           * 441); // 20 and 900 mS respectively. Lots is required for feeding-back flanger or phaser
					startSkippingRenderingAtTime = AudioEngine::audioSampleTimer + waitSamples;
				}

				// Or if already waiting, see if the wait is over yet
				else {
					if ((int32_t)(AudioEngine::audioSampleTimer - startSkippingRenderingAtTime) >= 0) {
						startSkippingRenderingAtTime = 0;
						goto yupStartSkipping;
					}

					// Ok, we wanted to check that before manually cutting the MODFX tail, to save time, but that's still an option...
					if (shouldJustCutModFX) goto doCutModFXTail;
				}
			}
			else {
yupStartSkipping:
				startSkippingRendering(modelStack);
			}
		}
		else stopSkippingRendering(arpSettings);
	}

	else {
		startSkippingRenderingAtTime = 0;
	}
}

void Sound::getThingWithMostReverb(Sound** soundWithMostReverb, ParamManager** paramManagerWithMostReverb,
                                   GlobalEffectableForClip** globalEffectableWithMostReverb,
                                   int32_t* highestReverbAmountFound, ParamManagerForTimeline* paramManager) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	if (!patchedParams->params[PARAM_GLOBAL_REVERB_AMOUNT].isAutomated()
	    && patchedParams->params[PARAM_GLOBAL_REVERB_AMOUNT].containsSomething(-2147483648)) {

		// We deliberately don't use the LPF'ed param here
		int32_t reverbHere = patchedParams->getValue(PARAM_GLOBAL_REVERB_AMOUNT);
		if (*highestReverbAmountFound < reverbHere) {
			*highestReverbAmountFound = reverbHere;
			*soundWithMostReverb = this;
			*paramManagerWithMostReverb = paramManager;
			*globalEffectableWithMostReverb = NULL;
		}
	}
}

// fromAutomation means whether the changes was caused by automation playing back - as opposed to the user turning the knob right now
void Sound::notifyValueChangeViaLPF(int p, bool shouldDoParamLPF, ModelStackWithThreeMainThings const* modelStack,
                                    int32_t oldValue, int32_t newValue, bool fromAutomation) {

	if (skippingRendering) goto dontDoLPF;

	if (!shouldDoParamLPF) {
		// If param LPF was active for this param, stop it
		if (paramLPF.p == p) {
			paramLPF.p = PARAM_LPF_OFF;
		}
		goto dontDoLPF;
	}

	// If doing param LPF
	if (paramNeedsLPF(p, fromAutomation)) {

		// If the param LPF was already busy...
		if (paramLPF.p != PARAM_LPF_OFF) {
			// If it was a different param, tell it to stop so that we can have it
			if (paramLPF.p != p) {
				char modelStackMemory[MODEL_STACK_MAX_SIZE];
				copyModelStack(modelStackMemory, modelStack, sizeof(ModelStackWithThreeMainThings));
				ModelStackWithThreeMainThings* modelStackCopy = (ModelStackWithThreeMainThings*)modelStackMemory;

				stopParamLPF(modelStackCopy->addSoundFlags());
			}

			// Otherwise, keep its current state, and just tell it it's going somewhere new
			else {
				goto changeWhereParamLPFGoing;
			}
		}

		paramLPF.currentValue = oldValue;

changeWhereParamLPFGoing:
		paramLPF.p = p;
	}

	// Or if not doing param LPF
	else {
dontDoLPF:
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		copyModelStack(modelStackMemory, modelStack, sizeof(ModelStackWithThreeMainThings));
		ModelStackWithThreeMainThings* modelStackCopy = (ModelStackWithThreeMainThings*)modelStackMemory;

		patchedParamPresetValueChanged(p, modelStackCopy->addSoundFlags(), oldValue, newValue);
	}
}

void Sound::doParamLPF(int numSamples, ModelStackWithSoundFlags* modelStack) {
	if (paramLPF.p == PARAM_LPF_OFF) return;

	int32_t oldValue = paramLPF.currentValue;

	int32_t diff = (modelStack->paramManager->getPatchedParamSet()->getValue(paramLPF.p) >> 8) - (oldValue >> 8);

	if (diff == 0) {
		stopParamLPF(modelStack);
	}
	else {
		int32_t amountToAdd = diff * numSamples;
		paramLPF.currentValue += amountToAdd;
		//patchedParamPresetValueChanged(paramLPF.p, notifySound, oldValue, paramLPF.currentValue);
		patchedParamPresetValueChanged(paramLPF.p, modelStack, oldValue, paramLPF.currentValue);
	}
}

// Unusually, modelStack may be supplied as NULL, because when unassigning all voices e.g. on song swap, we won't have it.
void Sound::stopParamLPF(ModelStackWithSoundFlags* modelStack) {
	bool wasActive = paramLPF.p != PARAM_LPF_OFF;
	if (wasActive) {
		int p = paramLPF.p;
		paramLPF.p =
		    PARAM_LPF_OFF; // Must do this first, because the below call will involve the Sound calling us back for the current value
		if (modelStack)
			patchedParamPresetValueChanged(p, modelStack, paramLPF.currentValue,
			                               modelStack->paramManager->getPatchedParamSet()->getValue(p));
	}
}

void Sound::render(ModelStackWithThreeMainThings* modelStack, StereoSample* outputBuffer, int numSamples,
                   int32_t* reverbBuffer, int32_t sideChainHitPending, int32_t reverbAmountAdjust,
                   bool shouldLimitDelayFeedback, int32_t pitchAdjust) {

	if (skippingRendering) return;

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;

	// Do global LFO
	if (paramManager->getPatchCableSet()->isSourcePatchedToSomething(PATCH_SOURCE_LFO_GLOBAL)) {
		int32_t old = globalSourceValues[PATCH_SOURCE_LFO_GLOBAL];
		globalSourceValues[PATCH_SOURCE_LFO_GLOBAL] =
		    globalLFO.render(numSamples, lfoGlobalWaveType, getGlobalLFOPhaseIncrement());
		unsigned int anyChange = (old != globalSourceValues[PATCH_SOURCE_LFO_GLOBAL]);
		sourcesChanged |= anyChange << PATCH_SOURCE_LFO_GLOBAL;
	}

	// Do compressor
	if (paramManager->getPatchCableSet()->isSourcePatchedToSomething(PATCH_SOURCE_COMPRESSOR)) {
		if (sideChainHitPending) {
			compressor.registerHit(sideChainHitPending);
		}

		int32_t old = globalSourceValues[PATCH_SOURCE_COMPRESSOR];
		globalSourceValues[PATCH_SOURCE_COMPRESSOR] = compressor.render(
		    numSamples, paramManager->getUnpatchedParamSet()->getValue(PARAM_UNPATCHED_COMPRESSOR_SHAPE));
		unsigned int anyChange = (old != globalSourceValues[PATCH_SOURCE_COMPRESSOR]);
		sourcesChanged |= anyChange << PATCH_SOURCE_COMPRESSOR;
	}

	// Perform the actual patching
	if (sourcesChanged) patcher.performPatching(sourcesChanged, this, paramManager);

	// Setup some reverb-related stuff
	int32_t reverbSendAmount =
	    multiply_32x32_rshift32_rounded(reverbAmountAdjust,
	                                    paramFinalValues[PARAM_GLOBAL_REVERB_AMOUNT - FIRST_GLOBAL_PARAM])
	    << 5;

	ModelStackWithSoundFlags* modelStackWithSoundFlags = modelStack->addSoundFlags();

	// Arpeggiator
	ArpeggiatorSettings* arpSettings = getArpSettings();
	if (arpSettings && arpSettings->mode) {

		UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
		uint32_t gateThreshold = (uint32_t)unpatchedParams->getValue(PARAM_UNPATCHED_SOUND_ARP_GATE) + 2147483648;
		uint32_t phaseIncrement =
		    arpSettings->getPhaseIncrement(paramFinalValues[PARAM_GLOBAL_ARP_RATE - FIRST_GLOBAL_PARAM]);

		ArpReturnInstruction instruction;

		getArp()->render(arpSettings, numSamples, gateThreshold, phaseIncrement, &instruction);

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
	}

	// Setup delay
	DelayWorkingState delayWorkingState;
	delayWorkingState.delayFeedbackAmount = paramFinalValues[PARAM_GLOBAL_DELAY_FEEDBACK - FIRST_GLOBAL_PARAM];
	if (shouldLimitDelayFeedback) {
		delayWorkingState.delayFeedbackAmount =
		    getMin(delayWorkingState.delayFeedbackAmount, (int32_t)(1 << 30) - (1 << 26));
	}
	delayWorkingState.userDelayRate = paramFinalValues[PARAM_GLOBAL_DELAY_RATE - FIRST_GLOBAL_PARAM];
	delay.setupWorkingState(&delayWorkingState, numVoicesAssigned != 0);

	// Render each voice into a local buffer here
	bool renderingInStereo = renderingVoicesInStereo(modelStackWithSoundFlags);
	static int32_t soundBuffer[SSI_TX_BUFFER_NUM_SAMPLES * 2];
	memset(soundBuffer, 0, (numSamples * sizeof(int32_t)) << renderingInStereo);

	if (numVoicesAssigned) {

		// Very often, we'll just apply panning here at the Sound level rather than the Voice level
		bool applyingPanAtVoiceLevel =
		    (AudioEngine::renderInStereo
		     && paramManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(PARAM_LOCAL_PAN));

		// Setup filters
		bool thisHasFilters = hasFilters();
		FilterSetConfig filterSetConfig;
		filterSetConfig.doLPF =
		    (thisHasFilters
		     && (lpfMode == LPF_MODE_TRANSISTOR_24DB_DRIVE
		         || paramManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(PARAM_LOCAL_LPF_FREQ)
		         || getSmoothedPatchedParamValue(PARAM_LOCAL_LPF_FREQ, paramManager) < 2147483602));
		filterSetConfig.doHPF =
		    (thisHasFilters
		     && (paramManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(PARAM_LOCAL_HPF_FREQ)
		         || getSmoothedPatchedParamValue(PARAM_LOCAL_HPF_FREQ, paramManager) != -2147483648));

		// Each voice will potentially alter the "sources changed" flags, so store a backup to restore between each voice
		/*
		bool backedUpSourcesChanged[FIRST_UNCHANGEABLE_SOURCE - FIRST_LOCAL_SOURCE];
		bool doneFirstVoice = false;
		*/

		int ends[2];
		AudioEngine::activeVoices.getRangeForSound(this, ends);
		for (int v = ends[0]; v < ends[1]; v++) {
			Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
			/*
			if (!doneFirstVoice) {
				if (numVoicesAssigned > 1) {
					memcpy(backedUpSourcesChanged, &sourcesChanged[FIRST_LOCAL_SOURCE], FIRST_UNCHANGEABLE_SOURCE - FIRST_LOCAL_SOURCE);
					doneFirstVoice = true;
				}
			}
			else memcpy(&sourcesChanged[FIRST_LOCAL_SOURCE], backedUpSourcesChanged, FIRST_UNCHANGEABLE_SOURCE - FIRST_LOCAL_SOURCE);
			*/

			ModelStackWithVoice* modelStackWithVoice = modelStackWithSoundFlags->addVoice(thisVoice);

			bool stillGoing = thisVoice->render(modelStackWithVoice, soundBuffer, numSamples, renderingInStereo,
			                                    applyingPanAtVoiceLevel, sourcesChanged, &filterSetConfig, pitchAdjust);
			if (!stillGoing) {
				AudioEngine::activeVoices.checkVoiceExists(thisVoice, this, "E201");
				AudioEngine::unassignVoice(thisVoice, this, modelStackWithSoundFlags);
				v--;
				ends[1]--;
			}
		}

		// If just rendered in mono, double that up to stereo now
		if (!renderingInStereo) {
			// We know that nothing's patched to pan, so can read it in this very basic way.
			int32_t pan = paramManager->getPatchedParamSet()->getValue(PARAM_LOCAL_PAN) >> 1;

			int32_t amplitudeL, amplitudeR;
			bool doPanning;
			doPanning = (AudioEngine::renderInStereo && shouldDoPanning(pan, &amplitudeL, &amplitudeR));
			if (doPanning) {
				for (int i = numSamples - 1; i >= 0; i--) {
					int32_t sampleValue = soundBuffer[i];
					soundBuffer[(i << 1)] = multiply_32x32_rshift32(sampleValue, amplitudeL) << 2;
					soundBuffer[(i << 1) + 1] = multiply_32x32_rshift32(sampleValue, amplitudeR) << 2;
				}
			}
			else {
				for (int i = numSamples - 1; i >= 0; i--) {
					int32_t sampleValue = soundBuffer[i];
					soundBuffer[(i << 1)] = sampleValue;
					soundBuffer[(i << 1) + 1] = sampleValue;
				}
			}
		}

		// Or if rendered in stereo...
		else {
			// And if we're only applying pan here at the Sound level...
			if (!applyingPanAtVoiceLevel) {

				// We know that nothing's patched to pan, so can read it in this very basic way.
				int32_t pan = paramManager->getPatchedParamSet()->getValue(PARAM_LOCAL_PAN) >> 1;

				int32_t amplitudeL, amplitudeR;
				bool doPanning;
				doPanning = (AudioEngine::renderInStereo && shouldDoPanning(pan, &amplitudeL, &amplitudeR));
				if (doPanning) {
					int32_t* thisSample = soundBuffer;
					int32_t* endSamples = thisSample + (numSamples << 1);
					do {
						*thisSample = multiply_32x32_rshift32(*thisSample, amplitudeL) << 2;
						thisSample++;

						*thisSample = multiply_32x32_rshift32(*thisSample, amplitudeR) << 2;
						thisSample++;
					} while (thisSample != endSamples);
				}
			}
		}
	}
	else {
		if (!delayWorkingState.doDelay) reassessRenderSkippingStatus(modelStackWithSoundFlags);

		if (!renderingInStereo) {
			memset(&soundBuffer[numSamples], 0, numSamples * sizeof(int32_t));
		}
	}

	int32_t postFXVolume = paramFinalValues[PARAM_GLOBAL_VOLUME_POST_FX - FIRST_GLOBAL_PARAM];
	int32_t postReverbVolume = paramFinalValues[PARAM_GLOBAL_VOLUME_POST_REVERB_SEND - FIRST_GLOBAL_PARAM];

	if (postReverbVolumeLastTime == -1) postReverbVolumeLastTime = postReverbVolume;

	int32_t modFXDepth = paramFinalValues[PARAM_GLOBAL_MOD_FX_DEPTH - FIRST_GLOBAL_PARAM];
	int32_t modFXRate = paramFinalValues[PARAM_GLOBAL_MOD_FX_RATE - FIRST_GLOBAL_PARAM];

	processSRRAndBitcrushing((StereoSample*)soundBuffer, numSamples, &postFXVolume, paramManager);
	processFX((StereoSample*)soundBuffer, numSamples, modFXType, modFXRate, modFXDepth, &delayWorkingState,
	          &postFXVolume, paramManager, 8);
	processStutter((StereoSample*)soundBuffer, numSamples, paramManager);

	int32_t postReverbSendVolumeIncrement = (int32_t)(postReverbVolume - postReverbVolumeLastTime) / numSamples;

	processReverbSendAndVolume((StereoSample*)soundBuffer, numSamples, reverbBuffer, postFXVolume,
	                           postReverbVolumeLastTime, reverbSendAmount, 0, true, postReverbSendVolumeIncrement);
	addAudio((StereoSample*)soundBuffer, outputBuffer, numSamples);

	postReverbVolumeLastTime = postReverbVolume;

	sourcesChanged = 0;
	whichExpressionSourcesChangedAtSynthLevel = 0;

	// Unlike all the other possible reasons we might want to start skipping rendering, delay.repeatsUntilAbandon may have changed state just now.
	if (!delay.repeatsUntilAbandon || startSkippingRenderingAtTime)
		reassessRenderSkippingStatus(modelStackWithSoundFlags);

	doParamLPF(numSamples, modelStackWithSoundFlags);
}

// This is virtual, and gets extended by drums!
void Sound::setSkippingRendering(bool newSkipping) {
	skippingRendering = newSkipping;
}

// Unusually, modelStack may be supplied as NULL, because when unassigning all voices e.g. on song swap, we won't have it.
void Sound::startSkippingRendering(ModelStackWithSoundFlags* modelStack) {
	timeStartedSkippingRenderingModFX = AudioEngine::audioSampleTimer;
	timeStartedSkippingRenderingLFO = AudioEngine::audioSampleTimer;
	timeStartedSkippingRenderingArp = AudioEngine::audioSampleTimer;
	// compressor.status = ENVELOPE_STAGE_OFF; // Was this doing anything? Have removed, to make all of this completely reversible without doing anything

	setSkippingRendering(true);

	stopParamLPF(modelStack);
}

void Sound::stopSkippingRendering(ArpeggiatorSettings* arpSettings) {
	if (skippingRendering) {

		int32_t modFXTimeOff =
		    AudioEngine::audioSampleTimer
		    - timeStartedSkippingRenderingModFX; // This variable is a good indicator of whether it actually was skipping at all

		// If rendering was actually stopped for any length of time...
		if (modFXTimeOff) {

			// Do LFO
			globalLFO.tick(AudioEngine::audioSampleTimer - timeStartedSkippingRenderingLFO,
			               getGlobalLFOPhaseIncrement());

			// Do Mod FX
			modFXLFO.tick(modFXTimeOff, paramFinalValues[PARAM_GLOBAL_MOD_FX_RATE - FIRST_GLOBAL_PARAM]);

			// Do arp
			getArpBackInTimeAfterSkippingRendering(arpSettings);

			// Do sidechain compressor
			//if (paramManager->getPatchCableSet()->isSourcePatchedToSomething(PATCH_SOURCE_COMPRESSOR)) {
			compressor.registerHitRetrospectively(AudioEngine::sizeLastSideChainHit,
			                                      AudioEngine::audioSampleTimer - AudioEngine::timeLastSideChainHit);
			//}

			postReverbVolumeLastTime =
			    -1; // Special state to make it grab the actual value the first time it's rendered

			//clearModFXMemory(); // No need anymore, now we wait for this to basically empty before starting skipping
		}

		setSkippingRendering(false);
	}
}

void Sound::getArpBackInTimeAfterSkippingRendering(ArpeggiatorSettings* arpSettings) {

	if (skippingRendering) {
		if (arpSettings && arpSettings->mode) {
			uint32_t phaseIncrement =
			    arpSettings->getPhaseIncrement(paramFinalValues[PARAM_GLOBAL_ARP_RATE - FIRST_GLOBAL_PARAM]);
			getArp()->gatePos +=
			    (phaseIncrement >> 8) * (AudioEngine::audioSampleTimer - timeStartedSkippingRenderingArp);

			timeStartedSkippingRenderingArp = AudioEngine::audioSampleTimer;
		}
	}
}

void Sound::unassignAllVoices() {
	if (!numVoicesAssigned) return;

	int ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		AudioEngine::activeVoices.checkVoiceExists(
		    thisVoice, this,
		    "E203"); // ronronsen got error! https://forums.synthstrom.com/discussion/4090/e203-by-changing-a-drum-kit#latest
		AudioEngine::unassignVoice(thisVoice, this, NULL,
		                           false); // Don't remove from Vector - we'll do that below, in bulk
	}

	int numToDelete = ends[1] - ends[0];
	if (numToDelete) AudioEngine::activeVoices.deleteAtIndex(ends[0], numToDelete);

	if (ALPHA_OR_BETA_VERSION) {
		if (numVoicesAssigned > 0)
			numericDriver.freezeWithError(
			    "E070"); // ronronsen got error! https://forums.synthstrom.com/discussion/4090/e203-by-changing-a-drum-kit#latest
		else if (numVoicesAssigned < 0)
			numericDriver.freezeWithError(
			    "E071"); // ronronsen got error! https://forums.synthstrom.com/discussion/4090/e203-by-changing-a-drum-kit#latest
	}

	// reassessRenderSkippingStatus(); // Nope, this will get called in voiceUnassigned(), which gets called for each voice we unassign above.
}

void Sound::confirmNumVoices(char const* error) {

	/*
	int voiceCount = 0;
	int reasonCount = 0;
	Voice* endAssignedVoices = audioDriver.endAssignedVoices;
	for (Voice* thisVoice = audioDriver.voices; thisVoice != endAssignedVoices; thisVoice++) {
		if (thisVoice->assignedToSound == this) {
			voiceCount++;

			for (int u = 0; u < maxNumUnison; u++) {
				for (int s = 0; s < NUM_SOURCES; s++) {
					for (int l = 0; l < NUM_SAMPLE_CLUSTERS_LOADED_AHEAD; l++) {
						if (thisVoice->unisonParts[u].sources[s].clusters[l]) {
							reasonCount++;
						}
					}
				}
			}

		}
	}

	if (numVoicesAssigned != voiceCount) {
		Uart::print("voice count tallied ");
		Uart::print(numVoicesAssigned);
		Uart::print(", but actually ");
		Uart::println(voiceCount);
		numericDriver.freezeWithError(error);
	}

	int reasonCountSources = 0;

	for (int l = 0; l < NUM_SAMPLE_CLUSTERS_LOADED_AHEAD; l++) {
		if (sources[0].clusters[l]) reasonCountSources++;
	}



	if (sources[0].sample) {
		int totalNumReasons = sources[0].sample->getTotalNumReasons(reasonCount + reasonCountSources);
		if (totalNumReasons != reasonCount + reasonCountSources) {

			Uart::println(sources[0].sample->fileName);
			Uart::print("voices: ");
			Uart::println(voiceCount);
			Uart::print("reasons on clusters: ");
			Uart::println(totalNumReasons);
			Uart::print("Num voice unison part pointers to those clusters: ");
			Uart::println(reasonCount);
			Uart::print("Num source pointers to those clusters: ");
			Uart::println(reasonCountSources);

			char buffer[5];
			strcpy(buffer, error);
			buffer[0] = 'F';
			numericDriver.freezeWithError(buffer);
		}
	}
	*/
}

uint32_t Sound::getGlobalLFOPhaseIncrement() {
	uint32_t phaseIncrement;
	if (lfoGlobalSyncLevel == SYNC_LEVEL_NONE)
		phaseIncrement = paramFinalValues[PARAM_GLOBAL_LFO_FREQ - FIRST_GLOBAL_PARAM];
	else {
		phaseIncrement = (playbackHandler.getTimePerInternalTickInverse()) >> (SYNC_LEVEL_256TH - lfoGlobalSyncLevel);
		switch (lfoGlobalSyncType) {
		case SYNC_TYPE_EVEN:
			// Nothing to do
			break;
		case SYNC_TYPE_TRIPLET:
			phaseIncrement = phaseIncrement * 3 / 2;
			break;
		case SYNC_TYPE_DOTTED:
			phaseIncrement = phaseIncrement * 2 / 3;
			break;
		}
	}
	//Uart::print("LFO phaseIncrement: ");
	//Uart::println(phaseIncrement);
	return phaseIncrement;
}

void Sound::setLFOGlobalSyncType(SyncType newType) {
	lfoGlobalSyncType = newType;
	if (playbackHandler.isEitherClockActive()) resyncGlobalLFO();
}

void Sound::setLFOGlobalSyncLevel(SyncLevel newLevel) {
	lfoGlobalSyncLevel = newLevel;
	if (playbackHandler.isEitherClockActive()) resyncGlobalLFO();
}

void Sound::setLFOGlobalWave(uint8_t newWave) {
	lfoGlobalWaveType = newWave;
	if (playbackHandler.isEitherClockActive()) resyncGlobalLFO();
}

// Only call this if playbackHandler.isEitherClockActive(), please
void Sound::resyncGlobalLFO() {
	if (lfoGlobalSyncLevel != 0) {

		timeStartedSkippingRenderingLFO = AudioEngine::
		    audioSampleTimer; // Resets the thing where the number of samples skipped is later converted into LFO phase increment

		if (lfoGlobalWaveType == OSC_TYPE_SINE || lfoGlobalWaveType == OSC_TYPE_TRIANGLE)
			globalLFO.phase = getLFOInitialPhaseForZero(lfoGlobalWaveType);
		else globalLFO.phase = getLFOInitialPhaseForNegativeExtreme(lfoGlobalWaveType);

		uint32_t timeSinceLastTick;

		int64_t lastInternalTickDone = playbackHandler.getCurrentInternalTickCount(&timeSinceLastTick);

		// If we're right at the first tick, no need to do anything else!
		if (!lastInternalTickDone && !timeSinceLastTick) return;

		uint32_t numInternalTicksPerPeriod = 3 << (SYNC_LEVEL_256TH - lfoGlobalSyncLevel);
		switch (lfoGlobalSyncType) {
		case SYNC_TYPE_EVEN:
			// Nothing to do
			break;
		case SYNC_TYPE_TRIPLET:
			numInternalTicksPerPeriod = numInternalTicksPerPeriod * 2 / 3;
			break;
		case SYNC_TYPE_DOTTED:
			numInternalTicksPerPeriod = numInternalTicksPerPeriod * 3 / 2;
			break;
		}
		uint32_t offsetTicks = (uint64_t)lastInternalTickDone % (uint16_t)numInternalTicksPerPeriod;

		// If we're right at a bar (or something), no need to do anyting else
		if (!timeSinceLastTick && !offsetTicks) return;

		uint32_t timePerInternalTick = playbackHandler.getTimePerInternalTick();
		uint32_t timePerPeriod = numInternalTicksPerPeriod * timePerInternalTick;
		uint32_t offsetTime = offsetTicks * timePerInternalTick + timeSinceLastTick;
		globalLFO.phase += (uint32_t)((float)offsetTime / timePerPeriod * 4294967296);
	}
}

// ------------------------------------
// ModControllable implementation
// ------------------------------------

// whichKnob is either which physical mod knob, or which MIDI CC code.
// For mod knobs, supply midiChannel as 255
// Returns false if fail due to insufficient RAM.
bool Sound::learnKnob(MIDIDevice* fromDevice, ParamDescriptor paramDescriptor, uint8_t whichKnob, uint8_t modKnobMode,
                      uint8_t midiChannel, Song* song) {

	// If a mod knob
	if (midiChannel >= 16) {

		// If that knob was patched to something else...
		bool overwroteExistingKnob = (modKnobs[modKnobMode][whichKnob].paramDescriptor != paramDescriptor);

		modKnobs[modKnobMode][whichKnob].paramDescriptor = paramDescriptor;

		if (overwroteExistingKnob) ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(song);

		return true;
	}

	// If a MIDI knob
	else {
		return ModControllableAudio::learnKnob(fromDevice, paramDescriptor, whichKnob, modKnobMode, midiChannel, song);
	}
}

// Song may be NULL
void Sound::ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(Song* song) {

	// We gotta do this for any backedUpParamManagers too!
	int i = song->backedUpParamManagers.search((uint32_t)(ModControllableAudio*)this,
	                                           GREATER_OR_EQUAL); // Search by first word only.

	while (true) {
		if (i >= song->backedUpParamManagers.getNumElements()) break;
		BackedUpParamManager* backedUp = (BackedUpParamManager*)song->backedUpParamManagers.getElementAddress(i);
		if (backedUp->modControllable != this) break;

		if (backedUp->clip) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    setupModelStackWithThreeMainThingsButNoNoteRow(modelStackMemory, song, this, backedUp->clip,
			                                                   &backedUp->paramManager);
			ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(modelStackWithThreeMainThings);
		}
		else {
			ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(&backedUp->paramManager);
		}
		i++;
	}

	song->ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(this); // What does this do exactly, again?
}

const uint8_t patchedParamsWhichShouldBeZeroIfNoKnobAssigned[] = {
    PARAM_LOCAL_PITCH_ADJUST, PARAM_LOCAL_OSC_A_PITCH_ADJUST, PARAM_LOCAL_OSC_B_PITCH_ADJUST,
    PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST, PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST};

void Sound::ensureInaccessibleParamPresetValuesWithoutKnobsAreZeroWithMinimalDetails(ParamManager* paramManager) {

	for (int i = 0; i < sizeof(patchedParamsWhichShouldBeZeroIfNoKnobAssigned); i++) {
		int p = patchedParamsWhichShouldBeZeroIfNoKnobAssigned[i];
		ensureParamPresetValueWithoutKnobIsZeroWithMinimalDetails(paramManager, p);
	}
}

// Song may be NULL
void Sound::ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(ModelStackWithThreeMainThings* modelStack) {

	ModelStackWithParamCollection* modelStackWithParamCollection =
	    modelStack->paramManager->getPatchCableSet(modelStack);

	for (int i = 0; i < sizeof(patchedParamsWhichShouldBeZeroIfNoKnobAssigned); i++) {
		ModelStackWithParamId* modelStackWithParamId =
		    modelStackWithParamCollection->addParamId(patchedParamsWhichShouldBeZeroIfNoKnobAssigned[i]);
		ModelStackWithAutoParam* modelStackWithAutoParam = modelStackWithParamId->paramCollection->getAutoParamFromId(
		    modelStackWithParamId, false); // Don't allow creation
		if (modelStackWithAutoParam->autoParam) {
			ensureParamPresetValueWithoutKnobIsZero(modelStackWithAutoParam);
		}
	}
}

// Only worked for patched params
void Sound::ensureParamPresetValueWithoutKnobIsZero(ModelStackWithAutoParam* modelStack) {

	// If the param is automated, we'd better not try setting it to 0 - the user probably wants the automation
	if (modelStack->autoParam->isAutomated()) return;

	for (int k = 0; k < NUM_MOD_BUTTONS; k++) {
		for (int w = 0; w < NUM_PHYSICAL_MOD_KNOBS; w++) {
			if (modKnobs[k][w].paramDescriptor.isSetToParamWithNoSource(modelStack->paramId)) return;
		}
	}

	for (int k = 0; k < midiKnobArray.getNumElements(); k++) {
		MIDIKnob* knob = midiKnobArray.getElement(k);
		if (knob->paramDescriptor.isSetToParamWithNoSource(modelStack->paramId)) return;
	}

	// If we're here, no knobs were assigned to this param, so make it 0
	modelStack->autoParam->setCurrentValueWithNoReversionOrRecording(modelStack, 0);
}

void Sound::ensureParamPresetValueWithoutKnobIsZeroWithMinimalDetails(ParamManager* paramManager, int p) {

	AutoParam* param = &paramManager->getPatchedParamSet()->params[p];

	// If the param is automated, we'd better not try setting it to 0 - the user probably wants the automation
	if (param->isAutomated()) return;

	for (int k = 0; k < NUM_MOD_BUTTONS; k++) {
		for (int w = 0; w < NUM_PHYSICAL_MOD_KNOBS; w++) {
			if (modKnobs[k][w].paramDescriptor.isSetToParamWithNoSource(p)) return;
		}
	}

	for (int k = 0; k < midiKnobArray.getNumElements(); k++) {
		MIDIKnob* knob = midiKnobArray.getElement(k);
		if (knob->paramDescriptor.isSetToParamWithNoSource(p)) return;
	}

	// If we're here, no knobs were assigned to this param, so make it 0
	param->setCurrentValueBasicForSetup(0);
}

void Sound::doneReadingFromFile() {
	calculateEffectiveVolume();

	for (int s = 0; s < NUM_SOURCES; s++) {
		sources[s].doneReadingFromFile(this);
	}

	setupUnisonDetuners(NULL);

	for (int m = 0; m < numModulators; m++) {
		recalculateModulatorTransposer(m, NULL);
	}
}

bool Sound::hasAnyVoices() {
	return (numVoicesAssigned != 0);
}

// Unusually, modelStack may be supplied as NULL, because when unassigning all voices e.g. on song swap, we won't have it.
void Sound::voiceUnassigned(ModelStackWithVoice* modelStack) {

	numVoicesAssigned--;
	reassessRenderSkippingStatus(modelStack);
}

// modelStack may be NULL if no voices currently active
void Sound::setupUnisonDetuners(ModelStackWithSoundFlags* modelStack) {
	if (numUnison != 1) {
		int32_t detuneScaled = (int32_t)unisonDetune * 42949672;
		int32_t lowestVoice = -(detuneScaled >> 1);
		int32_t voiceSpacing = detuneScaled / (numUnison - 1);

		for (int u = 0; u < numUnison; u++) {

			// Middle unison part gets no detune
			if ((numUnison & 1) && u == ((numUnison - 1) >> 1)) {
				unisonDetuners[u].setNoDetune();
			}
			else {
				unisonDetuners[u].setup(lowestVoice + voiceSpacing * u);
			}
		}
	}
	recalculateAllVoicePhaseIncrements(modelStack); // Can handle NULL
}

void Sound::calculateEffectiveVolume() {
	//volumeNeutralValueForUnison = (float)getParamNeutralValue(PARAM_LOCAL_VOLUME) / sqrt(numUnison);
	volumeNeutralValueForUnison = (float)134217728 / sqrtf(numUnison);
}

// May change mod knob functions. You must update mod knob levels after calling this
void Sound::setSynthMode(uint8_t value, Song* song) {

	unassignAllVoices(); // This saves a lot of potential problems, to do with samples playing. E002 was being caused

	uint8_t oldSynthMode = synthMode;
	synthMode = value;
	setupPatchingForAllParamManagers(song);

	// Change mod knob functions over. Switching *to* FM...
	if (synthMode == SYNTH_MODE_FM && oldSynthMode != SYNTH_MODE_FM) {
		for (int f = 0; f < NUM_MOD_BUTTONS; f++) {
			if (modKnobs[f][0].paramDescriptor.isJustAParam() && modKnobs[f][1].paramDescriptor.isJustAParam()) {
				int p0 = modKnobs[f][0].paramDescriptor.getJustTheParam();
				int p1 = modKnobs[f][1].paramDescriptor.getJustTheParam();

				if ((p0 == PARAM_LOCAL_LPF_RESONANCE || p0 == PARAM_LOCAL_HPF_RESONANCE
				     || p0 == PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_BASS)
				    && (p1 == PARAM_LOCAL_LPF_FREQ || p1 == PARAM_LOCAL_HPF_FREQ
				        || p1 == PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_TREBLE)) {
					modKnobs[f][0].paramDescriptor.setToHaveParamOnly(PARAM_LOCAL_MODULATOR_1_VOLUME);
					modKnobs[f][1].paramDescriptor.setToHaveParamOnly(PARAM_LOCAL_MODULATOR_0_VOLUME);
				}
			}
		}
	}

	// ... and switching *from* FM...
	if (synthMode != SYNTH_MODE_FM && oldSynthMode == SYNTH_MODE_FM) {
		for (int f = 0; f < NUM_MOD_BUTTONS; f++) {
			if (modKnobs[f][0].paramDescriptor.isSetToParamWithNoSource(PARAM_LOCAL_MODULATOR_1_VOLUME)
			    && modKnobs[f][1].paramDescriptor.isSetToParamWithNoSource(PARAM_LOCAL_MODULATOR_0_VOLUME)) {
				modKnobs[f][0].paramDescriptor.setToHaveParamOnly(PARAM_LOCAL_LPF_RESONANCE);
				modKnobs[f][1].paramDescriptor.setToHaveParamOnly(PARAM_LOCAL_LPF_FREQ);
			}
		}
	}
}

void Sound::setModulatorTranspose(int m, int value, ModelStackWithSoundFlags* modelStack) {
	modulatorTranspose[m] = value;
	recalculateAllVoicePhaseIncrements(modelStack);
}

void Sound::setModulatorCents(int m, int value, ModelStackWithSoundFlags* modelStack) {
	modulatorCents[m] = value;
	recalculateModulatorTransposer(m, modelStack);
}

// Can handle NULL modelStack, which you'd only want to do if no Voices active
void Sound::recalculateModulatorTransposer(uint8_t m, ModelStackWithSoundFlags* modelStack) {
	modulatorTransposers[m].setup((int32_t)modulatorCents[m] * 42949672);
	recalculateAllVoicePhaseIncrements(modelStack); // Can handle NULL
}

// Can handle NULL modelStack, which you'd only want to do if no Voices active
void Sound::recalculateAllVoicePhaseIncrements(ModelStackWithSoundFlags* modelStack) {

	if (!numVoicesAssigned || !modelStack) return; // These two "should" always be false in tandem...

	int ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		ModelStackWithVoice* modelStackWithVoice = modelStack->addVoice(thisVoice);
		thisVoice->calculatePhaseIncrements(modelStackWithVoice);
	}
}

void Sound::setNumUnison(int newNum, ModelStackWithSoundFlags* modelStack) {
	int oldNum = numUnison;

	numUnison = newNum;
	setupUnisonDetuners(modelStack); // Can handle NULL. Also calls recalculateAllVoicePhaseIncrements()
	calculateEffectiveVolume();

	// Effective volume has changed. Need to pass that change onto Voices
	if (numVoicesAssigned) {

		int ends[2];
		AudioEngine::activeVoices.getRangeForSound(this, ends);
		for (int v = ends[0]; v < ends[1]; v++) {
			Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);

			if (synthMode == SYNTH_MODE_SUBTRACTIVE) {

				for (int s = 0; s < NUM_SOURCES; s++) {

					bool sourceEverActive = modelStack->checkSourceEverActive(s);

					if (sourceEverActive && synthMode != SYNTH_MODE_FM && sources[s].oscType == OSC_TYPE_SAMPLE
					    && thisVoice->guides[s].audioFileHolder && thisVoice->guides[s].audioFileHolder->audioFile) {

						// For samples, set the current play pos for the new unison part, if num unison went up
						if (newNum > oldNum) {

							VoiceUnisonPartSource* newPart = &thisVoice->unisonParts[oldNum].sources[s];
							VoiceUnisonPartSource* oldPart = &thisVoice->unisonParts[oldNum - 1].sources[s];

							newPart->active = oldPart->active;

							if (newPart->active) {
								newPart->oscPos = oldPart->oscPos;
								newPart->phaseIncrementStoredValue = oldPart->phaseIncrementStoredValue;
								newPart->carrierFeedback = oldPart->carrierFeedback;

								newPart->voiceSample = AudioEngine::solicitVoiceSample();
								if (!newPart->voiceSample) {
									newPart->active = false;
								}

								else {
									VoiceSample* newVoiceSample = newPart->voiceSample;
									VoiceSample* oldVoiceSample = oldPart->voiceSample;

									newVoiceSample->cloneFrom(
									    oldVoiceSample); // Just clones the SampleLowLevelReader stuff
									newVoiceSample->pendingSamplesLate = oldVoiceSample->pendingSamplesLate;

									newVoiceSample->doneFirstRenderYet = true;

									// Don't do any caching for new part. Old parts will stop using their cache anyway because their pitch will have changed
									newVoiceSample->stopUsingCache(
									    &thisVoice->guides[s], (Sample*)thisVoice->guides[s].audioFileHolder->audioFile,
									    thisVoice->getPriorityRating(),
									    thisVoice->guides[s].getLoopingType(&sources[s]) == LOOP_LOW_LEVEL);
									// TODO: should really check success of that...
								}
							}
						}
						else if (newNum < oldNum) {
							for (int l = 0; l < NUM_CLUSTERS_LOADED_AHEAD; l++) {
								thisVoice->unisonParts[newNum].sources[s].unassign();
							}
						}
					}
				}
			}
		}
	}
}

void Sound::setUnisonDetune(int newAmount, ModelStackWithSoundFlags* modelStack) {
	unisonDetune = newAmount;
	setupUnisonDetuners(modelStack); // Can handle NULL
}

bool Sound::anyNoteIsOn() {

	ArpeggiatorSettings* arpSettings = getArpSettings();

	if (arpSettings && arpSettings->mode != ARP_MODE_OFF) {
		return (getArp()->hasAnyInputNotesActive());
	}

	return numVoicesAssigned;
}

bool Sound::hasFilters() {
	return (getSynthMode() != SYNTH_MODE_FM);
}

void Sound::readParamsFromFile(ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos) {
	char const* tagName;

	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (readParamTagFromFile(tagName, paramManager, readAutomationUpToPos)) {}
		else storageManager.exitTag(tagName);
	}
}

// paramManager only required for old old song files, or for presets (because you'd be wanting to extract the defaultParams into it)
// arpSettings optional - no need if you're loading a new V2.0+ song where Instruments are all separate from Clips and won't store any arp stuff
int Sound::readFromFile(ModelStackWithModControllable* modelStack, int32_t readAutomationUpToPos,
                        ArpeggiatorSettings* arpSettings) {

	modulatorTranspose[1] = 0;
	memset(oscRetriggerPhase, 0, sizeof(oscRetriggerPhase));
	memset(modulatorRetriggerPhase, 0, sizeof(modulatorRetriggerPhase));

	char const* tagName;

	ParamManagerForTimeline paramManager;

	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		int result = readTagFromFile(tagName, &paramManager, readAutomationUpToPos, arpSettings, modelStack->song);
		if (result == NO_ERROR) {}
		else if (result != RESULT_TAG_UNUSED) return result;
		else storageManager.exitTag(tagName);
	}

	// If we actually got a paramManager, we can do resonance compensation on it
	if (paramManager.containsAnyMainParamCollections()) {
		if (storageManager.firmwareVersionOfFileBeingRead < FIRMWARE_1P2P0) {
			compensateVolumeForResonance(modelStack->addParamManager(&paramManager));
		}

		possiblySetupDefaultExpressionPatching(&paramManager);

		// And, we file it with the Song
		modelStack->song->backUpParamManager(this, (Clip*)modelStack->getTimelineCounterAllowNull(), &paramManager,
		                                     true);
	}

	doneReadingFromFile();

	// Ensure all MIDI knobs reference correct volume...
	for (int k = 0; k < midiKnobArray.getNumElements(); k++) {
		MIDIKnob* knob = midiKnobArray.getElement(k);
		ensureKnobReferencesCorrectVolume(knob);
	}

	return NO_ERROR;
}

int Sound::createParamManagerForLoading(ParamManagerForTimeline* paramManager) {

	int error = paramManager->setupWithPatching();
	if (error) return error;

	initParams(paramManager);

	paramManager->getUnpatchedParamSet()->params[PARAM_UNPATCHED_COMPRESSOR_SHAPE].setCurrentValueBasicForSetup(
	    2147483647); // Hmm, why this here? Obviously I had some reason...
	return NO_ERROR;
}

void Sound::compensateVolumeForResonance(ModelStackWithThreeMainThings* modelStack) {

	// If it was an old-firmware file, we need to compensate for resonance
	if (storageManager.firmwareVersionOfFileBeingRead < FIRMWARE_1P2P0 && synthMode != SYNTH_MODE_FM) {
		if (modelStack->paramManager->resonanceBackwardsCompatibilityProcessed) return;

		modelStack->paramManager->resonanceBackwardsCompatibilityProcessed = true;

		PatchedParamSet* patchedParams = modelStack->paramManager->getPatchedParamSet();

		int32_t compensation = interpolateTableSigned(patchedParams->getValue(PARAM_LOCAL_LPF_RESONANCE) + 2147483648,
		                                              32, oldResonanceCompensation, 3);
		float compensationDB = (float)compensation / (1024 << 16);

		if (compensationDB > 0.1) {
			//Uart::print("compensating dB: ");
			//Uart::println((int)(compensationDB * 100));
			patchedParams->shiftParamVolumeByDB(PARAM_GLOBAL_VOLUME_POST_FX, compensationDB);
		}

		ModelStackWithParamCollection* modelStackWithParamCollection =
		    modelStack->paramManager->getPatchCableSet(modelStack);

		PatchCableSet* patchCableSet = (PatchCableSet*)modelStackWithParamCollection->paramCollection;

		patchCableSet->setupPatching(
		    modelStackWithParamCollection); // So that we may then call doesParamHaveSomethingPatchedToIt(), below

		// If no LPF on, and resonance is at 50%, set it to 0%
		if (!patchCableSet->doesParamHaveSomethingPatchedToIt(PARAM_LOCAL_LPF_FREQ)
		    && !patchedParams->params[PARAM_LOCAL_LPF_FREQ].isAutomated()
		    && patchedParams->params[PARAM_LOCAL_LPF_FREQ].getCurrentValue() >= 2147483602
		    && !patchedParams->params[PARAM_LOCAL_LPF_RESONANCE].isAutomated()
		    && patchedParams->params[PARAM_LOCAL_LPF_RESONANCE].getCurrentValue() <= 0
		    && patchedParams->params[PARAM_LOCAL_LPF_RESONANCE].getCurrentValue() >= -23) {
			patchedParams->params[PARAM_LOCAL_LPF_RESONANCE].currentValue = -2147483648;
		}
	}
}

// paramManager only required for old old song files
int Sound::readSourceFromFile(int s, ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos) {

	Source* source = &sources[s];

	char const* tagName;
	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "type")) {
			source->setOscType(stringToOscType(storageManager.readTagOrAttributeValue()));
			storageManager.exitTag("type");
		}
		else if (!strcmp(tagName, "phaseWidth")) {
			ENSURE_PARAM_MANAGER_EXISTS
			patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_A_PHASE_WIDTH + s, readAutomationUpToPos);
			storageManager.exitTag("phaseWidth");
		}
		else if (!strcmp(tagName, "volume")) {
			ENSURE_PARAM_MANAGER_EXISTS
			patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_A_VOLUME + s, readAutomationUpToPos);
			storageManager.exitTag("volume");
		}
		else if (!strcmp(tagName, "transpose")) {
			source->transpose = storageManager.readTagOrAttributeValueInt();
			storageManager.exitTag("transpose");
		}
		else if (!strcmp(tagName, "cents")) {
			source->cents = storageManager.readTagOrAttributeValueInt();
			storageManager.exitTag("cents");
		}
		else if (!strcmp(tagName, "loopMode")) {
			source->repeatMode = storageManager.readTagOrAttributeValueInt();
			source->repeatMode = getMin(source->repeatMode, (uint8_t)(NUM_REPEAT_MODES - 1));
			storageManager.exitTag("loopMode");
		}
		else if (!strcmp(tagName, "oscillatorSync")) {
			int value = storageManager.readTagOrAttributeValueInt();
			oscillatorSync = (value != 0);
			storageManager.exitTag("oscillatorSync");
		}
		else if (!strcmp(tagName, "reversed")) {
			source->sampleControls.reversed = storageManager.readTagOrAttributeValueInt();
			storageManager.exitTag("reversed");
		}
		/*
		else if (!strcmp(tagName, "sampleSync")) {
			source->sampleSync = stringToBool(storageManager.readTagContents());
			storageManager.exitTag("sampleSync");
		}
		*/
		else if (!strcmp(tagName, "timeStretchEnable")) {
			source->sampleControls.pitchAndSpeedAreIndependent = storageManager.readTagOrAttributeValueInt();
			storageManager.exitTag("timeStretchEnable");
		}
		else if (!strcmp(tagName, "timeStretchAmount")) {
			source->timeStretchAmount = storageManager.readTagOrAttributeValueInt();
			storageManager.exitTag("timeStretchAmount");
		}
		else if (!strcmp(tagName, "linearInterpolation")) {
			if (storageManager.readTagOrAttributeValueInt())
				source->sampleControls.interpolationMode = INTERPOLATION_MODE_LINEAR;
			storageManager.exitTag("linearInterpolation");
		}
		else if (!strcmp(tagName, "retrigPhase")) {
			oscRetriggerPhase[s] = storageManager.readTagOrAttributeValueInt();
			storageManager.exitTag("retrigPhase");
		}
		else if (!strcmp(tagName, "fileName")) {

			MultiRange* range = source->getOrCreateFirstRange();
			if (!range) return ERROR_INSUFFICIENT_RAM;

			storageManager.readTagOrAttributeValueString(&range->getAudioFileHolder()->filePath);

			storageManager.exitTag("fileName");
		}
		else if (!strcmp(tagName, "zone")) {

			MultisampleRange* range = (MultisampleRange*)source->getOrCreateFirstRange();
			if (!range) return ERROR_INSUFFICIENT_RAM;

			range->sampleHolder.startMSec = 0;
			range->sampleHolder.endMSec = 0;
			range->sampleHolder.startPos = 0;
			range->sampleHolder.endPos = 0;

			while (*(tagName = storageManager.readNextTagOrAttributeName())) {
				if (!strcmp(tagName, "startSeconds")) {
					range->sampleHolder.startMSec += storageManager.readTagOrAttributeValueInt() * 1000;
					storageManager.exitTag("startSeconds");
				}
				else if (!strcmp(tagName, "startMilliseconds")) {
					range->sampleHolder.startMSec += storageManager.readTagOrAttributeValueInt();
					storageManager.exitTag("startMilliseconds");
				}
				else if (!strcmp(tagName, "endSeconds")) {
					range->sampleHolder.endMSec += storageManager.readTagOrAttributeValueInt() * 1000;
					storageManager.exitTag("endSeconds");
				}
				else if (!strcmp(tagName, "endMilliseconds")) {
					range->sampleHolder.endMSec += storageManager.readTagOrAttributeValueInt();
					storageManager.exitTag("endMilliseconds");
				}

				else if (!strcmp(tagName, "startSamplePos")) {
					range->sampleHolder.startPos = storageManager.readTagOrAttributeValueInt();
					storageManager.exitTag("startSamplePos");
				}
				else if (!strcmp(tagName, "endSamplePos")) {
					range->sampleHolder.endPos = storageManager.readTagOrAttributeValueInt();
					storageManager.exitTag("endSamplePos");
				}

				else if (!strcmp(tagName, "startLoopPos")) {
					range->sampleHolder.loopStartPos = storageManager.readTagOrAttributeValueInt();
					storageManager.exitTag("startLoopPos");
				}
				else if (!strcmp(tagName, "endLoopPos")) {
					range->sampleHolder.loopEndPos = storageManager.readTagOrAttributeValueInt();
					storageManager.exitTag("endLoopPos");
				}

				else storageManager.exitTag(tagName);
			}
			storageManager.exitTag("zone");
		}
		else if (!strcmp(tagName, "sampleRanges") || !strcmp(tagName, "wavetableRanges")) {

			while (*(tagName = storageManager.readNextTagOrAttributeName())) {

				if (!strcmp(tagName, "sampleRange") || !strcmp(tagName, "wavetableRange")) {

					char tempMemory[source->ranges.elementSize];

					MultiRange* tempRange;
					if (source->oscType == OSC_TYPE_WAVETABLE) {
						tempRange = new (tempMemory) MultiWaveTableRange();
					}
					else {
						tempRange = new (tempMemory) MultisampleRange();
					}

					AudioFileHolder* holder = tempRange->getAudioFileHolder();

					while (*(tagName = storageManager.readNextTagOrAttributeName())) {

						if (!strcmp(tagName, "fileName")) {
							storageManager.readTagOrAttributeValueString(&holder->filePath);
							storageManager.exitTag("fileName");
						}
						else if (!strcmp(tagName, "rangeTopNote")) {
							tempRange->topNote = storageManager.readTagOrAttributeValueInt();
							storageManager.exitTag("rangeTopNote");
						}
						else if (source->oscType != OSC_TYPE_WAVETABLE) {
							if (!strcmp(tagName, "zone")) {

								while (*(tagName = storageManager.readNextTagOrAttributeName())) {
									if (!strcmp(tagName, "startSamplePos")) {
										((SampleHolder*)holder)->startPos = storageManager.readTagOrAttributeValueInt();
										storageManager.exitTag("startSamplePos");
									}
									else if (!strcmp(tagName, "endSamplePos")) {
										((SampleHolder*)holder)->endPos = storageManager.readTagOrAttributeValueInt();
										storageManager.exitTag("endSamplePos");
									}

									else if (!strcmp(tagName, "startLoopPos")) {
										((SampleHolderForVoice*)holder)->loopStartPos =
										    storageManager.readTagOrAttributeValueInt();
										storageManager.exitTag("startLoopPos");
									}
									else if (!strcmp(tagName, "endLoopPos")) {
										((SampleHolderForVoice*)holder)->loopEndPos =
										    storageManager.readTagOrAttributeValueInt();
										storageManager.exitTag("endLoopPos");
									}
									else storageManager.exitTag(tagName);
								}
								storageManager.exitTag("zone");
							}
							else if (!strcmp(tagName, "transpose")) {
								((SampleHolderForVoice*)holder)->transpose =
								    storageManager.readTagOrAttributeValueInt();
								storageManager.exitTag("transpose");
							}
							else if (!strcmp(tagName, "cents")) {
								((SampleHolderForVoice*)holder)->cents = storageManager.readTagOrAttributeValueInt();
								storageManager.exitTag("cents");
							}
							else goto justExitTag;
						}
						else {
justExitTag:
							storageManager.exitTag(tagName);
						}
					}

					int i = source->ranges.search(tempRange->topNote, GREATER_OR_EQUAL);
					int error;

					// Ensure no duplicate topNote.
					if (i < source->ranges.getNumElements()) {
						MultisampleRange* existingRange = (MultisampleRange*)source->ranges.getElementAddress(i);
						if (existingRange->topNote == tempRange->topNote) {
							error = ERROR_FILE_CORRUPTED;
							goto gotError;
						}
					}

					error = source->ranges.insertAtIndex(i);
					if (error) {
gotError:
						tempRange->~MultiRange();
						return error;
					}

					void* destinationRange = (MultisampleRange*)source->ranges.getElementAddress(i);
					memcpy(destinationRange, tempRange, source->ranges.elementSize);

					storageManager.exitTag();
				}
				else storageManager.exitTag();
			}

			storageManager.exitTag();
		}
		else {
			storageManager.exitTag();
		}
	}

	return NO_ERROR;
}

void Sound::writeSourceToFile(int s, char const* tagName) {

	Source* source = &sources[s];

	storageManager.writeOpeningTagBeginning(tagName);

	if (synthMode != SYNTH_MODE_FM) {
		storageManager.writeAttribute("type", oscTypeToString(source->oscType));
	}

	// If (multi)sample...
	if (source->oscType == OSC_TYPE_SAMPLE
	    && synthMode != SYNTH_MODE_FM) { // Don't combine this with the above "if" - there's an "else" below
		storageManager.writeAttribute("loopMode", source->repeatMode);
		storageManager.writeAttribute("reversed", source->sampleControls.reversed);
		storageManager.writeAttribute("timeStretchEnable", source->sampleControls.pitchAndSpeedAreIndependent);
		storageManager.writeAttribute("timeStretchAmount", source->timeStretchAmount);
		if (source->sampleControls.interpolationMode == INTERPOLATION_MODE_LINEAR)
			storageManager.writeAttribute("linearInterpolation", 1);

		int numRanges = source->ranges.getNumElements();

		if (numRanges > 1) {
			storageManager.writeOpeningTagEnd();
			storageManager.writeOpeningTag("sampleRanges");
		}

		for (int e = 0; e < numRanges; e++) {
			MultisampleRange* range = (MultisampleRange*)source->ranges.getElement(e);

			if (numRanges > 1) {
				storageManager.writeOpeningTagBeginning("sampleRange");

				if (e != numRanges - 1) {
					storageManager.writeAttribute("rangeTopNote", range->topNote);
				}
			}

			storageManager.writeAttribute("fileName", range->sampleHolder.audioFile
			                                              ? range->sampleHolder.audioFile->filePath.get()
			                                              : range->sampleHolder.filePath.get());
			if (range->sampleHolder.transpose)
				storageManager.writeAttribute("transpose", range->sampleHolder.transpose);
			if (range->sampleHolder.cents) storageManager.writeAttribute("cents", range->sampleHolder.cents);

			storageManager.writeOpeningTagEnd();

			storageManager.writeOpeningTagBeginning("zone");
			storageManager.writeAttribute("startSamplePos", range->sampleHolder.startPos);
			storageManager.writeAttribute("endSamplePos", range->sampleHolder.endPos);
			if (range->sampleHolder.loopStartPos)
				storageManager.writeAttribute("startLoopPos", range->sampleHolder.loopStartPos);
			if (range->sampleHolder.loopEndPos)
				storageManager.writeAttribute("endLoopPos", range->sampleHolder.loopEndPos);
			storageManager.closeTag();

			if (numRanges > 1) storageManager.writeClosingTag("sampleRange");
		}

		if (numRanges > 1) {
			storageManager.writeClosingTag("sampleRanges");
		}
		else if (numRanges == 0) {
			storageManager.writeOpeningTagEnd();
		}

		storageManager.writeClosingTag(tagName);
	}

	// Otherwise, if we're *not* a (multi)sample, here's the other option, which includes (multi)wavetable
	else {
		storageManager.writeAttribute("transpose", source->transpose);
		storageManager.writeAttribute("cents", source->cents);
		if (s == 1 && oscillatorSync) {
			storageManager.writeAttribute("oscillatorSync", oscillatorSync);
		}
		storageManager.writeAttribute("retrigPhase", oscRetriggerPhase[s]);

		// Sub-option for (multi)wavetable
		if (source->oscType == OSC_TYPE_WAVETABLE && synthMode != SYNTH_MODE_FM) {

			int numRanges = source->ranges.getNumElements();

			if (numRanges > 1) {
				storageManager.writeOpeningTagEnd();
				storageManager.writeOpeningTag("wavetableRanges");
			}

			for (int e = 0; e < numRanges; e++) {
				MultisampleRange* range = (MultisampleRange*)source->ranges.getElement(e);

				if (numRanges > 1) {
					storageManager.writeOpeningTagBeginning("wavetableRange");

					if (e != numRanges - 1) {
						storageManager.writeAttribute("rangeTopNote", range->topNote);
					}
				}

				storageManager.writeAttribute("fileName", range->sampleHolder.audioFile
				                                              ? range->sampleHolder.audioFile->filePath.get()
				                                              : range->sampleHolder.filePath.get());

				if (numRanges > 1) {
					storageManager.closeTag();
				}
			}

			if (numRanges > 1) {
				storageManager.writeClosingTag("wavetableRanges");
				storageManager.writeClosingTag(tagName);
			}
			else {
				goto justCloseTag;
			}
		}

		else {
justCloseTag:
			storageManager.closeTag();
		}
	}
}

bool Sound::readParamTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager,
                                 int32_t readAutomationUpToPos) {

	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;
	ParamCollectionSummary* patchedParamsSummary = paramManager->getPatchedParamSetSummary();
	PatchedParamSet* patchedParams = (PatchedParamSet*)patchedParamsSummary->paramCollection;

	if (!strcmp(tagName, "arpeggiatorGate")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_SOUND_ARP_GATE, readAutomationUpToPos);
		storageManager.exitTag("arpeggiatorGate");
	}
	else if (!strcmp(tagName, "portamento")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_SOUND_PORTA, readAutomationUpToPos);
		storageManager.exitTag("portamento");
	}
	else if (!strcmp(tagName, "compressorShape")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_COMPRESSOR_SHAPE, readAutomationUpToPos);
		storageManager.exitTag("compressorShape");
	}

	else if (!strcmp(tagName, "noiseVolume")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_NOISE_VOLUME, readAutomationUpToPos);
		storageManager.exitTag("noiseVolume");
	}
	else if (!strcmp(tagName, "oscAVolume")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_A_VOLUME, readAutomationUpToPos);
		storageManager.exitTag("oscAVolume");
	}
	else if (!strcmp(tagName, "oscBVolume")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_B_VOLUME, readAutomationUpToPos);
		storageManager.exitTag("oscBVolume");
	}
	else if (!strcmp(tagName, "oscAPulseWidth")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_A_PHASE_WIDTH, readAutomationUpToPos);
		storageManager.exitTag("oscAPulseWidth");
	}
	else if (!strcmp(tagName, "oscBPulseWidth")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_B_PHASE_WIDTH, readAutomationUpToPos);
		storageManager.exitTag("oscBPulseWidth");
	}
	else if (!strcmp(tagName, "oscAWavetablePosition")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_A_WAVE_INDEX, readAutomationUpToPos);
		storageManager.exitTag();
	}
	else if (!strcmp(tagName, "oscBWavetablePosition")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_B_WAVE_INDEX, readAutomationUpToPos);
		storageManager.exitTag();
	}
	else if (!strcmp(tagName, "volume")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_VOLUME_POST_FX, readAutomationUpToPos);
		storageManager.exitTag("volume");
	}
	else if (!strcmp(tagName, "pan")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_PAN, readAutomationUpToPos);
		storageManager.exitTag("pan");
	}
	else if (!strcmp(tagName, "lpfFrequency")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_LPF_FREQ, readAutomationUpToPos);
		storageManager.exitTag("lpfFrequency");
	}
	else if (!strcmp(tagName, "lpfResonance")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_LPF_RESONANCE, readAutomationUpToPos);
		storageManager.exitTag("lpfResonance");
	}
	else if (!strcmp(tagName, "hpfFrequency")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_HPF_FREQ, readAutomationUpToPos);
		storageManager.exitTag("hpfFrequency");
	}
	else if (!strcmp(tagName, "hpfResonance")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_HPF_RESONANCE, readAutomationUpToPos);
		storageManager.exitTag("hpfResonance");
	}
	else if (!strcmp(tagName, "envelope1")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "attack")) {
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_0_ATTACK, readAutomationUpToPos);
				storageManager.exitTag("attack");
			}
			else if (!strcmp(tagName, "decay")) {
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_0_DECAY, readAutomationUpToPos);
				storageManager.exitTag("decay");
			}
			else if (!strcmp(tagName, "sustain")) {
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_0_SUSTAIN, readAutomationUpToPos);
				storageManager.exitTag("sustain");
			}
			else if (!strcmp(tagName, "release")) {
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_0_RELEASE, readAutomationUpToPos);
				storageManager.exitTag("release");
			}
		}
		storageManager.exitTag("envelope1");
	}
	else if (!strcmp(tagName, "envelope2")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "attack")) {
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_1_ATTACK, readAutomationUpToPos);
				storageManager.exitTag("attack");
			}
			else if (!strcmp(tagName, "decay")) {
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_1_DECAY, readAutomationUpToPos);
				storageManager.exitTag("decay");
			}
			else if (!strcmp(tagName, "sustain")) {
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_1_SUSTAIN, readAutomationUpToPos);
				storageManager.exitTag("sustain");
			}
			else if (!strcmp(tagName, "release")) {
				patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_ENV_1_RELEASE, readAutomationUpToPos);
				storageManager.exitTag("release");
			}
		}
		storageManager.exitTag("envelope2");
	}
	else if (!strcmp(tagName, "lfo1Rate")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_LFO_FREQ, readAutomationUpToPos);
		storageManager.exitTag("lfo1Rate");
	}
	else if (!strcmp(tagName, "lfo2Rate")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_LFO_LOCAL_FREQ, readAutomationUpToPos);
		storageManager.exitTag("lfo2Rate");
	}
	else if (!strcmp(tagName, "modulator1Amount")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_MODULATOR_0_VOLUME, readAutomationUpToPos);
		storageManager.exitTag("modulator1Amount");
	}
	else if (!strcmp(tagName, "modulator2Amount")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_MODULATOR_1_VOLUME, readAutomationUpToPos);
		storageManager.exitTag("modulator2Amount");
	}
	else if (!strcmp(tagName, "modulator1Feedback")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_MODULATOR_0_FEEDBACK, readAutomationUpToPos);
		storageManager.exitTag("modulator1Feedback");
	}
	else if (!strcmp(tagName, "modulator2Feedback")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_MODULATOR_1_FEEDBACK, readAutomationUpToPos);
		storageManager.exitTag("modulator2Feedback");
	}
	else if (!strcmp(tagName, "carrier1Feedback")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_CARRIER_0_FEEDBACK, readAutomationUpToPos);
		storageManager.exitTag("carrier1Feedback");
	}
	else if (!strcmp(tagName, "carrier2Feedback")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_CARRIER_1_FEEDBACK, readAutomationUpToPos);
		storageManager.exitTag("carrier2Feedback");
	}
	else if (!strcmp(tagName, "pitchAdjust")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_PITCH_ADJUST, readAutomationUpToPos);
		storageManager.exitTag("pitchAdjust");
	}
	else if (!strcmp(tagName, "oscAPitchAdjust")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_A_PITCH_ADJUST, readAutomationUpToPos);
		storageManager.exitTag("oscAPitchAdjust");
	}
	else if (!strcmp(tagName, "oscBPitchAdjust")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_OSC_B_PITCH_ADJUST, readAutomationUpToPos);
		storageManager.exitTag("oscBPitchAdjust");
	}
	else if (!strcmp(tagName, "mod1PitchAdjust")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST, readAutomationUpToPos);
		storageManager.exitTag("mod1PitchAdjust");
	}
	else if (!strcmp(tagName, "mod2PitchAdjust")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST, readAutomationUpToPos);
		storageManager.exitTag("mod2PitchAdjust");
	}
	else if (!strcmp(tagName, "modFXRate")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_MOD_FX_RATE, readAutomationUpToPos);
		storageManager.exitTag("modFXRate");
	}
	else if (!strcmp(tagName, "modFXDepth")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_MOD_FX_DEPTH, readAutomationUpToPos);
		storageManager.exitTag("modFXDepth");
	}
	else if (!strcmp(tagName, "delayRate")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_DELAY_RATE, readAutomationUpToPos);
		storageManager.exitTag("delayRate");
	}
	else if (!strcmp(tagName, "delayFeedback")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_DELAY_FEEDBACK, readAutomationUpToPos);
		storageManager.exitTag("delayFeedback");
	}
	else if (!strcmp(tagName, "reverbAmount")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_REVERB_AMOUNT, readAutomationUpToPos);
		storageManager.exitTag("reverbAmount");
	}
	else if (!strcmp(tagName, "arpeggiatorRate")) {
		patchedParams->readParam(patchedParamsSummary, PARAM_GLOBAL_ARP_RATE, readAutomationUpToPos);
		storageManager.exitTag("arpeggiatorRate");
	}
	else if (!strcmp(tagName, "patchCables")) {
		paramManager->getPatchCableSet()->readPatchCablesFromFile(readAutomationUpToPos);
		storageManager.exitTag("patchCables");
	}
	else if (ModControllableAudio::readParamTagFromFile(tagName, paramManager, readAutomationUpToPos)) {}

	else return false;

	return true;
}

void Sound::writeParamsToFile(ParamManager* paramManager, bool writeAutomation) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->writeParamAsAttribute("arpeggiatorGate", PARAM_UNPATCHED_SOUND_ARP_GATE, writeAutomation);
	unpatchedParams->writeParamAsAttribute("portamento", PARAM_UNPATCHED_SOUND_PORTA, writeAutomation);
	unpatchedParams->writeParamAsAttribute("compressorShape", PARAM_UNPATCHED_COMPRESSOR_SHAPE, writeAutomation);

	patchedParams->writeParamAsAttribute("oscAVolume", PARAM_LOCAL_OSC_A_VOLUME, writeAutomation);
	patchedParams->writeParamAsAttribute("oscAPulseWidth", PARAM_LOCAL_OSC_A_PHASE_WIDTH, writeAutomation);
	patchedParams->writeParamAsAttribute("oscAWavetablePosition", PARAM_LOCAL_OSC_A_WAVE_INDEX, writeAutomation);
	patchedParams->writeParamAsAttribute("oscBVolume", PARAM_LOCAL_OSC_B_VOLUME, writeAutomation);
	patchedParams->writeParamAsAttribute("oscBPulseWidth", PARAM_LOCAL_OSC_B_PHASE_WIDTH, writeAutomation);
	patchedParams->writeParamAsAttribute("oscBWavetablePosition", PARAM_LOCAL_OSC_B_WAVE_INDEX, writeAutomation);
	patchedParams->writeParamAsAttribute("noiseVolume", PARAM_LOCAL_NOISE_VOLUME, writeAutomation);

	patchedParams->writeParamAsAttribute("volume", PARAM_GLOBAL_VOLUME_POST_FX, writeAutomation);
	patchedParams->writeParamAsAttribute("pan", PARAM_LOCAL_PAN, writeAutomation);

	// Filters
	patchedParams->writeParamAsAttribute("lpfFrequency", PARAM_LOCAL_LPF_FREQ, writeAutomation);
	patchedParams->writeParamAsAttribute("lpfResonance", PARAM_LOCAL_LPF_RESONANCE, writeAutomation);

	patchedParams->writeParamAsAttribute("hpfFrequency", PARAM_LOCAL_HPF_FREQ, writeAutomation);
	patchedParams->writeParamAsAttribute("hpfResonance", PARAM_LOCAL_HPF_RESONANCE, writeAutomation);

	patchedParams->writeParamAsAttribute("lfo1Rate", PARAM_GLOBAL_LFO_FREQ, writeAutomation);
	patchedParams->writeParamAsAttribute("lfo2Rate", PARAM_LOCAL_LFO_LOCAL_FREQ, writeAutomation);

	patchedParams->writeParamAsAttribute("modulator1Amount", PARAM_LOCAL_MODULATOR_0_VOLUME, writeAutomation);
	patchedParams->writeParamAsAttribute("modulator1Feedback", PARAM_LOCAL_MODULATOR_0_FEEDBACK, writeAutomation);
	patchedParams->writeParamAsAttribute("modulator2Amount", PARAM_LOCAL_MODULATOR_1_VOLUME, writeAutomation);
	patchedParams->writeParamAsAttribute("modulator2Feedback", PARAM_LOCAL_MODULATOR_1_FEEDBACK, writeAutomation);

	patchedParams->writeParamAsAttribute("carrier1Feedback", PARAM_LOCAL_CARRIER_0_FEEDBACK, writeAutomation);
	patchedParams->writeParamAsAttribute("carrier2Feedback", PARAM_LOCAL_CARRIER_1_FEEDBACK, writeAutomation);

	patchedParams->writeParamAsAttribute("pitchAdjust", PARAM_LOCAL_PITCH_ADJUST, writeAutomation, true);
	patchedParams->writeParamAsAttribute("oscAPitchAdjust", PARAM_LOCAL_OSC_A_PITCH_ADJUST, writeAutomation, true);
	patchedParams->writeParamAsAttribute("oscBPitchAdjust", PARAM_LOCAL_OSC_B_PITCH_ADJUST, writeAutomation, true);
	patchedParams->writeParamAsAttribute("mod1PitchAdjust", PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST, writeAutomation,
	                                     true);
	patchedParams->writeParamAsAttribute("mod2PitchAdjust", PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST, writeAutomation,
	                                     true);

	patchedParams->writeParamAsAttribute("modFXRate", PARAM_GLOBAL_MOD_FX_RATE, writeAutomation);
	patchedParams->writeParamAsAttribute("modFXDepth", PARAM_GLOBAL_MOD_FX_DEPTH, writeAutomation);

	patchedParams->writeParamAsAttribute("delayRate", PARAM_GLOBAL_DELAY_RATE, writeAutomation);
	patchedParams->writeParamAsAttribute("delayFeedback", PARAM_GLOBAL_DELAY_FEEDBACK, writeAutomation);

	patchedParams->writeParamAsAttribute("reverbAmount", PARAM_GLOBAL_REVERB_AMOUNT, writeAutomation);

	patchedParams->writeParamAsAttribute("arpeggiatorRate", PARAM_GLOBAL_ARP_RATE, writeAutomation);
	ModControllableAudio::writeParamAttributesToFile(paramManager, writeAutomation);

	storageManager.writeOpeningTagEnd();

	// Envelopes
	storageManager.writeOpeningTagBeginning("envelope1");
	patchedParams->writeParamAsAttribute("attack", PARAM_LOCAL_ENV_0_ATTACK, writeAutomation);
	patchedParams->writeParamAsAttribute("decay", PARAM_LOCAL_ENV_0_DECAY, writeAutomation);
	patchedParams->writeParamAsAttribute("sustain", PARAM_LOCAL_ENV_0_SUSTAIN, writeAutomation);
	patchedParams->writeParamAsAttribute("release", PARAM_LOCAL_ENV_0_RELEASE, writeAutomation);
	storageManager.closeTag();

	storageManager.writeOpeningTagBeginning("envelope2");
	patchedParams->writeParamAsAttribute("attack", PARAM_LOCAL_ENV_1_ATTACK, writeAutomation);
	patchedParams->writeParamAsAttribute("decay", PARAM_LOCAL_ENV_1_DECAY, writeAutomation);
	patchedParams->writeParamAsAttribute("sustain", PARAM_LOCAL_ENV_1_SUSTAIN, writeAutomation);
	patchedParams->writeParamAsAttribute("release", PARAM_LOCAL_ENV_1_RELEASE, writeAutomation);
	storageManager.closeTag();

	paramManager->getPatchCableSet()->writePatchCablesToFile(writeAutomation);

	ModControllableAudio::writeParamTagsToFile(paramManager, writeAutomation);
}

void Sound::writeToFile(bool savingSong, ParamManager* paramManager, ArpeggiatorSettings* arpSettings) {

	storageManager.writeAttribute("polyphonic", polyphonyModeToString(polyphonic));
	storageManager.writeAttribute("voicePriority", voicePriority);

	// Send level
	if (sideChainSendLevel != 0) storageManager.writeAttribute("sideChainSend", sideChainSendLevel);

	storageManager.writeAttribute("mode", (char*)synthModeToString(synthMode));

	if (transpose != 0) storageManager.writeAttribute("transpose", transpose);

	ModControllableAudio::writeAttributesToFile();

	storageManager.writeOpeningTagEnd(); // -------------------------------------------------------------------------

	writeSourceToFile(0, "osc1");
	writeSourceToFile(1, "osc2");

	// LFOs
	storageManager.writeOpeningTagBeginning("lfo1");
	storageManager.writeAttribute("type", lfoTypeToString(lfoGlobalWaveType), false);
	storageManager.writeSyncTypeToFile(currentSong, "syncType", lfoGlobalSyncType, false);
	storageManager.writeAbsoluteSyncLevelToFile(currentSong, "syncLevel", lfoGlobalSyncLevel, false);
	storageManager.closeTag();

	storageManager.writeOpeningTagBeginning("lfo2");
	storageManager.writeAttribute("type", lfoTypeToString(lfoLocalWaveType), false);
	storageManager.closeTag();

	if (synthMode == SYNTH_MODE_FM) {

		storageManager.writeOpeningTagBeginning("modulator1");
		storageManager.writeAttribute("transpose", modulatorTranspose[0]);
		storageManager.writeAttribute("cents", modulatorCents[0]);
		storageManager.writeAttribute("retrigPhase", modulatorRetriggerPhase[0]);
		storageManager.closeTag();

		storageManager.writeOpeningTagBeginning("modulator2");
		storageManager.writeAttribute("transpose", modulatorTranspose[1]);
		storageManager.writeAttribute("cents", modulatorCents[1]);
		storageManager.writeAttribute("retrigPhase", modulatorRetriggerPhase[1]);
		storageManager.writeAttribute("toModulator1", modulator1ToModulator0);
		storageManager.closeTag();
	}

	storageManager.writeOpeningTagBeginning("unison");
	storageManager.writeAttribute("num", numUnison, false);
	storageManager.writeAttribute("detune", unisonDetune, false);
	storageManager.closeTag();

	ModControllableAudio::writeTagsToFile();

	if (paramManager) {
		storageManager.writeOpeningTagBeginning("defaultParams");
		Sound::writeParamsToFile(paramManager, false);
		storageManager.writeClosingTag("defaultParams");
	}

	if (arpSettings) {
		storageManager.writeOpeningTagBeginning("arpeggiator");
		storageManager.writeAttribute("mode", arpModeToString(arpSettings->mode));
		storageManager.writeAttribute("numOctaves", arpSettings->numOctaves);
		storageManager.writeSyncTypeToFile(currentSong, "syncType", arpSettings->syncType);
		storageManager.writeAbsoluteSyncLevelToFile(currentSong, "syncLevel", arpSettings->syncLevel);
		storageManager.closeTag();
	}

	// Mod knobs
	storageManager.writeOpeningTag("modKnobs");
	for (int k = 0; k < NUM_MOD_BUTTONS; k++) {
		for (int w = 0; w < NUM_PHYSICAL_MOD_KNOBS; w++) {
			ModKnob* knob = &modKnobs[k][w];
			storageManager.writeOpeningTagBeginning("modKnob");
			storageManager.writeAttribute("controlsParam", paramToString(knob->paramDescriptor.getJustTheParam()),
			                              false);
			if (!knob->paramDescriptor.isJustAParam()) {
				storageManager.writeAttribute("patchAmountFromSource",
				                              sourceToString(knob->paramDescriptor.getTopLevelSource()), false);

				if (knob->paramDescriptor.hasSecondSource()) {
					storageManager.writeAttribute("patchAmountFromSecondSource",
					                              sourceToString(knob->paramDescriptor.getSecondSourceFromTop()));
				}
			}
			storageManager.closeTag();
		}
	}
	storageManager.writeClosingTag("modKnobs");
}

int16_t Sound::getMaxOscTranspose(InstrumentClip* clip) {

	int maxRawOscTranspose = -32768;
	for (int s = 0; s < NUM_SOURCES; s++) {
		if (getSynthMode() == SYNTH_MODE_FM || sources[s].oscType != OSC_TYPE_SAMPLE) {
			maxRawOscTranspose = getMax(maxRawOscTranspose, sources[s].transpose);
		}
	}

	if (getSynthMode() == SYNTH_MODE_FM) {
		maxRawOscTranspose = getMax(maxRawOscTranspose, (int)modulatorTranspose[0]);
		maxRawOscTranspose = getMax(maxRawOscTranspose, (int)modulatorTranspose[1]);
	}

	if (maxRawOscTranspose == -32768) maxRawOscTranspose = 0;

	ArpeggiatorSettings* arpSettings = getArpSettings(clip);

	if (arpSettings && arpSettings->mode) {
		maxRawOscTranspose += (arpSettings->numOctaves - 1) * 12;
	}

	return maxRawOscTranspose + transpose;
}

int16_t Sound::getMinOscTranspose() {

	int minRawOscTranspose = 32767;
	for (int s = 0; s < NUM_SOURCES; s++) {
		if (getSynthMode() == SYNTH_MODE_FM || sources[s].oscType != OSC_TYPE_SAMPLE) {
			minRawOscTranspose = getMin(minRawOscTranspose, sources[s].transpose);
		}
	}

	if (getSynthMode() == SYNTH_MODE_FM) {
		minRawOscTranspose = getMin(minRawOscTranspose, (int)modulatorTranspose[0]);
		minRawOscTranspose = getMin(minRawOscTranspose, (int)modulatorTranspose[1]);
	}

	if (minRawOscTranspose == 32767) minRawOscTranspose = 0;

	return minRawOscTranspose + transpose;
}

// Returns true if more loading needed later
int Sound::loadAllAudioFiles(bool mayActuallyReadFiles) {

	for (int s = 0; s < NUM_SOURCES; s++) {
		if (sources[s].oscType == OSC_TYPE_SAMPLE || sources[s].oscType == OSC_TYPE_WAVETABLE) {
			int error = sources[s].loadAllSamples(mayActuallyReadFiles);
			if (error) return error;
		}
	}

	return NO_ERROR;
}

bool Sound::envelopeHasSustainCurrently(int e, ParamManagerForTimeline* paramManager) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	// These params are fetched "pre-LPF"
	return (patchedParams->getValue(PARAM_LOCAL_ENV_0_SUSTAIN + e) != -2147483648
	        || patchedParams->getValue(PARAM_LOCAL_ENV_0_DECAY + e)
	               > patchedParams->getValue(PARAM_LOCAL_ENV_0_RELEASE + e));
}

bool Sound::envelopeHasSustainEver(int e, ParamManagerForTimeline* paramManager) {

	PatchedParamSet* patchedParams = paramManager->getPatchedParamSet();

	return (patchedParams->params[PARAM_LOCAL_ENV_0_SUSTAIN + e].containsSomething(-2147483648)
	        || patchedParams->params[PARAM_LOCAL_ENV_0_DECAY + e].isAutomated()
	        || patchedParams->params[PARAM_LOCAL_ENV_0_RELEASE + e].isAutomated()
	        || patchedParams->getValue(PARAM_LOCAL_ENV_0_DECAY + e)
	               > patchedParams->getValue(PARAM_LOCAL_ENV_0_RELEASE + e));
}

void Sound::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) {
	endStutter(paramManager);
}

ModelStackWithAutoParam* Sound::getParamFromModEncoder(int whichModEncoder, ModelStackWithThreeMainThings* modelStack,
                                                       bool allowCreation) {

	// If setting up a macro by holding its encoder down, the knobs will represent macro control-amounts rather than actual "params", so there's no "param".
	if (isUIModeActive(UI_MODE_MACRO_SETTING_UP)) {
		return modelStack->addParam(NULL, NULL, 0, NULL); // "none"
	}
	return getParamFromModEncoderDeeper(whichModEncoder, modelStack, allowCreation);
}

ModelStackWithAutoParam* Sound::getParamFromModEncoderDeeper(int whichModEncoder,
                                                             ModelStackWithThreeMainThings* modelStack,
                                                             bool allowCreation) {

	int paramId;
	ParamCollectionSummary* summary;

	ParamManagerForTimeline* paramManager = (ParamManagerForTimeline*)modelStack->paramManager;

	int modKnobMode = *getModKnobMode();
	ModKnob* knob = &modKnobs[modKnobMode][whichModEncoder];

	if (knob->paramDescriptor.isJustAParam()) {
		int p = knob->paramDescriptor.getJustTheParam();

		// Unpatched param
		if (p >= PARAM_UNPATCHED_SECTION) {
			paramId = p - PARAM_UNPATCHED_SECTION;
			summary = paramManager->getUnpatchedParamSetSummary();
		}

		// Patched param
		else {
			paramId = p;
			summary = paramManager->getPatchedParamSetSummary();
		}
	}

	// Patch cable
	else {
		paramId = knob->paramDescriptor.data;
		summary = paramManager->getPatchCableSetSummary();
	}

	ModelStackWithParamId* newModelStack1 =
	    modelStack->addParamCollectionAndId(summary->paramCollection, summary, paramId);
	return newModelStack1->paramCollection->getAutoParamFromId(newModelStack1, allowCreation);
}

bool Sound::modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack) {

	int modKnobMode = *getModKnobMode();

	ModKnob* ourModKnob = &modKnobs[modKnobMode][whichModEncoder];

	if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_STUTTER_RATE)) {
		if (on) {
			beginStutter((ParamManagerForTimeline*)modelStack->paramManager);
		}
		else {
			endStutter((ParamManagerForTimeline*)modelStack->paramManager);
		}
		reassessRenderSkippingStatus(modelStack->addSoundFlags());

		return false;
	}

	// Switch delay pingpong
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(PARAM_GLOBAL_DELAY_RATE)) {
		if (on) {
			switchDelayPingPong();
			return true;
		}
		else return false;
	}

	// Switch delay analog sim
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(PARAM_GLOBAL_DELAY_FEEDBACK)) {
		if (on) {
			switchDelayAnalog();
			return true;
		}
		else return false;
	}

	// Switch LPF mode
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(PARAM_LOCAL_LPF_RESONANCE)) {
		if (on) {
			switchLPFMode();
			return true;
		}
		else return false;
	}

	// Cycle through reverb presets
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(PARAM_GLOBAL_REVERB_AMOUNT)) {
		if (on) view.cycleThroughReverbPresets();
		return false;
	}

	// Switch sidechain sync level
	else if (ourModKnob->paramDescriptor.hasJustOneSource()
	         && ourModKnob->paramDescriptor.getTopLevelSource() == PATCH_SOURCE_COMPRESSOR) {
		if (on) {
			int insideWorldTickMagnitude;
			if (currentSong) { // Bit of a hack just referring to currentSong in here...
				insideWorldTickMagnitude =
				    (currentSong->insideWorldTickMagnitude + currentSong->insideWorldTickMagnitudeOffsetFromBPM);
			}
			else insideWorldTickMagnitude = FlashStorage::defaultMagnitude;

			if (compressor.sync == 7 - insideWorldTickMagnitude) {
				compressor.sync = 9 - insideWorldTickMagnitude;
				numericDriver.displayPopup(HAVE_OLED ? "Fast sidechain compressor" : "FAST");
			}
			else {
				compressor.sync = 7 - insideWorldTickMagnitude;
				numericDriver.displayPopup(HAVE_OLED ? "Slow sidechain compressor" : "SLOW");
			}
			return true;
		}
		else return false;
	}

	// Switching between LPF, HPF and EQ
	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(PARAM_LOCAL_LPF_FREQ)) {
		if (on && synthMode != SYNTH_MODE_FM) {
			ourModKnob->paramDescriptor.setToHaveParamOnly(PARAM_LOCAL_HPF_FREQ);
			// Switch resonance too
			if (modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.isSetToParamWithNoSource(
			        PARAM_LOCAL_LPF_RESONANCE)) {
				modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.setToHaveParamOnly(
				    PARAM_LOCAL_HPF_RESONANCE);
			}
			numericDriver.displayPopup("HPF");
		}
		return false;
	}

	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(PARAM_LOCAL_HPF_FREQ)) {
		if (on && synthMode != SYNTH_MODE_FM) {
			ourModKnob->paramDescriptor.setToHaveParamOnly(PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_TREBLE);
			// Switch resonance too
			if (modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.isSetToParamWithNoSource(
			        PARAM_LOCAL_HPF_RESONANCE)) {
				modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.setToHaveParamOnly(PARAM_UNPATCHED_SECTION
				                                                                              + PARAM_UNPATCHED_BASS);
			}
			numericDriver.displayPopup("EQ");
		}
		return false;
	}

	else if (ourModKnob->paramDescriptor.isSetToParamWithNoSource(PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_TREBLE)) {
		if (on && synthMode != SYNTH_MODE_FM) {
			ourModKnob->paramDescriptor.setToHaveParamOnly(PARAM_LOCAL_LPF_FREQ);
			// Switch resonance too
			if (modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.isSetToParamWithNoSource(
			        PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_BASS)) {
				modKnobs[modKnobMode][1 - whichModEncoder].paramDescriptor.setToHaveParamOnly(
				    PARAM_LOCAL_LPF_RESONANCE);
			}
			numericDriver.displayPopup("LPF");
		}
		return false;
	}

	return false;
}

// modelStack may be NULL
void Sound::fastReleaseAllVoices(ModelStackWithSoundFlags* modelStack) {
	if (!numVoicesAssigned) return;

	int ends[2];
	AudioEngine::activeVoices.getRangeForSound(this, ends);
	for (int v = ends[0]; v < ends[1]; v++) {
		Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);
		bool stillGoing = thisVoice->doFastRelease();

		if (!stillGoing) {
			AudioEngine::activeVoices.checkVoiceExists(thisVoice, this, "E212");
			AudioEngine::unassignVoice(thisVoice, this, modelStack); // Accepts NULL
			v--;
			ends[1]--;
		}
	}
}

void Sound::prepareForHibernation() {
	wontBeRenderedForAWhile();
	detachSourcesFromAudioFiles();
}

// This can get called either for hibernation, or because drum now has no active noteRow
void Sound::wontBeRenderedForAWhile() {
	ModControllableAudio::wontBeRenderedForAWhile();

	unassignAllVoices(); // Can't remember if this is always necessary, but it is when this is called from Instrumentclip::detachFromInstrument()

	getArp()->reset(); // Surely this shouldn't be quite necessary?
	compressor.status = ENVELOPE_STAGE_OFF;

	reassessRenderSkippingStatus(NULL, true); // Tell it to just cut the MODFX tail - we needa change status urgently!

	// If it still thinks it's meant to be rendering, we did something wrong
	if (ALPHA_OR_BETA_VERSION && !skippingRendering) {
		numericDriver.freezeWithError("E322");
	}
}

void Sound::detachSourcesFromAudioFiles() {
	for (int s = 0; s < NUM_SOURCES; s++) {
		sources[s].detachAllAudioFiles();
	}
}

void Sound::deleteMultiRange(int s, int r) {
	// Because range storage is about to change, must unassign all voices, and make sure no more can be assigned during memory allocation
	unassignAllVoices();
	AudioEngine::audioRoutineLocked = true;
	sources[s].ranges.getElement(r)->~MultiRange();
	sources[s].ranges.deleteAtIndex(r);
	AudioEngine::audioRoutineLocked = false;
}

// This function has to give the same outcome as Source::renderInStereo()
bool Sound::renderingVoicesInStereo(ModelStackWithSoundFlags* modelStack) {

	// audioDriver deciding we're rendering in mono overrides everything
	if (!AudioEngine::renderInStereo) return false;

	if (!numVoicesAssigned) return false;

	// Stereo live-input
	if ((sources[0].oscType == OSC_TYPE_INPUT_STEREO || sources[1].oscType == OSC_TYPE_INPUT_STEREO)
	    && (AudioEngine::micPluggedIn || AudioEngine::lineInPluggedIn))
		return true;

	if (modelStack->paramManager->getPatchCableSet()->doesParamHaveSomethingPatchedToIt(PARAM_LOCAL_PAN)) return true;

	unsigned int mustExamineSourceInEachVoice = 0;

	// Have a look at what samples, if any, are in each Source
	for (int s = 0; s < NUM_SOURCES; s++) {
		Source* source = &sources[s];

		if (!modelStack->checkSourceEverActive(s)) continue;

		if (source->oscType == OSC_TYPE_SAMPLE) { // Just SAMPLE, because WAVETABLEs can't be stereo.

			int numRanges = source->ranges.getNumElements();

			// If multiple ranges, we have to come back and examine Voices to see which are in use
			if (numRanges > 1) {
				mustExamineSourceInEachVoice |= (1 << s);
			}

			// Or if just 1 range, we can examine it now
			else if (numRanges == 1) {
				MultiRange* range = source->ranges.getElement(0);
				AudioFileHolder* holder = range->getAudioFileHolder();

				if (holder->audioFile && holder->audioFile->numChannels == 2) return true;
			}
		}
	}

	// Ok, if that determined that either source has multiple samples (multisample ranges), we now have to investigate each Voice
	if (mustExamineSourceInEachVoice) {

		int ends[2];
		AudioEngine::activeVoices.getRangeForSound(this, ends);
		for (int v = ends[0]; v < ends[1]; v++) {
			Voice* thisVoice = AudioEngine::activeVoices.getVoice(v);

			for (int s = 0; s < NUM_SOURCES; s++) {
				if (mustExamineSourceInEachVoice & (1 << s)) {
					AudioFileHolder* holder = thisVoice->guides[s].audioFileHolder;
					if (holder && holder->audioFile && holder->audioFile->numChannels == 2) return true;
				}
			}
		}
	}

	// No stereo stuff found - we're rendering in mono.
	return false;
}

char const* Sound::paramToString(uint8_t param) {

	switch (param) {

	case PARAM_LOCAL_OSC_A_VOLUME:
		return "oscAVolume";

	case PARAM_LOCAL_OSC_B_VOLUME:
		return "oscBVolume";

	case PARAM_LOCAL_VOLUME:
		return "volume";

	case PARAM_LOCAL_NOISE_VOLUME:
		return "noiseVolume";

	case PARAM_LOCAL_OSC_A_PHASE_WIDTH:
		return "oscAPhaseWidth";

	case PARAM_LOCAL_OSC_B_PHASE_WIDTH:
		return "oscBPhaseWidth";

	case PARAM_LOCAL_OSC_A_WAVE_INDEX:
		return "oscAWavetablePosition";

	case PARAM_LOCAL_OSC_B_WAVE_INDEX:
		return "oscBWavetablePosition";

	case PARAM_LOCAL_LPF_RESONANCE:
		return "lpfResonance";

	case PARAM_LOCAL_HPF_RESONANCE:
		return "hpfResonance";

	case PARAM_LOCAL_PAN:
		return "pan";

	case PARAM_LOCAL_MODULATOR_0_VOLUME:
		return "modulator1Volume";

	case PARAM_LOCAL_MODULATOR_1_VOLUME:
		return "modulator2Volume";

	case PARAM_LOCAL_LPF_FREQ:
		return "lpfFrequency";

	case PARAM_LOCAL_PITCH_ADJUST:
		return "pitch";

	case PARAM_LOCAL_OSC_A_PITCH_ADJUST:
		return "oscAPitch";

	case PARAM_LOCAL_OSC_B_PITCH_ADJUST:
		return "oscBPitch";

	case PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST:
		return "modulator1Pitch";

	case PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST:
		return "modulator2Pitch";

	case PARAM_LOCAL_HPF_FREQ:
		return "hpfFrequency";

	case PARAM_LOCAL_LFO_LOCAL_FREQ:
		return "lfo2Rate";

	case PARAM_LOCAL_ENV_0_ATTACK:
		return "env1Attack";

	case PARAM_LOCAL_ENV_0_DECAY:
		return "env1Decay";

	case PARAM_LOCAL_ENV_0_SUSTAIN:
		return "env1Sustain";

	case PARAM_LOCAL_ENV_0_RELEASE:
		return "env1Release";

	case PARAM_LOCAL_ENV_1_ATTACK:
		return "env2Attack";

	case PARAM_LOCAL_ENV_1_DECAY:
		return "env2Decay";

	case PARAM_LOCAL_ENV_1_SUSTAIN:
		return "env2Sustain";

	case PARAM_LOCAL_ENV_1_RELEASE:
		return "env2Release";

	case PARAM_GLOBAL_LFO_FREQ:
		return "lfo1Rate";

	case PARAM_GLOBAL_VOLUME_POST_FX:
		return "volumePostFX";

	case PARAM_GLOBAL_VOLUME_POST_REVERB_SEND:
		return "volumePostReverbSend";

	case PARAM_GLOBAL_DELAY_RATE:
		return "delayRate";

	case PARAM_GLOBAL_DELAY_FEEDBACK:
		return "delayFeedback";

	case PARAM_GLOBAL_REVERB_AMOUNT:
		return "reverbAmount";

	case PARAM_GLOBAL_MOD_FX_RATE:
		return "modFXRate";

	case PARAM_GLOBAL_MOD_FX_DEPTH:
		return "modFXDepth";

	case PARAM_GLOBAL_ARP_RATE:
		return "arpRate";

	case PARAM_LOCAL_MODULATOR_0_FEEDBACK:
		return "modulator1Feedback";

	case PARAM_LOCAL_MODULATOR_1_FEEDBACK:
		return "modulator2Feedback";

	case PARAM_LOCAL_CARRIER_0_FEEDBACK:
		return "carrier1Feedback";

	case PARAM_LOCAL_CARRIER_1_FEEDBACK:
		return "carrier2Feedback";

		// Unpatched params just for Sounds

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_SOUND_ARP_GATE:
		return "arpGate";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_SOUND_PORTA:
		return "portamento";

	default:
		return ModControllableAudio::paramToString(param);
	}
}

int Sound::stringToParam(char const* string) {
	for (int p = 0; p < NUM_PARAMS; p++) {
		if (!strcmp(string, Sound::paramToString(p))) return p;
	}

	for (int p = PARAM_UNPATCHED_SECTION + NUM_SHARED_UNPATCHED_PARAMS;
	     p < PARAM_UNPATCHED_SECTION + MAX_NUM_UNPATCHED_PARAM_FOR_SOUNDS; p++) {
		if (!strcmp(string, Sound::paramToString(p))) return p;
	}

	if (!strcmp(string, "range")) return PARAM_PLACEHOLDER_RANGE; // For compatibility reading files from before V3.2.0

	return ModControllableAudio::stringToParam(string);
}

ModelStackWithAutoParam* Sound::getParamFromMIDIKnob(MIDIKnob* knob, ModelStackWithThreeMainThings* modelStack) {

	ParamCollectionSummary* summary;
	int paramId;

	if (knob->paramDescriptor.isJustAParam()) {

		int p = knob->paramDescriptor.getJustTheParam();

		// Unpatched parameter
		if (p >= PARAM_UNPATCHED_SECTION) {
			return ModControllableAudio::getParamFromMIDIKnob(knob, modelStack);
		}

		// Actual (patched) parameter
		else {
			summary = modelStack->paramManager->getPatchedParamSetSummary();
			paramId = p;
		}
	}

	// Patch cable strength
	else {
		summary = modelStack->paramManager->getPatchCableSetSummary();
		paramId = knob->paramDescriptor.data;
	}

	ModelStackWithParamId* modelStackWithParamId =
	    modelStack->addParamCollectionAndId(summary->paramCollection, summary, paramId);

	ModelStackWithAutoParam* modelStackWithAutoParam = summary->paramCollection->getAutoParamFromId(
	    modelStackWithParamId, true); // Allow patch cable creation. TODO: think this through better...

	return modelStackWithAutoParam;
}

/*

int startV, endV;
audioDriver.voices.getRangeForSound(this, &startV, &endV);
for (int v = startV; v < endV; v++) {
	Voice* thisVoice = audioDriver.voices.getElement(v)->voice;
}


for (int s = 0; s < NUM_SOURCES; s++) {
*/
