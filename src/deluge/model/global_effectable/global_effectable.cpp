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
#include "util/comparison.h"
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
	unpatchedParams->params[params::UNPATCHED_MOD_FX_FEEDBACK].setCurrentValueBasicForSetup(NEGATIVE_ONE_Q31);
	unpatchedParams->params[params::UNPATCHED_MOD_FX_DEPTH].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[params::UNPATCHED_DELAY_RATE].setCurrentValueBasicForSetup(0);
	unpatchedParams->params[params::UNPATCHED_PAN].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[params::UNPATCHED_DELAY_AMOUNT].setCurrentValueBasicForSetup(NEGATIVE_ONE_Q31);
	unpatchedParams->params[params::UNPATCHED_REVERB_SEND_AMOUNT].setCurrentValueBasicForSetup(NEGATIVE_ONE_Q31);

	unpatchedParams->params[params::UNPATCHED_VOLUME].setCurrentValueBasicForSetup(889516852); // 3/4 of the way up
	unpatchedParams->params[params::UNPATCHED_SIDECHAIN_VOLUME].setCurrentValueBasicForSetup(NEGATIVE_ONE_Q31);
	unpatchedParams->params[params::UNPATCHED_PITCH_ADJUST].setCurrentValueBasicForSetup(0);

	unpatchedParams->params[params::UNPATCHED_LPF_RES].setCurrentValueBasicForSetup(NEGATIVE_ONE_Q31);
	unpatchedParams->params[params::UNPATCHED_LPF_FREQ].setCurrentValueBasicForSetup(ONE_Q31);

	unpatchedParams->params[params::UNPATCHED_HPF_RES].setCurrentValueBasicForSetup(NEGATIVE_ONE_Q31);
	unpatchedParams->params[params::UNPATCHED_HPF_FREQ].setCurrentValueBasicForSetup(NEGATIVE_ONE_Q31);

	unpatchedParams->params[params::UNPATCHED_LPF_MORPH].setCurrentValueBasicForSetup(NEGATIVE_ONE_Q31);
	unpatchedParams->params[params::UNPATCHED_HPF_MORPH].setCurrentValueBasicForSetup(NEGATIVE_ONE_Q31);
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
	// Other Mod Buttons
	else {
		// Env Attack / Release not relevant for global effectable context
		if (whichModButton != 2) {
			displayOtherModKnobSettings(whichModButton, on);
		}
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
	const char* params[util::to_underlying(CompParam::LAST)] = {"ratio", "attack", "release", "hpf", "blend"};
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
	auto modTypeCount = kNumModFXTypes;

	modFXType = static_cast<ModFXType>(util::to_underlying(modFXType) % modTypeCount);
	if (modFXType == ModFXType::NONE) {
		modFXType = static_cast<ModFXType>(1);
	}
	return modfx::modFXToString(modFXType);
}

char const* GlobalEffectable::getModFXParamDisplayName() {
	currentModFXParam = static_cast<ModFXParam>(util::to_underlying(currentModFXParam) % kNumModFXParams);

	return modfx::getParamName(modFXType, currentModFXParam);
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
				auto modTypeCount = kNumModFXTypes;
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

			case CompParam::BLEND:
				current = compressor.getBlend() >> 24;
				break;
			}
		}
	}
	return current - 64;
}

