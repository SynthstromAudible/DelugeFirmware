/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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
#include <GlobalEffectable.h>
#include <ParamManager.h>
#include "song.h"
#include "View.h"
#include "matrixdriver.h"
#include "ActionLogger.h"
#include "storagemanager.h"
#include "GeneralMemoryAllocator.h"
#include <new>
#include "Buttons.h"
#include "numericdriver.h"
#include "ModelStack.h"
#include "ParamSet.h"
#include "ParamCollection.h"

GlobalEffectable::GlobalEffectable() {
	lpfMode = LPF_MODE_TRANSISTOR_24DB;
	filterSets[0].reset();
	filterSets[1].reset();
	modFXType = MOD_FX_TYPE_FLANGER;
	currentModFXParam = MOD_FX_PARAM_FEEDBACK;
	currentFilterType = FILTER_TYPE_LPF;

	memset(allpassMemory, 0, sizeof(allpassMemory));
	memset(&phaserMemory, 0, sizeof(phaserMemory));
}

void GlobalEffectable::cloneFrom(ModControllableAudio* other) {
	ModControllableAudio::cloneFrom(other);

	currentModFXParam = ((GlobalEffectable*)other)->currentModFXParam;
	currentFilterType = ((GlobalEffectable*)other)->currentFilterType;
}

void GlobalEffectable::initParams(ParamManager* paramManager) {
	ModControllableAudio::initParams(paramManager);

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_RATE].setCurrentValueBasicForSetup(-536870912);
	unpatchedParams->params[PARAM_UNPATCHED_MOD_FX_FEEDBACK].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_DEPTH].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_RATE].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_PAN].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_AMOUNT].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_REVERB_SEND_AMOUNT].setCurrentValueBasicForSetup(
	    -2147483648);

	unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_VOLUME].setCurrentValueBasicForSetup(
	    889516852); // 3 quarters of the way up
	unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_SIDECHAIN_VOLUME].setCurrentValueBasicForSetup(
	    -2147483648);
	unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_PITCH_ADJUST].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_RES].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_FREQ].setCurrentValueBasicForSetup(2147483647);

	unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_RES].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_FREQ].setCurrentValueBasicForSetup(-2147483648);
}

void GlobalEffectable::initParamsForAudioClip(ParamManagerForTimeline* paramManager) {
	initParams(paramManager);
	paramManager->getUnpatchedParamSet()->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_VOLUME].setCurrentValueBasicForSetup(
	    -536870912);
}

void GlobalEffectable::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) {

	// Stutter
	if (DELUGE_MODEL == DELUGE_MODEL_40_PAD && whichModButton == 5 && *getModKnobMode() == 5) {
		if (on) {
			beginStutter(paramManager);
			return;
		}
	}

	// Otherwise, if we're leaving this mod function or anything else is happening, we want to be sure that stutter has stopped
	endStutter(paramManager);
}

