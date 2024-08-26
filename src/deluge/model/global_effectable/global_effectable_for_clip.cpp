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
#include "gui/l10n/l10n.h"
#include "gui/views/view.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "processing/engines/audio_engine.h"
#include <string.h>
// #include <algorithm>
#include "hid/buttons.h"
#include "memory/general_memory_allocator.h"
#include "model/clip/clip.h"
#include "model/instrument/kit.h"
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
[[gnu::hot]] void GlobalEffectableForClip::renderOutput(ModelStackWithTimelineCounter* modelStack,
                                                        ParamManager* paramManagerForClip, StereoSample* outputBuffer,
                                                        int32_t numSamples, int32_t* reverbBuffer,
                                                        int32_t reverbAmountAdjust, int32_t sideChainHitPending,
                                                        bool shouldLimitDelayFeedback, bool isClipActive,
                                                        OutputType outputType, SampleRecorder* recorder) {
	UnpatchedParamSet* unpatchedParams = paramManagerForClip->getUnpatchedParamSet();

	// Process FX and stuff. For kits, stutter happens before reverb send
	// The >>1 is to make up for the fact that we've got the preset default to effect a multiplication of 2 already (the
	// maximum multiplication would be 4)
	int32_t a = cableToLinearParamShortcut(unpatchedParams->getValue(params::UNPATCHED_VOLUME));
	int32_t volumeAdjustment = getFinalParameterValueVolume(134217728, a) >> 1;

	int32_t volumePostFX = volumeAdjustment;

	// Make it a bit bigger so that default filter resonance doesn't reduce volume overall.
	// Unfortunately when I first implemented this for Kits, I just fudged a number which didn't give the 100% accuracy
	// that I need for AudioOutputs, and I now have to maintain both for backwards compatibility
	if (outputType == OutputType::AUDIO) {
		volumePostFX += multiply_32x32_rshift32_rounded(volumeAdjustment, 471633397);
	}
	else {
		volumePostFX += (volumeAdjustment >> 2);
	}

	int32_t reverbAmountAdjustForDrums = multiply_32x32_rshift32_rounded(reverbAmountAdjust, volumeAdjustment) << 5;

	int32_t pitchAdjust =
	    getFinalParameterValueExp(kMaxSampleValue, unpatchedParams->getValue(params::UNPATCHED_PITCH_ADJUST) >> 3);

	Delay::State delayWorkingState =
	    createDelayWorkingState(*paramManagerForClip, shouldLimitDelayFeedback, renderedLastTime);
	if (outputType == OutputType::AUDIO) {
		delayWorkingState.analog_saturation = 5;
	}

	setupFilterSetConfig(&volumePostFX, paramManagerForClip);

	int32_t reverbSendAmount = getFinalParameterValueVolume(
	    reverbAmountAdjust,
	    cableToLinearParamShortcut(unpatchedParams->getValue(params::UNPATCHED_REVERB_SEND_AMOUNT)));

	int32_t pan = unpatchedParams->getValue(params::UNPATCHED_PAN) >> 1;

	// Render sidechain
	int32_t sidechainVolumeParam = unpatchedParams->getValue(params::UNPATCHED_SIDECHAIN_VOLUME);
	int32_t postReverbVolume = paramNeutralValues[params::GLOBAL_VOLUME_POST_REVERB_SEND];
	if (sidechainVolumeParam != -2147483648) {
		if (sideChainHitPending != 0) {
			sidechain.registerHit(sideChainHitPending);
		}
		int32_t sidechainOutput =
		    sidechain.render(numSamples, unpatchedParams->getValue(params::UNPATCHED_SIDECHAIN_SHAPE));

		int32_t positivePatchedValue =
		    multiply_32x32_rshift32(sidechainOutput, getSidechainVolumeAmountAsPatchCableDepth(paramManagerForClip))
		    + 536870912;
		postReverbVolume = (positivePatchedValue >> 15)
		                   * (positivePatchedValue
		                      >> 16); // This is tied to getParamNeutralValue(params::GLOBAL_VOLUME_POST_REVERB_SEND)
		                              // returning 134217728
	}
	q31_t compThreshold = unpatchedParams->getValue(params::UNPATCHED_COMPRESSOR_THRESHOLD);
	compressor.setThreshold(compThreshold);
	StereoSample globalEffectableBuffer[SSI_TX_BUFFER_NUM_SAMPLES] __attribute__((aligned(CACHE_LINE_SIZE)));

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
	                             delayWorkingState, renderedLastTime, reverbSendAmount);
	processStutter(globalEffectableBuffer, numSamples, paramManagerForClip);
	// record before pan/compression/volume to keep volumes consistent
	if (recorder && recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
		// we need to double it because for reasons I don't understand audio clips max volume is half the sample volume
		recorder->feedAudio((int32_t*)globalEffectableBuffer, numSamples, true, 2);
	}

	processReverbSendAndVolume(globalEffectableBuffer, numSamples, reverbBuffer, volumePostFX, postReverbVolume,
	                           reverbSendAmount, pan, true);
	if (compThreshold > 0) {
		compressor.renderVolNeutral(globalEffectableBuffer, numSamples, volumePostFX);
	}
	else {
		compressor.reset();
	}

	addAudio(globalEffectableBuffer, outputBuffer, numSamples);

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

void GlobalEffectableForClip::modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) {
	if (whichModButton == 4) {
		displaySidechainAndReverbSettings(on);
		return;
	}

	return GlobalEffectable::modButtonAction(whichModButton, on, paramManager);
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
					    (currentSong->insideWorldTickMagnitude + currentSong->insideWorldTickMagnitudeOffsetFromBPM);
				}
				else {
					insideWorldTickMagnitude = FlashStorage::defaultMagnitude;
				}
				if (sidechain.syncLevel == (SyncLevel)(7 - insideWorldTickMagnitude)) {
					sidechain.syncLevel = (SyncLevel)(9 - insideWorldTickMagnitude);
				}
				else {
					sidechain.syncLevel = (SyncLevel)(7 - insideWorldTickMagnitude);
				}

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
