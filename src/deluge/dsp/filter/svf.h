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
#include "util/functions.h"
#include <cstdint>
namespace deluge::dsp::filter {

class SVFilter : public Filter<SVFilter> {
public:
	SVFilter() = default;
	//returns a compensatory gain value
	q31_t setConfig(q31_t lpfFrequency, q31_t lpfResonance, LPFMode lpfMode, q31_t filterGain);
	void doFilter(q31_t* startSample, q31_t* endSample, int32_t sampleIncrememt, int32_t extraSaturation);
	void doFilterStereo(q31_t* startSample, q31_t* endSample, int32_t extraSaturation);
	void resetFilter() {
		l = (SVFState){0, 0};
		r = (SVFState){0, 0};
	}

private:
	struct SVFOuts {
		q31_t lpf;
		q31_t bpf;
		q31_t hpf;
		q31_t notch;
	};

	struct SVFState {
		q31_t low;
		q31_t band;
	};
	inline q31_t doSVF(q31_t input, SVFState* state);
	SVFState l;
	SVFState r;
	q31_t f;
	q31_t q;
	q31_t in;
};
} // namespace deluge::dsp::filter