// Returns whether Instrument changed
bool GlobalEffectable::modEncoderButtonAction(uint8_t whichModEncoder, bool on,
                                              ModelStackWithThreeMainThings* modelStack) {

	int modKnobMode = *getModKnobMode();

	// Stutter section
	if (modKnobMode == 6 && whichModEncoder == 1) {
		if (on) {
			beginStutter((ParamManagerForTimeline*)modelStack->paramManager);
		}
		else {
			endStutter((ParamManagerForTimeline*)modelStack->paramManager);
		}
		return false;
	}

	// Mod FX section
	else if (modKnobMode == 5) {
		if (whichModEncoder == 1) {
			if (on) {
				modFXType++;
				if (modFXType >= NUM_MOD_FX_TYPES) modFXType = 1;
				char* displayText;
				switch (modFXType) {
				case MOD_FX_TYPE_FLANGER:
					displayText = "FLANGER";
					break;

				case MOD_FX_TYPE_PHASER:
					displayText = "PHASER";
					break;

				case MOD_FX_TYPE_CHORUS:
					displayText = "CHORUS";
					break;
				}
				numericDriver.displayPopup(displayText);
				ensureModFXParamIsValid();
				return true;
			}
			else return false;
		}
		else {
			if (on) {
				currentModFXParam++;
				if (currentModFXParam == NUM_MOD_FX_PARAMS) currentModFXParam = 0;
				ensureModFXParamIsValid();

				char* displayText;
				switch (currentModFXParam) {
				case MOD_FX_PARAM_DEPTH:
					displayText = "DEPTH";
					break;

				case MOD_FX_PARAM_FEEDBACK:
					displayText = "FEEDBACK";
					break;

				case MOD_FX_PARAM_OFFSET:
					displayText = "OFFSET";
					break;
				}
				numericDriver.displayPopup(displayText);
			}

			return false;
		}
	}

	// Filter section
	else if (modKnobMode == 1) {
		if (whichModEncoder == 1) {
			if (on) {
				currentFilterType++;
				if (currentFilterType >= NUM_FILTER_TYPES) currentFilterType = 0;

				char* displayText;
				switch (currentFilterType) {
				case FILTER_TYPE_LPF:
					displayText = "LPF";
					break;

				case FILTER_TYPE_HPF:
					displayText = "HPF";
					break;

				case FILTER_TYPE_EQ:
					displayText = "EQ";
					break;
				}
				numericDriver.displayPopup(displayText);
			}

			return false;
		}
		else {
			if (on && currentFilterType == FILTER_TYPE_LPF) {
				switchLPFMode();
				return true;
			}

			else return false;
		}
	}

	// Delay section
	else if (modKnobMode == 3) {
		if (whichModEncoder == 1) {
			if (on) {
				switchDelayPingPong();
				return true;
			}
			else return false;
		}
		else {
			if (on) {
				switchDelayAnalog();
				return true;
			}
			else return false;
		}
	}

	// Reverb / sidechain section
	else if (modKnobMode == 4) {
		if (whichModEncoder == 0) { // Reverb
			if (on) view.cycleThroughReverbPresets();
		}

		return false;
	}

	return false; // Some cases could lead here
}

// Always check this doesn't return NULL!
int GlobalEffectable::getParameterFromKnob(int whichModEncoder) {

	int modKnobMode = *getModKnobMode();

	if (modKnobMode == 0) {
		if (whichModEncoder != 0) return PARAM_UNPATCHED_GLOBALEFFECTABLE_VOLUME;
		else return PARAM_UNPATCHED_GLOBALEFFECTABLE_PAN;
	}
	else if (modKnobMode == 1) {
		switch (currentFilterType) {
		case FILTER_TYPE_LPF:
			if (whichModEncoder != 0) return PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_FREQ;
			else return PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_RES;
		case FILTER_TYPE_HPF:
			if (whichModEncoder != 0) return PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_FREQ;
			else return PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_RES;
		default: //case FILTER_TYPE_EQ:
			if (whichModEncoder != 0) return PARAM_UNPATCHED_TREBLE;
			else return PARAM_UNPATCHED_BASS;
		}
	}
	else if (modKnobMode == 3) {
		if (whichModEncoder != 0) return PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_RATE;
		else return PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_AMOUNT;
	}

	else if (modKnobMode == 4) {
		if (whichModEncoder == 0) return PARAM_UNPATCHED_GLOBALEFFECTABLE_REVERB_SEND_AMOUNT;
	}
#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
	else if (modKnobMode == 5) {
		if (whichModEncoder != 0) return PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_RATE;
		else {
			if (currentModFXParam == MOD_FX_PARAM_DEPTH) return PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_DEPTH;
			else if (currentModFXParam == MOD_FX_PARAM_OFFSET) return PARAM_UNPATCHED_MOD_FX_OFFSET;
			else return PARAM_UNPATCHED_MOD_FX_FEEDBACK;
		}
	}
	else if (modKnobMode == 6) {
		if (whichModEncoder != 0) return PARAM_UNPATCHED_STUTTER_RATE;
	}
	else if (modKnobMode == 7) {
		if (whichModEncoder != 0) return PARAM_UNPATCHED_SAMPLE_RATE_REDUCTION;
		else return PARAM_UNPATCHED_BITCRUSHING;
	}
#else
	else if (modKnobMode == 5) {
		if (whichModEncoder != 0) return PARAM_UNPATCHED_STUTTER_RATE;
		else return PARAM_UNPATCHED_SAMPLE_RATE_REDUCTION;
	}
#endif
	return 255;
}

