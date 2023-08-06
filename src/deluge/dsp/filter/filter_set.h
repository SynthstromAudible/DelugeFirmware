/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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
#include "dsp/filter/filter.h"
#include "dsp/filter/filter_set_config.h"
#include "dsp/filter/svf.h"
#include "util/functions.h"
#include <cstdint>

class Sound;
class FilterSetConfig;

class BasicFilterComponent {
public:
	//moveability is tan(f)/(1+tan(f))
	inline q31_t doFilter(q31_t input, q31_t moveability) {
		q31_t a = multiply_32x32_rshift32_rounded(input - memory, moveability) << 1;
		q31_t b = a + memory;
		memory = b + a;
		return b;
	}

	inline int32_t doAPF(q31_t input, int32_t moveability) {
		q31_t a = multiply_32x32_rshift32_rounded(input - memory, moveability) << 1;
		q31_t b = a + memory;
		memory = a + b;
		return b * 2 - input;
	}

	inline void affectFilter(q31_t input, int32_t moveability) {
		memory += multiply_32x32_rshift32_rounded(input - memory, moveability) << 2;
	}

	inline void reset() { memory = 0; }

	inline q31_t getFeedbackOutput(int32_t feedbackAmount) {
		return multiply_32x32_rshift32_rounded(memory, feedbackAmount) << 2;
	}

	inline q31_t getFeedbackOutputWithoutLshift(int32_t feedbackAmount) {
		return multiply_32x32_rshift32_rounded(memory, feedbackAmount);
	}

	q31_t memory;
};

class FilterSet {
public:
	FilterSet();
	void reset();
	q31_t set_config(q31_t lpfFrequency, q31_t lpfResonance, bool doLPF, q31_t hpfFrequency, q31_t hpfResonance,
	                 bool doHPF, LPFMode lpfType, q31_t filterGain, bool adjustVolumeForHPFResonance = true,
	                 q31_t* overallOscAmplitude = NULL);
	void copy_config(FilterSet*);

	inline void renderLong(q31_t* outputSample, q31_t* endSample, int32_t numSamples, int32_t sampleIncrememt = 1,
	                       int32_t extraSaturation = 1) {

		// Do HPF, if it's on
		if (HPFOn) {
			renderHPFLong(outputSample, endSample, numSamples, sampleIncrememt);
		}
		else
			hpfOnLastTime = false;

		// Do LPF, if it's on
		if (LPFOn) {
			renderLPFLong(outputSample, endSample, lpfMode, sampleIncrememt, extraSaturation, extraSaturation >> 1);
		}
		else
			lpfOnLastTime = false;
	}
	//used to check whether the filter is used at all
	inline bool isLPFOn() { return LPFOn; }
	inline bool isHPFOn() { return HPFOn; }

private:
	q31_t noiseLastValue;

	q31_t do24dBLPFOnSample(q31_t input, int32_t saturationLevel);
	q31_t doDriveLPFOnSample(q31_t input, int32_t extraSaturation = 0);
	inline void renderLPLadder(q31_t* startSample, q31_t* endSample, LPFMode lpfMode, int32_t sampleIncrement,
	                           int32_t extraSaturation, int32_t extraSaturationDrive);
	void renderLPFLong(q31_t* outputSample, q31_t* endSample, LPFMode lpfMode, int32_t sampleIncrement = 1,
	                   int32_t extraSaturation = 0, int32_t extraSaturationDrive = 0);
	void renderHPFLong(q31_t* outputSample, q31_t* endSample, int32_t numSamples, int32_t sampleIncrement = 1,
	                   int32_t extraSaturation = 0);
	void renderLadderHPF(q31_t* outputSample, int32_t extraSaturation = 0);

	LPLadderConfig lpladderconfig;
	HPLadderConfig hpladderconfig;
	LPFMode lpfMode;

	BasicFilterComponent lpfLPF1;
	BasicFilterComponent lpfLPF2;
	BasicFilterComponent lpfLPF3;
	BasicFilterComponent lpfLPF4;

	Filter<SVFilter> lpsvf;

	BasicFilterComponent hpfHPF1;
	BasicFilterComponent hpfLPF1;
	BasicFilterComponent hpfHPF3;
	uint32_t hpfLastWorkingValue;
	bool hpfDoingAntialiasingNow;
	int32_t hpfDivideByTotalMoveabilityLastTime;
	int32_t hpfDivideByProcessedResonanceLastTime;

	bool hpfOnLastTime;
	bool lpfOnLastTime;
	bool LPFOn;
	bool HPFOn;

	bool doOversampling;
};
