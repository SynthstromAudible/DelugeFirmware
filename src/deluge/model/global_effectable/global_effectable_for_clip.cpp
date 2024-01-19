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
#include "definitions_cxx.hpp"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/matrix/matrix_driver.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "modulation/params/param_manager.h"
#include "processing/engines/audio_engine.h"
#include <string.h>
//#include <algorithm>
#include "dsp/sidechain/sidechain.h"
#include "hid/buttons.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "playback/playback_handler.h"

extern "C" {
#include "drivers/ssi/ssi.h"
}

namespace params = deluge::modulation::params;

GlobalEffectableForClip::GlobalEffectableForClip() {
	postReverbVolumeLastTime = paramNeutralValues[params::GLOBAL_VOLUME_POST_REVERB_SEND];

	lastSaturationTanHWorkingValue[0] = 2147483648;
	lastSaturationTanHWorkingValue[1] = 2147483648;
	renderedLastTime = false;
}

// Beware - unlike usual, modelStack might have a NULL timelineCounter.
void GlobalEffectableForClip::renderOutput(ModelStackWithTimelineCounter* modelStack, ParamManager* paramManagerForClip,
                                           StereoSample* outputBuffer, int32_t numSamples, int32_t* reverbBuffer,
                                           int32_t reverbAmountAdjust, int32_t sideChainHitPending,
                                           bool shouldLimitDelayFeedback, bool isClipActive, OutputType outputType,
                                           int32_t analogDelaySaturationAmount) {
	UnpatchedParamSet* unpatchedParams = paramManagerForClip->getUnpatchedParamSet();

	// Process FX and stuff. For kits, stutter happens before reverb send
	// The >>1 is to make up for the fact that we've got the preset default to effect a multiplication of 2 already (the maximum multiplication would be 4)
	int32_t a = cableToLinearParamShortcut(unpatchedParams->getValue(params::UNPATCHED_VOLUME));
	int32_t volumeAdjustment = getFinalParameterValueVolume(134217728, a) >> 1;

	int32_t volumePostFX = volumeAdjustment;

	// Make it a bit bigger so that default filter resonance doesn't reduce volume overall.
	// Unfortunately when I first implemented this for Kits, I just fudged a number which didn't give the 100% accuracy that I need for AudioOutputs,
	// and I now have to maintain both for backwards compatibility
	if (outputType == OutputType::AUDIO) {
		volumePostFX += multiply_32x32_rshift32_rounded(volumeAdjustment, 471633397);
	}
	else {
		volumePostFX += (volumeAdjustment >> 2);
	}

	int32_t reverbAmountAdjustForDrums = multiply_32x32_rshift32_rounded(reverbAmountAdjust, volumeAdjustment) << 5;

	int32_t pitchAdjust =
	    getFinalParameterValueExp(16777216, unpatchedParams->getValue(params::UNPATCHED_PITCH_ADJUST) >> 3);

	DelayWorkingState delayWorkingState;
	setupDelayWorkingState(&delayWorkingState, paramManagerForClip, shouldLimitDelayFeedback, renderedLastTime);

	setupFilterSetConfig(&volumePostFX, paramManagerForClip);

	int32_t reverbSendAmount = getFinalParameterValueVolume(
	    reverbAmountAdjust,
	    cableToLinearParamShortcut(unpatchedParams->getValue(params::UNPATCHED_REVERB_SEND_AMOUNT)));

	int32_t pan = unpatchedParams->getValue(params::UNPATCHED_PAN) >> 1;

	// Render compressor
	int32_t sidechainVolumeParam = unpatchedParams->getValue(params::UNPATCHED_SIDECHAIN_VOLUME);
	int32_t postReverbVolume = paramNeutralValues[params::GLOBAL_VOLUME_POST_REVERB_SEND];
	if (sidechainVolumeParam != -2147483648) {
		if (sideChainHitPending != 0) {
			compressor.registerHit(sideChainHitPending);
		}
		int32_t compressorOutput =
		    compressor.render(numSamples, unpatchedParams->getValue(params::UNPATCHED_COMPRESSOR_SHAPE));

		int32_t positivePatchedValue =
		    multiply_32x32_rshift32(compressorOutput, getSidechainVolumeAmountAsPatchCableDepth(paramManagerForClip))
		    + 536870912;
		postReverbVolume =
		    (positivePatchedValue >> 15)
		    * (positivePatchedValue
		       >> 16); // This is tied to getParamNeutralValue(params::GLOBAL_VOLUME_POST_REVERB_SEND) returning 134217728
	}

	static StereoSample globalEffectableBuffer[SSI_TX_BUFFER_NUM_SAMPLES] __attribute__((aligned(CACHE_LINE_SIZE)));

	bool canRenderDirectlyIntoSongBuffer =
	    !isKit() && !filterSet.isOn() && !delayWorkingState.doDelay && (!pan || !AudioEngine::renderInStereo)
	    && !clippingAmount && !hasBassAdjusted(paramManagerForClip) && !hasTrebleAdjusted(paramManagerForClip)
	    && !reverbSendAmount && !isBitcrushingEnabled(paramManagerForClip) && !isSRREnabled(paramManagerForClip)
	    && getActiveModFXType(paramManagerForClip) == ModFXType::NONE && stutterer.status == STUTTERER_STATUS_OFF;

	if (canRenderDirectlyIntoSongBuffer) {

		int32_t postFXAndReverbVolumeStart = (multiply_32x32_rshift32(postReverbVolumeLastTime, volumePostFX) << 5);
		if (postFXAndReverbVolumeStart > 134217728) {
			goto doNormal; // If it's too loud, this optimized routine can't handle it. This is a design flaw...
		}

		int32_t postFXAndReverbVolumeEnd = (multiply_32x32_rshift32(postReverbVolume, volumePostFX) << 5);
		if (postFXAndReverbVolumeEnd > 134217728) {
			goto doNormal;
		}

		// If it's a mono sample, that's going to have to get rendered into a mono buffer first before it can be copied out to the stereo song-level buffer
		if (willRenderAsOneChannelOnlyWhichWillNeedCopying()) {
			memset(globalEffectableBuffer, 0, sizeof(int32_t) * numSamples);
			renderedLastTime = renderGlobalEffectableForClip(
			    modelStack, globalEffectableBuffer, (int32_t*)outputBuffer, numSamples, reverbBuffer,
			    reverbAmountAdjustForDrums, sideChainHitPending, shouldLimitDelayFeedback, isClipActive, pitchAdjust,
			    postFXAndReverbVolumeStart, postFXAndReverbVolumeEnd);
		}

		// Or if it's a stereo sample, it can render directly into the song buffer
		else {
			renderedLastTime = renderGlobalEffectableForClip(modelStack, outputBuffer, NULL, numSamples, reverbBuffer,
			                                                 reverbAmountAdjustForDrums, sideChainHitPending,
			                                                 shouldLimitDelayFeedback, isClipActive, pitchAdjust,
			                                                 postFXAndReverbVolumeStart, postFXAndReverbVolumeEnd);
		}
	}

	else {
doNormal:
		memset(globalEffectableBuffer, 0, sizeof(StereoSample) * numSamples);

		// Render actual Drums / AudioClip
		renderedLastTime = renderGlobalEffectableForClip(
		    modelStack, globalEffectableBuffer, NULL, numSamples, reverbBuffer, reverbAmountAdjustForDrums,
		    sideChainHitPending, shouldLimitDelayFeedback, isClipActive, pitchAdjust, 134217728, 134217728);

		// Render saturation
		if (clippingAmount) {
			StereoSample const* const bufferEnd = globalEffectableBuffer + numSamples;

			StereoSample* __restrict__ currentSample = globalEffectableBuffer;
			do {
				saturate(&currentSample->l, &lastSaturationTanHWorkingValue[0]);
				saturate(&currentSample->r, &lastSaturationTanHWorkingValue[1]);
			} while (++currentSample != bufferEnd);
		}

		// Render filters
		processFilters(globalEffectableBuffer, numSamples);

		// Render FX
		processSRRAndBitcrushing(globalEffectableBuffer, numSamples, &volumePostFX, paramManagerForClip);
		processFXForGlobalEffectable(globalEffectableBuffer, numSamples, &volumePostFX, paramManagerForClip,
		                             &delayWorkingState, analogDelaySaturationAmount, renderedLastTime);
		processStutter(globalEffectableBuffer, numSamples, paramManagerForClip);

		int32_t postReverbSendVolumeIncrement =
		    (int32_t)((double)(postReverbVolume - postReverbVolumeLastTime) / (double)numSamples);

		processReverbSendAndVolume(globalEffectableBuffer, numSamples, reverbBuffer, volumePostFX,
		                           postReverbVolumeLastTime, reverbSendAmount, pan, true,
		                           postReverbSendVolumeIncrement);
		addAudio(globalEffectableBuffer, outputBuffer, numSamples);
	}

	postReverbVolumeLastTime = postReverbVolume;

	if (playbackHandler.isEitherClockActive() && !playbackHandler.ticksLeftInCountIn && isClipActive) {
		const bool result =
		    params::kMaxNumUnpatchedParams > 32
		        ? paramManagerForClip->getUnpatchedParamSetSummary()->whichParamsAreInterpolating[0]
		              || paramManagerForClip->getUnpatchedParamSetSummary()->whichParamsAreInterpolating[1]
		        : paramManagerForClip->getUnpatchedParamSetSummary()->whichParamsAreInterpolating[0];
		if (result) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addOtherTwoThingsButNoNoteRow(this, paramManagerForClip);
			paramManagerForClip->toForTimeline()->tickSamples(numSamples, modelStackWithThreeMainThings);
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

bool GlobalEffectableForClip::modEncoderButtonAction(uint8_t whichModEncoder, bool on,
                                                     ModelStackWithThreeMainThings* modelStack) {

	if (on && !Buttons::isShiftButtonPressed()) {
		if (*getModKnobMode() == 4) {
			if (whichModEncoder == 1) { // Sidechain
				if (compressor.syncLevel == SYNC_LEVEL_32ND) {
					compressor.syncLevel = SYNC_LEVEL_128TH;
				}
				else {
					compressor.syncLevel = SYNC_LEVEL_32ND;
				}
				if (compressor.syncLevel == SYNC_LEVEL_32ND) {
					display->displayPopup("SLOW");
				}
				else {
					display->displayPopup("FAST");
				}
				return true;
			}
		}
	}

	return GlobalEffectable::modEncoderButtonAction(whichModEncoder, on, modelStack);
}

// We pass activeClip into this because although each child of GlobalEffectableForClip inherits Output, one of them does so via Instrument, so
// we can't make GlobalEffectableForClip inherit directly from Output, so no access to activeClip
void GlobalEffectableForClip::getThingWithMostReverb(Clip* activeClip, Sound** soundWithMostReverb,
                                                     ParamManager** paramManagerWithMostReverb,
                                                     GlobalEffectableForClip** globalEffectableWithMostReverb,
                                                     int32_t* highestReverbAmountFound) {

	if (activeClip) {

		ParamManagerForTimeline* activeParamManager = &activeClip->paramManager;

		UnpatchedParamSet* unpatchedParams = activeParamManager->getUnpatchedParamSet();

		if (!unpatchedParams->params[params::UNPATCHED_REVERB_SEND_AMOUNT].isAutomated()
		    && unpatchedParams->params[params::UNPATCHED_REVERB_SEND_AMOUNT].containsSomething(-2147483648)) {

			int32_t reverbHere = unpatchedParams->getValue(params::UNPATCHED_REVERB_SEND_AMOUNT);
			if (*highestReverbAmountFound < reverbHere) {
				*highestReverbAmountFound = reverbHere;
				*soundWithMostReverb = NULL;
				*paramManagerWithMostReverb = activeParamManager;
				*globalEffectableWithMostReverb = this;
			}
		}
	}
}
