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
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/curves/param_curve.h"
#include "effects/codec.h"
#include "effects/words.h"
#include "gui/l10n/l10n.h"
#include "gui/views/performance_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/led/indicator_leds.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/edits/change_effect.h"
#include "model/mod_controllable/mod_fx_names.h"
#include "model/project_bridge/chain_bridge.h"
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
#include "synth/matrix/descriptors.h" // kParamNeutral, kParamRange
#include "util/etl_string.h"
#include "util/firmware_version.h"
#include "util/functions.h"

using namespace deluge;
namespace params = deluge::modulation::params;

GlobalEffectable::GlobalEffectable() {
	unpatchedParamKind_ = params::Kind::UNPATCHED_GLOBAL;

	editingComp = false;
	currentCompParam = CompParam::RATIO;
}

void GlobalEffectable::cloneFrom(ModControllableAudio* other) {
	ModControllableAudio::cloneFrom(other);

	const auto& from = static_cast<GlobalEffectable&>(*other);
	deluge::model::HostChainState next = chainState_;
	next.mod_fx_param = from.modFXParam();
	next.filter_type = from.filterType();
	setChainState(next);
}

void GlobalEffectable::setChainState(const deluge::model::HostChainState& next) {
	chainState_.mod_fx_param = next.mod_fx_param;
	chainState_.filter_type = next.filter_type;
	// The effects and the view reach the render copy together. Projected even when unchanged: a mod FX ring or a grain
	// that could not be allocated is retried at every effects edit.
	setEffects(next.effects);
}

