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
#include "deluge/dsp/stereo_sample.h"
#include "util/functions.h"
#include <cstdint>

// Filter Interface
template <typename T>
class Filter {
public:
	Filter() = default;
	//returns a gain compensation value
	q31_t configure(q31_t frequency, q31_t resonance, LPFMode lpfMode, q31_t filterGain) {
		return static_cast<T*>(this)->set_config(frequency, resonance, lpfMode, filterGain);
	}
	void filter_mono(q31_t* startSample, q31_t* endSample, int32_t sampleIncrememt = 1, int32_t extraSaturation = 1) {
		static_cast<T*>(this)->do_filter(startSample, endSample, sampleIncrememt, extraSaturation);
	}
	void filter_stereo(StereoSample* startSample, StereoSample* endSample, int32_t extraSaturation = 1) {
		static_cast<T*>(this)->do_filter_stereo(startSample, endSample, extraSaturation);
		;
	}
	void reset() { static_cast<T*>(this)->reset_filter(); }
};
