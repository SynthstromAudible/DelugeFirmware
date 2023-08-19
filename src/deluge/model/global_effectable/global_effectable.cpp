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

#include "model/global_effectable/global_effectable.h"
#include "definitions_cxx.hpp"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/numeric_driver.h"
#include "hid/matrix/matrix_driver.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/misc.h"
#include <new>
using namespace deluge;

GlobalEffectable::GlobalEffectable() {
	lpfMode = FilterMode::TRANSISTOR_24DB;
	filterSet.reset();

	modFXType = ModFXType::FLANGER;
	currentModFXParam = ModFXParam::FEEDBACK;
	currentFilterType = FilterType::LPF;

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

	unpatchedParams->params[Param::Unpatched::GlobalEffectable::MOD_FX_RATE].setCurrentValueBasicForSetup(-536870912);
	unpatchedParams->params[Param::Unpatched::MOD_FX_FEEDBACK].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[Param::Unpatched::GlobalEffectable::DELAY_RATE].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[Param::Unpatched::GlobalEffectable::PAN].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[Param::Unpatched::GlobalEffectable::DELAY_AMOUNT].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT].setCurrentValueBasicForSetup(
	    -2147483648);

	unpatchedParams->params[Param::Unpatched::GlobalEffectable::VOLUME].setCurrentValueBasicForSetup(
	    889516852); // 3 quarters of the way up
	unpatchedParams->params[Param::Unpatched::GlobalEffectable::SIDECHAIN_VOLUME].setCurrentValueBasicForSetup(
	    -2147483648);
	unpatchedParams->params[Param::Unpatched::GlobalEffectable::PITCH_ADJUST].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[Param::Unpatched::GlobalEffectable::LPF_RES].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[Param::Unpatched::GlobalEffectable::LPF_FREQ].setCurrentValueBasicForSetup(2147483647);

	unpatchedParams->params[Param::Unpatched::GlobalEffectable::HPF_RES].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[Param::Unpatched::GlobalEffectable::HPF_FREQ].setCurrentValueBasicForSetup(-2147483648);
}

void GlobalEffectable::initParamsForAudioClip(ParamManagerForTimeline* paramManager) {
	initParams(paramManager);
	paramManager->getUnpatchedParamSet()
	    ->params[Param::Unpatched::GlobalEffectable::VOLUME]
	    .setCurrentValueBasicForSetup(-536870912);
}

void GlobalEffectable::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) {

	// If we're leaving this mod function or anything else is happening, we want to be sure that stutter has stopped
	endStutter(paramManager);
}

// Returns whether Instrument changed
bool GlobalEffectable::modEncoderButtonAction(uint8_t whichModEncoder, bool on,
                                              ModelStackWithThreeMainThings* modelStack) {

	int32_t modKnobMode = *getModKnobMode();

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
				modFXType = static_cast<ModFXType>((util::to_underlying(modFXType) + 1) % kNumModFXTypes);
				if (modFXType == ModFXType::NONE) {
					modFXType = static_cast<ModFXType>(1);
				}
				char const* displayText;
				switch (modFXType) {
				case ModFXType::FLANGER:
					displayText = "FLANGER";
					break;

				case ModFXType::PHASER:
					displayText = "PHASER";
					break;

				case ModFXType::CHORUS:
					displayText = "CHORUS";
					break;

				case ModFXType::CHORUS_STEREO:
					displayText = "STEREO CHORUS";
					break;
				case ModFXType::GRAIN:
					displayText = "GRAIN";
					break;
				}
				numericDriver.displayPopup(displayText);
				ensureModFXParamIsValid();
				return true;
			}
			else {
				return false;
			}
		}
		else {
			if (on) {
				currentModFXParam =
				    static_cast<ModFXParam>((util::to_underlying(currentModFXParam) + 1) % kNumModFXParams);
				ensureModFXParamIsValid();

				char const* displayText;
				switch (currentModFXParam) {
				case ModFXParam::DEPTH:
					displayText = "DEPTH";
					break;

				case ModFXParam::FEEDBACK:
					displayText = "FEEDBACK";
					break;

				case ModFXParam::OFFSET:
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
				currentFilterType =
				    static_cast<FilterType>((util::to_underlying(currentFilterType) + 1) % kNumFilterTypes);

				char const* displayText;
				switch (currentFilterType) {
				case FilterType::LPF:
					displayText = "LPF";
					break;

				case FilterType::HPF:
					displayText = "HPF";
					break;

				case FilterType::EQ:
					displayText = "EQ";
					break;
				}
				numericDriver.displayPopup(displayText);
			}

			return false;
		}
		else {
			if (on) {
				if (currentFilterType == FilterType::LPF) {
					switchLPFMode();
					return true;
				}
				else if (currentFilterType == FilterType::HPF) {
					switchHPFMode();
					return true;
				}
				else {
					return false;
				}
			}

			else {
				return false;
			}
		}
	}

	// Delay section
	else if (modKnobMode == 3) {
		if (whichModEncoder == 1) {
			if (on) {
				if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::AltGoldenKnobDelayParams)
				    == RuntimeFeatureStateToggle::On) {
					switchDelaySyncType();
				}
				else {
					switchDelayPingPong();
				}
				return true;
			}
			else {
				return false;
			}
		}
		else {
			if (on) {
				if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::AltGoldenKnobDelayParams)
				    == RuntimeFeatureStateToggle::On) {
					switchDelaySyncLevel();
				}
				else {
					switchDelayAnalog();
				}
				return true;
			}
			else {
				return false;
			}
		}
	}

	// Reverb / sidechain section
	else if (modKnobMode == 4) {
		if (whichModEncoder == 0) { // Reverb
			if (on) {
				view.cycleThroughReverbPresets();
			}
		}

		return false;
	}

	return false; // Some cases could lead here
}