ActionResult GlobalEffectable::modEncoderActionForNonExistentParam(int32_t offset, int32_t whichModEncoder,
                                                                   ModelStackWithAutoParam* modelStack) {
	if (*getModKnobMode() == 4) {
		DEF_STACK_STRING_BUF(popupMsg, 40);
		int current;
		int displayLevel;
		int ledLevel;
		const char* unit;
		// this is only reachable in comp editing mode, otherwise it's an existent param
		if (whichModEncoder == 1) { // sidechain (threshold)
			if (display->haveOLED()) {
				popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_THRESHOLD));
			}
			current = (compressor.getThreshold() >> 24) - 64;
			current += offset;
			current = std::clamp(current, -64, 64);
			ledLevel = (64 + current);
			displayLevel = ((ledLevel)*kMaxMenuValue) / 128;
			compressor.setThreshold(lshiftAndSaturate<24>(current + 64));
			indicator_leds::setKnobIndicatorLevel(1, ledLevel);
			unit = "";
		}
		else if (whichModEncoder == 0) {
			switch (currentCompParam) {

			case CompParam::RATIO:
				if (display->haveOLED()) {
					popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RATIO));
				}
				current = (compressor.getRatio() >> 24) - 64;
				current += offset;
				// this range is ratio of 2 to 20
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);
				displayLevel = compressor.setRatio(lshiftAndSaturate<24>(current + 64));
				unit = " : 1";
				break;

			case CompParam::ATTACK:
				if (display->haveOLED()) {
					popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_ATTACK));
				}
				current = (compressor.getAttack() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				displayLevel = compressor.setAttack(lshiftAndSaturate<24>(current + 64));
				unit = " MS";
				break;

			case CompParam::RELEASE:
				if (display->haveOLED()) {
					popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RELEASE));
				}
				current = (compressor.getRelease() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				displayLevel = compressor.setRelease(lshiftAndSaturate<24>(current + 64));
				unit = " MS";
				break;

			case CompParam::SIDECHAIN:
				if (display->haveOLED()) {
					popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_HPF));
				}
				current = (compressor.getSidechain() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				displayLevel = compressor.setSidechain(lshiftAndSaturate<24>(current + 64));
				unit = " HZ";
				break;

			case CompParam::BLEND:
				if (display->haveOLED()) {
					popupMsg.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_BLEND));
				}
				current = (compressor.getBlend() >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);
				q31_t level = current == 64 ? ONE_Q31 : lshiftAndSaturate<24>(current + 64);
				displayLevel = compressor.setBlend(level);
				unit = " %";
				break;
			}
			indicator_leds::setKnobIndicatorLevel(0, ledLevel);
		}

		if (display->haveOLED()) {
			popupMsg.append(": ");
			popupMsg.appendInt(displayLevel);
			popupMsg.append(unit);
		}
		else {
			popupMsg.appendInt(displayLevel);
		}
		display->displayPopup(popupMsg.c_str());

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
	int32_t lpfMorph =
	    getFinalParameterValueLinear(paramNeutralValues[params::LOCAL_LPF_MORPH],
	                                 cableToExpParamShortcut(unpatchedParams->getValue(params::UNPATCHED_LPF_MORPH)));
	int32_t hpfFrequency =
	    getFinalParameterValueExp(paramNeutralValues[params::LOCAL_HPF_FREQ],
	                              cableToExpParamShortcut(unpatchedParams->getValue(params::UNPATCHED_HPF_FREQ)));
	int32_t hpfResonance =
	    getFinalParameterValueLinear(paramNeutralValues[params::LOCAL_HPF_RESONANCE],
	                                 cableToLinearParamShortcut(unpatchedParams->getValue(params::UNPATCHED_HPF_RES)));
	int32_t hpfMorph =
	    getFinalParameterValueLinear(paramNeutralValues[params::LOCAL_HPF_MORPH],
	                                 cableToExpParamShortcut(unpatchedParams->getValue(params::UNPATCHED_HPF_MORPH)));

	bool doLPF = (lpfMode == FilterMode::TRANSISTOR_24DB_DRIVE
	              || unpatchedParams->getValue(params::UNPATCHED_LPF_FREQ) < 2147483602
	              || unpatchedParams->getValue(params::UNPATCHED_LPF_MORPH) > NEGATIVE_ONE_Q31);
	bool doHPF = unpatchedParams->getValue(params::UNPATCHED_HPF_FREQ) > NEGATIVE_ONE_Q31
	             || unpatchedParams->getValue(params::UNPATCHED_HPF_MORPH) > NEGATIVE_ONE_Q31;
	FilterMode lpfModeForRender = doLPF ? lpfMode : FilterMode::OFF;
	FilterMode hpfModeForRender = doHPF ? hpfMode : FilterMode::OFF;
	*postFXVolume =
	    filterSet.setConfig(lpfFrequency, lpfResonance, lpfModeForRender, lpfMorph, hpfFrequency, hpfResonance,
	                        hpfModeForRender, hpfMorph, *postFXVolume, filterRoute, false, NULL);
}

