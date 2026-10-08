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
#include "deluge-project/param_automation.h"
#include "dsp/stereo_sample.h"
#include "engines/effects/codec.h"
#include "engines/effects/words.h"
#include "gui/l10n/l10n.h"
#include "gui/views/performance_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/led/indicator_leds.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/edits/change_effect.h"
#include "model/mod_controllable/ModFXProcessor.h"
#include "model/project_bridge/param_projection.h"
#include "model/project_bridge/param_serialization.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/render_scratch.h"
#include "storage/storage_manager.h"
#include "util/comparison.h"
#include "util/etl_string.h"
#include "util/firmware_version.h"

using namespace deluge;
namespace params = deluge::modulation::params;

GlobalEffectable::GlobalEffectable() {
	unpatchedParamKind_ = params::Kind::UNPATCHED_GLOBAL;
	setLpfMode(FilterMode::TRANSISTOR_24DB);
	filter_.reset();

	assignModFXType(ModFXType::NONE);
	currentModFXParam = ModFXParam::FEEDBACK;
	currentFilterType = FilterType::LPF;

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

	// Overwrite default arp Gate to 50 for Kit affect-entire arp
	unpatchedParams->params[params::UNPATCHED_ARP_GATE].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_ARP_GATE), 2147483647);
	unpatchedParams->params[params::UNPATCHED_ARP_RATE].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_ARP_RATE), 0);

	unpatchedParams->params[params::UNPATCHED_MOD_FX_RATE].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_MOD_FX_RATE), -536870912);
	unpatchedParams->params[params::UNPATCHED_MOD_FX_FEEDBACK].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_MOD_FX_FEEDBACK), NEGATIVE_ONE_Q31);
	unpatchedParams->params[params::UNPATCHED_MOD_FX_DEPTH].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_MOD_FX_DEPTH), 0);
	unpatchedParams->params[params::UNPATCHED_DELAY_RATE].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_DELAY_RATE), 0);
	unpatchedParams->params[params::UNPATCHED_PAN].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_PAN), 0);

	unpatchedParams->params[params::UNPATCHED_DELAY_AMOUNT].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_DELAY_AMOUNT), NEGATIVE_ONE_Q31);
	unpatchedParams->params[params::UNPATCHED_REVERB_SEND_AMOUNT].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_REVERB_SEND_AMOUNT), NEGATIVE_ONE_Q31);

	unpatchedParams->params[params::UNPATCHED_VOLUME].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_VOLUME), 0); // half of the way up
	unpatchedParams->params[params::UNPATCHED_SIDECHAIN_VOLUME].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_SIDECHAIN_VOLUME), NEGATIVE_ONE_Q31);
	unpatchedParams->params[params::UNPATCHED_PITCH_ADJUST].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_PITCH_ADJUST), 0);

	unpatchedParams->params[params::UNPATCHED_LPF_RES].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_LPF_RES), NEGATIVE_ONE_Q31);
	unpatchedParams->params[params::UNPATCHED_LPF_FREQ].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_LPF_FREQ), ONE_Q31);

	unpatchedParams->params[params::UNPATCHED_HPF_RES].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_HPF_RES), NEGATIVE_ONE_Q31);
	unpatchedParams->params[params::UNPATCHED_HPF_FREQ].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_HPF_FREQ), NEGATIVE_ONE_Q31);

	unpatchedParams->params[params::UNPATCHED_LPF_MORPH].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_LPF_MORPH), NEGATIVE_ONE_Q31);
	unpatchedParams->params[params::UNPATCHED_HPF_MORPH].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_HPF_MORPH), NEGATIVE_ONE_Q31);
}

void GlobalEffectable::initParamsForAudioClip(ParamManagerForTimeline* paramManager) {
	initParams(paramManager);
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->params[params::UNPATCHED_VOLUME].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_VOLUME), -536870912);
}

