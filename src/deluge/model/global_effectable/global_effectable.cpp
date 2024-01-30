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
#include "gui/l10n/l10n.h"
#include "gui/views/performance_session_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/model_stack.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/storage_manager.h"
#include "util/misc.h"
#include <new>

using namespace deluge;
namespace params = deluge::modulation::params;

GlobalEffectable::GlobalEffectable() {
	unpatchedParamKind_ = params::Kind::UNPATCHED_GLOBAL;
	lpfMode = FilterMode::TRANSISTOR_24DB;
	filterSet.reset();

	modFXType = ModFXType::NONE;
	currentModFXParam = ModFXParam::FEEDBACK;
	currentFilterType = FilterType::LPF;

	memset(allpassMemory, 0, sizeof(allpassMemory));
	memset(&phaserMemory, 0, sizeof(phaserMemory));
	editingComp = false;
	currentCompParam = CompParam::RATIO;
}

void GlobalEffectable::cloneFrom(ModControllableAudio* other) {
	ModControllableAudio::cloneFrom(other);

	currentModFXParam = ((GlobalEffectable*)other)->currentModFXParam;
	currentFilterType = ((GlobalEffectable*)other)->currentFilterType;
}

void GlobalEffectable::initParams(ParamManager* paramManager) {
	ModControllableAudio::initParams(paramManager);

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->kind = deluge::modulation::params::Kind::UNPATCHED_GLOBAL;

	unpatchedParams->params[params::UNPATCHED_MOD_FX_RATE].setCurrentValueBasicForSetup(-536870912);
	unpatchedParams->params[params::UNPATCHED_MOD_FX_FEEDBACK].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_MOD_FX_DEPTH].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[params::UNPATCHED_DELAY_RATE].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[params::UNPATCHED_PAN].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[params::UNPATCHED_DELAY_AMOUNT].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_REVERB_SEND_AMOUNT].setCurrentValueBasicForSetup(-2147483648);

	unpatchedParams->params[params::UNPATCHED_VOLUME].setCurrentValueBasicForSetup(
	    889516852); // 3 quarters of the way up
	unpatchedParams->params[params::UNPATCHED_SIDECHAIN_VOLUME].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_PITCH_ADJUST].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[params::UNPATCHED_LPF_RES].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_LPF_FREQ].setCurrentValueBasicForSetup(2147483647);

	unpatchedParams->params[params::UNPATCHED_HPF_RES].setCurrentValueBasicForSetup(-2147483648);
	unpatchedParams->params[params::UNPATCHED_HPF_FREQ].setCurrentValueBasicForSetup(-2147483648);
}

void GlobalEffectable::initParamsForAudioClip(ParamManagerForTimeline* paramManager) {
	initParams(paramManager);
	paramManager->getUnpatchedParamSet()->params[params::UNPATCHED_VOLUME].setCurrentValueBasicForSetup(-536870912);
}

void GlobalEffectable::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) {

	// leave stutter running in perfomance session view
	if (getRootUI() != &performanceSessionView) {
		// If we're leaving this mod function or anything else is happening, we want to be sure that stutter has stopped
		endStutter(paramManager);
	}

	// LPF/HPF/EQ
	if (whichModButton == 1) {
		currentFilterType = static_cast<FilterType>(util::to_underlying(currentFilterType) % kNumFilterTypes);
		switch (currentFilterType) {
		case FilterType::LPF:
			displayLPFMode(on);
			break;

		case FilterType::HPF:
			displayHPFMode(on);
			break;

		case FilterType::EQ:
			if (on) {
				display->popupText(deluge::l10n::get(deluge::l10n::String::STRING_FOR_EQ));
			}
			else {
				display->cancelPopup();
			}
			break;
		}
	}
	// Delay
	else if (whichModButton == 3) {
		displayDelaySettings(on);
	}
	// Compressor / Reverb
	else if (whichModButton == 4) {
		displayCompressorAndReverbSettings(on);
	}
	// Mod FX
	else if (whichModButton == 5) {
		displayModFXSettings(on);
	}
}

