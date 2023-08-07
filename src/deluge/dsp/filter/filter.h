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
namespace deluge::dsp::filter {
/// @brief Interface for filters in the sound engine
///This is a CRTP base class for all filters used in the sound engine. To implement a new filter,
///Add an entry for the filter in deluge::dsp::filter::LPFMode
///Create a subclass of Filter, using itself as the template parameter (the CRTP). Then implement the following methods:
///	a. setConfig. This is run when the filter is reconfigured, and should be used to convert the user-level parameters (frequency, resonance) to internal parameters
///	b. doFilter runs the filter on a series of samples, densely packed
/// c. doFilterStereo runs the filter on stereo samples packed in the LRLRLR... format.
/// d. reset resets any internal state of the filter to avoid clicks when processing a new voice
///Add a menu entry for the filter.
///Filters are extremely sensitive to performance, as they're run per channel, per voice. One additional multiply instruction can have a noticeable impact on maximum voice count, so care should be taken to ensure any filter is as performant as possible.
/// @tparam T
template <typename T>
class Filter {
public:
	Filter() = default;
	//returns a gain compensation value
	q31_t configure(q31_t frequency, q31_t resonance, LPFMode lpfMode, q31_t filterGain) {
		return static_cast<T*>(this)->setConfig(frequency, resonance, lpfMode, filterGain);
	}
	void filterMono(q31_t* startSample, q31_t* endSample, int32_t sampleIncrememt = 1, int32_t extraSaturation = 1) {
		static_cast<T*>(this)->doFilter(startSample, endSample, sampleIncrememt, extraSaturation);
	}
	//filter an interleaved stereo buffer
	void filterStereo(q31_t* startSample, q31_t* endSample, int32_t extraSaturation = 1) {
		static_cast<T*>(this)->doFilterStereo(startSample, endSample, extraSaturation);
		;
	}
	void reset() { static_cast<T*>(this)->resetFilter(); }
};

} // namespace deluge::dsp::filter
