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

#include "dsp/filter/filter_set.h"
#include "definitions_cxx.hpp"
#include "dsp/filter/filter.h"
#include "dsp/filter/filter_set_config.h"
#include "dsp/filter/lpladder.h"
#include "dsp/filter/svf.h"
#include "dsp/timestretch/time_stretcher.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

FilterSet::FilterSet() {

	lpsvf = SVFilter();
	lpladder = LpLadderFilter();

	hpladderconfig = HPLadderConfig();
}

inline void FilterSet::renderLadderHPF(q31_t* outputSample, int32_t extraSaturation) {
	q31_t input = *outputSample;

	q31_t firstHPFOutput = input - hpfHPF1.doFilter(input, hpladderconfig.hpfMoveability);

	q31_t feedbacksValue = hpfHPF3.getFeedbackOutput(hpladderconfig.hpfHPF3Feedback)
	                       + hpfLPF1.getFeedbackOutput(hpladderconfig.hpfLPF1Feedback);

	q31_t a = multiply_32x32_rshift32_rounded(hpladderconfig.divideByTotalMoveability, firstHPFOutput + feedbacksValue)
	          << (4 + 1);

	// Only saturate / anti-alias if lots of resonance
	if (hpladderconfig.hpfProcessedResonance > 900000000) { // 890551738
		a = getTanHAntialiased(a, &hpfLastWorkingValue, 2 + extraSaturation);
	}
	else {
		hpfLastWorkingValue = (uint32_t)lshiftAndSaturate<2>(a) + 2147483648u;
		if (hpladderconfig.hpfProcessedResonance > 750000000) { // 400551738
			a = getTanHUnknown(a, 2 + extraSaturation);
		}
	}

	hpfLPF1.doFilter(a - hpfHPF3.doFilter(a, hpladderconfig.hpfMoveability), hpladderconfig.hpfMoveability);

	a = multiply_32x32_rshift32_rounded(a, hpladderconfig.hpfDivideByProcessedResonance) << (8 - 1); // Normalization

	*outputSample = a;
}

void FilterSet::renderHPFLong(q31_t* outputSample, q31_t* endSample, int32_t numSamples, int32_t sampleIncrement,
                              int32_t extraSaturation) {

	do {

		renderLadderHPF(outputSample, extraSaturation);
		outputSample += sampleIncrement;
	} while (outputSample < endSample);
}

void FilterSet::renderLPFLong(q31_t* startSample, q31_t* endSample, LPFMode lpfMode, int32_t sampleIncrement,
                              int32_t extraSaturation, int32_t extraSaturationDrive) {

	if (lpfMode == LPFMode::SVF) {
		if (lastLPFMode != LPFMode::SVF) {
			lpsvf.reset();
		}
		lpsvf.filter(startSample, endSample, sampleIncrement);
	}
	else {
		if (lastLPFMode > kLastLadder) {
			lpladder.reset();
		}
		lpladder.filter(startSample, endSample, sampleIncrement, extraSaturation);
	}
	lastLPFMode = lpfMode;
}

int32_t FilterSet::set_config(int32_t lpfFrequency, int32_t lpfResonance, bool doLPF, int32_t hpfFrequency,
                              int32_t hpfResonance, bool doHPF, LPFMode lpfmode, int32_t filterGain,
                              bool adjustVolumeForHPFResonance, int32_t* overallOscAmplitude) {
	LPFOn = doLPF;
	HPFOn = doHPF;
	lpfMode = lpfmode;
	hpfResonance =
	    (hpfResonance >> 21) << 21; // Insanely, having changes happen in the small bytes too often causes rustling

	if (LPFOn) {
		if (lpfmode == LPFMode::SVF) {
			filterGain = lpsvf.configure(lpfFrequency, lpfResonance, lpfmode, filterGain);
		}
		else {
			filterGain = lpladder.configure(lpfFrequency, lpfResonance, lpfmode, filterGain);
		}
	}

	filterGain =
	    multiply_32x32_rshift32(filterGain, 1720000000)
	    << 1; // This changes the overall amplitude so that, with resonance on 50%, the amplitude is the same as it was pre June 2017

	// HPF
	if (HPFOn) {
		filterGain = hpladderconfig.init(hpfFrequency, hpfResonance, adjustVolumeForHPFResonance, filterGain);
	}

	return filterGain;
}
void FilterSet::copy_config(FilterSet* other) {
	if (other->LPFOn) {
		if (other->lpfMode == LPFMode::SVF) {
			memcpy(&lpsvf, &other->lpsvf, sizeof(lpsvf));
		}
		else {
			memcpy(&lpladder, &other->lpladder, sizeof(lpladder));
		}
	}
	if (other->HPFOn) {
		memcpy(&hpladderconfig, &other->hpladderconfig, sizeof(HPLadderConfig));
	}
	LPFOn = other->isLPFOn();
	HPFOn = other->isHPFOn();
	lpfMode = other->lpfMode;
	lastLPFMode = other->lastLPFMode;
}
void FilterSet::reset() {
	hpfHPF1.reset();
	hpfLPF1.reset();
	hpfHPF3.reset();
	hpfLastWorkingValue = 2147483648;
	hpfDoingAntialiasingNow = false;
	hpfOnLastTime = false;
	lpsvf.reset();
	lpladder.reset();
	lpfOnLastTime = false;
	noiseLastValue = 0;
}
