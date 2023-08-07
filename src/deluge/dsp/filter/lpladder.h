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
#include "dsp/filter/ladder_components.h"
#include "util/functions.h"
#include <cstdint>
namespace deluge::dsp::filter {
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

class LpLadderFilter : public Filter<LpLadderFilter> {
public:
	LpLadderFilter() = default;
	//returns a compensatory gain value
	q31_t setConfig(q31_t lpfFrequency, q31_t lpfResonance, LPFMode lpfMode, q31_t filterGain);
	void doFilter(q31_t* outputSample, q31_t* endSample, int32_t sampleIncrememt, int32_t extraSaturation);
	void doFilterStereo(q31_t* startSample, q31_t* endSample, q31_t extraSaturation);
	void resetFilter() {
		l.reset();
		r.reset();
	}

private:
	inline q31_t do24dBLPFOnSample(q31_t input, LpLadderState* state, int32_t saturationLevel);
	inline q31_t do12dBLPFOnSample(q31_t input, LpLadderState* state, int32_t saturationLevel);
	inline q31_t doDriveLPFOnSample(q31_t input, LpLadderState* state, int32_t extraSaturation = 0);
	inline void renderLPLadder(q31_t* startSample, q31_t* endSample, LPFMode lpfMode, int32_t sampleIncrement,
	                           int32_t extraSaturation, int32_t extraSaturationDrive);

	//all ladders are in this class to share the basic components
	//this differentiates between them
	LPFMode lpfMode;

	//state
	LpLadderState l;
	LpLadderState r;

	//configuration
	q31_t processedResonance;
	q31_t divideByTotalMoveabilityAndProcessedResonance;

	//moveability is tan(f)/(1+tan(f))
	q31_t moveability;
	q31_t divideBy1PlusTannedFrequency;

	// All feedbacks have 1 represented as 1073741824
	q31_t lpf1Feedback;
	q31_t lpf2Feedback;
	q31_t lpf3Feedback;

	bool doOversampling;
};
} // namespace deluge::dsp::filter
