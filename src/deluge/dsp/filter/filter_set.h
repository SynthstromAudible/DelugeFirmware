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

#include "RZA1/system/r_typedefs.h"
#include "util/functions.h"
#include "definitions.h"
#include "dsp/filter/filter_set_config.h"

class Sound;
class FilterSetConfig;

struct SVF_outs {
	q31_t lpf;
	q31_t bpf;
	q31_t hpf;
	q31_t notch;
};

class SVFilter {
public:
	//input f is actually filter 'moveability', tan(f)/(1+tan(f)) and falls between 0 and 1. 1 represented by 2147483648
	//resonance is 2147483647 - rawResonance2 Always between 0 and 2. 1 represented as 1073741824
	SVF_outs doSVF(q31_t input, FilterSetConfig* filterSetConfig);
	void reset() {
		low = 0;
		band = 0;
	}

	q31_t low;
	q31_t band;
};

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
	void renderLPFLong(q31_t* outputSample, q31_t* endSample, FilterSetConfig* filterSetConfig, uint8_t lpfMode,
	                   int sampleIncrement = 1, int extraSaturation = 0, int extraSaturationDrive = 0);
	void renderHPFLong(q31_t* outputSample, q31_t* endSample, FilterSetConfig* filterSetConfig, int numSamples,
	                   int sampleIncrement = 1);
	void renderHPF(q31_t* outputSample, FilterSetConfig* filterSetConfig, int extraSaturation = 0);
	void reset();
	BasicFilterComponent lpfLPF1;
	BasicFilterComponent lpfLPF2;
	BasicFilterComponent lpfLPF3;
	BasicFilterComponent lpfLPF4;

	SVFilter svf;

	BasicFilterComponent hpfHPF1;
	BasicFilterComponent hpfLPF1;
	BasicFilterComponent hpfHPF3;
	uint32_t hpfLastWorkingValue;
	bool hpfDoingAntialiasingNow;
	int32_t hpfDivideByTotalMoveabilityLastTime;
	int32_t hpfDivideByProcessedResonanceLastTime;

	bool hpfOnLastTime;
	bool lpfOnLastTime;

	inline void renderLong(q31_t* outputSample, q31_t* endSample, FilterSetConfig* filterSetConfig, uint8_t lpfMode,
	                       int numSamples, int sampleIncrememt = 1) {

		// Do HPF, if it's on
		if (filterSetConfig->doHPF) {
			renderHPFLong(outputSample, endSample, filterSetConfig, numSamples, sampleIncrememt);
		}
		else
			hpfOnLastTime = false;

		// Do LPF, if it's on
		if (filterSetConfig->doLPF) {
			renderLPFLong(outputSample, endSample, filterSetConfig, lpfMode, sampleIncrememt, 1, 1);
		}
		else
			lpfOnLastTime = false;
	}

private:
	q31_t noiseLastValue;

	q31_t do24dBLPFOnSample(q31_t input, FilterSetConfig* filterSetConfig, int saturationLevel);
	q31_t doDriveLPFOnSample(q31_t input, FilterSetConfig* filterSetConfig, int extraSaturation = 0);
};