[[gnu::hot]] void GlobalEffectable::processFilters(StereoSample* buffer, int32_t numSamples) {
	filterSet.renderLongStereo(&buffer->l, &(buffer + numSamples)->l);
}

void GlobalEffectable::writeAttributesToFile(Serializer& writer, bool writeAutomation) {
	writer.writeAttribute("modFXCurrentParam", (char*)modFXParamToString(currentModFXParam));
	writer.writeAttribute("currentFilterType", (char*)filterTypeToString(currentFilterType));
	ModControllableAudio::writeAttributesToFile(writer);
	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	// <--
}

void GlobalEffectable::writeTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation) {
	if (paramManager) {
		writer.writeOpeningTagBeginning("defaultParams");
		GlobalEffectable::writeParamAttributesToFile(writer, paramManager, writeAutomation);
		writer.writeOpeningTagEnd();
		GlobalEffectable::writeParamTagsToFile(writer, paramManager, writeAutomation);
		writer.writeClosingTag("defaultParams");
	}

	ModControllableAudio::writeTagsToFile(writer);
}

void GlobalEffectable::writeParamAttributesToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                                  int32_t* valuesForOverride) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	unpatchedParams->writeParamAsAttribute(writer, "reverbAmount", params::UNPATCHED_REVERB_SEND_AMOUNT,
	                                       writeAutomation, false, valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "volume", params::UNPATCHED_VOLUME, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "pan", params::UNPATCHED_PAN, writeAutomation, false,
	                                       valuesForOverride);

	if (unpatchedParams->params[params::UNPATCHED_PITCH_ADJUST].containsSomething(0)) {
		unpatchedParams->writeParamAsAttribute(writer, "pitchAdjust", params::UNPATCHED_PITCH_ADJUST, writeAutomation,
		                                       false, valuesForOverride);
	}

	if (unpatchedParams->params[params::UNPATCHED_SIDECHAIN_VOLUME].containsSomething(-2147483648)) {
		unpatchedParams->writeParamAsAttribute(writer, "sidechainCompressorVolume", params::UNPATCHED_SIDECHAIN_VOLUME,
		                                       writeAutomation, false, valuesForOverride);
	}

	unpatchedParams->writeParamAsAttribute(writer, "sidechainCompressorShape", params::UNPATCHED_SIDECHAIN_SHAPE,
	                                       writeAutomation, false, valuesForOverride);

	unpatchedParams->writeParamAsAttribute(writer, "modFXDepth", params::UNPATCHED_MOD_FX_DEPTH, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "modFXRate", params::UNPATCHED_MOD_FX_RATE, writeAutomation, false,
	                                       valuesForOverride);

	ModControllableAudio::writeParamAttributesToFile(writer, paramManager, writeAutomation, valuesForOverride);

	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	unpatchedParams->writeParamAsAttribute(writer, "lpfMorph", params::UNPATCHED_LPF_MORPH, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "hpfMorph", params::UNPATCHED_HPF_MORPH, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "tempo", params::UNPATCHED_TEMPO, writeAutomation, false,
	                                       valuesForOverride);
}

void GlobalEffectable::writeParamTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                            int32_t* valuesForOverride) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	writer.writeOpeningTagBeginning("delay");
	unpatchedParams->writeParamAsAttribute(writer, "rate", params::UNPATCHED_DELAY_RATE, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "feedback", params::UNPATCHED_DELAY_AMOUNT, writeAutomation, false,
	                                       valuesForOverride);
	writer.closeTag();

	writer.writeOpeningTagBeginning("lpf");
	unpatchedParams->writeParamAsAttribute(writer, "frequency", params::UNPATCHED_LPF_FREQ, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "resonance", params::UNPATCHED_LPF_RES, writeAutomation, false,
	                                       valuesForOverride);
	writer.closeTag();

	writer.writeOpeningTagBeginning("hpf");
	unpatchedParams->writeParamAsAttribute(writer, "frequency", params::UNPATCHED_HPF_FREQ, writeAutomation, false,
	                                       valuesForOverride);
	unpatchedParams->writeParamAsAttribute(writer, "resonance", params::UNPATCHED_HPF_RES, writeAutomation, false,
	                                       valuesForOverride);
	writer.closeTag();

	ModControllableAudio::writeParamTagsToFile(writer, paramManager, writeAutomation, valuesForOverride);
}

