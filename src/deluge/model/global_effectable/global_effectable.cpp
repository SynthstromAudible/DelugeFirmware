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
#include "effects/codec.h"
#include "effects/global_effectable/params.h"
#include "effects/plugin.h"
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
#include "model/song/reverb_presets.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/render_scratch.h"
#include "storage/storage_manager.h"
#include "util/etl_string.h"
#include "util/firmware_version.h"
#include "util/functions.h"
#include "util/notify.h"

using namespace deluge;
namespace params = deluge::modulation::params;

GlobalEffectable::GlobalEffectable() {
	unpatchedParamKind_ = params::Kind::UNPATCHED_GLOBAL;
}

void GlobalEffectable::cloneFrom(ModControllableAudio* other) {
	ModControllableAudio::cloneFrom(other);

	const auto& from = static_cast<GlobalEffectable&>(*other);
	setView(from.modFXParam(), from.filterType());
}

ModFXParam GlobalEffectable::modFXParam() const {
	return boundChain_ != nullptr
	           ? static_cast<ModFXParam>(std::min<int32_t>(boundChain_->mod_fx_param, kNumModFXParams - 1))
	           : modFXParam_;
}

FilterType GlobalEffectable::filterType() const {
	return boundChain_ != nullptr
	           ? static_cast<FilterType>(std::min<int32_t>(boundChain_->filter_type, kNumFilterTypes - 1))
	           : filterType_;
}

void GlobalEffectable::bindChain(deluge::project::FxChain& chain) {
	if (boundChain_ == &chain) {
		return;
	}
	chain.effects = deluge::plugin::StateHandle::of<deluge::effects::DelugeEffects>(effects());
	chain.mod_fx_param = static_cast<uint8_t>(util::to_underlying(modFXParam()));
	chain.filter_type = static_cast<uint8_t>(util::to_underlying(filterType()));
	boundChain_ = &chain;
	bindEffects(chain.effects.as<deluge::effects::DelugeEffects>());
}

void GlobalEffectable::unbindChain() {
	if (boundChain_ == nullptr) {
		return;
	}
	modFXParam_ = modFXParam();
	filterType_ = filterType();
	bindEffects(nullptr); // copies the record's effects in
	boundChain_ = nullptr;
}

void GlobalEffectable::setView(ModFXParam param, FilterType type) {
	writeView(param, type);
	projectModControllableSnapshot();
}

void GlobalEffectable::writeView(ModFXParam param, FilterType type) {
	if (boundChain_ != nullptr) {
		boundChain_->mod_fx_param = static_cast<uint8_t>(util::to_underlying(param));
		boundChain_->filter_type = static_cast<uint8_t>(util::to_underlying(type));
	}
	else {
		modFXParam_ = param;
		filterType_ = type;
	}
}

void GlobalEffectable::projectView(deluge::snapshot::ModControllableSnapshot& copy) {
	copy.mod_fx_param = modFXParam();
}

void GlobalEffectable::initParams(ParamManager* paramManager) {
	ModControllableAudio::initParams(paramManager, params::Kind::UNPATCHED_GLOBAL);

	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->kind = deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
	setParamsToDefaults(*unpatchedParams, params::Kind::UNPATCHED_GLOBAL,
	                    {params::UNPATCHED_ARP_RATE, params::UNPATCHED_MOD_FX_RATE, params::UNPATCHED_MOD_FX_DEPTH,
	                     params::UNPATCHED_DELAY_RATE, params::UNPATCHED_PAN, params::UNPATCHED_DELAY_AMOUNT,
	                     params::UNPATCHED_REVERB_SEND_AMOUNT, params::UNPATCHED_VOLUME,
	                     params::UNPATCHED_SIDECHAIN_VOLUME, params::UNPATCHED_PITCH_ADJUST, params::UNPATCHED_LPF_RES,
	                     params::UNPATCHED_LPF_FREQ, params::UNPATCHED_HPF_RES, params::UNPATCHED_HPF_FREQ,
	                     params::UNPATCHED_LPF_MORPH, params::UNPATCHED_HPF_MORPH});
}

