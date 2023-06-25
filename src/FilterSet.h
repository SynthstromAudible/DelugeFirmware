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

#ifndef FILTERSET_H_
#define FILTERSET_H_

#include "r_typedefs.h"
#include "functions.h"
#include "definitions.h"
#include "FilterSetConfig.h"

class Sound;
class FilterSetConfig;

struct SVF_outs {
	int32_t lpf;
	int32_t bpf;
	int32_t hpf;
	int32_t notch;
};

class SVFilter {
public:
	//input f is actually filter 'moveability', tan(f)/(1+tan(f)) and falls between 0 and 1. 1 represented by 2147483648
	//resonance is 2147483647 - rawResonance2 Always between 0 and 2. 1 represented as 1073741824
	SVF_outs doSVF(int32_t input, FilterSetConfig* filterSetConfig);
	void reset() {
		low = 0;
		band = 0;
	}

	int32_t low;
	int32_t band;
};

class BasicFilterComponent {
public:
	//moveability is tan(f)/(1+tan(f))
	inline int32_t doFilter(int32_t input, int32_t moveability) {
		int32_t a = multiply_32x32_rshift32_rounded(input - memory, moveability) << 1;
		int32_t b = a + memory;
		memory = b + a;
		return b;
	}

	inline int32_t doAPF(int32_t input, int32_t moveability) {
		int32_t a = multiply_32x32_rshift32_rounded(input - memory, moveability) << 1;
		int32_t b = a + memory;
		memory = a + b;
		return b * 2 - input;
	}

	inline void affectFilter(int32_t input, int32_t moveability) {
		memory += multiply_32x32_rshift32_rounded(input - memory, moveability) << 2;
	}

	inline void reset() { memory = 0; }

	inline int32_t getFeedbackOutput(int32_t feedbackAmount) {
		return multiply_32x32_rshift32_rounded(memory, feedbackAmount) << 2;
	}

	inline int32_t getFeedbackOutputWithoutLshift(int32_t feedbackAmount) {
		return multiply_32x32_rshift32_rounded(memory, feedbackAmount);
	}

	int32_t memory;
};

class FilterSet {
public:
	FilterSet();
	void renderLPFLong(int32_t* outputSample, int32_t* endSample, FilterSetConfig* filterSetConfig, uint8_t lpfMode,
	                   int sampleIncrement = 1, int extraSaturation = 0, int extraSaturationDrive = 0);
	void renderHPFLong(int32_t* outputSample, int32_t* endSample, FilterSetConfig* filterSetConfig, int numSamples,
	                   int sampleIncrement = 1);
	void renderHPF(int32_t* outputSample, FilterSetConfig* filterSetConfig, int extraSaturation = 0);
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

	inline void renderLong(int32_t* outputSample, int32_t* endSample, FilterSetConfig* filterSetConfig, uint8_t lpfMode,
	                       int numSamples, int sampleIncrememt = 1) {

		// Do HPF, if it's on
		if (filterSetConfig->doHPF) {
			renderHPFLong(outputSample, endSample, filterSetConfig, numSamples, sampleIncrememt);
		}
		else hpfOnLastTime = false;

		// Do LPF, if it's on
		if (filterSetConfig->doLPF) {
			renderLPFLong(outputSample, endSample, filterSetConfig, lpfMode, sampleIncrememt, 1, 1);
		}
		else lpfOnLastTime = false;
	}

private:
	int32_t noiseLastValue;

	int32_t do24dBLPFOnSample(int32_t input, FilterSetConfig* filterSetConfig, int saturationLevel);
	int32_t doDriveLPFOnSample(int32_t input, FilterSetConfig* filterSetConfig, int extraSaturation = 0);
};

#endif /* FILTERSET_H_ */