// Always check this doesn't return NULL!
int32_t GlobalEffectable::getParameterFromKnob(int32_t whichModEncoder) {

	int32_t modKnobMode = *getModKnobMode();

	if (modKnobMode == 0) {
		if (whichModEncoder != 0) {
			return Param::Unpatched::GlobalEffectable::VOLUME;
		}
		else {
			return Param::Unpatched::GlobalEffectable::PAN;
		}
	}
	else if (modKnobMode == 1) {
		switch (currentFilterType) {
		case FilterType::LPF:
			if (whichModEncoder != 0) {
				return Param::Unpatched::GlobalEffectable::LPF_FREQ;
			}
			else {
				return Param::Unpatched::GlobalEffectable::LPF_RES;
			}
		case FilterType::HPF:
			if (whichModEncoder != 0) {
				return Param::Unpatched::GlobalEffectable::HPF_FREQ;
			}
			else {
				return Param::Unpatched::GlobalEffectable::HPF_RES;
			}
		default: //case FilterType::EQ:
			if (whichModEncoder != 0) {
				return Param::Unpatched::TREBLE;
			}
			else {
				return Param::Unpatched::BASS;
			}
		}
	}
	else if (modKnobMode == 3) {
		if (whichModEncoder != 0) {
			return Param::Unpatched::GlobalEffectable::DELAY_RATE;
		}
		else {
			return Param::Unpatched::GlobalEffectable::DELAY_AMOUNT;
		}
	}

	else if (modKnobMode == 4) {
		if (whichModEncoder == 0) {
			return Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT;
		}
	}
	else if (modKnobMode == 5) {
		if (whichModEncoder != 0) {
			return Param::Unpatched::GlobalEffectable::MOD_FX_RATE;
		}
		else {
			if (currentModFXParam == ModFXParam::DEPTH) {
				return Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH;
			}
			else if (currentModFXParam == ModFXParam::OFFSET) {
				return Param::Unpatched::MOD_FX_OFFSET;
			}
			else {
				return Param::Unpatched::MOD_FX_FEEDBACK;
			}
		}
	}
	else if (modKnobMode == 6) {
		if (whichModEncoder != 0) {
			return Param::Unpatched::STUTTER_RATE;
		}
	}
	else if (modKnobMode == 7) {
		if (whichModEncoder != 0) {
			return Param::Unpatched::SAMPLE_RATE_REDUCTION;
		}
		else {
			return Param::Unpatched::BITCRUSHING;
		}
	}
	return 255;
}

ModelStackWithAutoParam* GlobalEffectable::getParamFromModEncoder(int32_t whichModEncoder,
                                                                  ModelStackWithThreeMainThings* modelStack,
                                                                  bool allowCreation) {
	ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();
	ParamCollection* paramCollection = summary->paramCollection;
	int32_t paramId;

	paramId = getParameterFromKnob(whichModEncoder);

	ModelStackWithParamId* newModelStack1 = modelStack->addParamCollectionAndId(paramCollection, summary, paramId);

	if (paramId == 255) {
		return newModelStack1->addAutoParam(NULL); // Communicate there's no param, back to caller
	}
	else {
		return newModelStack1->paramCollection->getAutoParamFromId(newModelStack1, allowCreation);
	}
}