void GlobalEffectable::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) {

	// leave stutter running in perfomance session view
	if (getRootUI() != &performanceView) {
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
	if (on) {
		etl::string<100> popupMsg;
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

char const* GlobalEffectable::getCompressorModeDisplayName() {
	return editingComp ? "FULL" : "ONE";
}

char const* GlobalEffectable::getCompressorParamDisplayName() {
	currentCompParam = static_cast<CompParam>(util::to_underlying(currentCompParam) % maxCompParam);
	const char* params[util::to_underlying(CompParam::LAST)] = {"ratio", "attack", "release", "hpf", "blend"};
	return params[int(currentCompParam)];
}

void GlobalEffectable::displayModFXSettings(bool on) {
	if (on) {
		etl::string<100> popupMsg;
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

char const* GlobalEffectable::getModFXTypeDisplayName() {
	return modfx::modFXToString(modControllableSnapshot_.effects.mod_fx.type);
}

char const* GlobalEffectable::getModFXParamDisplayName() {
	return modfx::getParamName(modControllableSnapshot_.effects.mod_fx.type, currentModFXParam);
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
				const auto next = static_cast<ModFXType>(
				    (util::to_underlying(modControllableSnapshot_.effects.mod_fx.type) + 1) % kNumModFXTypes);
				deluge::edits::changeEffect(
				    *this, "modFXType",
				    deluge::serialization::Value::makeScalar(std::string(deluge::effects::to_string(next))));
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
				deluge::edits::changeView(*this, "modFXCurrentParam",
				                          modFXParamToString(static_cast<ModFXParam>(
				                              (util::to_underlying(currentModFXParam) + 1) % kNumModFXParams)));
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
				deluge::edits::changeView(*this, "currentFilterType",
				                          filterTypeToString(static_cast<FilterType>(
				                              (util::to_underlying(currentFilterType) + 1) % kNumFilterTypes)));

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
			current = (savedCompressorThreshold() >> 24);
		}
		else if (whichModEncoder == 0) {
			switch (currentCompParam) {

			case CompParam::RATIO:
				current = (effects().compressor.ratio >> 24);

				break;

			case CompParam::ATTACK:
				current = effects().compressor.attack >> 24;

				break;

			case CompParam::RELEASE:
				current = effects().compressor.release >> 24;

				break;

			case CompParam::SIDECHAIN:
				current = effects().compressor.sidechain_hpf >> 24;
				break;

			case CompParam::BLEND:
				current = effects().compressor.blend >> 24;
				break;

			// explicit fallthrough case
			case CompParam::LAST:;
			}
		}
	}
	return current - 64;
}

ActionResult GlobalEffectable::modEncoderActionForNonExistentParam(int32_t offset, int32_t whichModEncoder,
                                                                   ModelStackWithAutoParam* modelStack) {
	if (*getModKnobMode() == 4) {
		etl::string<40> parameterName;
		etl::string<40> parameterValue;
		int current;
		int displayLevel;
		int ledLevel;
		const char* unit;
		// this is only reachable in comp editing mode, otherwise it's an existent param
		if (whichModEncoder == 1) { // sidechain (threshold)
			parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_THRESHOLD));
			current = (savedCompressorThreshold() >> 24) - 64;
			current += offset;
			current = std::clamp(current, -64, 64);
			ledLevel = (64 + current);
			displayLevel = ((ledLevel)*kMaxMenuValue) / 128;
			deluge::edits::changeEffect(*this, "audioCompressor/thresh",
			                            deluge::serialization::Value::makeNumber(lshiftAndSaturate<24>(current + 64)));
			indicator_leds::setKnobIndicatorLevel(1, ledLevel);
			unit = "";
		}
		else if (whichModEncoder == 0) {
			switch (currentCompParam) {

			case CompParam::RATIO:
				parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RATIO));
				current = (effects().compressor.ratio >> 24) - 64;
				current += offset;
				// this range is ratio of 2 to 20
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);
				deluge::edits::changeEffect(
				    *this, "audioCompressor/ratio",
				    deluge::serialization::Value::makeNumber(lshiftAndSaturate<24>(current + 64)));
				displayLevel = static_cast<int32_t>(deluge::dsp::fixed::compressor_ratio(effects().compressor.ratio));
				unit = " : 1";
				break;

			case CompParam::ATTACK:
				parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_ATTACK));
				current = (effects().compressor.attack >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				deluge::edits::changeEffect(
				    *this, "audioCompressor/attack",
				    deluge::serialization::Value::makeNumber(lshiftAndSaturate<24>(current + 64)));
				displayLevel =
				    static_cast<int32_t>(deluge::dsp::fixed::compressor_attack_ms(effects().compressor.attack));
				unit = " MS";
				break;

			case CompParam::RELEASE:
				parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_RELEASE));
				current = (effects().compressor.release >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				deluge::edits::changeEffect(
				    *this, "audioCompressor/release",
				    deluge::serialization::Value::makeNumber(lshiftAndSaturate<24>(current + 64)));
				displayLevel =
				    static_cast<int32_t>(deluge::dsp::fixed::compressor_release_ms(effects().compressor.release));
				unit = " MS";
				break;

			case CompParam::SIDECHAIN:
				parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_HPF));
				current = (effects().compressor.sidechain_hpf >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);

				deluge::edits::changeEffect(
				    *this, "audioCompressor/compHPF",
				    deluge::serialization::Value::makeNumber(lshiftAndSaturate<24>(current + 64)));
				displayLevel = static_cast<int32_t>(
				    deluge::dsp::fixed::compressor_sidechain_hz(effects().compressor.sidechain_hpf));
				unit = " HZ";
				break;

			case CompParam::BLEND: {
				parameterName.append(deluge::l10n::get(deluge::l10n::String::STRING_FOR_BLEND));
				current = (effects().compressor.blend >> 24) - 64;
				current += offset;
				current = std::clamp(current, -64, 64);
				ledLevel = (64 + current);
				q31_t level = current == 64 ? ONE_Q31 : lshiftAndSaturate<24>(current + 64);
				deluge::edits::changeEffect(*this, "audioCompressor/compBlend",
				                            deluge::serialization::Value::makeNumber(level));
				displayLevel = deluge::dsp::fixed::compressor_blend_percent(level);
				unit = " %";
				break;
			}

			// explicit fallthrough case
			case CompParam::LAST:;
			}
			indicator_leds::setKnobIndicatorLevel(0, ledLevel);
		}

		deluge::string::appendInt(parameterValue, displayLevel);
		parameterValue.append(unit);
		display->displayNotification(parameterName.c_str(), parameterValue.c_str());

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
		return newModelStack1->addAutoParam(nullptr); // Communicate there's no param, back to caller
	}
	else {
		return newModelStack1->paramCollection->getAutoParamFromId(newModelStack1, allowCreation);
	}
}

