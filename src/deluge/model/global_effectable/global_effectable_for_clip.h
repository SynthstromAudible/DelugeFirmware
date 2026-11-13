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

#pragma once

#include "definitions_cxx.hpp"
#include "dsp/stereo_sample.h"
#include "model/global_effectable/global_effectable.h"
#include "model/sample/sample_recorder.h"
#include <optional>

class ParamManagerForTimeline;
class TimelineCounter;
class Output;
class Clip;
class ModelStack;
class ModelStackWithThreeMainThings;
class ModelStackWithTimelineCounter;

class GlobalEffectableForClip : public GlobalEffectable {
public:
	GlobalEffectableForClip();

	int32_t getSidechainVolumeAmountAsPatchCableDepth(ParamManager* paramManager);
	void modButtonAction(uint8_t whichModButton, bool on, ParamManagerForTimeline* paramManager) final;
	bool modEncoderButtonAction(uint8_t whichModEncoder, bool on, ModelStackWithThreeMainThings* modelStack) final;
	virtual Output* toOutput() = 0;
	void getThingWithMostReverb(Clip* activeClip, Sound** soundWithMostReverb,
	                            ParamManager** paramManagerWithMostReverb,
	                            GlobalEffectableForClip** globalEffectableWithMostReverb,
	                            int32_t* highestReverbAmountFound);

	int32_t getShiftAmountForSaturation() { return (clippingAmount >= 3) ? (clippingAmount - 3) : 0; }

	/// clipping amount must be greater than 0! Check before calling
	/// Shift amount is givben by getShiftAmountForSaturation
	[[gnu::always_inline]] q31_t saturate(q31_t data, uint32_t* workingValue, int32_t shiftAmount) {
		// Clipping
		return getTanHAntialiased(data, workingValue, 3 + clippingAmount) << (shiftAmount);
	}

	std::array<uint32_t, 2> lastSaturationTanHWorkingValue = {2147483648u, 2147483648u};

protected:
	// The clip's volume and pitch adjust, and the reverb amount for what renderGlobalEffectableForClip() renders.
	// Worked out on first use: an idle clip renders nothing, and reading its params and their cached conversions
	// (cold memory) every render was most of what skipping it cost
	class RenderAdjustments {
	public:
		RenderAdjustments(GlobalEffectableForClip& owner, ParamManager& paramManager, int32_t reverbAmountAdjust,
		                  bool reverbFollowsVolume)
		    : owner_(owner), paramManager_(paramManager), reverbAmountAdjust_(reverbAmountAdjust),
		      reverbFollowsVolume_(reverbFollowsVolume) {}
		int32_t volume();
		int32_t pitch();
		int32_t reverbAmount() {
			return reverbFollowsVolume_ ? multiply_32x32_rshift32_rounded(reverbAmountAdjust_, volume()) << 5
			                            : reverbAmountAdjust_;
		}

	private:
		GlobalEffectableForClip& owner_;
		ParamManager& paramManager_;
		int32_t reverbAmountAdjust_;
		bool reverbFollowsVolume_;
		std::optional<int32_t> volume_;
		std::optional<int32_t> pitch_;
	};

	int32_t getParameterFromKnob(int32_t whichModEncoder) final;
	void renderOutput(ModelStackWithTimelineCounter* modelStack, ParamManager* paramManagerForClip,
	                  std::span<StereoSample> output, int32_t* reverbBuffer, int32_t reverbAmountAdjust,
	                  int32_t sideChainHitPending, bool shouldLimitDelayFeedback, bool isClipActive,
	                  OutputType outputType, SampleRecorder* recorder);

	virtual bool renderGlobalEffectableForClip(ModelStackWithTimelineCounter* modelStack,
	                                           std::span<StereoSample> globalEffectableBuffer,
	                                           int32_t* bufferToTransferTo, int32_t* reverbBuffer,
	                                           RenderAdjustments& adjustments, int32_t sideChainHitPending,
	                                           bool shouldLimitDelayFeedback, bool isClipActive,
	                                           int32_t amplitudeAtStart, int32_t amplitudeAtEnd) = 0;

	virtual bool willRenderAsOneChannelOnlyWhichWillNeedCopying() { return false; }

	bool renderedLastTime = false;

private:
	// Render skipping once nothing renders and the FX tails have died away, renderOutput()
	// skips the FX chain only asking renderGlobalEffectableForClip() whether it has anything yet
	bool skippingRendering = false;
	bool compressorClearedWhileSkipping = false; // Next to skippingRendering, so checking it costs no cache miss
	uint32_t startSkippingRenderingAtTime = 0;   // Valid when not 0: when to start skipping, after mod FX / comp tails
	uint32_t timeStartedSkippingRendering = 0;
	void reassessRenderSkippingStatus(ParamManager* paramManager, SampleRecorder* recorder);
	void startSkippingRendering();
	void stopSkippingRendering(ParamManager* paramManager);
	void tickParamAutomation(ModelStackWithTimelineCounter* modelStack, ParamManager* paramManagerForClip,
	                         size_t numSamples, bool isClipActive);
};
