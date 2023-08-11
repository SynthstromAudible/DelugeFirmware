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
#include "dsp/filter/svf.h"
#include "definitions_cxx.hpp"
#include "util/functions.h"
#include <cstdint>
namespace deluge::dsp::filter {
void SVFilter::doFilter(q31_t* startSample, q31_t* endSample, int32_t sampleIncrememt) {
	q31_t* currentSample = startSample;
	do {
		q31_t outs = doSVF(*currentSample, l);
		*currentSample = outs;

		currentSample += sampleIncrememt;
	} while (currentSample < endSample);
}
void SVFilter::doFilterStereo(q31_t* startSample, q31_t* endSample) {
	q31_t* currentSample = startSample;
	do {
		q31_t outs = doSVF(*currentSample, l);

		*currentSample = outs;
		q31_t outs2 = doSVF(*(currentSample + 1), r);
		*(currentSample + 1) = outs2;
		currentSample += 2;
	} while (currentSample < endSample);
}

q31_t SVFilter::setConfig(q31_t lpfFrequency, q31_t lpfResonance, FilterMode lpfMode, q31_t filterGain) {
	curveFrequency(lpfFrequency);
	// raw resonance is 0 - 536870896 (2^28ish, don't know where it comes from)
	// Multiply by 4 to bring it to the q31 0-1 range
	q = (ONE_Q31 - 4 * (lpfResonance));
	in = (q >> 1) + (ONE_Q31 >> 1);
	//squared q is a better match for the ladders
	//also the input scale needs to be sqrt(q) for the level compensation to work so it's a win win
	q = multiply_32x32_rshift32_rounded(q, q) << 1;
	if (lpfMode == FilterMode::SVF) {
		c_low = ONE_Q31;
		c_band = 0;
		c_high = 0;
	}
	else if (lpfMode == FilterMode::HPSVF) {
		c_low = 0;
		c_band = 0;
		c_high = ONE_Q31;
	}
	return filterGain;
}

inline q31_t SVFilter::doSVF(int32_t input, SVFState& state) {
	q31_t high = 0;
	q31_t notch = 0;
	q31_t lowi;
	q31_t highi;
	q31_t bandi;
	q31_t low = state.low;
	q31_t band = state.band;

	input = multiply_32x32_rshift32(in, input);

	low = low + 2 * multiply_32x32_rshift32(band, fc);
	high = input - low;
	high = high - 2 * multiply_32x32_rshift32(band, q);
	band = 2 * multiply_32x32_rshift32(high, fc) + band;
	//notch = high + low;

	//saturate band feedback
	band = getTanHUnknown(band, 3);

	lowi = low;
	highi = high;
	bandi = band;
	//double sample to increase the cutoff frequency
	low = low + 2 * multiply_32x32_rshift32(band, fc);
	high = input - low;
	high = high - 2 * multiply_32x32_rshift32(band, q);
	band = 2 * multiply_32x32_rshift32(high, fc) + band;

	//saturate band feedback
	band = getTanHUnknown(band, 3);
	//notch = high + low;
	q31_t result = multiply_32x32_rshift32_rounded(lowi + low, c_low);
	result = multiply_accumulate_32x32_rshift32_rounded(result, highi + high, c_high);
	result = multiply_accumulate_32x32_rshift32_rounded(result, bandi + band, c_band);
	//result = multiply_accumulate_32x32_rshift32_rounded(0, notchi+notch, c_notch);
	result = 2 * result; //compensate for division by two on each multiply

	state.low = low;
	state.band = band;

	return result;
}
} // namespace deluge::dsp::filter