void GlobalEffectable::initParamsForAudioClip(ParamManagerForTimeline* paramManager) {
	initParams(paramManager);
	// An audio track starts quieter than the song and a kit's affect-entire, which keep the table's default
	UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
	unpatchedParams->params[params::UNPATCHED_VOLUME].setCurrentValueBasicForSetup(
	    *unpatchedParams->docForParam(params::UNPATCHED_VOLUME), -536870912);
}

void GlobalEffectable::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager,
                                       const deluge::project::CompView& comp) {

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
		displayCompressorAndReverbSettings(on, comp);
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

void GlobalEffectable::displayCompressorAndReverbSettings(bool on, const deluge::project::CompView& comp) {
	if (on) {
		etl::string<100> popupMsg;
		popupMsg.append("Comp Mode: ");
		popupMsg.append(getCompressorModeDisplayName(comp));
		popupMsg.append("\n");

		if (comp.editing) {
			popupMsg.append("Comp Param: ");
			popupMsg.append(getCompressorParamDisplayName(comp));
		}
		else {
			// Reverb
			popupMsg.append(deluge::model::reverbPresetName(deluge::model::reverbPresetOf(currentSong->project)));
		}

		display->popupText(popupMsg.c_str());
	}
	else {
		// NOTIFY-DEFERRED: the show half of this pair is display->popupText(), which is out of scope
		// here (the shell will own PopupType). Deferring only the hide half would split an atomic
		// show/hide across the immediate path and the queue, so both move together later.
		display->cancelPopup();
	}
}

char const* GlobalEffectable::getCompressorModeDisplayName(const deluge::project::CompView& comp) {
	return comp.editing ? "FULL" : "ONE";
}

char const* GlobalEffectable::getCompressorParamDisplayName(const deluge::project::CompView& comp) {
	const char* params[util::to_underlying(CompParam::LAST)] = {"ratio", "attack", "release", "hpf", "blend"};
	return params[comp.param % util::to_underlying(CompParam::LAST)];
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
		// NOTIFY-DEFERRED: the show half of this pair is display->popupText(), which is out of scope
		// here (the shell will own PopupType). Deferring only the hide half would split an atomic
		// show/hide across the immediate path and the queue, so both move together later.
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
                                              ModelStackWithThreeMainThings* modelStack,
                                              deluge::project::CompView* comp) {
	// A push with no comp view to move edits a view of its own, which nothing keeps.
	deluge::project::CompView unkept{};
	deluge::project::CompView& compView = comp != nullptr ? *comp : unkept;
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
					deluge::notify::notice(std::string_view(getModFXTypeDisplayName()));
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
					deluge::notify::notice(std::string_view(getModFXParamDisplayName()));
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
					deluge::notify::notice(std::string_view(getFilterTypeDisplayName(filterType())));
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
						deluge::notify::notice(std::string_view(getFilterModeDisplayName(filterType())));
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
						deluge::notify::notice(std::string_view(getFilterModeDisplayName(filterType())));
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
						deluge::notify::notice(std::string_view(getDelaySyncTypeDisplayName()));
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
						deluge::notify::notice(std::string_view(getDelayPingPongStatusDisplayName()));
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
						deluge::notify::notice(std::string_view(displayName));
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
						deluge::notify::notice(std::string_view(getDelayTypeDisplayName()));
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
				// in the compressor view, cycle through the comp params
				// otherwise cycle reverb sizes
				if (!compView.editing) {
					deluge::model::cycleReverbPreset(*currentSong);

					// if mod button is pressed, update mod button pop up
					if (Buttons::isButtonPressed(
					        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
						displayCompressorAndReverbSettings(on, compView);
					}
					else {
						deluge::notify::notice(std::string_view(
						    deluge::model::reverbPresetName(deluge::model::reverbPresetOf(currentSong->project))));
					}
				}
				else {
					compView.param = (compView.param + 1) % util::to_underlying(CompParam::LAST);

					// if mod button is pressed, update mod button pop up
					if (Buttons::isButtonPressed(
					        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
						displayCompressorAndReverbSettings(on, compView);
					}
					else {
						deluge::notify::notice(std::string_view(getCompressorParamDisplayName(compView)));
					}
				}
			}
		}
		else {
			if (on) {
				compView.editing = !compView.editing;

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displayCompressorAndReverbSettings(on, compView);
				}
				else {
					deluge::notify::notice(std::string_view(getCompressorModeDisplayName(compView)));
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

int32_t GlobalEffectable::getKnobPosForNonExistentParam(int32_t whichModEncoder, ModelStackWithAutoParam* modelStack,
                                                        const deluge::project::CompView& comp) {
	int current = 0;
	if (*getModKnobMode() == 4) {

		// this is only reachable in comp editing mode, otherwise it's an existent param
		if (whichModEncoder == 1) { // sidechain (threshold)
			current = (displayedCompressorThreshold() >> 24);
		}
		else if (whichModEncoder == 0) {
			switch (static_cast<CompParam>(comp.param)) {

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
                                                                   ModelStackWithAutoParam* modelStack,
                                                                   const deluge::project::CompView& comp) {
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
			switch (static_cast<CompParam>(comp.param)) {

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
namespace {

/// A mod knob's param in a global effectable: one param, or one for each filter type or mod FX param selected.
struct KnobCell {
	enum class By : uint8_t { NOTHING, FILTER, MOD_FX };
	By by{By::NOTHING};
	std::array<std::optional<deluge::plugin::ParamId>, 3> params{}; // by filter type, or by ModFXParam's order
	bool hiddenWhileEditingComp{false};
};

/// The global effectable's default mod knob layout, [mode][knob], knob 1 the upper.
constexpr std::array<std::array<KnobCell, kNumPhysicalModKnobs>, kNumModButtons> kKnobLayout = [] {
	namespace gp = deluge::global_effectable::param;
	using By = KnobCell::By;
	return std::array<std::array<KnobCell, kNumPhysicalModKnobs>, kNumModButtons>{{
	    {{{By::NOTHING, {gp::kPan}}, {By::NOTHING, {gp::kVolume}}}},
	    {{{By::FILTER, {gp::kLpfResonance, gp::kHpfResonance, gp::kBass}},
	      {By::FILTER, {gp::kLpfFrequency, gp::kHpfFrequency, gp::kTreble}}}},
	    {{{}, {}}},
	    {{{By::NOTHING, {gp::kDelayFeedback}}, {By::NOTHING, {gp::kDelayRate}}}},
	    {{{By::NOTHING, {gp::kReverbAmount}, true}, {}}},
	    {{{By::MOD_FX, {gp::kModFXDepth, gp::kModFXFeedback, gp::kModFXOffset}}, {By::NOTHING, {gp::kModFXRate}}}},
	    {{{}, {By::NOTHING, {gp::kStutterRate}}}},
	    {{{By::NOTHING, {gp::kBitcrushAmount}}, {By::NOTHING, {gp::kSampleRateReduction}}}},
	}};
}();

} // namespace

std::optional<deluge::plugin::ParamId> GlobalEffectable::knobParam(int32_t modKnobMode, int32_t whichModEncoder,
                                                                   FilterType filter, ModFXParam modFX,
                                                                   bool editingCompressor) {
	if (modKnobMode < 0 || modKnobMode >= kNumModButtons) {
		return std::nullopt;
	}
	const KnobCell& cell = kKnobLayout[modKnobMode][whichModEncoder != 0 ? 1 : 0];
	if (cell.hiddenWhileEditingComp && editingCompressor) {
		return std::nullopt;
	}
	switch (cell.by) {
	case KnobCell::By::FILTER:
		// an EQ, or any other filter type, takes the last
		return cell.params[filter == FilterType::LPF ? 0 : filter == FilterType::HPF ? 1 : 2];
	case KnobCell::By::MOD_FX:
		return cell.params[util::to_underlying(modFX)];
	case KnobCell::By::NOTHING:
		break;
	}
	return cell.params[0];
}

int32_t GlobalEffectable::getParameterFromKnob(int32_t whichModEncoder, const deluge::project::CompView& comp) {
	// The compressor view is the song's alone: a clip's chain keeps its reverb send.
	std::optional<deluge::plugin::ParamId> param =
	    knobParam(*getModKnobMode(), whichModEncoder, filterType(), modFXParam(), comp.editing && isSong());
	return param ? *deluge::global_effectable::runtime_index(*param) : 255;
}

ModelStackWithAutoParam* GlobalEffectable::getParamFromModEncoder(int32_t whichModEncoder,
                                                                  ModelStackWithThreeMainThings* modelStack,
                                                                  bool allowCreation,
                                                                  const deluge::project::CompView& comp) {
	ParamCollectionSummary* summary = modelStack->paramManager->getUnpatchedParamSetSummary();
	ParamCollection* paramCollection = summary->paramCollection;
	int32_t paramId;

	paramId = getParameterFromKnob(whichModEncoder, comp);

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

void GlobalEffectable::writeAttributesToFile(Serializer& writer, bool writeAutomation) {
	// Its record's, while it plays one.
	writer.writeAttribute("modFXCurrentParam", modFXParamToString(modFXParam()));
	writer.writeAttribute("currentFilterType", filterTypeToString(filterType()));
	ModControllableAudio::writeAttributesToFile(writer, effects());
	// Community Firmware parameters (always write them after the official ones, just before closing the parent tag)
	// <--
}

void GlobalEffectable::writeTagsToFile(Serializer& writer, ParamManager* paramManager, bool writeAutomation,
                                       Song* knobsSong, std::optional<deluge::project::ControlsAddress> knobsOwner) {
	if (paramManager) {
		writer.writeOpeningTagBeginning("defaultParams");
		GlobalEffectable::writeParamAttributesToFile(writer, paramManager, writeAutomation);
		writer.writeOpeningTagEnd();
		GlobalEffectable::writeParamTagsToFile(writer, paramManager, writeAutomation);
		writer.writeClosingTag("defaultParams");
	}

	// A clip chain's render follows its threshold param; the file holds the setting, as the record does.
	ModControllableAudio::writeTagsToFile(writer, effects(), knobsSong, knobsOwner);
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
	deluge::effects::EffectStates next = effects();
	deluge::effects::decode(element, next, {.input_tick_magnitude = tickMagnitude});
	delayTypePending = !deluge::effects::saves_delay_type(element);
	ModFXParam param = modFXParam();
	FilterType type = filterType();
	if (const deluge::serialization::Value* word = element.find("modFXCurrentParam"); word && word->isScalar()) {
		param = stringToModFXParam(word->str().c_str());
	}
	if (const deluge::serialization::Value* word = element.find("currentFilterType"); word && word->isScalar()) {
		type = stringToFilterType(word->str().c_str());
	}
	writeView(param, type);
	setEffects(next); // projects the view with the effects
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

	delayWorkingState.delayFeedbackAmount = deluge::effects::final_value(
	    deluge::effects::Delay::kFeedback, unpatchedParams->getValue(params::UNPATCHED_DELAY_AMOUNT));
	if (shouldLimitDelayFeedback) {
		delayWorkingState.delayFeedbackAmount =
		    std::min(delayWorkingState.delayFeedbackAmount, (int32_t)(1 << 30) - (1 << 26));
	}
	delayWorkingState.userDelayRate = deluge::effects::final_value(
	    deluge::effects::Delay::kRate, unpatchedParams->getValue(params::UNPATCHED_DELAY_RATE));
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