void GlobalEffectable::readParamsFromFile(Deserializer& reader, ParamManagerForTimeline* paramManager,
                                          int32_t readAutomationUpToPos) {
	char const* tagName;

	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (readParamTagFromFile(reader, tagName, paramManager, readAutomationUpToPos)) {}
		else {
			reader.exitTag(tagName);
		}
	}
}

bool GlobalEffectable::readParamTagFromFile(Deserializer& reader, char const* tagName,
                                            ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos) {

	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;

	if (!strcmp(tagName, "delay")) {
		reader.match('{');
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "rate")) {
				unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_DELAY_RATE,
				                           readAutomationUpToPos);
				reader.exitTag("rate");
			}
			else if (!strcmp(tagName, "feedback")) {
				unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_DELAY_AMOUNT,
				                           readAutomationUpToPos);
				reader.exitTag("feedback");
			}
		}
		reader.exitTag("delay", true);
	}

	else if (!strcmp(tagName, "lpf")) {
		reader.match('{');
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "frequency")) {
				unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_LPF_FREQ,
				                           readAutomationUpToPos);
				reader.exitTag("frequency");
			}
			else if (!strcmp(tagName, "resonance")) {
				unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_LPF_RES,
				                           readAutomationUpToPos);
				reader.exitTag("resonance");
			}

			else if (!strcmp(tagName, "morph")) {
				unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_LPF_MORPH,
				                           readAutomationUpToPos);
				reader.exitTag("morph");
			}
		}
		reader.exitTag("lpf", true);
	}

	else if (!strcmp(tagName, "hpf")) {
		reader.match('{');
		while (*(tagName = reader.readNextTagOrAttributeName())) {
			if (!strcmp(tagName, "frequency")) {
				unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_HPF_FREQ,
				                           readAutomationUpToPos);
				reader.exitTag("frequency");
			}
			else if (!strcmp(tagName, "resonance")) {
				unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_HPF_RES,
				                           readAutomationUpToPos);
				reader.exitTag("resonance");
			}

			else if (!strcmp(tagName, "morph")) {
				unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_HPF_MORPH,
				                           readAutomationUpToPos);
				reader.exitTag("morph");
			}
		}
		reader.exitTag("hpf", true);
	}

	else if (!strcmp(tagName, "reverbAmount")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_REVERB_SEND_AMOUNT,
		                           readAutomationUpToPos);
		reader.exitTag("reverbAmount");
	}

	else if (!strcmp(tagName, "lpfMorph")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_LPF_MORPH, readAutomationUpToPos);
		reader.exitTag("lpfMorph");
	}

	else if (!strcmp(tagName, "hpfMorph")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_HPF_MORPH, readAutomationUpToPos);
		reader.exitTag("hpfMorph");
	}
	else if (!strcmp(tagName, "tempo")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_TEMPO, readAutomationUpToPos);
		reader.exitTag("tempo");
	}

	else if (!strcmp(tagName, "volume")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_VOLUME, readAutomationUpToPos);
		reader.exitTag("volume");
	}

	else if (!strcmp(tagName, "sidechainCompressorVolume")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_SIDECHAIN_VOLUME,
		                           readAutomationUpToPos);
		reader.exitTag("sidechainCompressorVolume");
	}

	else if (!strcmp(tagName, "sidechainCompressorShape")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_SIDECHAIN_SHAPE,
		                           readAutomationUpToPos);
		reader.exitTag("sidechainCompressorShape");
	}

	else if (!strcmp(tagName, "pan")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_PAN, readAutomationUpToPos);
		reader.exitTag("pan");
	}

	else if (!strcmp(tagName, "pitchAdjust")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_PITCH_ADJUST,
		                           readAutomationUpToPos);
		reader.exitTag("pitchAdjust");
	}

	else if (!strcmp(tagName, "modFXDepth")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_MOD_FX_DEPTH,
		                           readAutomationUpToPos);
		reader.exitTag("modFXDepth");
	}

	else if (!strcmp(tagName, "modFXRate")) {
		unpatchedParams->readParam(reader, unpatchedParamsSummary, params::UNPATCHED_MOD_FX_RATE,
		                           readAutomationUpToPos);
		reader.exitTag("modFXRate");
	}

	else if (ModControllableAudio::readParamTagFromFile(reader, tagName, paramManager, readAutomationUpToPos)) {}

	else {
		return false;
	}

	return true;
}