void GlobalEffectable::ensureModFXParamIsValid() {
	while (true) {
		if (currentModFXParam == ModFXParam::DEPTH) {
			if (modFXType == ModFXType::FLANGER) {
				goto ohNo;
			}
		}
		else if (currentModFXParam == ModFXParam::OFFSET) {
			if (modFXType != ModFXType::CHORUS && modFXType != ModFXType::CHORUS_STEREO
			    && modFXType != ModFXType::GRAIN) {
				goto ohNo;
			}
		}
		else { // ModFXParam::FEEDBACK
			if (modFXType == ModFXType::CHORUS || modFXType == ModFXType::CHORUS_STEREO) {
				goto ohNo;
			}
		}
		return; // If we got here, we're fine

ohNo:
		currentModFXParam = static_cast<ModFXParam>((util::to_underlying(currentModFXParam) + 1) % kNumModFXParams);
	}
}

void GlobalEffectable::setupFilterSetConfig(int32_t* postFXVolume, ParamManager* paramManager) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	int32_t lpfFrequency = getFinalParameterValueExp(
	    paramNeutralValues[Param::Local::LPF_FREQ],
	    cableToExpParamShortcut(unpatchedParams->getValue(Param::Unpatched::GlobalEffectable::LPF_FREQ)));
	int32_t lpfResonance = getFinalParameterValueLinear(
	    paramNeutralValues[Param::Local::LPF_RESONANCE],
	    cableToLinearParamShortcut(unpatchedParams->getValue(Param::Unpatched::GlobalEffectable::LPF_RES)));

	int32_t hpfFrequency = getFinalParameterValueExp(
	    paramNeutralValues[Param::Local::HPF_FREQ],
	    cableToExpParamShortcut(unpatchedParams->getValue(Param::Unpatched::GlobalEffectable::HPF_FREQ)));
	int32_t hpfResonance = getFinalParameterValueLinear(
	    paramNeutralValues[Param::Local::HPF_RESONANCE],
	    cableToLinearParamShortcut(unpatchedParams->getValue(Param::Unpatched::GlobalEffectable::HPF_RES)));

	bool doLPF = (lpfMode == FilterMode::TRANSISTOR_24DB_DRIVE
	              || unpatchedParams->getValue(Param::Unpatched::GlobalEffectable::LPF_FREQ) < 2147483602);
	bool doHPF = unpatchedParams->getValue(Param::Unpatched::GlobalEffectable::HPF_FREQ) != -2147483648;

	//no morph for global effectable
	*postFXVolume = filterSet.setConfig(lpfFrequency, lpfResonance, doLPF, lpfMode, 0, hpfFrequency, hpfResonance,
	                                    doHPF, FilterMode::HPLADDER, 0, *postFXVolume, filterRoute, false, NULL);
}

