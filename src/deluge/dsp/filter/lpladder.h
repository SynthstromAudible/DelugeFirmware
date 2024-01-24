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
	[[gnu::always_inline]] inline q31_t scaleInput(q31_t input, q31_t feedbacksSum) {
		q31_t temp;
		if (morph > 0 || processedResonance > 510000000) {
			temp = multiply_32x32_rshift32_rounded(
			           (input - (multiply_32x32_rshift32_rounded(feedbacksSum, processedResonance) << 3)),
			           divideByTotalMoveabilityAndProcessedResonance)
			       << 2;
			q31_t extra = 2 * multiply_32x32_rshift32(input, morph);
			temp = getTanHUnknown(temp + extra, 2);
		}
		else {
			temp = multiply_32x32_rshift32_rounded(
			           (input - (multiply_32x32_rshift32_rounded(feedbacksSum, processedResonance) << 3)),
			           divideByTotalMoveabilityAndProcessedResonance)
			       << 2;
		}

		return temp;
	}
	[[gnu::always_inline]] inline q31_t do24dBLPFOnSample(q31_t input, LpLadderState& state);
	[[gnu::always_inline]] inline q31_t do12dBLPFOnSample(q31_t input, LpLadderState& state);
	[[gnu::always_inline]] inline q31_t doDriveLPFOnSample(q31_t input, LpLadderState& state);
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