void GlobalEffectable::displayCompressorAndReverbSettings(bool on) {
	if (display->haveOLED()) {
		if (on) {
			DEF_STACK_STRING_BUF(popupMsg, 100);
			popupMsg.append("Comp Mode: ");
			popupMsg.append(getCompressorModeDisplayName());
			popupMsg.append("\n");

			if (editingComp) {
				popupMsg.append("Comp Param: ");
				popupMsg.append(getCompressorParamDisplayName());
			}
			else {
				// Reverb
				popupMsg.append(view.getReverbPresetDisplayName(view.getCurrentReverbPreset()));
			}

			display->popupText(popupMsg.c_str());
		}
		else {
			display->cancelPopup();
		}
	}
	else {
		if (on) {
			display->displayPopup(getCompressorModeDisplayName());
		}
		else {
			if (editingComp) {
				display->displayPopup(getCompressorParamDisplayName());
			}
			else {
				display->displayPopup(view.getReverbPresetDisplayName(view.getCurrentReverbPreset()));
			}
		}
	}
}

char const* GlobalEffectable::getCompressorModeDisplayName() {
	return editingComp ? "FULL" : "ONE";
}

char const* GlobalEffectable::getCompressorParamDisplayName() {
	currentCompParam = static_cast<CompParam>(util::to_underlying(currentCompParam) % maxCompParam);
	const char* params[util::to_underlying(CompParam::LAST)] = {"ratio", "attack", "release", "hpf"};
	return params[int(currentCompParam)];
}

void GlobalEffectable::displayModFXSettings(bool on) {
	if (display->haveOLED()) {
		if (on) {
			DEF_STACK_STRING_BUF(popupMsg, 100);
			popupMsg.append("Type: ");
			popupMsg.append(getModFXTypeDisplayName());

			popupMsg.append("\nParam: ");
			popupMsg.append(getModFXParamDisplayName());

			display->popupText(popupMsg.c_str());
		}
		else {
			display->cancelPopup();
		}
	}
	else {
		if (on) {
			display->displayPopup(getModFXTypeDisplayName());
		}
		else {
			display->displayPopup(getModFXParamDisplayName());
		}
	}
}

char const* GlobalEffectable::getModFXTypeDisplayName() {
	auto modTypeCount =
	    (runtimeFeatureSettings.get(RuntimeFeatureSettingType::EnableGrainFX) == RuntimeFeatureStateToggle::Off)
	        ? (kNumModFXTypes - 1)
	        : kNumModFXTypes;

	modFXType = static_cast<ModFXType>(util::to_underlying(modFXType) % modTypeCount);
	if (modFXType == ModFXType::NONE) {
		modFXType = static_cast<ModFXType>(1);
	}
	switch (modFXType) {
		using enum deluge::l10n::String;
	case ModFXType::FLANGER:
		return l10n::get(STRING_FOR_FLANGER);
	case ModFXType::PHASER:
		return l10n::get(STRING_FOR_PHASER);
	case ModFXType::CHORUS:
		return l10n::get(STRING_FOR_CHORUS);
	case ModFXType::CHORUS_STEREO:
		return l10n::get(STRING_FOR_STEREO_CHORUS);
	case ModFXType::GRAIN:
		return l10n::get(STRING_FOR_GRAIN);
	default:
		return l10n::get(STRING_FOR_NONE);
	}
}

char const* GlobalEffectable::getModFXParamDisplayName() {
	currentModFXParam = static_cast<ModFXParam>(util::to_underlying(currentModFXParam) % kNumModFXParams);

	switch (currentModFXParam) {
		using enum deluge::l10n::String;
	case ModFXParam::DEPTH:
		return l10n::get(STRING_FOR_DEPTH);
	case ModFXParam::FEEDBACK:
		return l10n::get(STRING_FOR_FEEDBACK);
	case ModFXParam::OFFSET:
		return l10n::get(STRING_FOR_OFFSET);
	default:
		return l10n::get(STRING_FOR_NONE);
	}
}

