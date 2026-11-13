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
#include "dsp/stereo_sample.h"
#include "gui/l10n/l10n.h"
#include "gui/views/view.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/global_effectable/output_stage_gain.h"
#include "processing/engines/audio_engine.h"
#include <limits>
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
	this->postReverbVolumeLastTime = paramNeutralValues[params::GLOBAL_VOLUME_POST_REVERB_SEND];
}

// Beware - unlike usual, modelStack might have a NULL timelineCounter.
[[gnu::hot]] void GlobalEffectableForClip::renderOutput(ModelStackWithTimelineCounter* modelStack,
                                                        ParamManager* paramManagerForClip,
                                                        std::span<StereoSample> output, int32_t* reverbBuffer,
                                                        int32_t reverbAmountAdjust, int32_t sideChainHitPending,
                                                        bool shouldLimitDelayFeedback, bool isClipActive,
                                                        OutputType outputType, SampleRecorder* recorder) {
	// Volume (for the drums' reverb) and pitch adjust, only read from the params if something renders
	RenderAdjustments adjustments{*this, *paramManagerForClip, reverbAmountAdjust, true};

	alignas(CACHE_LINE_SIZE) StereoSample global_effectable_memory[SSI_TX_BUFFER_NUM_SAMPLES];
	memset(global_effectable_memory, 0, sizeof(StereoSample) * output.size());
	std::span<StereoSample> global_effectable_audio{global_effectable_memory, output.size()};

	// Render actual Drums / AudioClip. Nothing below affects this, so it comes first, to know whether we can skip the
	// rest: their own render skipping makes it cheap when there's nothing to play.
	bool renderedNow = renderGlobalEffectableForClip(modelStack, global_effectable_audio, nullptr, reverbBuffer,
	                                                 adjustments, sideChainHitPending, shouldLimitDelayFeedback,
	                                                 isClipActive, 134217728, 134217728);

	bool resumed = false;
	// check if we should still skip, then return early if so
	if (skippingRendering) {
		bool recording = recorder != nullptr && recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING;
		if (!renderedNow && !recording) {
			// we're still skipping rendering so just clear stuff, tick params, get out
			renderedLastTime = false;
			// Nothing else touches the compressor while skipping, so it only needs clearing once (it's cold memory)
			if (!compressorClearedWhileSkipping) {
				compressor.reset();
				compressor.gainReduction = 0;
				compressorClearedWhileSkipping = true;
			}
			tickParamAutomation(modelStack, paramManagerForClip, output.size(), isClipActive);
			return;
		}
		stopSkippingRendering(paramManagerForClip);
		resumed = true;
	}

	UnpatchedParamSet* unpatchedParams = paramManagerForClip->getUnpatchedParamSet();

	// Process FX and stuff. For kits, stutter happens before reverb send
	// The >>1 is to make up for the fact that we've got the preset default to effect a multiplication of 2 already (the
	// maximum multiplication would be 4)
	int32_t volumeAdjustment = adjustments.volume();

	int32_t volumePostFX = volumeAdjustment;

	// Make it a bit bigger so that default filter resonance doesn't reduce volume overall.
	// Unfortunately when I first implemented this for Kits, I just fudged a number which didn't give the 100% accuracy
	// that I need for AudioOutputs, and I now have to maintain both for backwards compatibility
	volumePostFX += (outputType == OutputType::AUDIO) ? multiply_32x32_rshift32_rounded(volumeAdjustment, 471633397)
	                                                  : (volumeAdjustment >> kKitPostFXBaselineShift);

	// The delay takes in sound from this render
	Delay::State delayWorkingState =
	    createDelayWorkingState(*paramManagerForClip, shouldLimitDelayFeedback, renderedNow);
	renderedLastTime = renderedNow;
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
	if (sidechainVolumeParam != std::numeric_limits<q31_t>::min()) {
		if (sideChainHitPending != 0) {
			sidechain.registerHit(sideChainHitPending);
		}
		int32_t sidechainOutput =
		    sidechain.render(output.size(), unpatchedParams->getValue(params::UNPATCHED_SIDECHAIN_SHAPE));

		int32_t positivePatchedValue =
		    multiply_32x32_rshift32(sidechainOutput, getSidechainVolumeAmountAsPatchCableDepth(paramManagerForClip))
		    + 536870912;

		// This is tied to getParamNeutralValue(params::GLOBAL_VOLUME_POST_REVERB_SEND) returning 134217728
		postReverbVolume = (positivePatchedValue >> 15) * (positivePatchedValue >> 16);
	}
	if (resumed) {
		postReverbVolumeLastTime = postReverbVolume; // Its last value is from before skipping: don't ramp from it
	}

	const q31_t compThreshold = unpatchedParams->getValue(params::UNPATCHED_COMPRESSOR_THRESHOLD);
	compressor.setThreshold(compThreshold);

	// Render saturation
	if (clippingAmount != 0u) {
		int32_t shiftAmount = getShiftAmountForSaturation();
		for (StereoSample& sample : global_effectable_audio) {
			sample.l = saturate(sample.l, &lastSaturationTanHWorkingValue[0], shiftAmount);
			sample.r = saturate(sample.r, &lastSaturationTanHWorkingValue[1], shiftAmount);
		}
	}

	if (renderedLastTime) {
		// Render filters
		processFilters(global_effectable_audio);

		// Render FX
		processSRRAndBitcrushing(global_effectable_audio, &volumePostFX, paramManagerForClip);
	}

	processFXForGlobalEffectable(global_effectable_audio, &volumePostFX, paramManagerForClip, delayWorkingState,
	                             renderedLastTime, reverbSendAmount);
	processStutter(global_effectable_audio, paramManagerForClip);
	// record before pan/compression/volume to keep volumes consistent
	if (recorder != nullptr && recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING) {
		// Self-overdubs compensate for sample playback's 1/8 scaling. Standalone sampling retains its existing gain.
		recorder->feedAudio(global_effectable_audio, true, recorder->recordingOwnClip ? 3 : 2);
	}

	processReverbSendAndVolume(global_effectable_audio, reverbBuffer, volumePostFX, postReverbVolume, reverbSendAmount,
	                           pan, true);

	if (compThreshold > 0) {
		compressor.renderVolNeutral(global_effectable_audio, volumePostFX);
	}
	else {
		compressor.reset();
	}

	// Add the global effectable data to the output
	std::ranges::transform(global_effectable_audio, output, output.begin(), std::plus{});

	postReverbVolumeLastTime = postReverbVolume;

	tickParamAutomation(modelStack, paramManagerForClip, output.size(), isClipActive);

	reassessRenderSkippingStatus(paramManagerForClip, recorder);
}

