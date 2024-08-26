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
#include "model/global_effectable/global_effectable.h"
#include "model/sample/sample_recorder.h"

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

	inline void saturate(int32_t* data, uint32_t* workingValue) {
		// Clipping
		if (clippingAmount) {
			int32_t shiftAmount = (clippingAmount >= 3) ? (clippingAmount - 3) : 0;
			//*data = getTanHUnknown(*data, 5 + clippingAmount) << (shiftAmount);
			*data = getTanHAntialiased(*data, workingValue, 3 + clippingAmount) << (shiftAmount);
		}
	}

	uint32_t lastSaturationTanHWorkingValue[2];

protected:
	int32_t getParameterFromKnob(int32_t whichModEncoder) final;
	void renderOutput(ModelStackWithTimelineCounter* modelStack, ParamManager* paramManagerForClip,
	                  StereoSample* outputBuffer, int32_t numSamples, int32_t* reverbBuffer, int32_t reverbAmountAdjust,
	                  int32_t sideChainHitPending, bool shouldLimitDelayFeedback, bool isClipActive,
	                  OutputType outputType, SampleRecorder* recorder);

	virtual bool renderGlobalEffectableForClip(ModelStackWithTimelineCounter* modelStack,
	                                           StereoSample* globalEffectableBuffer, int32_t* bufferToTransferTo,
	                                           int32_t numSamples, int32_t* reverbBuffer, int32_t reverbAmountAdjust,
	                                           int32_t sideChainHitPending, bool shouldLimitDelayFeedback,
	                                           bool isClipActive, int32_t pitchAdjust, int32_t amplitudeAtStart,
	                                           int32_t amplitudeAtEnd) = 0;

	virtual bool willRenderAsOneChannelOnlyWhichWillNeedCopying() { return false; }

	bool renderedLastTime = false;
};
