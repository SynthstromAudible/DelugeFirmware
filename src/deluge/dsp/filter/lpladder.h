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

#include "dsp/filter/filter.h"
#include "dsp/filter/ladder_components.h"
#include "util/fixedpoint.h"

namespace deluge::dsp::filter {

class LpLadderFilter : public Filter<LpLadderFilter> {
public:
	LpLadderFilter() = default;
	// returns a compensatory gain value
	q31_t setConfig(q31_t hpfFrequency, q31_t hpfResonance, FilterMode lpfMode, q31_t lpfMorph, q31_t filterGain);
	void doFilter(q31_t* outputSample, q31_t* endSample, int32_t sampleIncrememt);
	void doFilterStereo(q31_t* startSample, q31_t* endSample);
	void resetFilter() {
		l.reset();
		r.reset();
	}

private:
	struct LpLadderState {
		q31_t noiseLastValue;
		BasicFilterComponent lpfLPF1;
		BasicFilterComponent lpfLPF2;
		BasicFilterComponent lpfLPF3;
		BasicFilterComponent lpfLPF4;
		void reset() {
			lpfLPF1.reset();
			lpfLPF2.reset();
			lpfLPF3.reset();
			lpfLPF4.reset();
		}
	};
	// Per-render copy of the configuration. doFilter() works on local copies of the coefficients, the filter state and
	// the noise seed, so the compiler can keep them in registers: stores to the sample buffer (q31_t*) may otherwise
	// alias the q31_t members and the global noise seed, forcing reloads and stores of all of them every sample.
	struct Coeffs {
		q31_t moveability;
		q31_t processedResonance;
		q31_t divideByTotalMoveabilityAndProcessedResonance;
		q31_t morph;
		q31_t lpf1Feedback;
		q31_t lpf2Feedback;
		q31_t lpf3Feedback;
		q31_t lpf4Feedback; // divideBy1PlusTannedFrequency
	};
	[[gnu::always_inline]] Coeffs getCoeffs() const {
		return Coeffs{moveability,
		              processedResonance,
		              divideByTotalMoveabilityAndProcessedResonance,
		              morph,
		              lpf1Feedback,
		              lpf2Feedback,
		              lpf3Feedback,
		              divideBy1PlusTannedFrequency};
	}

	// kSaturate: morph > 0 || processedResonance > 510000000 (loop invariant, so decided once per buffer)
	// kFeedback: false only if processedResonance == 0 and divideByTotalMoveabilityAndProcessedResonance == 1 << 30.
	// multiply_32x32_rshift32_rounded(feedbacksSum, 0) is exactly 0 for any feedbacksSum, so skipping the feedback
	// path is bit-exact.
	template <bool kSaturate, bool kFeedback>
	[[gnu::always_inline]] static inline q31_t scaleInput(q31_t input, q31_t feedbacksSum, const Coeffs& c) {
		q31_t temp;
		if constexpr (kFeedback) {
			q31_t in = input - (multiply_32x32_rshift32_rounded(feedbacksSum, c.processedResonance) << 3);
			temp = multiply_32x32_rshift32_rounded(in, c.divideByTotalMoveabilityAndProcessedResonance) << 2;
		}
		else {
			// Only used when divideByTotalMoveabilityAndProcessedResonance == 1 << 30, which is what setConfig()
			// produces whenever processedResonance ends up 0. multiply_32x32_rshift32_rounded(input, 1 << 30) << 2
			// is floor((input + 2) / 4) * 4, i.e. input rounded to a multiple of 4.
			temp = (q31_t)(((uint32_t)input + 2u) & ~3u);
		}
		if constexpr (kSaturate) {
			q31_t extra = 2 * multiply_32x32_rshift32(input, c.morph);
			temp = getTanHUnknown(temp + extra, 2);
		}
		return temp;
	}
	template <bool kSaturate, bool kFeedback>
	[[gnu::always_inline]] static inline q31_t do24dBLPFOnSample(q31_t input, LpLadderState& state, const Coeffs& c,
	                                                             uint32_t& seed);
	template <bool kSaturate, bool kFeedback>
	[[gnu::always_inline]] static inline q31_t do12dBLPFOnSample(q31_t input, LpLadderState& state, const Coeffs& c,
	                                                             uint32_t& seed);
	template <bool kSaturate, bool kFeedback>
	[[gnu::always_inline]] static inline q31_t doDriveLPFOnSample(q31_t input, LpLadderState& state, const Coeffs& c,
	                                                              uint32_t& seed);
	template <bool kSaturate, bool kFeedback>
	void doFilterT(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement);
	template <bool kSaturate, bool kFeedback>
	void doFilterStereoT(q31_t* startSample, q31_t* endSample);
	// all ladders are in this class to share the basic components
	// this differentiates between them
	FilterMode lpfMode;

	// state
	LpLadderState l;
	LpLadderState r;

	// configuration
	q31_t processedResonance;
	q31_t divideByTotalMoveabilityAndProcessedResonance;

	// moveability is tan(f)/(1+tan(f))
	q31_t moveability;

	q31_t morph;

	// All feedbacks have 1 represented as 1073741824
	q31_t lpf1Feedback;
	q31_t lpf2Feedback;
	q31_t lpf3Feedback;

	bool doOversampling;
};
} // namespace deluge::dsp::filter
