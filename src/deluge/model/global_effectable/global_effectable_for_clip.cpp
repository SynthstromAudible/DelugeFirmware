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
#include "gui/l10n/l10n.h"
#include "gui/views/view.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/edits/change_effect.h"
#include "processing/engines/audio_engine.h"
#include "util/functions.h"
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
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"

namespace params = deluge::modulation::params;

GlobalEffectableForClip::GlobalEffectableForClip()
    : chain_{deluge::model::kModFxRingAllocator, deluge::model::kGranularAllocator, deluge::model::delayArena(),
             paramNeutralValues[params::GLOBAL_VOLUME_POST_REVERB_SEND]} {
	// A new owner's settings live in its chain state, and its filter mode and mod FX type are set, once its chain
	// exists.
	bindChainState();
	setLpfMode(FilterMode::TRANSISTOR_24DB);
	filter().reset();
	assignModFXType(ModFXType::NONE);
}

GlobalEffectableForClip::~GlobalEffectableForClip() {
	releaseChainMemory();
}

// Beware - unlike usual, modelStack might have a NULL timelineCounter.
[[gnu::hot]] void GlobalEffectableForClip::renderOutput(ModelStackWithTimelineCounter* modelStack,
                                                        ParamManager* paramManagerForClip,
                                                        std::span<StereoSample> output, int32_t* reverbBuffer,
                                                        int32_t reverbAmountAdjust, int32_t sideChainHitPending,
                                                        bool shouldLimitDelayFeedback, bool isClipActive,
                                                        OutputType outputType, SampleRecorder* recorder) {
	UnpatchedParamSet* unpatchedParams = paramManagerForClip->getUnpatchedParamSet();

	// Process FX and stuff. For kits, stutter happens before reverb send
	// The >>1 is to make up for the fact that we've got the preset default to effect a multiplication of 2 already (the
	// maximum multiplication would be 4)
	int32_t a = cableToLinearParamShortcut(unpatchedParams->getValue(params::UNPATCHED_VOLUME));
	int32_t volumeAdjustment = getFinalParameterValueVolume(134217728, a) >> 1;

	int32_t reverbAmountAdjustForDrums = multiply_32x32_rshift32_rounded(reverbAmountAdjust, volumeAdjustment) << 5;

	int32_t pitchAdjust =
	    getFinalParameterValueExp(kMaxSampleValue, unpatchedParams->getValue(params::UNPATCHED_PITCH_ADJUST) >> 3);

	// The delay starts or stops on whether the content rendered last block.
	deluge::effects::Delay::State delayWorkingState =
	    createDelayWorkingState(*paramManagerForClip, shouldLimitDelayFeedback, renderedLastTime);
	if (outputType == OutputType::AUDIO) {
		delayWorkingState.analog_saturation = 5;
	}

	const deluge::effects::ClipChainInputs in =
	    deluge::model::clipChainInputs(*this, *paramManagerForClip, volumeAdjustment, reverbAmountAdjust,
	                                   sideChainHitPending, delayWorkingState, outputType);
	deluge::effects::ClipBlock levels = chain_.begin_block(in, output.size());

	alignas(CACHE_LINE_SIZE) StereoSample global_effectable_memory[SSI_TX_BUFFER_NUM_SAMPLES];
	memset(global_effectable_memory, 0, sizeof(StereoSample) * output.size());
	std::span<StereoSample> global_effectable_audio{global_effectable_memory, output.size()};

	// Render actual Drums / AudioClip
	renderedLastTime = renderGlobalEffectableForClip(
	    modelStack, global_effectable_audio, nullptr, reverbBuffer, reverbAmountAdjustForDrums, sideChainHitPending,
	    shouldLimitDelayFeedback, isClipActive, pitchAdjust, 134217728, 134217728);

	checkStack("delay");
	// The recorder takes the block after the stutter, before pan, volume and compression, so its level is the clip's.
	auto record = [&](std::span<const StereoSample> /*block*/) {
		if (recorder != nullptr && recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
			// Self-overdubs compensate for sample playback's 1/8 scaling. Standalone sampling retains its existing
			// gain.
			recorder->feedAudio(global_effectable_audio, true, recorder->recordingOwnClip ? 3 : 2);
		}
	};
	if (chain_.process(global_effectable_audio, in, levels, renderedLastTime, {reverbBuffer, output.size()}, record)) {
		AudioEngine::timeThereWasLastSomeReverb = AudioEngine::audioSampleTimer;
	}

	// Add the global effectable data to the output
	std::ranges::transform(global_effectable_audio, output, output.begin(), std::plus{});

	if (playbackHandler.isEitherClockActive() && (playbackHandler.ticksLeftInCountIn == 0) && isClipActive) {
		auto& interpolating_params = paramManagerForClip->getUnpatchedParamSetSummary()->whichParamsAreInterpolating;
		const bool result = (params::kMaxNumUnpatchedParams > 32)
		                        ? (interpolating_params[0] != 0u || interpolating_params[1] != 0u)
		                        : (interpolating_params[0] != 0u);
		if (result) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addOtherTwoThingsButNoNoteRow(this, paramManagerForClip);
			paramManagerForClip->toForTimeline()->tickSamples(output.size(), modelStackWithThreeMainThings);
		}
	}
}

int32_t GlobalEffectableForClip::getSidechainVolumeAmountAsPatchCableDepth(ParamManager* paramManager) {
	int32_t sidechainVolumeParam = paramManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_SIDECHAIN_VOLUME);
	return (sidechainVolumeParam >> 2) + 536870912;
}

int32_t GlobalEffectableForClip::getParameterFromKnob(int32_t whichModEncoder) {

	int32_t modKnobMode = *getModKnobMode();

	if (modKnobMode == 4 && whichModEncoder) {
		return params::UNPATCHED_SIDECHAIN_VOLUME;
	}
	else if (modKnobMode == 6 && !whichModEncoder) {
		return params::UNPATCHED_PITCH_ADJUST;
	}

	return GlobalEffectable::getParameterFromKnob(whichModEncoder);
}

void GlobalEffectableForClip::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) {
	if (whichModButton == 4) {
		displaySidechainAndReverbSettings(on);
		return;
	}

	GlobalEffectable::modButtonAction(whichModButton, on, paramManager);
}

bool GlobalEffectableForClip::modEncoderButtonAction(uint8_t whichModEncoder, bool on,
                                                     ModelStackWithThreeMainThings* modelStack) {

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
					display->displayPopup(getSidechainDisplayName());
				}
				return true;
			}
			else if (whichModEncoder == 0) { // reverb
				view.cycleThroughReverbPresets();

				// if mod button is pressed, update mod button pop up
				if (Buttons::isButtonPressed(
				        deluge::hid::button::fromXY(modButtonX[modKnobMode], modButtonY[modKnobMode]))) {
					displaySidechainAndReverbSettings(on);
				}
				else {
					display->displayPopup(view.getReverbPresetDisplayName(view.getCurrentReverbPreset()));
				}
				return true;
			}
		}
	}

	return GlobalEffectable::modEncoderButtonAction(whichModEncoder, on, modelStack);
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
