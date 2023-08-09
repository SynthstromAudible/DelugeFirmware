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

#include "dsp/filter/filter_set.h"
#include "definitions_cxx.hpp"
#include "dsp/filter/filter.h"
#include "dsp/filter/lpladder.h"
#include "dsp/filter/svf.h"
#include "dsp/timestretch/time_stretcher.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
namespace deluge::dsp::filter {
FilterSet::FilterSet() {

	lpsvf = SVFilter();
	lpladder = LpLadderFilter();
	hpladder = HpLadderFilter();
}

void FilterSet::renderHPFLong(q31_t* startSample, q31_t* endSample, FilterMode lpfMode, int32_t sampleIncrement,
                              int32_t extraSaturation) {
	if (HPFOn) {
		hpladder.filterMono(startSample, endSample, sampleIncrement, extraSaturation);
	}
}
void FilterSet::renderHPFLongStereo(q31_t* startSample, q31_t* endSample, int32_t extraSaturation) {
	if (HPFOn) {
		hpladder.filterStereo(startSample, endSample, extraSaturation);
	}
}

void FilterSet::renderLPFLong(q31_t* startSample, q31_t* endSample, FilterMode lpfMode, int32_t sampleIncrement,
                              int32_t extraSaturation, int32_t extraSaturationDrive) {
	if (LPFOn) {
		if (lpfMode == FilterMode::SVF) {
			if (lastLPFMode != FilterMode::SVF) {
				lpsvf.reset();
			}
			lpsvf.filterMono(startSample, endSample, sampleIncrement);
		}
		else {
			if (lastLPFMode > kLastLadder) {
				lpladder.reset();
			}
			lpladder.filterMono(startSample, endSample, sampleIncrement, extraSaturation);
		}
	}
}

void FilterSet::renderLPFLongStereo(q31_t* startSample, q31_t* endSample, int32_t extraSaturation) {
	if (LPFOn) {
		if (lpfMode == FilterMode::SVF) {

			lpsvf.filterStereo(startSample, endSample, extraSaturation);
		}
		else {

			lpladder.filterStereo(startSample, endSample, extraSaturation);
		}
	}
}
void FilterSet::renderLong(q31_t* startSample, q31_t* endSample, int32_t numSamples, int32_t sampleIncrememt,
                           int32_t extraSaturation) {
	switch (routing_) {
	case FilterRoute::HIGH_TO_LOW:
		renderHPFLong(startSample, endSample, lpfMode, sampleIncrememt);
		renderLPFLong(startSample, endSample, lpfMode, sampleIncrememt, extraSaturation, extraSaturation >> 1);
		break;
	case FilterRoute::LOW_TO_HIGH:
		renderLPFLong(startSample, endSample, lpfMode, sampleIncrememt, extraSaturation, extraSaturation >> 1);
		renderHPFLong(startSample, endSample, lpfMode, sampleIncrememt);
		break;
	case FilterRoute::PARALLEL:
		int32_t length = endSample - startSample;
		q31_t temp[length];
		memcpy(temp, startSample, length * sizeof(q31_t));
		renderHPFLong(temp, temp + length, lpfMode, sampleIncrememt);
		renderLPFLong(startSample, endSample, lpfMode, sampleIncrememt);
		for (int i = 0; i < length; i++) {
			startSample[i] += temp[i];
		}
		break;
	}
}
//expects to receive an interleaved stereo stream
void FilterSet::renderLongStereo(q31_t* startSample, q31_t* endSample, int32_t extraSaturation) {
	// Do HPF, if it's on
	switch (routing_) {
	case FilterRoute::HIGH_TO_LOW:
		if (HPFOn) {
			renderHPFLongStereo(startSample, endSample, extraSaturation);
		}

		// Do LPF, if it's on
		if (LPFOn) {
			renderLPFLongStereo(startSample, endSample, extraSaturation);
		}

		break;
	case FilterRoute::LOW_TO_HIGH:
		if (LPFOn) {
			renderLPFLongStereo(startSample, endSample, extraSaturation);
		}
		if (HPFOn) {
			renderHPFLongStereo(startSample, endSample, extraSaturation);
		}
		break;
	case FilterRoute::PARALLEL:
		int32_t length = endSample - startSample;
		q31_t temp[length];
		memcpy(temp, startSample, length * sizeof(q31_t));

		if (HPFOn) {
			renderHPFLongStereo(temp, temp + length, extraSaturation);
		}

		// Do LPF, if it's on
		if (LPFOn) {
			renderLPFLongStereo(startSample, endSample, extraSaturation);
		}
		for (int i = 0; i < length; i++) {
			startSample[i] += temp[i];
		}
		break;
	}
}

int32_t FilterSet::setConfig(int32_t lpfFrequency, int32_t lpfResonance, bool doLPF, FilterMode lpfmode,
                             int32_t hpfFrequency, int32_t hpfResonance, bool doHPF, FilterMode hpfmode,
                             int32_t filterGain, FilterRoute routing, bool adjustVolumeForHPFResonance,
                             int32_t* overallOscAmplitude) {
	LPFOn = doLPF;
	HPFOn = doHPF;
	lpfMode = lpfmode;
	hpfMode = hpfmode;
	routing_ = routing;
	hpfResonance =
	    (hpfResonance >> 21) << 21; // Insanely, having changes happen in the small bytes too often causes rustling

	if (LPFOn) {
		if (lpfmode == FilterMode::SVF) {
			if (lastLPFMode != FilterMode::SVF) {
				lpsvf.reset();
			}
			filterGain = lpsvf.configure(lpfFrequency, lpfResonance, lpfmode, filterGain);
		}
		else {
			if (lastLPFMode > kLastLadder) {
				lpladder.reset();
			}
			filterGain = lpladder.configure(lpfFrequency, lpfResonance, lpfmode, filterGain);
		}
		lastLPFMode = lpfMode;
	}
	else {
		lastLPFMode = FilterMode::OFF;
	}
	// This changes the overall amplitude so that, with resonance on 50%, the amplitude is the same as it was pre June 2017
	filterGain = multiply_32x32_rshift32(filterGain, 1720000000) << 1;

	// HPF
	if (HPFOn) {
		if (hpfMode == FilterMode::HPLADDER) {
			filterGain = hpladder.configure(hpfFrequency, hpfResonance, hpfMode, filterGain);
			if (lastHPFMode != hpfMode) {
				hpladder.reset();
			}
		}
		lastHPFMode = hpfMode;
	}
	else {
		lastHPFMode = FilterMode::OFF;
	}

	return filterGain;
}

void FilterSet::reset() {
	hpladder.reset();
	lpsvf.reset();
	lpladder.reset();
	noiseLastValue = 0;
}
} // namespace deluge::dsp::filter