void GlobalEffectable::projectView(deluge::snapshot::ModControllableSnapshot& copy) {
	copy.mod_fx_param = chainState_.mod_fx_param;
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
	    *unpatchedParams->docForParam(params::UNPATCHED_MOD_FX_FEEDBACK), kNegativeOneQ31);
	unpatchedParams->params[params::UNPATCHED_MOD_FX_DEPTH].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_MOD_FX_DEPTH), 0);
	unpatchedParams->params[params::UNPATCHED_DELAY_RATE].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_DELAY_RATE), 0);
	unpatchedParams->params[params::UNPATCHED_PAN].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_PAN), 0);

	unpatchedParams->params[params::UNPATCHED_DELAY_AMOUNT].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_DELAY_AMOUNT), kNegativeOneQ31);
	unpatchedParams->params[params::UNPATCHED_REVERB_SEND_AMOUNT].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_REVERB_SEND_AMOUNT), kNegativeOneQ31);

	unpatchedParams->params[params::UNPATCHED_VOLUME].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_VOLUME), 0); // half of the way up
	unpatchedParams->params[params::UNPATCHED_SIDECHAIN_VOLUME].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_SIDECHAIN_VOLUME), kNegativeOneQ31);
	unpatchedParams->params[params::UNPATCHED_PITCH_ADJUST].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_PITCH_ADJUST), 0);

	unpatchedParams->params[params::UNPATCHED_LPF_RES].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_LPF_RES), kNegativeOneQ31);
	unpatchedParams->params[params::UNPATCHED_LPF_FREQ].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_LPF_FREQ), kOneQ31);

	unpatchedParams->params[params::UNPATCHED_HPF_RES].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_HPF_RES), kNegativeOneQ31);
	unpatchedParams->params[params::UNPATCHED_HPF_FREQ].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_HPF_FREQ), kNegativeOneQ31);

	unpatchedParams->params[params::UNPATCHED_LPF_MORPH].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_LPF_MORPH), kNegativeOneQ31);
	unpatchedParams->params[params::UNPATCHED_HPF_MORPH].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_HPF_MORPH), kNegativeOneQ31);
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
		displayFilterSettings(on, filterType());
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
	return modfx::getParamName(modControllableSnapshot_.effects.mod_fx.type, modFXParam());
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
				                              (util::to_underlying(modFXParam()) + 1) % kNumModFXParams)));
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
				                              (util::to_underlying(filterType()) + 1) % kNumFilterTypes)));

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayFilterSettings(on, filterType());
				}
				else {
					display->displayPopup(getFilterTypeDisplayName(filterType()));
				}
				return true;
			}

			return false;
		}
		else {
			if (on) {
				if (filterType() == FilterType::LPF) {
					switchLPFMode();

					// if mod button is pressed, update mod button pop up
					if (Buttons::isButtonPressed(
					        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
						displayFilterSettings(on, filterType());
					}
					else {
						display->displayPopup(getFilterModeDisplayName(filterType()));
					}
					return true;
				}
				else if (filterType() == FilterType::HPF) {
					switchHPFMode();

					// if mod button is pressed, update mod button pop up
					if (Buttons::isButtonPressed(
					        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
						displayFilterSettings(on, filterType());
					}
					else {
						display->displayPopup(getFilterModeDisplayName(filterType()));
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
					cycleDelayType();

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

q31_t GlobalEffectable::displayedCompressorThreshold() {
	// The master's threshold is its setting; a clip chain's render follows its threshold param, which the knob shows.
	return isSong() ? effects().compressor.threshold : core().compressor().dsp().threshold();
}

int32_t GlobalEffectable::getKnobPosForNonExistentParam(int32_t whichModEncoder, ModelStackWithAutoParam* modelStack) {
	int current = 0;
	if (*getModKnobMode() == 4) {

		// this is only reachable in comp editing mode, otherwise it's an existent param
		if (whichModEncoder == 1) { // sidechain (threshold)
			current = (displayedCompressorThreshold() >> 24);
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
			current = (displayedCompressorThreshold() >> 24) - 64;
			current += offset;
			current = std::clamp(current, -64, 64);
			ledLevel = (64 + current);
			displayLevel = ((ledLevel)*kMaxMenuValue) / 128;
			deluge::edits::changeEffect(
			    *this, "audioCompressor/thresh",
			    deluge::serialization::Value::makeNumber(lshift_and_saturate<24>(current + 64)));
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
				    deluge::serialization::Value::makeNumber(lshift_and_saturate<24>(current + 64)));
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
				    deluge::serialization::Value::makeNumber(lshift_and_saturate<24>(current + 64)));
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
				    deluge::serialization::Value::makeNumber(lshift_and_saturate<24>(current + 64)));
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
				    deluge::serialization::Value::makeNumber(lshift_and_saturate<24>(current + 64)));
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
				q31_t level = current == 64 ? kOneQ31 : lshift_and_saturate<24>(current + 64);
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
		switch (filterType()) {
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
			if (modFXParam() == ModFXParam::DEPTH) {
				return params::UNPATCHED_MOD_FX_DEPTH;
			}
			else if (modFXParam() == ModFXParam::OFFSET) {
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
	ModFXParam param = modFXParam();
	for (int32_t tried = 0; tried < kNumModFXParams && !valid(param); tried++) {
		param = static_cast<ModFXParam>((util::to_underlying(param) + 1) % kNumModFXParams);
	}
	if (param != modFXParam()) {
		deluge::edits::changeView(*this, "modFXCurrentParam", modFXParamToString(param));
	}
}

deluge::model::HostChainState GlobalEffectable::savedChainState() {
	if (currentSong != nullptr) {
		if (const std::optional<deluge::project::ChainAddress> address =
		        deluge::bridge::chainAddressOf(*this, *currentSong)) {
			if (const deluge::project::FxChain* record = currentSong->project.resolveChain(*address)) {
				return deluge::bridge::chainStateOf(*record);
			}
		}
	}
	return chainState_;
}

void GlobalEffectable::writeAttributesToFile(Serializer& writer, bool writeAutomation) {
	const deluge::model::HostChainState saved = savedChainState();
	writer.writeAttribute("modFXCurrentParam", modFXParamToString(saved.mod_fx_param));
	writer.writeAttribute("currentFilterType", filterTypeToString(saved.filter_type));
	ModControllableAudio::writeAttributesToFile(writer, saved.effects);
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

	// A clip chain's render follows its threshold param; the file holds the setting, as the record does.
	const deluge::model::HostChainState saved = savedChainState();
	ModControllableAudio::writeTagsToFile(writer, saved.effects);
}

void GlobalEffectable::writeParamAttributesToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                                  int32_t* valuesForOverride) {
	// Serialize from the value DOM (deluge::project). serializeGlobalEffectableParamAttributes emits the global flat
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
	deluge::model::HostChainState next = chainState_;
	deluge::effects::decode(element, next.effects, {.input_tick_magnitude = tickMagnitude});
	delayTypePending = !deluge::effects::saves_delay_type(element);
	if (const deluge::serialization::Value* param = element.find("modFXCurrentParam"); param && param->isScalar()) {
		next.mod_fx_param = stringToModFXParam(param->str().c_str());
	}
	if (const deluge::serialization::Value* type = element.find("currentFilterType"); type && type->isScalar()) {
		next.filter_type = stringToFilterType(type->str().c_str());
	}
	setChainState(next);
}

ModFXType GlobalEffectable::getActiveModFXType(ParamManager* paramManager) {
	ModFXType modFXTypeNow = modControllableSnapshot_.effects.mod_fx.type;

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();

	if ((modControllableSnapshot_.mod_fx_param == ModFXParam::DEPTH
	     && unpatchedParams->getValue(params::UNPATCHED_MOD_FX_DEPTH) == -2147483648)
	    || (modControllableSnapshot_.mod_fx_param == ModFXParam::FEEDBACK
	        && unpatchedParams->getValue(params::UNPATCHED_MOD_FX_FEEDBACK) == -2147483648)
	    || (modControllableSnapshot_.mod_fx_param == ModFXParam::OFFSET
	        && unpatchedParams->getValue(params::UNPATCHED_MOD_FX_OFFSET) == -2147483648)) {
		modFXTypeNow = ModFXType::NONE;
	}
	return modFXTypeNow;
}

deluge::effects::Delay::State GlobalEffectable::createDelayWorkingState(ParamManager& paramManager,
                                                                        bool shouldLimitDelayFeedback,
                                                                        bool soundComingIn) {

	deluge::effects::Delay::State delayWorkingState;
	UnpatchedParamSet* unpatchedParams = paramManager.getUnpatchedParamSet();

	delayWorkingState.delayFeedbackAmount = deluge::dsp::fixed::curve_linear(
	    deluge::synth::matrix::kParamNeutral[params::GLOBAL_DELAY_FEEDBACK],
	    deluge::dsp::fixed::preset_combination(unpatchedParams->getValue(params::UNPATCHED_DELAY_AMOUNT)));
	if (shouldLimitDelayFeedback) {
		delayWorkingState.delayFeedbackAmount =
		    std::min(delayWorkingState.delayFeedbackAmount, (int32_t)(1 << 30) - (1 << 26));
	}
	delayWorkingState.userDelayRate = deluge::dsp::fixed::curve_exponential(
	    deluge::synth::matrix::kParamNeutral[params::GLOBAL_DELAY_RATE],
	    deluge::dsp::fixed::preset_combination(unpatchedParams->getValue(params::UNPATCHED_DELAY_RATE)));
	uint32_t timePerTickInverse = playbackHandler.getTimePerInternalTickInverse(true);
	core().delay().setupWorkingState(delayWorkingState, modControllableSnapshot_.effects.delay, timePerTickInverse,
	                                 soundComingIn);

	return delayWorkingState;
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