ModelStackWithAutoParam* GlobalEffectable::getParamFromModEncoder(int whichModEncoder,
                                                                  ModelStackWithThreeMainThings* modelStack,
                                                                  bool allowCreation) {
	ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();
	ParamCollection* paramCollection = summary->paramCollection;
	int paramId;

	paramId = getParameterFromKnob(whichModEncoder);

	ModelStackWithParamId* newModelStack1 = modelStack->addParamCollectionAndId(paramCollection, summary, paramId);

	if (paramId == 255) return newModelStack1->addAutoParam(NULL); // Communicate there's no param, back to caller
	else return newModelStack1->paramCollection->getAutoParamFromId(newModelStack1, allowCreation);
}

void GlobalEffectable::ensureModFXParamIsValid() {
	while (true) {
		if (currentModFXParam == MOD_FX_PARAM_DEPTH) {
			if (modFXType == MOD_FX_TYPE_FLANGER) {
				goto ohNo;
			}
		}
		else if (currentModFXParam == MOD_FX_PARAM_OFFSET) {
			if (modFXType != MOD_FX_TYPE_CHORUS) {
				goto ohNo;
			}
		}
		else { // MOD_FX_PARAM_FEEDBACK
			if (modFXType == MOD_FX_TYPE_CHORUS) {
				goto ohNo;
			}
		}
		return; // If we got here, we're fine

ohNo:
		currentModFXParam++;
		if (currentModFXParam == NUM_MOD_FX_PARAMS) currentModFXParam = 0;
	}
}

void GlobalEffectable::setupFilterSetConfig(FilterSetConfig* filterSetConfig, int32_t* postFXVolume,
                                            ParamManager* paramManager) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	int lpfFrequency = getFinalParameterValueExp(
	    paramNeutralValues[PARAM_LOCAL_LPF_FREQ],
	    cableToExpParamShortcut(unpatchedParams->getValue(PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_FREQ)));
	int lpfResonance = getFinalParameterValueLinear(
	    paramNeutralValues[PARAM_LOCAL_LPF_RESONANCE],
	    cableToLinearParamShortcut(unpatchedParams->getValue(PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_RES)));

	int hpfFrequency = getFinalParameterValueExp(
	    paramNeutralValues[PARAM_LOCAL_HPF_FREQ],
	    cableToExpParamShortcut(unpatchedParams->getValue(PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_FREQ)));
	int hpfResonance = getFinalParameterValueLinear(
	    paramNeutralValues[PARAM_LOCAL_HPF_RESONANCE],
	    cableToLinearParamShortcut(unpatchedParams->getValue(PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_RES)));

	filterSetConfig->doLPF = (lpfMode == LPF_MODE_TRANSISTOR_24DB_DRIVE
	                          || unpatchedParams->getValue(PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_FREQ) < 2147483602);
	filterSetConfig->doHPF = unpatchedParams->getValue(PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_FREQ) != -2147483648;

	*postFXVolume = filterSetConfig->init(lpfFrequency, lpfResonance, hpfFrequency, hpfResonance, lpfMode,
	                                      *postFXVolume, false, NULL);
}