void GlobalEffectable::processFilters(StereoSample* buffer, int32_t numSamples) {
	filterSet.renderLongStereo(&buffer->l, &(buffer + numSamples)->l);
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

	unpatchedParams->writeParamAsAttribute("reverbAmount", Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT,
	                                       writeAutomation, false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute("volume", Param::Unpatched::GlobalEffectable::VOLUME, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("pan", Param::Unpatched::GlobalEffectable::PAN, writeAutomation, false,
	                                       valuesForOverride);

	if (unpatchedParams->params[Param::Unpatched::GlobalEffectable::PITCH_ADJUST].containsSomething(0)) {
		unpatchedParams->writeParamAsAttribute("pitchAdjust", Param::Unpatched::GlobalEffectable::PITCH_ADJUST,
		                                       writeAutomation, false, valuesForOverride);
	}

	if (unpatchedParams->params[Param::Unpatched::GlobalEffectable::SIDECHAIN_VOLUME].containsSomething(-2147483648)) {
		unpatchedParams->writeParamAsAttribute("sidechainCompressorVolume",
		                                       Param::Unpatched::GlobalEffectable::SIDECHAIN_VOLUME, writeAutomation,
		                                       false, valuesForOverride);
	}

	unpatchedParams->writeParamAsAttribute("sidechainCompressorShape", Param::Unpatched::COMPRESSOR_SHAPE,
	                                       writeAutomation, false, valuesForOverride);

	unpatchedParams->writeParamAsAttribute("modFXDepth", Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH,
	                                       writeAutomation, false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute("modFXRate", Param::Unpatched::GlobalEffectable::MOD_FX_RATE,
	                                       writeAutomation, false, valuesForOverride);

	ModControllableAudio::writeParamAttributesToFile(paramManager, writeAutomation, valuesForOverride);
}

void GlobalEffectable::writeParamTagsToFile(ParamManager* paramManager, bool writeAutomation,
                                            int32_t* valuesForOverride) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	storageManager.writeOpeningTagBeginning("delay");
	unpatchedParams->writeParamAsAttribute("rate", Param::Unpatched::GlobalEffectable::DELAY_RATE, writeAutomation,
	                                       false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute("feedback", Param::Unpatched::GlobalEffectable::DELAY_AMOUNT,
	                                       writeAutomation, false, valuesForOverride);
	storageManager.closeTag();

	storageManager.writeOpeningTagBeginning("lpf");
	unpatchedParams->writeParamAsAttribute("frequency", Param::Unpatched::GlobalEffectable::LPF_FREQ, writeAutomation,
	                                       false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute("resonance", Param::Unpatched::GlobalEffectable::LPF_RES, writeAutomation,
	                                       false, valuesForOverride);
	storageManager.closeTag();

	storageManager.writeOpeningTagBeginning("hpf");
	unpatchedParams->writeParamAsAttribute("frequency", Param::Unpatched::GlobalEffectable::HPF_FREQ, writeAutomation,
	                                       false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute("resonance", Param::Unpatched::GlobalEffectable::HPF_RES, writeAutomation,
	                                       false, valuesForOverride);
	storageManager.closeTag();

	ModControllableAudio::writeParamTagsToFile(paramManager, writeAutomation, valuesForOverride);
}

void GlobalEffectable::readParamsFromFile(ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos) {
	char const* tagName;

	while (*(tagName = storageManager.readNextTagOrAttributeName())) {
		if (readParamTagFromFile(tagName, paramManager, readAutomationUpToPos)) {}
		else {
			storageManager.exitTag(tagName);
		}
	}
}

bool GlobalEffectable::readParamTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager,
                                            int32_t readAutomationUpToPos) {

	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;

	if (!strcmp(tagName, "delay")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "rate")) {
				unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::GlobalEffectable::DELAY_RATE,
				                           readAutomationUpToPos);
				storageManager.exitTag("rate");
			}
			else if (!strcmp(tagName, "feedback")) {
				unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::GlobalEffectable::DELAY_AMOUNT,
				                           readAutomationUpToPos);
				storageManager.exitTag("feedback");
			}
		}
		storageManager.exitTag("delay");
	}

	else if (!strcmp(tagName, "lpf")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "frequency")) {
				unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::GlobalEffectable::LPF_FREQ,
				                           readAutomationUpToPos);
				storageManager.exitTag("frequency");
			}
			else if (!strcmp(tagName, "resonance")) {
				unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::GlobalEffectable::LPF_RES,
				                           readAutomationUpToPos);
				storageManager.exitTag("resonance");
			}
		}
		storageManager.exitTag("lpf");
	}

	else if (!strcmp(tagName, "hpf")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "frequency")) {
				unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::GlobalEffectable::HPF_FREQ,
				                           readAutomationUpToPos);
				storageManager.exitTag("frequency");
			}
			else if (!strcmp(tagName, "resonance")) {
				unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::GlobalEffectable::HPF_RES,
				                           readAutomationUpToPos);
				storageManager.exitTag("resonance");
			}
		}
		storageManager.exitTag("hpf");
	}

	else if (!strcmp(tagName, "reverbAmount")) {
		unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT,
		                           readAutomationUpToPos);
		storageManager.exitTag("reverbAmount");
	}

	else if (!strcmp(tagName, "volume")) {
		unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::GlobalEffectable::VOLUME,
		                           readAutomationUpToPos);
		storageManager.exitTag("volume");
	}

	else if (!strcmp(tagName, "sidechainCompressorVolume")) {
		unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::GlobalEffectable::SIDECHAIN_VOLUME,
		                           readAutomationUpToPos);
		storageManager.exitTag("sidechainCompressorVolume");
	}

	else if (!strcmp(tagName, "sidechainCompressorShape")) {
		unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::COMPRESSOR_SHAPE, readAutomationUpToPos);
		storageManager.exitTag("sidechainCompressorShape");
	}

	else if (!strcmp(tagName, "pan")) {
		unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::GlobalEffectable::PAN,
		                           readAutomationUpToPos);
		storageManager.exitTag("pan");
	}

	else if (!strcmp(tagName, "pitchAdjust")) {
		unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::GlobalEffectable::PITCH_ADJUST,
		                           readAutomationUpToPos);
		storageManager.exitTag("pitchAdjust");
	}

	else if (!strcmp(tagName, "modFXDepth")) {
		unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH,
		                           readAutomationUpToPos);
		storageManager.exitTag("modFXDepth");
	}

	else if (!strcmp(tagName, "modFXRate")) {
		unpatchedParams->readParam(unpatchedParamsSummary, Param::Unpatched::GlobalEffectable::MOD_FX_RATE,
		                           readAutomationUpToPos);
		storageManager.exitTag("modFXRate");
	}

	else if (ModControllableAudio::readParamTagFromFile(tagName, paramManager, readAutomationUpToPos)) {}

	else {
		return false;
	}

	return true;
}