ModFXType GlobalEffectable::getModFXType() {
	return modControllableSnapshot_.effects.mod_fx.type;
}

void GlobalEffectable::ensureModFXParamIsValid() {
	const ModFXType type = modControllableSnapshot_.effects.mod_fx.type;
	const auto valid = [type](ModFXParam param) {
		switch (param) {
		case ModFXParam::DEPTH:
			return type != ModFXType::FLANGER;
		case ModFXParam::OFFSET:
			return type == ModFXType::CHORUS || type == ModFXType::CHORUS_STEREO || type == ModFXType::GRAIN;
		default: // ModFXParam::FEEDBACK
			return type != ModFXType::CHORUS && type != ModFXType::CHORUS_STEREO;
		}
	};
	ModFXParam param = currentModFXParam;
	for (int32_t tried = 0; tried < kNumModFXParams && !valid(param); tried++) {
		param = static_cast<ModFXParam>((util::to_underlying(param) + 1) % kNumModFXParams);
	}
	if (param != currentModFXParam) {
		deluge::edits::changeView(*this, "modFXCurrentParam", modFXParamToString(param));
	}
}

void GlobalEffectable::configureFilter(int32_t* postFXVolume, ParamManager* paramManager) {

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	const int32_t lpfFrequencyParam = unpatchedParams->getValue(params::UNPATCHED_LPF_FREQ);
	const int32_t lpfMorphParam = unpatchedParams->getValue(params::UNPATCHED_LPF_MORPH);
	const int32_t hpfFrequencyParam = unpatchedParams->getValue(params::UNPATCHED_HPF_FREQ);
	const int32_t hpfMorphParam = unpatchedParams->getValue(params::UNPATCHED_HPF_MORPH);

	*postFXVolume =
	    filter_.configure(modControllableSnapshot_.effects.filter,
	                      {
	                          .lpf_frequency = getFinalParameterValueExp(paramNeutralValues[params::LOCAL_LPF_FREQ],
	                                                                     cableToExpParamShortcut(lpfFrequencyParam)),
	                          .lpf_resonance = getFinalParameterValueLinear(
	                              paramNeutralValues[params::LOCAL_LPF_RESONANCE],
	                              cableToLinearParamShortcut(unpatchedParams->getValue(params::UNPATCHED_LPF_RES))),
	                          .lpf_morph = getFinalParameterValueLinear(paramNeutralValues[params::LOCAL_LPF_MORPH],
	                                                                    cableToExpParamShortcut(lpfMorphParam)),
	                          .hpf_frequency = getFinalParameterValueExp(paramNeutralValues[params::LOCAL_HPF_FREQ],
	                                                                     cableToExpParamShortcut(hpfFrequencyParam)),
	                          .hpf_resonance = getFinalParameterValueLinear(
	                              paramNeutralValues[params::LOCAL_HPF_RESONANCE],
	                              cableToLinearParamShortcut(unpatchedParams->getValue(params::UNPATCHED_HPF_RES))),
	                          .hpf_morph = getFinalParameterValueLinear(paramNeutralValues[params::LOCAL_HPF_MORPH],
	                                                                    cableToExpParamShortcut(hpfMorphParam)),
	                          .lpf_frequency_param = lpfFrequencyParam,
	                          .lpf_morph_param = lpfMorphParam,
	                          .hpf_frequency_param = hpfFrequencyParam,
	                          .hpf_morph_param = hpfMorphParam,
	                          .allow_oversampling = AudioEngine::cpuDireness < 14,
	                      },
	                      *postFXVolume);
}