// Returns whether Instrument changed
bool GlobalEffectable::modEncoderButtonAction(uint8_t whichModEncoder, bool on,
                                              ModelStackWithThreeMainThings* modelStack) {
	using enum l10n::String;
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
				auto modTypeCount = (runtimeFeatureSettings.get(RuntimeFeatureSettingType::EnableGrainFX)
				                     == RuntimeFeatureStateToggle::Off)
				                        ? (kNumModFXTypes - 1)
				                        : kNumModFXTypes;
				modFXType = static_cast<ModFXType>((util::to_underlying(modFXType) + 1) % modTypeCount);
				if (modFXType == ModFXType::NONE) {
					modFXType = static_cast<ModFXType>(1);
				}
				std::string_view displayText;
				switch (modFXType) {
				case ModFXType::FLANGER:
					displayText = l10n::getView(STRING_FOR_FLANGER);
					break;

				case ModFXType::PHASER:
					displayText = l10n::getView(STRING_FOR_PHASER);
					break;

				case ModFXType::CHORUS:
					displayText = l10n::getView(STRING_FOR_CHORUS);
					break;

				case ModFXType::CHORUS_STEREO:
					displayText = l10n::getView(STRING_FOR_STEREO_CHORUS);
					break;
				case ModFXType::GRAIN:
					displayText = l10n::getView(STRING_FOR_GRAIN);
					break;
				case ModFXType::NONE:
					__builtin_unreachable();
				}
				display->displayPopup(displayText.data());
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

				std::string_view displayText;
				switch (currentModFXParam) {
				case ModFXParam::DEPTH:
					displayText = l10n::getView(STRING_FOR_DEPTH);
					break;

				case ModFXParam::FEEDBACK:
					displayText = l10n::getView(STRING_FOR_FEEDBACK);
					break;

				case ModFXParam::OFFSET:
					displayText = l10n::getView(STRING_FOR_OFFSET);
					break;
				}
				display->displayPopup(displayText.data());
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

				std::string_view displayText;
				switch (currentFilterType) {
				case FilterType::LPF:
					displayText = l10n::getView(STRING_FOR_LPF);
					break;

				case FilterType::HPF:
					displayText = l10n::getView(STRING_FOR_HPF);
					break;

				case FilterType::EQ:
					displayText = l10n::getView(STRING_FOR_EQ);
					break;
				}
				display->displayPopup(displayText.data());
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
				// if we're in full move/editingComp then we cycle through the comp params
				// otherwise cycle reverb sizes
				if (!editingComp) {
					view.cycleThroughReverbPresets();
				}
				else {
					currentCompParam =
					    static_cast<CompParam>((util::to_underlying(currentCompParam) + 1) % maxCompParam);
					display->displayPopup(getCompressorParamDisplayName());
				}
			}
		}
		else {
			if (on) {
				editingComp = !editingComp;
				display->displayPopup(getCompressorModeDisplayName());
			}
		}

		return false;
	}

	return false; // Some cases could lead here
}

int32_t GlobalEffectable::getKnobPosForNonExistentParam(int32_t whichModEncoder, ModelStackWithAutoParam* modelStack) {
	int current = 0;
	if (*getModKnobMode() == 4) {

		// this is only reachable in comp editing mode, otherwise it's an existent param
		if (whichModEncoder == 1) { // sidechain (threshold)
			current = (AudioEngine::mastercompressor.getThreshold() >> 24);
		}
		else if (whichModEncoder == 0) {
			switch (currentCompParam) {

			case CompParam::RATIO:
				current = (AudioEngine::mastercompressor.getRatio() >> 24);

				break;

			case CompParam::ATTACK:
				current = AudioEngine::mastercompressor.getAttack() >> 24;

				break;

			case CompParam::RELEASE:
				current = AudioEngine::mastercompressor.getRelease() >> 24;

				break;

			case CompParam::SIDECHAIN:
				current = AudioEngine::mastercompressor.getSidechain() >> 24;
				break;
			}
		}
	}
	return current - 64;
}

