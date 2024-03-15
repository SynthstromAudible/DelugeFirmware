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
#include "hid/led/indicator_leds.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "storage/storage_manager.h"
#include "util/firmware_version.h"

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

	unpatchedParams->params[params::UNPATCHED_VOLUME].setCurrentValueBasicForSetup(0); // half of the way up
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
		displayFilterSettings(on, currentFilterType);
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
				ensureModFXParamIsValid();

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayModFXSettings(on);
				}
				else {
					display->displayPopup(getModFXTypeDisplayName());
				}
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

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayModFXSettings(on);
				}
				else {
					display->displayPopup(getModFXParamDisplayName());
				}
				return true;
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

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayFilterSettings(on, currentFilterType);
				}
				else {
					display->displayPopup(getFilterTypeDisplayName(currentFilterType));
				}
				return true;
			}

			return false;
		}
		else {
			if (on) {
				if (currentFilterType == FilterType::LPF) {
					switchLPFMode();

					// if mod button is pressed, update mod button pop up
					if (Buttons::isButtonPressed(
					        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
						displayFilterSettings(on, currentFilterType);
					}
					else {
						display->displayPopup(getFilterModeDisplayName(currentFilterType));
					}
					return true;
				}
				else if (currentFilterType == FilterType::HPF) {
					switchHPFMode();

					// if mod button is pressed, update mod button pop up
					if (Buttons::isButtonPressed(
					        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
						displayFilterSettings(on, currentFilterType);
					}
					else {
						display->displayPopup(getFilterModeDisplayName(currentFilterType));
					}
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

					// if mod button is pressed, update mod button pop up
					if (Buttons::isButtonPressed(
					        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
						displayDelaySettings(on);
					}
					else {
						display->displayPopup(getDelaySyncTypeDisplayName());
					}
				}
				else {
					switchDelayPingPong();

					// if mod button is pressed, update mod button pop up
					if (Buttons::isButtonPressed(
					        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
						displayDelaySettings(on);
					}
					else {
						display->displayPopup(getDelayPingPongStatusDisplayName());
					}
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

					// if mod button is pressed, update mod button pop up
					if (Buttons::isButtonPressed(
					        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
						displayDelaySettings(on);
					}
					else {
						char displayName[30];
						getDelaySyncLevelDisplayName(displayName);
						display->displayPopup(displayName);
					}
				}
				else {
					switchDelayAnalog();

					// if mod button is pressed, update mod button pop up
					if (Buttons::isButtonPressed(
					        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
						displayDelaySettings(on);
					}
					else {
						display->displayPopup(getDelayTypeDisplayName());
					}
				}
				return true;
			}
			else {
				return false;
			}
		}
	}

	// Reverb / compressor section
	else if (modKnobMode == 4) {
		if (whichModEncoder == 0) { // Reverb
			if (on) {
				// if we're in full mode/editingComp then we cycle through the comp params
				// otherwise cycle reverb sizes
				if (!editingComp) {
					view.cycleThroughReverbPresets();

					// if mod button is pressed, update mod button pop up
					if (Buttons::isButtonPressed(
					        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
						displayCompressorAndReverbSettings(on);
					}
					else {
						display->displayPopup(view.getReverbPresetDisplayName(view.getCurrentReverbPreset()));
					}
				}
				else {
					currentCompParam =
					    static_cast<CompParam>((util::to_underlying(currentCompParam) + 1) % maxCompParam);

					// if mod button is pressed, update mod button pop up
					if (Buttons::isButtonPressed(
					        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
						displayCompressorAndReverbSettings(on);
					}
					else {
						display->displayPopup(getCompressorParamDisplayName());
					}
				}
			}
		}
		else {
			if (on) {
				editingComp = !editingComp;

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayCompressorAndReverbSettings(on);
				}
				else {
					display->displayPopup(getCompressorModeDisplayName());
				}
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
			current = (compressor.getThreshold() >> 24);
		}
		else if (whichModEncoder == 0) {
			switch (currentCompParam) {

			case CompParam::RATIO:
				current = (compressor.getRatio() >> 24);

				break;

			case CompParam::ATTACK:
				current = compressor.getAttack() >> 24;

				break;

			case CompParam::RELEASE:
				current = compressor.getRelease() >> 24;

				break;

			case CompParam::SIDECHAIN:
				current = compressor.getSidechain() >> 24;
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
			current = (compressor.getThreshold() >> 24) - 64;
			current += offset;
			current = std::clamp(current, -64, 64);
			ledLevel = (64 + current);
			displayLevel = ((ledLevel)*kMaxMenuValue) / 128;
			compressor.setThreshold(lshiftAndSaturate<24>(current + 64));
			indicator_leds::setKnobIndicatorLevel(1, ledLevel);
		}
		else if (whichModEncoder == 0) {
			switch (currentCompParam) {

			case CompParam::RATIO:
				current = (compressor.getRatio() >> 24) - 64;
				current += offset;
				// this range is ratio of 2 to 20
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);
				displayLevel = ((ledLevel)*kMaxMenuValue) / 128;

				displayLevel = compressor.setRatio(lshiftAndSaturate<24>(current + 64));
				break;

			case CompParam::ATTACK:
				current = (compressor.getAttack() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				displayLevel = compressor.setAttack(lshiftAndSaturate<24>(current + 64));
				break;

			case CompParam::RELEASE:
				current = (compressor.getRelease() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				displayLevel = compressor.setRelease(lshiftAndSaturate<24>(current + 64));
				break;

			case CompParam::SIDECHAIN:
				current = (compressor.getSidechain() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				displayLevel = compressor.setSidechain(lshiftAndSaturate<24>(current + 64));
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

ModFXType GlobalEffectable::getModFXType() {
	return modFXType;
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

void GlobalEffectable::writeAttributesToFile(StorageManager &bdsm, bool writeAutomation) {

	ModControllableAudio::writeAttributesToFile(bdsm);

	bdsm.writeAttribute("modFXCurrentParam", (char*)modFXParamToString(currentModFXParam));
	bdsm.writeAttribute("currentFilterType", (char*)filterTypeToString(currentFilterType));
}

void GlobalEffectable::writeTagsToFile(StorageManager &bdsm, ParamManager* paramManager, bool writeAutomation) {

	ModControllableAudio::writeTagsToFile(bdsm);

	if (paramManager) {
		bdsm.writeOpeningTagBeginning("defaultParams");
		GlobalEffectable::writeParamAttributesToFile(bdsm, paramManager, writeAutomation);
		bdsm.writeOpeningTagEnd();
		GlobalEffectable::writeParamTagsToFile(bdsm, paramManager, writeAutomation);
		bdsm.writeClosingTag("defaultParams");
	}
}

void GlobalEffectable::writeParamAttributesToFile(StorageManager &bdsm, ParamManager* paramManager, bool writeAutomation,
                                                  int32_t* valuesForOverride) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->writeParamAsAttribute(bdsm, "reverbAmount", params::UNPATCHED_REVERB_SEND_AMOUNT, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(bdsm, "volume", params::UNPATCHED_VOLUME, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(bdsm, "pan", params::UNPATCHED_PAN, writeAutomation, false, valuesForOverride);

	if (unpatchedParams->params[params::UNPATCHED_PITCH_ADJUST].containsSomething(0)) {
		unpatchedParams->writeParamAsAttribute(bdsm, "pitchAdjust", params::UNPATCHED_PITCH_ADJUST, writeAutomation, false,
		                                       valuesForOverride);
	}

	if (unpatchedParams->params[params::UNPATCHED_SIDECHAIN_VOLUME].containsSomething(-2147483648)) {
		unpatchedParams->writeParamAsAttribute(bdsm, "sidechainCompressorVolume", params::UNPATCHED_SIDECHAIN_VOLUME,
		                                       writeAutomation, false, valuesForOverride);
	}

	unpatchedParams->writeParamAsAttribute(bdsm, "sidechainCompressorShape", params::UNPATCHED_SIDECHAIN_SHAPE,
	                                       writeAutomation, false, valuesForOverride);

	unpatchedParams->writeParamAsAttribute(bdsm, "modFXDepth", params::UNPATCHED_MOD_FX_DEPTH, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(bdsm, "modFXRate", params::UNPATCHED_MOD_FX_RATE, writeAutomation, false,
	                                       valuesForOverride);

	ModControllableAudio::writeParamAttributesToFile(bdsm, paramManager, writeAutomation, valuesForOverride);
}

void GlobalEffectable::writeParamTagsToFile(StorageManager &bdsm, ParamManager* paramManager, bool writeAutomation,
                                            int32_t* valuesForOverride) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	bdsm.writeOpeningTagBeginning("delay");
	unpatchedParams->writeParamAsAttribute(bdsm, "rate", params::UNPATCHED_DELAY_RATE, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(bdsm, "feedback", params::UNPATCHED_DELAY_AMOUNT, writeAutomation, false,
	                                       valuesForOverride);
	bdsm.closeTag();

	bdsm.writeOpeningTagBeginning("lpf");
	unpatchedParams->writeParamAsAttribute(bdsm, "frequency", params::UNPATCHED_LPF_FREQ, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(bdsm, "resonance", params::UNPATCHED_LPF_RES, writeAutomation, false,
	                                       valuesForOverride);
	bdsm.closeTag();

	bdsm.writeOpeningTagBeginning("hpf");
	unpatchedParams->writeParamAsAttribute(bdsm, "frequency", params::UNPATCHED_HPF_FREQ, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(bdsm, "resonance", params::UNPATCHED_HPF_RES, writeAutomation, false,
	                                       valuesForOverride);
	bdsm.closeTag();

	ModControllableAudio::writeParamTagsToFile(bdsm, paramManager, writeAutomation, valuesForOverride);
}

void GlobalEffectable::readParamsFromFile(StorageManager &bdsm, ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos) {
	char const* tagName;

	while (*(tagName = bdsm.readNextTagOrAttributeName())) {
		if (readParamTagFromFile(bdsm, tagName, paramManager, readAutomationUpToPos)) {}
		else {
			bdsm.exitTag(tagName);
		}
	}
}

bool GlobalEffectable::readParamTagFromFile(StorageManager &bdsm, char const* tagName, ParamManagerForTimeline* paramManager,
                                            int32_t readAutomationUpToPos) {

	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;

	if (!strcmp(tagName, "delay")) {
		while (*(tagName = bdsm.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "rate")) {
				unpatchedParams->readParam(bdsm, unpatchedParamsSummary, params::UNPATCHED_DELAY_RATE, readAutomationUpToPos);
				bdsm.exitTag("rate");
			}
			else if (!strcmp(tagName, "feedback")) {
				unpatchedParams->readParam(bdsm, unpatchedParamsSummary, params::UNPATCHED_DELAY_AMOUNT,
				                           readAutomationUpToPos);
				bdsm.exitTag("feedback");
			}
		}
		bdsm.exitTag("delay");
	}

	else if (!strcmp(tagName, "lpf")) {
		while (*(tagName = bdsm.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "frequency")) {
				unpatchedParams->readParam(bdsm, unpatchedParamsSummary, params::UNPATCHED_LPF_FREQ, readAutomationUpToPos);
				bdsm.exitTag("frequency");
			}
			else if (!strcmp(tagName, "resonance")) {
				unpatchedParams->readParam(bdsm, unpatchedParamsSummary, params::UNPATCHED_LPF_RES, readAutomationUpToPos);
				bdsm.exitTag("resonance");
			}
		}
		bdsm.exitTag("lpf");
	}

	else if (!strcmp(tagName, "hpf")) {
		while (*(tagName = bdsm.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "frequency")) {
				unpatchedParams->readParam(bdsm, unpatchedParamsSummary, params::UNPATCHED_HPF_FREQ, readAutomationUpToPos);
				bdsm.exitTag("frequency");
			}
			else if (!strcmp(tagName, "resonance")) {
				unpatchedParams->readParam(bdsm, unpatchedParamsSummary, params::UNPATCHED_HPF_RES, readAutomationUpToPos);
				bdsm.exitTag("resonance");
			}
		}
		bdsm.exitTag("hpf");
	}

	else if (!strcmp(tagName, "reverbAmount")) {
		unpatchedParams->readParam(bdsm, unpatchedParamsSummary, params::UNPATCHED_REVERB_SEND_AMOUNT, readAutomationUpToPos);
		bdsm.exitTag("reverbAmount");
	}

	else if (!strcmp(tagName, "volume")) {
		unpatchedParams->readParam(bdsm, unpatchedParamsSummary, params::UNPATCHED_VOLUME, readAutomationUpToPos);
		// volume adjustment for songs saved on community 1.0.0 or later, but before version 1.1.0
		// reduces the saved song volume by approximately 21% (889516852 / 4294967295)
		if (bdsm.firmware_version >= FirmwareVersion::official({4, 1, 4, "alpha"})
		    && bdsm.firmware_version < FirmwareVersion::community({1, 0, 0})) {
			unpatchedParams->shiftParamValues(params::UNPATCHED_VOLUME, -889516852);
		}
		bdsm.exitTag("volume");
	}

	else if (!strcmp(tagName, "sidechainCompressorVolume")) {
		unpatchedParams->readParam(bdsm, unpatchedParamsSummary, params::UNPATCHED_SIDECHAIN_VOLUME, readAutomationUpToPos);
		bdsm.exitTag("sidechainCompressorVolume");
	}

	else if (!strcmp(tagName, "sidechainCompressorShape")) {
		unpatchedParams->readParam(bdsm, unpatchedParamsSummary, params::UNPATCHED_SIDECHAIN_SHAPE, readAutomationUpToPos);
		bdsm.exitTag("sidechainCompressorShape");
	}

	else if (!strcmp(tagName, "pan")) {
		unpatchedParams->readParam(bdsm, unpatchedParamsSummary, params::UNPATCHED_PAN, readAutomationUpToPos);
		bdsm.exitTag("pan");
	}

	else if (!strcmp(tagName, "pitchAdjust")) {
		unpatchedParams->readParam(bdsm, unpatchedParamsSummary, params::UNPATCHED_PITCH_ADJUST, readAutomationUpToPos);
		bdsm.exitTag("pitchAdjust");
	}

	else if (!strcmp(tagName, "modFXDepth")) {
		unpatchedParams->readParam(bdsm, unpatchedParamsSummary, params::UNPATCHED_MOD_FX_DEPTH, readAutomationUpToPos);
		bdsm.exitTag("modFXDepth");
	}

	else if (!strcmp(tagName, "modFXRate")) {
		unpatchedParams->readParam(bdsm, unpatchedParamsSummary, params::UNPATCHED_MOD_FX_RATE, readAutomationUpToPos);
		bdsm.exitTag("modFXRate");
	}

	else if (ModControllableAudio::readParamTagFromFile(bdsm, tagName, paramManager, readAutomationUpToPos)) {}

	else {
		return false;
	}

	return true;
}

// paramManager is optional
Error GlobalEffectable::readTagFromFile(StorageManager &bdsm, char const* tagName, ParamManagerForTimeline* paramManager,
                                        int32_t readAutomationUpToPos, Song* song) {

	// This is here for compatibility only for people (Lou and Ian) who saved songs with firmware in September 2016
	// if (paramManager && strcmp(tagName, "delay") && GlobalEffectable::readParamTagFromFile(tagName, paramManager,
	// readAutomation)) {}

	// else
	if (paramManager && !strcmp(tagName, "defaultParams")) {

		if (!paramManager->containsAnyMainParamCollections()) {
			Error error = paramManager->setupUnpatched();
			if (error != Error::NONE) {
				return error;
			}
			initParams(paramManager);
		}

		GlobalEffectable::readParamsFromFile(bdsm, paramManager, readAutomationUpToPos);
		bdsm.exitTag("defaultParams");
	}

	else if (!strcmp(tagName, "modFXType")) {
		modFXType = stringToFXType(bdsm.readTagOrAttributeValue());
		bdsm.exitTag("modFXType");
	}

	else if (!strcmp(tagName, "modFXCurrentParam")) {
		currentModFXParam = stringToModFXParam(bdsm.readTagOrAttributeValue());
		bdsm.exitTag("modFXCurrentParam");
	}

	else if (!strcmp(tagName, "currentFilterType")) {
		currentFilterType = stringToFilterType(bdsm.readTagOrAttributeValue());
		bdsm.exitTag("currentFilterType");
	}

	else {
		return ModControllableAudio::readTagFromFile(bdsm, tagName, NULL, readAutomationUpToPos, song);
	}

	return Error::NONE;
}

// Before calling this, check that (bdsm.firmwareVersionOfFileBeingRead < FIRMWARE_1P2P0 &&
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

Delay::State GlobalEffectable::createDelayWorkingState(ParamManager& paramManager, bool shouldLimitDelayFeedback,
                                                       bool soundComingIn) {

	Delay::State delayWorkingState;
	UnpatchedParamSet* unpatchedParams = paramManager.getUnpatchedParamSet();

	delayWorkingState.delayFeedbackAmount = getFinalParameterValueLinear(
	    paramNeutralValues[params::GLOBAL_DELAY_FEEDBACK],
	    cableToLinearParamShortcut(unpatchedParams->getValue(params::UNPATCHED_DELAY_AMOUNT)));
	if (shouldLimitDelayFeedback) {
		delayWorkingState.delayFeedbackAmount =
		    std::min(delayWorkingState.delayFeedbackAmount, (int32_t)(1 << 30) - (1 << 26));
	}
	delayWorkingState.userDelayRate =
	    getFinalParameterValueExp(paramNeutralValues[params::GLOBAL_DELAY_RATE],
	                              cableToExpParamShortcut(unpatchedParams->getValue(params::UNPATCHED_DELAY_RATE)));
	uint32_t timePerTickInverse = playbackHandler.getTimePerInternalTickInverse(true);
	delay.setupWorkingState(delayWorkingState, timePerTickInverse, soundComingIn);

	return delayWorkingState;
}

void GlobalEffectable::processFXForGlobalEffectable(StereoSample* inputBuffer, int32_t numSamples,
                                                    int32_t* postFXVolume, ParamManager* paramManager,
                                                    const Delay::State& delayWorkingState, bool grainHadInput) {

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
	          paramManager);
}