void GlobalEffectableForClip::tickParamAutomation(ModelStackWithTimelineCounter* modelStack,
                                                  ParamManager* paramManagerForClip, size_t numSamples,
                                                  bool isClipActive) {
	if (playbackHandler.isEitherClockActive() && (playbackHandler.ticksLeftInCountIn == 0) && isClipActive) {
		auto& interpolating_params = paramManagerForClip->getUnpatchedParamSetSummary()->whichParamsAreInterpolating;
		const bool result = (params::kMaxNumUnpatchedParams > 32)
		                        ? (interpolating_params[0] != 0u || interpolating_params[1] != 0u)
		                        : (interpolating_params[0] != 0u);
		if (result) {
			ModelStackWithThreeMainThings* modelStackWithThreeMainThings =
			    modelStack->addOtherTwoThingsButNoNoteRow(this, paramManagerForClip);
			paramManagerForClip->toForTimeline()->tickSamples(numSamples, modelStackWithThreeMainThings);
		}
	}
}

int32_t GlobalEffectableForClip::RenderAdjustments::volume() {
	if (!volume_) {
		volume_ = owner_.conversions.volume(
		    paramManager_.getUnpatchedParamSet()->getValue(params::UNPATCHED_VOLUME), [](int32_t v) {
			    return getFinalParameterValueVolume(134217728, cableToLinearParamShortcut(v))
			           >> kClipVolumeAdjustmentShift;
		    });
	}
	return *volume_;
}

int32_t GlobalEffectableForClip::RenderAdjustments::pitch() {
	if (!pitch_) {
		pitch_ = owner_.conversions.pitchAdjust(
		    paramManager_.getUnpatchedParamSet()->getValue(params::UNPATCHED_PITCH_ADJUST),
		    [](int32_t v) { return getFinalParameterValueExp(kMaxSampleValue, v >> 3); });
	}
	return *pitch_;
}

// skip once nothing rendered, nothing's
// stuttering or recording this, all tails done
void GlobalEffectableForClip::reassessRenderSkippingStatus(ParamManager* paramManager, SampleRecorder* recorder) {
	bool recording = recorder != nullptr && recorder->status < RecorderStatus::FINISHED_CAPTURING_BUT_STILL_WRITING;
	bool skippingStatusNow =
	    !renderedLastTime && (delay.repeatsUntilAbandon == 0u) && !stutterer.isStuttering(this) && !recording;

	if (!skippingStatusNow) {
		startSkippingRenderingAtTime = 0;
		return;
	}

	int32_t waitSamples = getSamplesUntilFXTailsEnd(getActiveModFXType(paramManager));
	if (waitSamples == 0) {
		startSkippingRenderingAtTime = 0;
		startSkippingRendering();
		return;
	}

	// If we didn't start the wait-time yet, start it now
	if (!startSkippingRenderingAtTime) {
		startSkippingRenderingAtTime = AudioEngine::audioSampleTimer + waitSamples;
	}

	// Or if already waiting, see if the wait is over yet
	else if ((int32_t)(AudioEngine::audioSampleTimer - startSkippingRenderingAtTime) >= 0) {
		startSkippingRenderingAtTime = 0;
		startSkippingRendering();
	}
}

void GlobalEffectableForClip::startSkippingRendering() {
	timeStartedSkippingRendering = AudioEngine::audioSampleTimer;
	skippingRendering = true;
	compressorClearedWhileSkipping = false;
	if (grainFX != nullptr) {
		grainFX->startSkippingRendering();
	}
}

// Catch up what would have moved on while skipping, as Sound::stopSkippingRendering() does
void GlobalEffectableForClip::stopSkippingRendering(ParamManager* paramManager) {
	uint32_t timeOff = AudioEngine::audioSampleTimer - timeStartedSkippingRendering;
	if (timeOff != 0u) {
		UnpatchedParamSet* unpatchedParams = paramManager->getUnpatchedParamSet();
		int32_t modFXRate = getFinalParameterValueExp(
		    paramNeutralValues[params::GLOBAL_MOD_FX_RATE],
		    cableToExpParamShortcut(unpatchedParams->getValue(params::UNPATCHED_MOD_FX_RATE)));
		modfx.tickLFO(timeOff, modFXRate);

		if (AudioEngine::sizeLastSideChainHit != 0) {
			sidechain.registerHitRetrospectively(AudioEngine::sizeLastSideChainHit,
			                                     AudioEngine::audioSampleTimer - AudioEngine::timeLastSideChainHit);
		}
	}
	startSkippingRenderingAtTime = 0;
	skippingRendering = false;
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
				*soundWithMostReverb = nullptr;
				*paramManagerWithMostReverb = activeParamManager;
				*globalEffectableWithMostReverb = this;
			}
		}
	}
}