// paramManager is optional
int32_t GlobalEffectable::readTagFromFile(char const* tagName, ParamManagerForTimeline* paramManager,
                                          int32_t readAutomationUpToPos, Song* song) {

	// This is here for compatibility only for people (Lou and Ian) who saved songs with firmware in September 2016
	//if (paramManager && strcmp(tagName, "delay") && GlobalEffectable::readParamTagFromFile(tagName, paramManager, readAutomation)) {}

	//else
	if (paramManager && !strcmp(tagName, "defaultParams")) {

		if (!paramManager->containsAnyMainParamCollections()) {
			int32_t error = paramManager->setupUnpatched();
			if (error) {
				return error;
			}
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
	if (!unpatchedParams->params[Param::Unpatched::GlobalEffectable::LPF_FREQ].isAutomated()
	    && unpatchedParams->params[Param::Unpatched::GlobalEffectable::LPF_FREQ].getCurrentValue() >= 2147483602
	    && !unpatchedParams->params[Param::Unpatched::GlobalEffectable::LPF_RES].containsSomething(0)) {
		unpatchedParams->params[Param::Unpatched::GlobalEffectable::LPF_RES].currentValue = -2147483648;
	}

	// If no HPF on, and resonance is at 25%, set it to 0%
	if (!unpatchedParams->params[Param::Unpatched::GlobalEffectable::HPF_FREQ].containsSomething(-2147483648)
	    && !unpatchedParams->params[Param::Unpatched::GlobalEffectable::LPF_RES].containsSomething(-1073741824)) {
		unpatchedParams->params[Param::Unpatched::GlobalEffectable::HPF_RES].currentValue = -2147483648;
	}
}

ModFXType GlobalEffectable::getActiveModFXType(ParamManager* paramManager) {
	ModFXType modFXTypeNow = modFXType;

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	if ((currentModFXParam == ModFXParam::DEPTH
	     && unpatchedParams->getValue(Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH) == -2147483648)
	    || (currentModFXParam == ModFXParam::FEEDBACK
	        && unpatchedParams->getValue(Param::Unpatched::MOD_FX_FEEDBACK) == -2147483648)
	    || (currentModFXParam == ModFXParam::OFFSET
	        && unpatchedParams->getValue(Param::Unpatched::MOD_FX_OFFSET) == -2147483648)) {
		modFXTypeNow = ModFXType::NONE;
	}
	return modFXTypeNow;
}

void GlobalEffectable::setupDelayWorkingState(DelayWorkingState* delayWorkingState, ParamManager* paramManager,
                                              bool shouldLimitDelayFeedback) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	delayWorkingState->delayFeedbackAmount = getFinalParameterValueLinear(
	    paramNeutralValues[Param::Global::DELAY_FEEDBACK],
	    cableToLinearParamShortcut(unpatchedParams->getValue(Param::Unpatched::GlobalEffectable::DELAY_AMOUNT)));
	if (shouldLimitDelayFeedback) {
		delayWorkingState->delayFeedbackAmount =
		    std::min(delayWorkingState->delayFeedbackAmount, (int32_t)(1 << 30) - (1 << 26));
	}
	delayWorkingState->userDelayRate = getFinalParameterValueExp(
	    paramNeutralValues[Param::Global::DELAY_RATE],
	    cableToExpParamShortcut(unpatchedParams->getValue(Param::Unpatched::GlobalEffectable::DELAY_RATE)));
	delay.setupWorkingState(delayWorkingState);
}

void GlobalEffectable::processFXForGlobalEffectable(StereoSample* inputBuffer, int32_t numSamples,
                                                    int32_t* postFXVolume, ParamManager* paramManager,
                                                    DelayWorkingState* delayWorkingState,
                                                    int32_t analogDelaySaturationAmount) {

	StereoSample* inputBufferEnd = inputBuffer + numSamples;

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	int32_t modFXRate = getFinalParameterValueExp(
	    paramNeutralValues[Param::Global::MOD_FX_RATE],
	    cableToExpParamShortcut(unpatchedParams->getValue(Param::Unpatched::GlobalEffectable::MOD_FX_RATE)));
	int32_t modFXDepth = getFinalParameterValueVolume(
	    paramNeutralValues[Param::Global::MOD_FX_DEPTH],
	    cableToLinearParamShortcut(unpatchedParams->getValue(Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH)));

	ModFXType modFXTypeNow = getActiveModFXType(paramManager);

	// For GlobalEffectables, mod FX buffer memory is allocated here in the rendering routine - this might seem strange, but
	// it's because unlike for Sounds, the effect can be switched on and off by changing a parameter like "depth".
	if (modFXTypeNow == ModFXType::FLANGER || modFXTypeNow == ModFXType::CHORUS
	    || modFXTypeNow == ModFXType::CHORUS_STEREO) {
		if (!modFXBuffer) {
			modFXBuffer = (StereoSample*)GeneralMemoryAllocator::get().alloc(kModFXBufferSize * sizeof(StereoSample),
			                                                                 NULL, false, true);
			if (!modFXBuffer) {
				modFXTypeNow = ModFXType::NONE;
			}
			else {
				memset(modFXBuffer, 0, kModFXBufferSize * sizeof(StereoSample));
			}
		}
		if (modFXGrainBuffer) {
			GeneralMemoryAllocator::get().dealloc(modFXGrainBuffer);
			modFXGrainBuffer = NULL;
		}
	}
	else if (modFXTypeNow == ModFXType::GRAIN) {
		if (!modFXGrainBuffer) {
			modFXGrainBuffer = (StereoSample*)GeneralMemoryAllocator::get().alloc(
			    kModFXGrainBufferSize * sizeof(StereoSample), NULL, false, true);
			if (!modFXGrainBuffer) {
				modFXTypeNow = ModFXType::NONE;
			}
			for (int i = 0; i < 8; i++) {
				grains[i].length = 0;
			}
			grainInitialized = false;
			modFXGrainBufferWriteIndex = 0;
		}
		if (modFXBuffer) {
			GeneralMemoryAllocator::get().dealloc(modFXBuffer);
			modFXBuffer = NULL;
		}
	}
	else {
		if (modFXBuffer) {
			GeneralMemoryAllocator::get().dealloc(modFXBuffer);
			modFXBuffer = NULL;
		}
		if (modFXGrainBuffer) {
			GeneralMemoryAllocator::get().dealloc(modFXGrainBuffer);
			modFXGrainBuffer = NULL;
		}
	}

	processFX(inputBuffer, numSamples, modFXTypeNow, modFXRate, modFXDepth, delayWorkingState, postFXVolume,
	          paramManager, analogDelaySaturationAmount);
}

char const* GlobalEffectable::paramToString(uint8_t param) {

	switch (param) {

		// Unpatched params just for GlobalEffectables

	case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::MOD_FX_RATE:
		return "modFXRate";

	case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::MOD_FX_DEPTH:
		return "modFXDepth";

	case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::DELAY_RATE:
		return "delayRate";

	case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::DELAY_AMOUNT:
		return "delayFeedback";

	case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::PAN:
		return "pan";

	case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::LPF_FREQ:
		return "lpfFrequency";

	case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::LPF_RES:
		return "lpfResonance";

	case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::HPF_FREQ:
		return "hpfFrequency";

	case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::HPF_RES:
		return "hpfResonance";

	case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::REVERB_SEND_AMOUNT:
		return "reverbAmount";

	case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::VOLUME:
		return "volume";

	case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::SIDECHAIN_VOLUME:
		return "sidechainCompressorVolume";

	case Param::Unpatched::START + Param::Unpatched::GlobalEffectable::PITCH_ADJUST:
		return "pitchAdjust";

	default:
		return ModControllableAudio::paramToString(param);
	}
}

int32_t GlobalEffectable::stringToParam(char const* string) {
	for (int32_t p = Param::Unpatched::START + Param::Unpatched::NUM_SHARED;
	     p < Param::Unpatched::START + kMaxNumUnpatchedParams; p++) {
		if (!strcmp(string, GlobalEffectable::paramToString(p))) {
			return p;
		}
	}
	return ModControllableAudio::stringToParam(string);
}