// paramManager is optional
Error GlobalEffectable::readTagFromFile(Deserializer& reader, char const* tagName,
                                        ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
                                        Song* song) {

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

		GlobalEffectable::readParamsFromFile(reader, paramManager, readAutomationUpToPos);
		reader.exitTag("defaultParams");
	}

	else if (!strcmp(tagName, "modFXType")) {
		modFXType = stringToFXType(reader.readTagOrAttributeValue());
		reader.exitTag("modFXType");
	}

	else if (!strcmp(tagName, "modFXCurrentParam")) {
		currentModFXParam = stringToModFXParam(reader.readTagOrAttributeValue());
		reader.exitTag("modFXCurrentParam");
	}

	else if (!strcmp(tagName, "currentFilterType")) {
		currentFilterType = stringToFilterType(reader.readTagOrAttributeValue());
		reader.exitTag("currentFilterType");
	}

	else {
		return ModControllableAudio::readTagFromFile(reader, tagName, NULL, readAutomationUpToPos, song);
	}

	return Error::NONE;
}

// Before calling this, check that (reader.firmwareVersionOfFileBeingRead < FIRMWARE_1P2P0 &&
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

	// For GlobalEffectables, mod FX buffer memory is allocated here in the rendering routine - this might seem
	// strange, but it's because unlike for Sounds, the effect can be switched on and off by changing a parameter
	// like "depth".
	if (util::one_of(modFXTypeNow,
	                 {ModFXType::CHORUS_STEREO, ModFXType::CHORUS, ModFXType::FLANGER, ModFXType::WARBLE})) {
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

namespace modfx {

deluge::vector<std::string_view> getModNames() {
	using enum deluge::l10n::String;
	using namespace deluge;
	return {
	    l10n::getView(STRING_FOR_DISABLED),      //<
	    l10n::getView(STRING_FOR_FLANGER),       //<
	    l10n::getView(STRING_FOR_CHORUS),        //<
	    l10n::getView(STRING_FOR_PHASER),        //<
	    l10n::getView(STRING_FOR_STEREO_CHORUS), //<
	    l10n::getView(STRING_FOR_WARBLE),
	    l10n::getView(STRING_FOR_GRAIN), //<
	};
}

const char* getParamName(ModFXType type, ModFXParam param) {
	using enum deluge::l10n::String;
	using namespace deluge;
	switch (type) {
	case ModFXType::GRAIN: {
		switch (param) {
			using enum deluge::l10n::String;
		case ModFXParam::DEPTH:
			return l10n::get(STRING_FOR_GRAIN_AMOUNT);
		case ModFXParam::FEEDBACK:
			return l10n::get(STRING_FOR_GRAIN_TYPE);
		case ModFXParam::OFFSET:
			return l10n::get(STRING_FOR_GRAIN_SIZE);
		default:
			return l10n::get(STRING_FOR_NONE);
		}
	}

	default: {
		switch (param) {
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
	}
}
const char* modFXToString(ModFXType type) {
	switch (type) {
		using namespace deluge;
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
	case ModFXType::WARBLE:
		return l10n::get(STRING_FOR_WARBLE);
	default:
		return l10n::get(STRING_FOR_NONE);
	}
}
} // namespace modfx
