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

	// ONE_Q31 - rawResonance2; // Always between 0 and 2. 1 represented as 1073741824
	hpfProcessedResonance = ONE_Q31 - resonance;

	hpfProcessedResonance = std::max(hpfProcessedResonance, (int32_t)134217728); // Set minimum resonance amount

	int32_t hpfProcessedResonanceUnaltered = hpfProcessedResonance;

	// Extra feedback
	hpfProcessedResonance = multiply_32x32_rshift32(hpfProcessedResonance, extraFeedback) << 1;

	hpfDivideByProcessedResonance = (q31_t)(2147483648.0 / (double)(hpfProcessedResonance >> (23)));

	// 1 = 536870912
	int32_t moveabilityTimesProcessedResonance = multiply_32x32_rshift32(hpfProcessedResonanceUnaltered, fc);

	// 1 = 268435456
	int32_t moveabilitySquaredTimesProcessedResonance = multiply_32x32_rshift32(moveabilityTimesProcessedResonance, fc);

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
// Performance notes for doFilter()/doFilterStereo(). Same approach as LpLadderFilter, see the notes in lpladder.cpp.
//
// Design: the per-sample loops are doFilterT()/doFilterStereoT(), templated on two loop-invariant conditions picked
// once per buffer by dispatch():
//  - kSaturation: 2 for the antialiased tanh (hpfProcessedResonance > 900000000), 1 for the plain tanh (> 750000000),
//    0 for none. The thresholds are the ones the per-sample branch used to check.
//  - kMorph: false when morph_ == 0. smmlar(fc, x, 0) is exactly fc, so the per-sample cutoff modulation goes and the
//    cutoff is just max(fc, lower_limit).
// The loops work on local copies of the coefficients and the filter state, written back once per buffer. Stores
// through the q31_t* sample buffer could otherwise alias the q31_t members, so GCC reloaded all of them and stored the
// state back on every sample. hpfLastWorkingValue is still worked out every sample in the non-antialiased variants
// (now without a store), so a later buffer that crosses into the antialiased variant starts from the same value as
// before. Only the last sample's value matters there, so it could be worked out once per buffer instead; not done.
//
// Alternatives considered:
//  - Only the locals, keeping the saturation branch inside the loop. Most of the gain is the loads and stores, but the
//    branch is loop invariant and both tanh paths make the loop bigger than it needs to be.
//  - Templating on morph sign as well, or folding the 6 variants down to fewer to save code size. Not done: only one
//    variant is hot per voice.
//  - Interleaving left and right (or two voices) to hide smmulr latency, as for the LP ladder. Not done here either.
template <typename F>
[[gnu::always_inline]] inline void HpLadderFilter::dispatch(F&& f) {
	bool morph = morph_ != 0;
	if (hpfProcessedResonance > 900000000) { // 890551738
		morph ? f.template operator()<2, true>() : f.template operator()<2, false>();
	}
	else if (hpfProcessedResonance > 750000000) { // 400551738
		morph ? f.template operator()<1, true>() : f.template operator()<1, false>();
	}
	else {
		morph ? f.template operator()<0, true>() : f.template operator()<0, false>();
	}
}
[[gnu::hot]] void HpLadderFilter::doFilter(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement) {
	dispatch([&]<int kSaturation, bool kMorph>() {
		doFilterT<kSaturation, kMorph>(startSample, endSample, sampleIncrement);
	});
}
// filter an interleaved stereo buffer
[[gnu::hot]] void HpLadderFilter::doFilterStereo(q31_t* startSample, q31_t* endSample) {
	dispatch([&]<int kSaturation, bool kMorph>() { doFilterStereoT<kSaturation, kMorph>(startSample, endSample); });
}
template <int kSaturation, bool kMorph>
[[gnu::hot]] void HpLadderFilter::doFilterT(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement) {
	const Coeffs c = getCoeffs();
	HPLadderState s = l;
	q31_t* currentSample = startSample;
	do {
		*currentSample = doHPF<kSaturation, kMorph>(*currentSample, s, c);
		currentSample += sampleIncrement;
	} while (currentSample < endSample);
	l = s;
}
template <int kSaturation, bool kMorph>
[[gnu::hot]] void HpLadderFilter::doFilterStereoT(q31_t* startSample, q31_t* endSample) {
	const Coeffs c = getCoeffs();
	HPLadderState sl = l;
	HPLadderState sr = r;
	q31_t* currentSample = startSample;
	do {
		currentSample[0] = doHPF<kSaturation, kMorph>(currentSample[0], sl, c);
		currentSample[1] = doHPF<kSaturation, kMorph>(currentSample[1], sr, c);
		currentSample += 2;
	} while (currentSample < endSample);
	l = sl;
	r = sr;
}
template <int kSaturation, bool kMorph>
[[gnu::always_inline]] inline q31_t HpLadderFilter::doHPF(q31_t input, HPLadderState& state, const Coeffs& c) {
	// inputs are only 16 bit so this is pretty small
	// this limit was found experimentally as about the lowest fc can get without sounding broken
	q31_t constexpr lower_limit = -(ONE_Q31 >> 8);
	// With morph 0 the multiply-accumulate adds exactly 0, so this is fc
	q31_t temp_fc = kMorph
	                    ? std::max(multiply_accumulate_32x32_rshift32_rounded(c.fc, input << 4, c.morph), lower_limit)
	                    : std::max(c.fc, lower_limit);

	q31_t firstHPFOutput = input - state.hpfHPF1.doFilter(input, temp_fc);

	q31_t feedbacksValue =
	    state.hpfHPF3.getFeedbackOutput(c.hpfHPF3Feedback) + state.hpfLPF1.getFeedbackOutput(c.hpfLPF1Feedback);

	q31_t a = multiply_32x32_rshift32_rounded(c.divideByTotalMoveability, firstHPFOutput + feedbacksValue) << (4 + 1);

	// Only saturate / anti-alias if lots of resonance
	if constexpr (kSaturation == 2) {
		a = getTanHAntialiased(a, &state.hpfLastWorkingValue, 1);
	}
	else {
		state.hpfLastWorkingValue = (uint32_t)lshiftAndSaturate<2>(a) + 2147483648u;
		if constexpr (kSaturation == 1) {
			a = getTanHUnknown(a, 2);
		}
	}

	state.hpfLPF1.doFilter(a - state.hpfHPF3.doFilter(a, temp_fc), temp_fc);

	// Normalization
	a = multiply_32x32_rshift32_rounded(a, c.hpfDivideByProcessedResonance) << (8 - 1);

	return a;
}
} // namespace deluge::dsp::filter
