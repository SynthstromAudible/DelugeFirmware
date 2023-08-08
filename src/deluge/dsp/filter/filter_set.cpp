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

void FilterSet::renderHPFLong(q31_t* startSample, q31_t* endSample, LPFMode lpfMode, int32_t sampleIncrement,
                              int32_t extraSaturation) {

	hpladder.filterMono(startSample, endSample, sampleIncrement, extraSaturation);
}
void FilterSet::renderHPFLongStereo(q31_t* startSample, q31_t* endSample, int32_t extraSaturation) {

	hpladder.filterStereo(startSample, endSample, extraSaturation);
}

void FilterSet::renderLPFLong(q31_t* startSample, q31_t* endSample, LPFMode lpfMode, int32_t sampleIncrement,
                              int32_t extraSaturation, int32_t extraSaturationDrive) {

	if (lpfMode == LPFMode::SVF) {
		if (lastLPFMode != LPFMode::SVF) {
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
	lastLPFMode = lpfMode;
}

void FilterSet::renderLPFLongStereo(q31_t* startSample, q31_t* endSample, int32_t extraSaturation) {

	if (lpfMode == LPFMode::SVF) {

		lpsvf.filterStereo(startSample, endSample, extraSaturation);
	}
	else {

		lpladder.filterStereo(startSample, endSample, extraSaturation);
	}
}

int32_t FilterSet::setConfig(int32_t lpfFrequency, int32_t lpfResonance, bool doLPF, LPFMode lpfmode,
                             int32_t hpfFrequency, int32_t hpfResonance, bool doHPF, HPFMode hpfmode,
                             int32_t filterGain, bool adjustVolumeForHPFResonance, int32_t* overallOscAmplitude) {
	LPFOn = doLPF;
	HPFOn = doHPF;
	lpfMode = lpfmode;
	hpfMode = hpfmode;
	hpfResonance =
	    (hpfResonance >> 21) << 21; // Insanely, having changes happen in the small bytes too often causes rustling

	if (LPFOn) {
		if (lpfmode == LPFMode::SVF) {
			if (lastLPFMode != LPFMode::SVF) {
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
		lastLPFMode = LPFMode::OFF;
	}
	// This changes the overall amplitude so that, with resonance on 50%, the amplitude is the same as it was pre June 2017
	filterGain = multiply_32x32_rshift32(filterGain, 1720000000) << 1;

	// HPF
	if (HPFOn) {
		if (hpfMode == HPFMode::HPLADDER) {
			filterGain = hpladder.configure(hpfFrequency, hpfResonance, lpfmode, filterGain);
			if (lastHPFMode != hpfMode) {
				hpladder.reset();
			}
		}
		lastHPFMode = hpfMode;
	}
	else {
		lastHPFMode == HPFMode::OFF;
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