[[gnu::hot]] void GlobalEffectable::processFilter(std::span<StereoSample> buffer) {
	filter_.process(buffer, deluge::render::filter_scratch());
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
	// Serialize from the value DOM (deluge::project) — byte-identical to the former hand-written sequence
	// (verified in the param-projection driver). serializeGlobalEffectableParamAttributes emits the global flat
	// attrs around the shared ModControllableAudio attrs and the community attrs.
	deluge::project::DocParamSet unpatched =
	    deluge::bridge::toDocParamSetFromStore(*paramManager->getUnpatchedParamSet());
	deluge::bridge::serializeGlobalEffectableParamAttributes(unpatched, writer, writeAutomation, valuesForOverride);
}

void GlobalEffectable::writeParamTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                            int32_t* valuesForOverride) {
	deluge::project::DocParamSet unpatched =
	    deluge::bridge::toDocParamSetFromStore(*paramManager->getUnpatchedParamSet());
	deluge::bridge::serializeGlobalEffectableParamTags(unpatched, writer, writeAutomation, valuesForOverride);
}

void GlobalEffectable::readParamsFromFile(Deserializer& reader, ParamManagerForTimeline* paramManager,
                                          int32_t readAutomationUpToPos) {
	char const* tagName;

	reader.match('{'); // descend into the <defaultParams> object (needed for JSON; a no-op in XML)
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (readParamTagFromFile(reader, tagName, paramManager, readAutomationUpToPos)) {}
		else {
			reader.exitTag(tagName);
		}
	}
	reader.match('}');
}

void GlobalEffectable::readParamsFromFileDomFirst(Deserializer& reader, ParamManagerForTimeline* paramManager,
                                                  int32_t readAutomationUpToPos) {
	// initParams already wrote each default straight into the set's own DOM store (via docForParam), so file-omitted
	// params keep their default with NO engine->store copy. Read the present params into the store via the same
	// tables the engine reader uses, then project each engine RT cursor from the store.
	UnpatchedParamSet* unpatched = paramManager->getUnpatchedParamSet();
	ParamCollectionSummary* summary = paramManager->getUnpatchedParamSetSummary();
	unpatched->bindDomStore();
	deluge::project::DocParamSet* store = unpatched->domStore();
	char const* tagName;
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (deluge::bridge::readGlobalEffectableParamTagToDoc(reader, tagName, *store, readAutomationUpToPos)) {}
		else {
			reader.exitTag(tagName);
		}
	}
	unpatched->projectFromStoreAfterRead(summary);
}

bool GlobalEffectable::readParamTagFromFile(Deserializer& reader, char const* tagName,
                                            ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos) {
	// Read the GlobalEffectable param block from the value DOM (deluge::project), as the writer writes it.
	ParamCollectionSummary* unpatchedParamsSummary = paramManager->getUnpatchedParamSetSummary();
	UnpatchedParamSet* unpatchedParams = (UnpatchedParamSet*)unpatchedParamsSummary->paramCollection;
	return deluge::bridge::readGlobalEffectableParamTag(reader, tagName, *unpatchedParams, *unpatchedParamsSummary,
	                                                    readAutomationUpToPos);
}

// paramManager is optional
Error GlobalEffectable::readTagFromFile(Deserializer& reader, char const* tagName,
                                        ParamManagerForTimeline* paramManager, int32_t readAutomationUpToPos,
                                        ArpeggiatorSettings* arpSettings, Song* song) {

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

	else {
		return ModControllableAudio::readTagFromFile(reader, tagName, NULL, readAutomationUpToPos, arpSettings, song);
	}

	return Error::NONE;
}

