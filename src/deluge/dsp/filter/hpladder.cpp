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
#include "dsp/filter/hpladder.h"

namespace deluge::dsp::filter {

q31_t HpLadderFilter::setConfig(q31_t hpfFrequency, q31_t hpfResonance, FilterMode lpfMode, q31_t lpfMorph,
                                q31_t filterGain) {
	int32_t extraFeedback = 1200000000;
	morph_ = lpfMorph;
	curveFrequency(hpfFrequency);

	int32_t resonanceUpperLimit = 536870911;
	int32_t resonance = ONE_Q31 - (std::min(hpfResonance, resonanceUpperLimit) << 2); // Limits it

	resonance = multiply_32x32_rshift32_rounded(resonance, resonance) << 1;
	hpfProcessedResonance =
	    ONE_Q31 - resonance; // ONE_Q31 - rawResonance2; // Always between 0 and 2. 1 represented as 1073741824

	hpfProcessedResonance = std::max(hpfProcessedResonance, (int32_t)134217728); // Set minimum resonance amount

	int32_t hpfProcessedResonanceUnaltered = hpfProcessedResonance;

	// Extra feedback
	hpfProcessedResonance = multiply_32x32_rshift32(hpfProcessedResonance, extraFeedback) << 1;

	hpfDivideByProcessedResonance = (q31_t)(2147483648.0 / (double)(hpfProcessedResonance >> (23)));

	int32_t moveabilityTimesProcessedResonance =
	    multiply_32x32_rshift32(hpfProcessedResonanceUnaltered, fc); // 1 = 536870912
	int32_t moveabilitySquaredTimesProcessedResonance =
	    multiply_32x32_rshift32(moveabilityTimesProcessedResonance, fc); // 1 = 268435456

	hpfHPF3Feedback = -multiply_32x32_rshift32_rounded(fc, divideBy1PlusTannedFrequency);
	hpfLPF1Feedback = divideBy1PlusTannedFrequency >> 1;

	uint32_t toDivideBy =
	    ((int32_t)268435456 - (moveabilityTimesProcessedResonance >> 1) + moveabilitySquaredTimesProcessedResonance);
	divideByTotalMoveability = (int32_t)((double)hpfProcessedResonance * 67108864.0 / (double)toDivideBy);

	hpfDoAntialiasing = (hpfProcessedResonance > 900000000);

	// Adjust volume for HPF resonance
	q31_t rawResonance = std::min(hpfResonance, (q31_t)ONE_Q31 >> 2) << 2;
	q31_t squared = multiply_32x32_rshift32(rawResonance, rawResonance) << 1;
	// Make bigger to have more of a volume cut happen at high resonance
	squared = (multiply_32x32_rshift32(squared, squared) >> 4) * 19;
	filterGain = multiply_32x32_rshift32(filterGain, ONE_Q31 - squared) << 1;

	return filterGain;
}
void HpLadderFilter::doFilter(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement) {
	q31_t* currentSample = startSample;
	do {
		*currentSample = doHPF(*currentSample, l);
		currentSample += sampleIncrement;
	} while (currentSample < endSample);
}
// filter an interleaved stereo buffer
void HpLadderFilter::doFilterStereo(q31_t* startSample, q31_t* endSample) {
	q31_t* currentSample = startSample;
	do {
		*currentSample = doHPF(*currentSample, l);
		currentSample += 1;
		*currentSample = doHPF(*currentSample, r);
		currentSample += 1;
	} while (currentSample < endSample);
}
[[gnu::always_inline]] inline q31_t HpLadderFilter::doHPF(q31_t input, HPLadderState& state) {
	// inputs are only 16 bit so this is pretty small
	// this limit was found experimentally as about the lowest fc can get without sounding broken
	q31_t constexpr lower_limit = -(ONE_Q31 >> 8);
	q31_t temp_fc = std::max(multiply_accumulate_32x32_rshift32_rounded(fc, input << 4, morph_), lower_limit);

	q31_t firstHPFOutput = input - state.hpfHPF1.doFilter(input, temp_fc);

	q31_t feedbacksValue =
	    state.hpfHPF3.getFeedbackOutput(hpfHPF3Feedback) + state.hpfLPF1.getFeedbackOutput(hpfLPF1Feedback);

	q31_t a = multiply_32x32_rshift32_rounded(divideByTotalMoveability, firstHPFOutput + feedbacksValue) << (4 + 1);

	// Only saturate / anti-alias if lots of resonance
	if (hpfProcessedResonance > 900000000) { // 890551738
		a = getTanHAntialiased(a, &state.hpfLastWorkingValue, 1);
	}
	else {
		state.hpfLastWorkingValue = (uint32_t)lshiftAndSaturate<2>(a) + 2147483648u;
		if (hpfProcessedResonance > 750000000) { // 400551738
			a = getTanHUnknown(a, 2);
		}
	}

	state.hpfLPF1.doFilter(a - state.hpfHPF3.doFilter(a, temp_fc), temp_fc);

	a = multiply_32x32_rshift32_rounded(a, hpfDivideByProcessedResonance) << (8 - 1); // Normalization

	return a;
}
} // namespace deluge::dsp::filter
