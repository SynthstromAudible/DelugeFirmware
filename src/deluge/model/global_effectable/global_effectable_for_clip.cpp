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

#include "model/global_effectable/global_effectable_for_clip.h"
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/curves/param_curve.h"
#include "effects/global_effectable/params.h"
#include "gui/l10n/l10n.h"
#include "gui/views/view.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/edits/change_effect.h"
#include "processing/engines/audio_engine.h"
#include "synth/matrix/descriptors.h" // kParamNeutral, kParamRange
#include "util/functions.h"
#include "util/notify.h"
#include <limits>
#include <string.h>
// #include <algorithm>
#include "hid/buttons.h"
#include "memory/general_memory_allocator.h"
#include "memory/stack_guard.h"
#include "model/clip/clip.h"
#include "model/fx/chain_inputs.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/delay_arena_app.h"
#include "model/mod_controllable/mod_fx_allocator.h"
#include "model/song/reverb_presets.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"

namespace params = deluge::modulation::params;

int32_t GlobalEffectableForClip::getSidechainVolumeAmountAsPatchCableDepth(ParamManager* paramManager) {
	return deluge::model::sidechainDepth(*paramManager);
}

std::optional<deluge::plugin::ParamId> GlobalEffectableForClip::clipKnobParam(int32_t modKnobMode,
                                                                              int32_t whichModEncoder) {
	if (modKnobMode == 4 && whichModEncoder != 0) {
		return deluge::global_effectable::param::kSidechainCompressorVolume;
	}
	if (modKnobMode == 6 && whichModEncoder == 0) {
		return deluge::global_effectable::param::kPitchAdjust;
	}
	return std::nullopt;
}

int32_t GlobalEffectableForClip::getParameterFromKnob(int32_t whichModEncoder, const deluge::project::CompView& comp) {
	if (std::optional<deluge::plugin::ParamId> param = clipKnobParam(*getModKnobMode(), whichModEncoder)) {
		return *deluge::global_effectable::runtime_index(*param);
	}
	return GlobalEffectable::getParameterFromKnob(whichModEncoder, comp);
}

void GlobalEffectableForClip::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager,
                                              const deluge::project::CompView& comp) {
	if (whichModButton == 4) {
		displaySidechainAndReverbSettings(on);
		return;
	}

	GlobalEffectable::modButtonAction(whichModButton, on, paramManager, comp);
}

bool GlobalEffectableForClip::modEncoderButtonAction(uint8_t whichModEncoder, bool on,
                                                     ModelStackWithThreeMainThings* modelStack,
                                                     deluge::project::CompView* comp) {

	if (on && !Buttons::isShiftButtonPressed()) {
		int32_t modKnobMode = *getModKnobMode();
		if (modKnobMode == 4) {
			if (whichModEncoder == 1) { // Sidechain
				int32_t insideWorldTickMagnitude;
				if (currentSong) { // Bit of a hack just referring to currentSong in here...
					insideWorldTickMagnitude =
					    (currentSong->project.transport.inside_world_tick_magnitude
					     + currentSong->project.transport.inside_world_tick_magnitude_offset_from_bpm);
				}
				else {
					insideWorldTickMagnitude = FlashStorage::defaultMagnitude;
				}
				const int32_t level = effects().sidechain.sync_level == (SyncLevel)(7 - insideWorldTickMagnitude)
				                          ? 9 - insideWorldTickMagnitude
				                          : 7 - insideWorldTickMagnitude;
				deluge::edits::changeEffect(*this, "sidechain/syncLevel",
				                            deluge::serialization::Value::makeNumber(level));

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displaySidechainAndReverbSettings(on);
				}
				else {
					deluge::notify::notice(std::string_view(getSidechainDisplayName()));
				}
				return true;
			}
			else if (whichModEncoder == 0) { // reverb
				deluge::model::cycleReverbPreset(*currentSong);

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displaySidechainAndReverbSettings(on);
				}
				else {
					deluge::notify::notice(std::string_view(
					    deluge::model::reverbPresetName(deluge::model::reverbPresetOf(currentSong->project))));
				}
				return true;
			}
		}
	}

	return GlobalEffectable::modEncoderButtonAction(whichModEncoder, on, modelStack, comp);
}

// We pass activeClip into this because although each child of GlobalEffectableForClip inherits Output, one of them does
// so via Instrument, so we can't make GlobalEffectableForClip inherit directly from Output, so no access to activeClip
void GlobalEffectableForClip::getThingWithMostReverb(Clip* activeClip, Sound** soundWithMostReverb,
                                                     ParamManager** paramManagerWithMostReverb,
                                                     GlobalEffectableForClip** globalEffectableWithMostReverb,
                                                     int32_t* highestReverbAmountFound) {

	if (activeClip) {

		ParamManagerForTimeline* activeParamManager = &activeClip->paramManager;

		UnpatchedParamSet* unpatchedParams = activeParamManager->getUnpatchedParamSet();

		if (!unpatchedParams->params[params::UNPATCHED_REVERB_SEND_AMOUNT].isAutomated(
		        *unpatchedParams->docForParam(params::UNPATCHED_REVERB_SEND_AMOUNT))
		    && unpatchedParams->params[params::UNPATCHED_REVERB_SEND_AMOUNT].containsSomething(
		        *unpatchedParams->docForParam(params::UNPATCHED_REVERB_SEND_AMOUNT), -2147483648)) {

			int32_t reverbHere = unpatchedParams->getValue(params::UNPATCHED_REVERB_SEND_AMOUNT);
			if (*highestReverbAmountFound < reverbHere) {
				*highestReverbAmountFound = reverbHere;
				*soundWithMostReverb = nullptr;
				*paramManagerWithMostReverb = activeParamManager;
				*globalEffectableWithMostReverb = this;
			}
		}
	}
}