void GlobalEffectable::readEffectsFromElement(const deluge::serialization::Value& element, int32_t tickMagnitude) {
	deluge::effects::EffectStates states = effects();
	deluge::effects::decode(element, states, {.input_tick_magnitude = tickMagnitude});
	setEffects(states);
	// The runtime threshold is what a save before any render writes back: the file's, when it gives one.
	if (const deluge::serialization::Value* settings = element.find("audioCompressor");
	    settings != nullptr && settings->isMap() && settings->find("thresh") != nullptr) {
		compressor.dsp().setThreshold(states.compressor.threshold);
	}
	if (const deluge::serialization::Value* param = element.find("modFXCurrentParam"); param && param->isScalar()) {
		currentModFXParam = stringToModFXParam(param->str().c_str());
	}
	if (const deluge::serialization::Value* type = element.find("currentFilterType"); type && type->isScalar()) {
		currentFilterType = stringToFilterType(type->str().c_str());
	}
}

ModFXType GlobalEffectable::getActiveModFXType(ParamManager* paramManager) {
	ModFXType modFXTypeNow = modControllableSnapshot_.effects.mod_fx.type;

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
	delay.setupWorkingState(delayWorkingState, modControllableSnapshot_.effects.delay, timePerTickInverse,
	                        soundComingIn);

	return delayWorkingState;
}

void GlobalEffectable::processFXForGlobalEffectable(std::span<StereoSample> buffer, int32_t* postFXVolume,
                                                    ParamManager* paramManager, const Delay::State& delayWorkingState,
                                                    bool anySoundComingIn, q31_t verbAmount) {
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
	// TODO: this saves a tiny amount of memory at the cost of needing allocations during the audio render cycle...
	// seems kinda dumb
	if (util::one_of(modFXTypeNow, {ModFXType::CHORUS_STEREO, ModFXType::CHORUS, ModFXType::FLANGER, ModFXType::WARBLE,
	                                ModFXType::DIMENSION})) {
		modfx.setupBuffer();
		disableGrain();
	}
	else if (modFXTypeNow == ModFXType::GRAIN) {
		if (anySoundComingIn && !grainFX) {
			enableGrain();
		}
	}
	else {
		modfx.disableBuffer();
		disableGrain();
	}

	processFX(buffer, modFXTypeNow, modFXRate, modFXDepth, delayWorkingState, postFXVolume, paramManager,
	          anySoundComingIn, verbAmount);
}

namespace modfx {
// note this is dumb but it needs to match the enum order currently
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
	    l10n::getView(STRING_FOR_DIMENSION), //<
	    l10n::getView(STRING_FOR_GRAIN),     //<
	};
}

const char* getParamName(ModFXType type, ModFXParam param, bool shortName) {
	using enum deluge::l10n::String;
	using namespace deluge;

	switch (type) {
	case ModFXType::GRAIN: {
		switch (param) {
		case ModFXParam::DEPTH:
			return l10n::get(shortName ? STRING_FOR_GRAIN_AMOUNT_SHORT : STRING_FOR_GRAIN_AMOUNT);
		case ModFXParam::FEEDBACK:
			return l10n::get(shortName ? STRING_FOR_GRAIN_RANDOMNESS_SHORT : STRING_FOR_GRAIN_RANDOMNESS);
		case ModFXParam::OFFSET:
			return l10n::get(shortName ? STRING_FOR_GRAIN_DENSITY_SHORT : STRING_FOR_GRAIN_DENSITY);
		default:
			return l10n::get(STRING_FOR_NONE);
		}
	}

	default: {
		switch (param) {
		case ModFXParam::DEPTH:
			return l10n::get(shortName ? STRING_FOR_DEPTH_SHORT : STRING_FOR_DEPTH);
		case ModFXParam::FEEDBACK:
			return l10n::get(shortName ? STRING_FOR_FEEDBACK_SHORT : STRING_FOR_FEEDBACK);
		case ModFXParam::OFFSET:
			return l10n::get(shortName ? STRING_FOR_OFFSET_SHORT : STRING_FOR_OFFSET);
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
	case ModFXType::DIMENSION:
		return l10n::get(STRING_FOR_DIMENSION);
	case ModFXType::GRAIN:
		return l10n::get(STRING_FOR_GRAIN);
	case ModFXType::WARBLE:
		return l10n::get(STRING_FOR_WARBLE);
	default:
		return l10n::get(STRING_FOR_DISABLED);
	}
}
} // namespace modfx
