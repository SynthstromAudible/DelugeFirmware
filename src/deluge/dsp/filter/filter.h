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

#include "model/mod_controllable/filters/filter_config.h"
#include "util/fixedpoint.h"
#include "util/functions.h"
#include <cstdint>

namespace deluge::dsp::filter {
constexpr int32_t ONE_Q16 = 134217728;

extern q31_t blendBuffer[SSI_TX_BUFFER_NUM_SAMPLES * 2];
/**
 *  Interface for filters in the sound engine
 * This is a CRTP base class for all filters used in the sound engine. To implement a new filter,
 * Add an entry for the filter in deluge::dsp::filter::FilterMode
 * Create a subclass of Filter, using itself as the template parameter (the CRTP). Then implement the following methods:
 * a. setConfig. This is run when the filter is reconfigured, and should be used to convert the user-level parameters
 * (frequency, resonance) to internal parameters b. doFilter runs the filter on a series of samples, densely packed c.
 * doFilterStereo runs the filter on stereo samples packed in the LRLRLR... format. d. reset resets any internal state
 * of the filter to avoid clicks when processing a new voice Then add a menu entry for the filter, and add item to
 * string conversion in ModControllableAudio::switchLPFMode Filters are extremely sensitive to performance, as they're
 * run per channel, per voice. One additional multiply instruction can have a noticeable impact on maximum voice count,
 * so care should be taken to ensure any filter is as performant as possible.
 */
template <typename T>
class Filter {
public:
	Filter() = default;
	// returns a gain compensation value
	q31_t configure(q31_t frequency, q31_t resonance, FilterMode lpfMode, q31_t lpfMorph, q31_t filterGain) {

		// lpfmorph comes in q28 but we want q31
		return static_cast<T*>(this)->setConfig(frequency, resonance, lpfMode, lshiftAndSaturate<2>(lpfMorph),
		                                        filterGain);
	}
	/**
	 * Filter a buffer of mono samples from startSample to endSample incrememnting by the increment
	 * @param startSample pointer to first sample in buffer
	 * @param endSample pointer to last sample
	 * @param sampleIncrement increment between samples
	 * @param extraSaturation extra saturation value
	 */
	[[gnu::hot]] void filterMono(q31_t* startSample, q31_t* endSample, int32_t sampleIncrememt = 1) {
		if (dryFade < 0.001) {
			static_cast<T*>(this)->doFilter(startSample, endSample, sampleIncrememt);
		}
		else {
			memcpy(blendBuffer, startSample, (endSample - startSample) * sizeof(q31_t));
			static_cast<T*>(this)->doFilter(startSample, endSample, sampleIncrememt);

			q31_t* currentSample = startSample;
			q31_t* currentDrySample = blendBuffer;
			do {
				q31_t wet = multiply_32x32_rshift32(*currentSample, wetLevel);
				*currentSample = multiply_accumulate_32x32_rshift32_rounded(wet, *currentDrySample, ONE_Q31 - wetLevel)
				                 << 1;
				currentSample += 1;
				currentDrySample += 1;
				updateBlend(); // will go to one over 512 samples
			} while (currentSample < endSample);
		}
	}
	/**
	 * Filter a buffer of interleaved stereo samples from startSample to endSample incrememnting by the increment
	 * @param startSample pointer to first sample in buffer
	 * @param endSample pointer to last sample
	 * @param extraSaturation extra saturation value
	 */
	[[gnu::hot]] void filterStereo(q31_t* startSample, q31_t* endSample) {
		if (dryFade < 0.001) {
			static_cast<T*>(this)->doFilterStereo(startSample, endSample);
		}
		else {
			memcpy(blendBuffer, startSample, (endSample - startSample) * sizeof(q31_t));
			static_cast<T*>(this)->doFilterStereo(startSample, endSample);

			q31_t* currentSample = startSample;
			q31_t* currentDrySample = blendBuffer;
			do {
				q31_t wet = multiply_32x32_rshift32(*currentSample, wetLevel);
				*currentSample = multiply_accumulate_32x32_rshift32_rounded(wet, *currentDrySample, ONE_Q31 - wetLevel)
				                 << 1;
				currentSample += 1;
				currentDrySample += 1;
				updateBlend();
			} while (currentSample < endSample);
		}
	}
	/**
	 * reset the internal filter state to avoid clicks and pops
	 * All zeroes must be a valid reset state as the filter data will be zeroed by the filterset
	 */
	void reset(bool fade = false) {
		static_cast<T*>(this)->resetFilter();
		if (fade) {
			dryFade = 1;
			wetLevel = 0;
		}
	}

	inline void updateBlend() {
		// fades over around 500 samples
		dryFade = dryFade * 0.99;
		wetLevel = (q31_t)(ONE_Q31 * (1 - dryFade));
	}

	/**
	 * Applies a pleasing curve to the linear frequency from the knob
	 * Stores tan(f) and 1/(tan(f)) as well for use in further calculations
	 */
	void curveFrequency(q31_t frequency) {
		// Between 0 and 8, by my making. 1 represented by 268435456
		tannedFrequency = instantTan(lshiftAndSaturate<5>(frequency));

		// this is 1q31*1q16/(1q16+tan(f)/2)
		// tan(f) is q17
		divideBy1PlusTannedFrequency = (q31_t)(288230376151711744.0 / (double)(ONE_Q16 + (tannedFrequency >> 1)));
		fc = multiply_32x32_rshift32_rounded(tannedFrequency, divideBy1PlusTannedFrequency) << 4;
	}
	q31_t fc;
	float dryFade = 1;
	q31_t wetLevel = ONE_Q31;
	q31_t tannedFrequency;
	q31_t divideBy1PlusTannedFrequency;
};

} // namespace deluge::dsp::filter