ActionResult GlobalEffectable::modEncoderActionForNonExistentParam(int32_t offset, int32_t whichModEncoder,
                                                                   ModelStackWithAutoParam* modelStack) {
	if (*getModKnobMode() == 4) {
		int current;
		int displayLevel;
		int ledLevel;
		// this is only reachable in comp editing mode, otherwise it's an existent param
		if (whichModEncoder == 1) { // sidechain (threshold)
			current = (AudioEngine::mastercompressor.getThreshold() >> 24) - 64;
			current += offset;
			current = std::clamp(current, -64, 64);
			ledLevel = (64 + current);
			displayLevel = ((ledLevel)*kMaxMenuValue) / 128;
			AudioEngine::mastercompressor.setThreshold(lshiftAndSaturate<24>(current + 64));
			indicator_leds::setKnobIndicatorLevel(1, ledLevel);
		}
		else if (whichModEncoder == 0) {
			switch (currentCompParam) {

			case CompParam::RATIO:
				current = (AudioEngine::mastercompressor.getRatio() >> 24) - 64;
				current += offset;
				// this range is ratio of 2 to 20
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);
				displayLevel = ((ledLevel)*kMaxMenuValue) / 128;

				displayLevel = AudioEngine::mastercompressor.setRatio(lshiftAndSaturate<24>(current + 64));
				break;

			case CompParam::ATTACK:
				current = (AudioEngine::mastercompressor.getAttack() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				displayLevel = AudioEngine::mastercompressor.setAttack(lshiftAndSaturate<24>(current + 64));
				break;

			case CompParam::RELEASE:
				current = (AudioEngine::mastercompressor.getRelease() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				displayLevel = AudioEngine::mastercompressor.setRelease(lshiftAndSaturate<24>(current + 64));
				break;

			case CompParam::SIDECHAIN:
				current = (AudioEngine::mastercompressor.getSidechain() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				displayLevel = AudioEngine::mastercompressor.setSidechain(lshiftAndSaturate<24>(current + 64));
				break;
			}
			indicator_leds::setKnobIndicatorLevel(0, ledLevel);
		}
		char buffer[5];
		intToString(displayLevel, buffer);
		display->displayPopup(buffer);

		return ActionResult::DEALT_WITH;
	}
	return ActionResult::NOT_DEALT_WITH;
}
// Always check this doesn't return NULL!
int32_t GlobalEffectable::getParameterFromKnob(int32_t whichModEncoder) {

	int32_t modKnobMode = *getModKnobMode();

	if (modKnobMode == 0) {
		if (whichModEncoder != 0) {
			return params::UNPATCHED_VOLUME;
		}
		else {
			return params::UNPATCHED_PAN;
		}
	}
	else if (modKnobMode == 1) {
		switch (currentFilterType) {
		case FilterType::LPF:
			if (whichModEncoder != 0) {
				return params::UNPATCHED_LPF_FREQ;
			}
			else {
				return params::UNPATCHED_LPF_RES;
			}
		case FilterType::HPF:
			if (whichModEncoder != 0) {
				return params::UNPATCHED_HPF_FREQ;
			}
			else {
				return params::UNPATCHED_HPF_RES;
			}
		default: // case FilterType::EQ:
			if (whichModEncoder != 0) {
				return params::UNPATCHED_TREBLE;
			}
			else {
				return params::UNPATCHED_BASS;
			}
		}
	}
	else if (modKnobMode == 3) {
		if (whichModEncoder != 0) {
			return params::UNPATCHED_DELAY_RATE;
		}
		else {
			return params::UNPATCHED_DELAY_AMOUNT;
		}
	}

	else if (modKnobMode == 4) {
		if (whichModEncoder == 0 && !editingComp) {
			return params::UNPATCHED_REVERB_SEND_AMOUNT;
		}
	}
	else if (modKnobMode == 5) {
		if (whichModEncoder != 0) {
			return params::UNPATCHED_MOD_FX_RATE;
		}
		else {
			if (currentModFXParam == ModFXParam::DEPTH) {
				return params::UNPATCHED_MOD_FX_DEPTH;
			}
			else if (currentModFXParam == ModFXParam::OFFSET) {
				return params::UNPATCHED_MOD_FX_OFFSET;
			}
			else {
				return params::UNPATCHED_MOD_FX_FEEDBACK;
			}
		}
	}
	else if (modKnobMode == 6) {
		if (whichModEncoder != 0) {
			return params::UNPATCHED_STUTTER_RATE;
		}
	}
	else if (modKnobMode == 7) {
		if (whichModEncoder != 0) {
			return params::UNPATCHED_SAMPLE_RATE_REDUCTION;
		}
		else {
			return params::UNPATCHED_BITCRUSHING;
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

	int32_t lpfFrequency =
	    getFinalParameterValueExp(paramNeutralValues[params::LOCAL_LPF_FREQ],
	                              cableToExpParamShortcut(unpatchedParams->getValue(params::UNPATCHED_LPF_FREQ)));
	int32_t lpfResonance =
	    getFinalParameterValueLinear(paramNeutralValues[params::LOCAL_LPF_RESONANCE],
	                                 cableToLinearParamShortcut(unpatchedParams->getValue(params::UNPATCHED_LPF_RES)));

	int32_t hpfFrequency =
	    getFinalParameterValueExp(paramNeutralValues[params::LOCAL_HPF_FREQ],
	                              cableToExpParamShortcut(unpatchedParams->getValue(params::UNPATCHED_HPF_FREQ)));
	int32_t hpfResonance =
	    getFinalParameterValueLinear(paramNeutralValues[params::LOCAL_HPF_RESONANCE],
	                                 cableToLinearParamShortcut(unpatchedParams->getValue(params::UNPATCHED_HPF_RES)));

	bool doLPF = (lpfMode == FilterMode::TRANSISTOR_24DB_DRIVE
	              || unpatchedParams->getValue(params::UNPATCHED_LPF_FREQ) < 2147483602);
	bool doHPF = unpatchedParams->getValue(params::UNPATCHED_HPF_FREQ) != -2147483648;

	// no morph for global effectable
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

	unpatchedParams->writeParamAsAttribute("reverbAmount", params::UNPATCHED_REVERB_SEND_AMOUNT, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("volume", params::UNPATCHED_VOLUME, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("pan", params::UNPATCHED_PAN, writeAutomation, false, valuesForOverride);

	if (unpatchedParams->params[params::UNPATCHED_PITCH_ADJUST].containsSomething(0)) {
		unpatchedParams->writeParamAsAttribute("pitchAdjust", params::UNPATCHED_PITCH_ADJUST, writeAutomation, false,
		                                       valuesForOverride);
	}

	if (unpatchedParams->params[params::UNPATCHED_SIDECHAIN_VOLUME].containsSomething(-2147483648)) {
		unpatchedParams->writeParamAsAttribute("sidechainCompressorVolume", params::UNPATCHED_SIDECHAIN_VOLUME,
		                                       writeAutomation, false, valuesForOverride);
	}

	unpatchedParams->writeParamAsAttribute("sidechainCompressorShape", params::UNPATCHED_COMPRESSOR_SHAPE,
	                                       writeAutomation, false, valuesForOverride);

	unpatchedParams->writeParamAsAttribute("modFXDepth", params::UNPATCHED_MOD_FX_DEPTH, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("modFXRate", params::UNPATCHED_MOD_FX_RATE, writeAutomation, false,
	                                       valuesForOverride);

	ModControllableAudio::writeParamAttributesToFile(paramManager, writeAutomation, valuesForOverride);
}

void GlobalEffectable::writeParamTagsToFile(ParamManager* paramManager, bool writeAutomation,
                                            int32_t* valuesForOverride) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	storageManager.writeOpeningTagBeginning("delay");
	unpatchedParams->writeParamAsAttribute("rate", params::UNPATCHED_DELAY_RATE, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("feedback", params::UNPATCHED_DELAY_AMOUNT, writeAutomation, false,
	                                       valuesForOverride);
	storageManager.closeTag();

	storageManager.writeOpeningTagBeginning("lpf");
	unpatchedParams->writeParamAsAttribute("frequency", params::UNPATCHED_LPF_FREQ, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("resonance", params::UNPATCHED_LPF_RES, writeAutomation, false,
	                                       valuesForOverride);
	storageManager.closeTag();

	storageManager.writeOpeningTagBeginning("hpf");
	unpatchedParams->writeParamAsAttribute("frequency", params::UNPATCHED_HPF_FREQ, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute("resonance", params::UNPATCHED_HPF_RES, writeAutomation, false,
	                                       valuesForOverride);
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
				unpatchedParams->readParam(unpatchedParamsSummary, params::UNPATCHED_DELAY_RATE, readAutomationUpToPos);
				storageManager.exitTag("rate");
			}
			else if (!strcmp(tagName, "feedback")) {
				unpatchedParams->readParam(unpatchedParamsSummary, params::UNPATCHED_DELAY_AMOUNT,
				                           readAutomationUpToPos);
				storageManager.exitTag("feedback");
			}
		}
		storageManager.exitTag("delay");
	}

	else if (!strcmp(tagName, "lpf")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "frequency")) {
				unpatchedParams->readParam(unpatchedParamsSummary, params::UNPATCHED_LPF_FREQ, readAutomationUpToPos);
				storageManager.exitTag("frequency");
			}
			else if (!strcmp(tagName, "resonance")) {
				unpatchedParams->readParam(unpatchedParamsSummary, params::UNPATCHED_LPF_RES, readAutomationUpToPos);
				storageManager.exitTag("resonance");
			}
		}
		storageManager.exitTag("lpf");
	}

	else if (!strcmp(tagName, "hpf")) {
		while (*(tagName = storageManager.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "frequency")) {
				unpatchedParams->readParam(unpatchedParamsSummary, params::UNPATCHED_HPF_FREQ, readAutomationUpToPos);
				storageManager.exitTag("frequency");
			}
			else if (!strcmp(tagName, "resonance")) {
				unpatchedParams->readParam(unpatchedParamsSummary, params::UNPATCHED_HPF_RES, readAutomationUpToPos);
				storageManager.exitTag("resonance");
			}
		}
		storageManager.exitTag("hpf");
	}

	else if (!strcmp(tagName, "reverbAmount")) {
		unpatchedParams->readParam(unpatchedParamsSummary, params::UNPATCHED_REVERB_SEND_AMOUNT, readAutomationUpToPos);
		storageManager.exitTag("reverbAmount");
	}

	else if (!strcmp(tagName, "volume")) {
		unpatchedParams->readParam(unpatchedParamsSummary, params::UNPATCHED_VOLUME, readAutomationUpToPos);
		storageManager.exitTag("volume");
	}

	else if (!strcmp(tagName, "sidechainCompressorVolume")) {
		unpatchedParams->readParam(unpatchedParamsSummary, params::UNPATCHED_SIDECHAIN_VOLUME, readAutomationUpToPos);
		storageManager.exitTag("sidechainCompressorVolume");
	}

	else if (!strcmp(tagName, "sidechainCompressorShape")) {
		unpatchedParams->readParam(unpatchedParamsSummary, params::UNPATCHED_COMPRESSOR_SHAPE, readAutomationUpToPos);
		storageManager.exitTag("sidechainCompressorShape");
	}

	else if (!strcmp(tagName, "pan")) {
		unpatchedParams->readParam(unpatchedParamsSummary, params::UNPATCHED_PAN, readAutomationUpToPos);
		storageManager.exitTag("pan");
	}

	else if (!strcmp(tagName, "pitchAdjust")) {
		unpatchedParams->readParam(unpatchedParamsSummary, params::UNPATCHED_PITCH_ADJUST, readAutomationUpToPos);
		storageManager.exitTag("pitchAdjust");
	}

	else if (!strcmp(tagName, "modFXDepth")) {
		unpatchedParams->readParam(unpatchedParamsSummary, params::UNPATCHED_MOD_FX_DEPTH, readAutomationUpToPos);
		storageManager.exitTag("modFXDepth");
	}

	else if (!strcmp(tagName, "modFXRate")) {
		unpatchedParams->readParam(unpatchedParamsSummary, params::UNPATCHED_MOD_FX_RATE, readAutomationUpToPos);
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
	// if (paramManager && strcmp(tagName, "delay") && GlobalEffectable::readParamTagFromFile(tagName, paramManager,
	// readAutomation)) {}

	// else
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

// Before calling this, check that (storageManager.firmwareVersionOfFileBeingRead < FIRMWARE_1P2P0 &&
// !paramManager->resonanceBackwardsCompatibilityProcessed)
void GlobalEffectable::compensateVolumeForResonance(ParamManagerForTimeline* paramManager) {

	paramManager->resonanceBackwardsCompatibilityProcessed = true;

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	// If no LPF on, and resonance is at 50%, set it to 0%
	if (!unpatchedParams->params[params::UNPATCHED_LPF_FREQ].isAutomated()
	    && unpatchedParams->params[params::UNPATCHED_LPF_FREQ].getCurrentValue() >= 2147483602
	    && !unpatchedParams->params[params::UNPATCHED_LPF_RES].containsSomething(0)) {
		unpatchedParams->params[params::UNPATCHED_LPF_RES].currentValue = -2147483648;
	}

	// If no HPF on, and resonance is at 25%, set it to 0%
	if (!unpatchedParams->params[params::UNPATCHED_HPF_FREQ].containsSomething(-2147483648)
	    && !unpatchedParams->params[params::UNPATCHED_LPF_RES].containsSomething(-1073741824)) {
		unpatchedParams->params[params::UNPATCHED_HPF_RES].currentValue = -2147483648;
	}
}

ModFXType GlobalEffectable::getActiveModFXType(ParamManager* paramManager) {
	ModFXType modFXTypeNow = modFXType;

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	if ((currentModFXParam == ModFXParam::DEPTH
	     && unpatchedParams->getValue(params::UNPATCHED_MOD_FX_DEPTH) == -2147483648)
	    || (currentModFXParam == ModFXParam::FEEDBACK
	        && unpatchedParams->getValue(params::UNPATCHED_MOD_FX_FEEDBACK) == -2147483648)
	    || (currentModFXParam == ModFXParam::OFFSET
	        && unpatchedParams->getValue(params::UNPATCHED_MOD_FX_OFFSET) == -2147483648)) {
		modFXTypeNow = ModFXType::NONE;
	}
	return modFXTypeNow;
}

void GlobalEffectable::setupDelayWorkingState(DelayWorkingState* delayWorkingState, ParamManager* paramManager,
                                              bool shouldLimitDelayFeedback, bool soundComingIn) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	delayWorkingState->delayFeedbackAmount = getFinalParameterValueLinear(
	    paramNeutralValues[params::GLOBAL_DELAY_FEEDBACK],
	    cableToLinearParamShortcut(unpatchedParams->getValue(params::UNPATCHED_DELAY_AMOUNT)));
	if (shouldLimitDelayFeedback) {
		delayWorkingState->delayFeedbackAmount =
		    std::min(delayWorkingState->delayFeedbackAmount, (int32_t)(1 << 30) - (1 << 26));
	}
	delayWorkingState->userDelayRate =
	    getFinalParameterValueExp(paramNeutralValues[params::GLOBAL_DELAY_RATE],
	                              cableToExpParamShortcut(unpatchedParams->getValue(params::UNPATCHED_DELAY_RATE)));
	uint32_t timePerTickInverse = playbackHandler.getTimePerInternalTickInverse(true);
	delay.setupWorkingState(delayWorkingState, timePerTickInverse, soundComingIn);
}

void GlobalEffectable::processFXForGlobalEffectable(StereoSample* inputBuffer, int32_t numSamples,
                                                    int32_t* postFXVolume, ParamManager* paramManager,
                                                    DelayWorkingState* delayWorkingState,
                                                    int32_t analogDelaySaturationAmount, bool grainHadInput) {

	StereoSample* inputBufferEnd = inputBuffer + numSamples;

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	int32_t modFXRate =
	    getFinalParameterValueExp(paramNeutralValues[params::GLOBAL_MOD_FX_RATE],
	                              cableToExpParamShortcut(unpatchedParams->getValue(params::UNPATCHED_MOD_FX_RATE)));
	int32_t modFXDepth = getFinalParameterValueVolume(
	    paramNeutralValues[params::GLOBAL_MOD_FX_DEPTH],
	    cableToLinearParamShortcut(unpatchedParams->getValue(params::UNPATCHED_MOD_FX_DEPTH)));

	ModFXType modFXTypeNow = getActiveModFXType(paramManager);

	// For GlobalEffectables, mod FX buffer memory is allocated here in the rendering routine - this might seem strange,
	// but it's because unlike for Sounds, the effect can be switched on and off by changing a parameter like "depth".
	if (modFXTypeNow == ModFXType::FLANGER || modFXTypeNow == ModFXType::CHORUS
	    || modFXTypeNow == ModFXType::CHORUS_STEREO) {
		if (!modFXBuffer) {
			modFXBuffer =
			    (StereoSample*)GeneralMemoryAllocator::get().allocLowSpeed(kModFXBufferSize * sizeof(StereoSample));
			if (!modFXBuffer) {
				modFXTypeNow = ModFXType::NONE;
			}
			else {
				memset(modFXBuffer, 0, kModFXBufferSize * sizeof(StereoSample));
			}
		}
		if (modFXGrainBuffer) {
			delugeDealloc(modFXGrainBuffer);
			modFXGrainBuffer = NULL;
		}
	}
	else if (modFXTypeNow == ModFXType::GRAIN) {
		if (grainHadInput) {
			setWrapsToShutdown();
		}
		if (wrapsToShutdown >= 0) {
			if (!modFXGrainBuffer) {
				modFXGrainBuffer = (StereoSample*)GeneralMemoryAllocator::get().allocLowSpeed(kModFXGrainBufferSize
				                                                                              * sizeof(StereoSample));
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
				delugeDealloc(modFXBuffer);
				modFXBuffer = NULL;
			}
		}
		else if (modFXGrainBuffer) {
			delugeDealloc(modFXGrainBuffer);
			modFXGrainBuffer = NULL;
		}
	}
	else {
		if (modFXBuffer) {
			delugeDealloc(modFXBuffer);
			modFXBuffer = NULL;
		}
		if (modFXGrainBuffer) {
			delugeDealloc(modFXGrainBuffer);
			modFXGrainBuffer = NULL;
		}
	}

	processFX(inputBuffer, numSamples, modFXTypeNow, modFXRate, modFXDepth, delayWorkingState, postFXVolume,
	          paramManager, analogDelaySaturationAmount);
}
