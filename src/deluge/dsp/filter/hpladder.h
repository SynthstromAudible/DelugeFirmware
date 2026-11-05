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

class HpLadderFilter : public Filter<HpLadderFilter> {
public:
	HpLadderFilter() = default;
	// returns a compensatory gain value
	q31_t setConfig(q31_t hpfFrequency, q31_t hpfResonance, FilterMode lpfMode, q31_t lpfMorph, q31_t filterGain);
	void doFilter(q31_t* startSample, q31_t* endSample, int32_t sampleIncrememt);
	void doFilterStereo(q31_t* startSample, q31_t* endSample);
	void resetFilter() {
		l.reset();
		r.reset();
	}

private:
	struct HPLadderState {
		BasicFilterComponent hpfHPF1;
		BasicFilterComponent hpfLPF1;
		BasicFilterComponent hpfHPF3;
		uint32_t hpfLastWorkingValue;
		void reset() {
			hpfHPF1.reset();
			hpfLPF1.reset();
			hpfHPF3.reset();
		}
	};
	[[gnu::always_inline]] inline q31_t doHPF(q31_t input, HPLadderState& state);

	bool hpfDoingAntialiasingNow;
	int32_t hpfDivideByTotalMoveabilityLastTime;
	int32_t hpfDivideByProcessedResonanceLastTime;

	// All feedbacks have 1 represented as 1073741824
	q31_t hpfLPF1Feedback;
	q31_t hpfHPF3Feedback;

	q31_t hpfProcessedResonance; // 1 represented as 1073741824
	bool hpfDoAntialiasing;
	q31_t hpfDivideByProcessedResonance;

	q31_t divideByTotalMoveability; // 1 represented as 268435456

	q31_t alteredHpfMomentumMultiplier;
	q31_t thisHpfResonance;
	q31_t morph_;
	HPLadderState l;
	HPLadderState r;
};
} // namespace deluge::dsp::filter