void GlobalEffectable::processFilters(StereoSample* buffer, int numSamples, FilterSetConfig* filterSetConfig) {

	if (filterSetConfig->doHPF) {
		StereoSample* thisSample = buffer;
		StereoSample* bufferEnd = buffer + numSamples;

		do {
			filterSets[0].renderHPF(&thisSample->l, filterSetConfig, 2);
			filterSets[1].renderHPF(&thisSample->r, filterSetConfig, 2);
		} while (++thisSample != bufferEnd);
	}

	if (filterSetConfig->doLPF) {
		filterSets[0].renderLPFLong(&buffer->l, &(buffer + numSamples)->l, filterSetConfig, lpfMode, 2, 2, 1);
		filterSets[1].renderLPFLong(&buffer->r, &(buffer + numSamples)->r, filterSetConfig, lpfMode, 2, 2, 1);
	}
}

void GlobalEffectable::writeAttributesToFile(bool writeAutomation) {

	ModControllableAudio::writeAttributesToFile();

	storageManager.writeAttribute("modFXCurrentParam", (char*)modFXParamToString(currentModFXParam));
	storageManager.writeAttribute("currentFilterType", (char*)filterTypeToString(currentFilterType));
}

void GlobalEffectable::writeTagsToFile(ParamManager* paramManager, bool writeAutomation) {

	ModControllableAudio::writeTagsToFile();

	if (paramManager) {
		storageManager.writeOpeningTagBeginning("defaultParams");
		GlobalEffectable::writeParamAttributesToFile(paramManager, writeAutomation);
		storageManager.writeOpeningTagEnd();
		GlobalEffectable::writeParamTagsToFile(paramManager, writeAutomation);
		storageManager.writeClosingTag("defaultParams");
	}
}

void GlobalEffectable::writeParamAttributesToFile(ParamManager* paramManager, bool writeAutomation,
                                                  int32_t* valuesForOverride) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->writeParamAsAttribute("reverbAmount", PARAM_UNPATCHED_GLOBALEFFECTABLE_REVERB_SEND_AMOUNT,
	                                       writeAutomation, false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute("volume", PARAM_UNPATCHED_GLOBALEFFECTABLE_VOLUME, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("pan", PARAM_UNPATCHED_GLOBALEFFECTABLE_PAN, writeAutomation, false,
	                                       valuesForOverride);

	if (unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_PITCH_ADJUST].containsSomething(0))
		unpatchedParams->writeParamAsAttribute("pitchAdjust", PARAM_UNPATCHED_GLOBALEFFECTABLE_PITCH_ADJUST,
		                                       writeAutomation, false, valuesForOverride);

	if (unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_SIDECHAIN_VOLUME].containsSomething(-2147483648))
		unpatchedParams->writeParamAsAttribute("sidechainCompressorVolume",
		                                       PARAM_UNPATCHED_GLOBALEFFECTABLE_SIDECHAIN_VOLUME, writeAutomation,
		                                       false, valuesForOverride);

	unpatchedParams->writeParamAsAttribute("sidechainCompressorShape", PARAM_UNPATCHED_COMPRESSOR_SHAPE,
	                                       writeAutomation, false, valuesForOverride);

	unpatchedParams->writeParamAsAttribute("modFXDepth", PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_DEPTH, writeAutomation,
	                                       false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute("modFXRate", PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_RATE, writeAutomation,
	                                       false, valuesForOverride);

	ModControllableAudio::writeParamAttributesToFile(paramManager, writeAutomation, valuesForOverride);
}

