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

namespace deluge::dsp::filter {
// Performance notes for doFilter()/doFilterStereo(). Same approach as LpLadderFilter, see the notes in lpladder.cpp.
//
// Design: the per-sample loops are doFilterT()/doFilterStereoT(), templated on band_mode (whether the band output is
// mixed in), picked once per buffer here. They work on local copies of the coefficients and the filter state, written
// back once per buffer. Stores through the q31_t* sample buffer could otherwise alias the q31_t members, so GCC
// reloaded all of them and stored the state back on every sample.
//
// The gain is smaller than for the ladders: the two tanh table lookups and 7 multiplies per sample are most of the
// cost, and they're the algorithm, so can't go without changing the sound.
//
// Alternatives considered:
//  - Skipping the c_high multiply-accumulate when morph is 0 (c_high == 0 adds exactly 0 with smmlar). Bit-exact, but
//    one instruction per sample for another set of variants; not done.
//  - One tanh per sample instead of two. Changes the sound; not done.
[[gnu::hot]] void SVFilter::doFilter(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement) {
	if (band_mode) {
		doFilterT<true>(startSample, endSample, sampleIncrement);
	}
	else {
		doFilterT<false>(startSample, endSample, sampleIncrement);
	}
}
[[gnu::hot]] void SVFilter::doFilterStereo(q31_t* startSample, q31_t* endSample) {
	if (band_mode) {
		doFilterStereoT<true>(startSample, endSample);
	}
	else {
		doFilterStereoT<false>(startSample, endSample);
	}
}
template <bool kBand>
[[gnu::hot]] void SVFilter::doFilterT(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement) {
	const Coeffs c = getCoeffs();
	SVFState s = l;
	q31_t* currentSample = startSample;
	do {
		*currentSample = doSVF<kBand>(*currentSample, s, c);
		currentSample += sampleIncrement;
	} while (currentSample < endSample);
	l = s;
}
template <bool kBand>
[[gnu::hot]] void SVFilter::doFilterStereoT(q31_t* startSample, q31_t* endSample) {
	const Coeffs c = getCoeffs();
	SVFState sl = l;
	SVFState sr = r;
	q31_t* currentSample = startSample;
	do {
		currentSample[0] = doSVF<kBand>(currentSample[0], sl, c);
		currentSample[1] = doSVF<kBand>(currentSample[1], sr, c);
		currentSample += 2;
	} while (currentSample < endSample);
	l = sl;
	r = sr;
}

q31_t SVFilter::setConfig(q31_t freq, q31_t res, FilterMode lpfMode, q31_t lpfMorph, q31_t filterGain) {
	curveFrequency(freq);
	// multiply by 1.25 to loosely correct for equivalency to ladders
	// Caused by the actual svf cutoff being sin inverse of this fc
	constexpr q31_t POINT_25 = ONE_Q31 * 0.25;
	fc = fc + multiply_32x32_rshift32(fc, POINT_25);

	band_mode = (lpfMode == FilterMode::SVF_BAND);
	// raw resonance is 0 - 536870896 (2^28ish, don't know where it comes from)
	// Multiply by 4 to bring it to the q31 0-1 range
	q = (ONE_Q31 - 4 * (res));
	in = (q >> 1) + (ONE_Q31 >> 1);
	// squared q is a better match for the ladders
	// also the input scale needs to be sqrt(q) for the level compensation to work so it's a win win
	q = multiply_32x32_rshift32_rounded(q, q) << 1;

	// note - the if statements are to avoid overflow issues
	// do not remove
	constexpr q31_t ONE_HALF = ONE_Q31 >> 1;
	if (band_mode) {
		if (lpfMorph > (ONE_HALF)) {
			lpfMorph = 2 * (lpfMorph - (ONE_HALF));
			c_low = 0;
			c_band = ONE_Q31 - lpfMorph;
			c_high = lpfMorph;
		}
		else {
			lpfMorph = 2 * lpfMorph;
			c_low = ONE_Q31 - lpfMorph;
			c_band = lpfMorph;
			c_high = 0;
		}
	}
	else {
		c_low = ONE_Q31 - lpfMorph;
		c_high = lpfMorph;
		c_band = 0;
	}
	return filterGain;
}

template <bool kBand>
[[gnu::always_inline]] inline q31_t SVFilter::doSVF(int32_t input, SVFState& state, const Coeffs& c) {
	q31_t high = 0;
	q31_t notch = 0;
	q31_t lowi;
	q31_t highi;
	q31_t bandi;
	q31_t low = state.low;
	q31_t band = state.band;

	input = multiply_32x32_rshift32(c.in, input);

	low = low + 2 * multiply_32x32_rshift32(band, c.fc);
	high = input - low;
	high = high - 2 * multiply_32x32_rshift32(band, c.q);
	band = 2 * multiply_32x32_rshift32(high, c.fc) + band;
	// notch = high + low;

	// saturate band feedback
	band = getTanHUnknown(band, 3);

	lowi = low;
	highi = high;
	bandi = band;
	// double sample to increase the cutoff frequency
	low = low + 2 * multiply_32x32_rshift32(band, c.fc);
	high = input - low;
	high = high - 2 * multiply_32x32_rshift32(band, c.q);
	band = 2 * multiply_32x32_rshift32(high, c.fc) + band;

	// notch = high + low;
	lowi = lowi + low;
	highi = highi + high;
	bandi = bandi + band;

	q31_t result = multiply_32x32_rshift32_rounded(lowi, c.c_low);
	result = multiply_accumulate_32x32_rshift32_rounded(result, highi, c.c_high);
	if constexpr (kBand) {
		result = multiply_accumulate_32x32_rshift32_rounded(result, bandi, c.c_band);
	}

	// saturate band feedback
	band = getTanHUnknown(band, 3);
	// compensate for division by two on each multiply
	// then multiply by 1.5 to match ladders
	result = 3 * result;

	state.low = low;
	state.band = band;

	return result;
}
} // namespace deluge::dsp::filter
