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
	// Per-buffer copy of the coefficients. The loops work on locals so stores to the q31_t sample buffer can't force
	// the compiler to reload the q31_t members and store the state back every sample.
	struct Coeffs {
		q31_t fc;
		q31_t morph;
		q31_t hpfHPF3Feedback;
		q31_t hpfLPF1Feedback;
		q31_t divideByTotalMoveability;
		q31_t hpfDivideByProcessedResonance;
	};
	[[gnu::always_inline]] Coeffs getCoeffs() const {
		return Coeffs{
		    fc, morph_, hpfHPF3Feedback, hpfLPF1Feedback, divideByTotalMoveability, hpfDivideByProcessedResonance};
	}
	// kSaturation: 0 none, 1 tanh, 2 antialiased tanh (from hpfProcessedResonance, loop invariant)
	// kMorph: false when morph_ == 0, where the cutoff modulation term is exactly 0
	template <int kSaturation, bool kMorph>
	[[gnu::always_inline]] static inline q31_t doHPF(q31_t input, HPLadderState& state, const Coeffs& c);
	template <int kSaturation, bool kMorph>
	void doFilterT(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement);
	template <int kSaturation, bool kMorph>
	void doFilterStereoT(q31_t* startSample, q31_t* endSample);
	template <typename F>
	[[gnu::always_inline]] inline void dispatch(F&& f);

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