void GlobalEffectable::writeParamTagsToFile(ParamManager* paramManager, bool writeAutomation,
                                            int32_t* valuesForOverride) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	storageManager.writeOpeningTagBeginning("delay");
	unpatchedParams->writeParamAsAttribute("rate", PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_RATE, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("feedback", PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_AMOUNT, writeAutomation,
	                                       false, valuesForOverride);
	storageManager.closeTag();

	storageManager.writeOpeningTagBeginning("lpf");
	unpatchedParams->writeParamAsAttribute("frequency", PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_FREQ, writeAutomation,
	                                       false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute("resonance", PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_RES, writeAutomation,
	                                       false, valuesForOverride);
	storageManager.closeTag();

	storageManager.writeOpeningTagBeginning("hpf");
	unpatchedParams->writeParamAsAttribute("frequency", PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_FREQ, writeAutomation,
	                                       false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute("resonance", PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_RES, writeAutomation,
	                                       false, valuesForOverride);
	storageManager.closeTag();

	ModControllableAudio::writeParamTagsToFile(paramManager, writeAutomation, valuesForOverride);
}

void GlobalEffectable::readParamsFromFile(ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos) {
	char const* tagName;

	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (readParamTagFromFile(tagName, paramManager, readAutomationUpToPos)) {}
		else storageManager.exitTag(tagName);
	}
}

bool GlobalEffectable::readParamTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager,
                                            int32_t readAutomationUpToPos) {

	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;

	if (!strcmp(tagName, "delay")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "rate")) {
				unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_RATE,
				                           readAutomationUpToPos);
				storageManager.exitTag("rate");
			}
			else if (!strcmp(tagName, "feedback")) {
				unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_AMOUNT,
				                           readAutomationUpToPos);
				storageManager.exitTag("feedback");
			}
		}
		storageManager.exitTag("delay");
	}

	else if (!strcmp(tagName, "lpf")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "frequency")) {
				unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_FREQ,
				                           readAutomationUpToPos);
				storageManager.exitTag("frequency");
			}
			else if (!strcmp(tagName, "resonance")) {
				unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_RES,
				                           readAutomationUpToPos);
				storageManager.exitTag("resonance");
			}
		}
		storageManager.exitTag("lpf");
	}

	else if (!strcmp(tagName, "hpf")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "frequency")) {
				unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_FREQ,
				                           readAutomationUpToPos);
				storageManager.exitTag("frequency");
			}
			else if (!strcmp(tagName, "resonance")) {
				unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_RES,
				                           readAutomationUpToPos);
				storageManager.exitTag("resonance");
			}
		}
		storageManager.exitTag("hpf");
	}

	else if (!strcmp(tagName, "reverbAmount")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_GLOBALEFFECTABLE_REVERB_SEND_AMOUNT,
		                           readAutomationUpToPos);
		storageManager.exitTag("reverbAmount");
	}

	else if (!strcmp(tagName, "volume")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_GLOBALEFFECTABLE_VOLUME,
		                           readAutomationUpToPos);
		storageManager.exitTag("volume");
	}

	else if (!strcmp(tagName, "sidechainCompressorVolume")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_GLOBALEFFECTABLE_SIDECHAIN_VOLUME,
		                           readAutomationUpToPos);
		storageManager.exitTag("sidechainCompressorVolume");
	}

	else if (!strcmp(tagName, "sidechainCompressorShape")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_COMPRESSOR_SHAPE, readAutomationUpToPos);
		storageManager.exitTag("sidechainCompressorShape");
	}

	else if (!strcmp(tagName, "pan")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_GLOBALEFFECTABLE_PAN, readAutomationUpToPos);
		storageManager.exitTag("pan");
	}

	else if (!strcmp(tagName, "pitchAdjust")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_GLOBALEFFECTABLE_PITCH_ADJUST,
		                           readAutomationUpToPos);
		storageManager.exitTag("pitchAdjust");
	}

	else if (!strcmp(tagName, "modFXDepth")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_DEPTH,
		                           readAutomationUpToPos);
		storageManager.exitTag("modFXDepth");
	}

	else if (!strcmp(tagName, "modFXRate")) {
		unpatchedParams->readParam(unpatchedParamsSummary, PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_RATE,
		                           readAutomationUpToPos);
		storageManager.exitTag("modFXRate");
	}

	else if (ModControllableAudio::readParamTagFromFile(tagName, paramManager, readAutomationUpToPos)) {}

	else return false;

	return true;
}

