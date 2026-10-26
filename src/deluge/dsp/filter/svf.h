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
#include "util/fixedpoint.h"

namespace deluge::dsp::filter {

class SVFilter : public Filter<SVFilter> {
public:
	SVFilter() = default;
	// returns a compensatory gain value
	q31_t setConfig(q31_t hpfFrequency, q31_t hpfResonance, FilterMode lpfMode, q31_t lpfMorph, q31_t filterGain);
	void doFilter(q31_t* startSample, q31_t* endSample, int32_t sampleIncrememt);
	void doFilterStereo(q31_t* startSample, q31_t* endSample);
	void resetFilter() {
		l = (SVFState){0, 0};
		r = (SVFState){0, 0};
	}

private:
	struct SVFState {
		q31_t low;
		q31_t band;
	};
	// Per-buffer copy of the coefficients. The loops work on locals so stores to the q31_t sample buffer can't force
	// the compiler to reload the q31_t members and store the state back every sample.
	struct Coeffs {
		q31_t fc;
		q31_t q;
		q31_t in;
		q31_t c_low;
		q31_t c_band;
		q31_t c_high;
	};
	[[gnu::always_inline]] Coeffs getCoeffs() const { return Coeffs{fc, q, in, c_low, c_band, c_high}; }
	template <bool kBand>
	[[gnu::always_inline]] static inline q31_t doSVF(q31_t input, SVFState& state, const Coeffs& c);
	template <bool kBand>
	void doFilterT(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement);
	template <bool kBand>
	void doFilterStereoT(q31_t* startSample, q31_t* endSample);
	SVFState l;
	SVFState r;

	q31_t q;
	q31_t in;
	q31_t c_low;
	q31_t c_band;
	q31_t c_notch;
	q31_t c_high;
	bool band_mode;
};
} // namespace deluge::dsp::filter