// paramManager is optional
int GlobalEffectable::readTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager,
                                      int32_t readAutomationUpToPos, Song* song) {

	// This is here for compatibility only for people (Lou and Ian) who saved songs with firmware in September 2016
	//if (paramManager && strcmp(tagName, "delay") && GlobalEffectable::readParamTagFromFile(tagName, paramManager, readAutomation)) {}

	//else
	if (paramManager && !strcmp(tagName, "defaultParams")) {

		if (!paramManager->containsAnyMainParamCollections()) {
			int error = paramManager->setupUnpatched();
			if (error) return error;
			initParams(paramManager);
		}

		GlobalEffectable::readParamsFromFile(paramManager, readAutomationUpToPos);
		storageManager.exitTag("defaultParams");
	}

	else if (!strcmp(tagName, "modFXType")) {
		modFXType = stringToFXType(storageManager.readTagOrAttributeValue());
		storageManager.exitTag("modFXType");
	}

	else if (!strcmp(tagName, "modFXCurrentParam")) {
		currentModFXParam = stringToModFXParam(storageManager.readTagOrAttributeValue());
		storageManager.exitTag("modFXCurrentParam");
	}

	else if (!strcmp(tagName, "currentFilterType")) {
		currentFilterType = stringToFilterType(storageManager.readTagOrAttributeValue());
		storageManager.exitTag("currentFilterType");
	}

	else {
		return ModControllableAudio::readTagFromFile(tagName, NULL, readAutomationUpToPos, song);
	}

	return NO_ERROR;
}

// Before calling this, check that (storageManager.firmwareVersionOfFileBeingRead < FIRMWARE_1P2P0 && !paramManager->resonanceBackwardsCompatibilityProcessed)
void GlobalEffectable::compensateVolumeForResonance(ParamManagerForTimeline* paramManager) {

	paramManager->resonanceBackwardsCompatibilityProcessed = true;

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	// If no LPF on, and resonance is at 50%, set it to 0%
	if (!unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_FREQ].isAutomated()
	    && unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_FREQ].getCurrentValue() >= 2147483602
	    && !unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_RES].containsSomething(0)) {
		unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_RES].currentValue = -2147483648;
	}

	// If no HPF on, and resonance is at 25%, set it to 0%
	if (!unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_FREQ].containsSomething(-2147483648)
	    && !unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_RES].containsSomething(-1073741824)) {
		unpatchedParams->params[PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_RES].currentValue = -2147483648;
	}
}

int GlobalEffectable::getActiveModFXType(ParamManager* paramManager) {
	int modFXTypeNow = modFXType;

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	if ((currentModFXParam == MOD_FX_PARAM_DEPTH
	     && unpatchedParams->getValue(PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_DEPTH) == -2147483648)
	    || (currentModFXParam == MOD_FX_PARAM_FEEDBACK
	        && unpatchedParams->getValue(PARAM_UNPATCHED_MOD_FX_FEEDBACK) == -2147483648)
	    || (currentModFXParam == MOD_FX_PARAM_OFFSET
	        && unpatchedParams->getValue(PARAM_UNPATCHED_MOD_FX_OFFSET) == -2147483648)) {
		modFXTypeNow = MOD_FX_TYPE_NONE;
	}
	return modFXTypeNow;
}

void GlobalEffectable::setupDelayWorkingState(DelayWorkingState* delayWorkingState, ParamManager* paramManager,
                                              bool shouldLimitDelayFeedback) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	delayWorkingState->delayFeedbackAmount = getFinalParameterValueLinear(
	    paramNeutralValues[PARAM_GLOBAL_DELAY_FEEDBACK],
	    cableToLinearParamShortcut(unpatchedParams->getValue(PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_AMOUNT)));
	if (shouldLimitDelayFeedback) {
		delayWorkingState->delayFeedbackAmount =
		    getMin(delayWorkingState->delayFeedbackAmount, (int32_t)(1 << 30) - (1 << 26));
	}
	delayWorkingState->userDelayRate = getFinalParameterValueExp(
	    paramNeutralValues[PARAM_GLOBAL_DELAY_RATE],
	    cableToExpParamShortcut(unpatchedParams->getValue(PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_RATE)));
	delay.setupWorkingState(delayWorkingState);
}

void GlobalEffectable::processFXForGlobalEffectable(StereoSample* inputBuffer, int numSamples, int32_t* postFXVolume,
                                                    ParamManager* paramManager, DelayWorkingState* delayWorkingState,
                                                    int analogDelaySaturationAmount) {

	StereoSample* inputBufferEnd = inputBuffer + numSamples;

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	int32_t modFXRate = getFinalParameterValueExp(
	    paramNeutralValues[PARAM_GLOBAL_MOD_FX_RATE],
	    cableToExpParamShortcut(unpatchedParams->getValue(PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_RATE)));
	int32_t modFXDepth = getFinalParameterValueVolume(
	    paramNeutralValues[PARAM_GLOBAL_MOD_FX_DEPTH],
	    cableToLinearParamShortcut(unpatchedParams->getValue(PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_DEPTH)));

	int modFXTypeNow = getActiveModFXType(paramManager);

	// For GlobalEffectables, mod FX buffer memory is allocated here in the rendering routine - this might seem strange, but
	// it's because unlike for Sounds, the effect can be switched on and off by changing a parameter like "depth".
	if (modFXTypeNow == MOD_FX_TYPE_FLANGER || modFXTypeNow == MOD_FX_TYPE_CHORUS) {
		if (!modFXBuffer) {
			modFXBuffer =
			    (StereoSample*)generalMemoryAllocator.alloc(modFXBufferSize * sizeof(StereoSample), NULL, false, true);
			if (!modFXBuffer) modFXTypeNow = 0;
			else memset(modFXBuffer, 0, modFXBufferSize * sizeof(StereoSample));
		}
	}
	else {
		if (modFXBuffer) {
			generalMemoryAllocator.dealloc(modFXBuffer);
			modFXBuffer = NULL;
		}
	}

	processFX(inputBuffer, numSamples, modFXTypeNow, modFXRate, modFXDepth, delayWorkingState, postFXVolume,
	          paramManager, analogDelaySaturationAmount);
}

char const* GlobalEffectable::paramToString(uint8_t param) {

	switch (param) {

		// Unpatched params just for GlobalEffectables

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_RATE:
		return "modFXRate";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_MOD_FX_DEPTH:
		return "modFXDepth";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_RATE:
		return "delayRate";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_DELAY_AMOUNT:
		return "delayFeedback";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_PAN:
		return "pan";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_FREQ:
		return "lpfFrequency";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_LPF_RES:
		return "lpfResonance";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_FREQ:
		return "hpfFrequency";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_HPF_RES:
		return "hpfResonance";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_REVERB_SEND_AMOUNT:
		return "reverbAmount";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_VOLUME:
		return "volume";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_SIDECHAIN_VOLUME:
		return "sidechainCompressorVolume";

	case PARAM_UNPATCHED_SECTION + PARAM_UNPATCHED_GLOBALEFFECTABLE_PITCH_ADJUST:
		return "pitchAdjust";

	default:
		return ModControllableAudio::paramToString(param);
	}
}

int GlobalEffectable::stringToParam(char const* string) {
	for (int p = PARAM_UNPATCHED_SECTION + NUM_SHARED_UNPATCHED_PARAMS;
	     p < PARAM_UNPATCHED_SECTION + MAX_NUM_UNPATCHED_PARAMS; p++) {
		if (!strcmp(string, GlobalEffectable::paramToString(p))) return p;
	}
	return ModControllableAudio::stringToParam(string);
}
