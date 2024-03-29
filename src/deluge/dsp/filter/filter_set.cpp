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

namespace deluge::dsp::filter {

q31_t tempRenderBuffer[SSI_TX_BUFFER_NUM_SAMPLES * 2]; // * 2 to accomodate stereo samples

[[gnu::hot]] void FilterSet::renderHPFLong(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement) {
	if (HPFOn) {
		if (hpfMode_ == FilterMode::HPLADDER) {
			hpfilter.ladder.filterMono(startSample, endSample, sampleIncrement);
		}
		else if ((hpfMode_ == FilterMode::SVF_BAND) || (hpfMode_ == FilterMode::SVF_NOTCH)) {
			hpfilter.svf.filterMono(startSample, endSample, sampleIncrement);
		}
	}
}
[[gnu::hot]] void FilterSet::renderHPFLongStereo(q31_t* startSample, q31_t* endSample) {
	if (HPFOn) {
		if (hpfMode_ == FilterMode::HPLADDER) {
			hpfilter.ladder.filterStereo(startSample, endSample);
		}
		else if ((hpfMode_ == FilterMode::SVF_BAND) || (hpfMode_ == FilterMode::SVF_NOTCH)) {
			hpfilter.svf.filterStereo(startSample, endSample);
		}
	}
}

[[gnu::hot]] void FilterSet::renderLPFLong(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement) {
	if (LPFOn) {
		if ((lpfMode_ == FilterMode::SVF_BAND) || (lpfMode_ == FilterMode::SVF_NOTCH)) {
			lpfilter.svf.filterMono(startSample, endSample, sampleIncrement);
		}
		else {
			lpfilter.ladder.filterMono(startSample, endSample, sampleIncrement);
		}
	}
}

[[gnu::hot]] void FilterSet::renderLPFLongStereo(q31_t* startSample, q31_t* endSample) {
	if (LPFOn) {
		if ((lpfMode_ == FilterMode::SVF_BAND) || (lpfMode_ == FilterMode::SVF_NOTCH)) {

			lpfilter.svf.filterStereo(startSample, endSample);
		}
		else {

			lpfilter.ladder.filterStereo(startSample, endSample);
		}
	}
}
[[gnu::hot]] void FilterSet::renderLong(q31_t* startSample, q31_t* endSample, int32_t numSamples,
                                        int32_t sampleIncrememt) {
	switch (routing_) {
	case FilterRoute::HIGH_TO_LOW:

		renderHPFLong(startSample, endSample, sampleIncrememt);
		renderLPFLong(startSample, endSample, sampleIncrememt);

		break;
	case FilterRoute::LOW_TO_HIGH:

		renderLPFLong(startSample, endSample, sampleIncrememt);
		renderHPFLong(startSample, endSample, sampleIncrememt);

		break;

	case FilterRoute::PARALLEL:
		// render one filter in the temp buffer so we can add
		// them together
		int32_t length = endSample - startSample;
		memcpy(tempRenderBuffer, startSample, length * sizeof(q31_t));

		renderHPFLong(tempRenderBuffer, tempRenderBuffer + length, sampleIncrememt);
		renderLPFLong(startSample, endSample, sampleIncrememt);

		for (int i = 0; i < length; i++) {
			startSample[i] += tempRenderBuffer[i];
		}
		break;
	}
}
// expects to receive an interleaved stereo stream
[[gnu::hot]] void FilterSet::renderLongStereo(q31_t* startSample, q31_t* endSample) {
	// Do HPF, if it's on
	switch (routing_) {
	case FilterRoute::HIGH_TO_LOW:

		renderHPFLongStereo(startSample, endSample);

		renderLPFLongStereo(startSample, endSample);

		break;
	case FilterRoute::LOW_TO_HIGH:

		renderLPFLongStereo(startSample, endSample);

		renderHPFLongStereo(startSample, endSample);

		break;
	case FilterRoute::PARALLEL:
		int32_t length = endSample - startSample;

		memcpy(tempRenderBuffer, startSample, length * sizeof(q31_t));

		renderHPFLongStereo(tempRenderBuffer, tempRenderBuffer + length);

		renderLPFLongStereo(startSample, endSample);

		for (int i = 0; i < length; i++) {
			startSample[i] += tempRenderBuffer[i];
		}
		break;
	}
}

int32_t FilterSet::setConfig(q31_t lpfFrequency, q31_t lpfResonance, FilterMode lpfmode, q31_t lpfMorph,
                             q31_t hpfFrequency, q31_t hpfResonance, FilterMode hpfmode, q31_t hpfMorph,
                             q31_t filterGain, FilterRoute routing, bool adjustVolumeForHPFResonance,
                             q31_t* overallOscAmplitude) {
	LPFOn = lpfmode != FilterMode::OFF;
	HPFOn = hpfmode != FilterMode::OFF;
	lpfMode_ = lpfmode;
	hpfMode_ = hpfmode;
	routing_ = routing;
	// Insanely, having changes happen in the small bytes too often causes rustling
	hpfResonance = (hpfResonance >> 21) << 21;

	if (LPFOn) {
		if ((lpfMode_ == FilterMode::SVF_BAND) || (lpfMode_ == FilterMode::SVF_NOTCH)) {
			if (SpecificFilter(lastLPFMode_).getFamily() != FilterFamily::SVF) {
				lpfilter.svf.reset(lastLPFMode_ == FilterMode::OFF);
			}
			filterGain = lpfilter.svf.configure(lpfFrequency, lpfResonance, lpfMode_, lpfMorph, filterGain);
		}
		else {
			if (SpecificFilter(lastLPFMode_).getFamily() != FilterFamily::LP_LADDER) {
				lpfilter.ladder.reset(lastLPFMode_ == FilterMode::OFF);
			}
			filterGain = lpfilter.ladder.configure(lpfFrequency, lpfResonance, lpfMode_, lpfMorph, filterGain);
		}
		lastLPFMode_ = lpfMode_;
	}
	else {
		lastLPFMode_ = FilterMode::OFF;
	}
	// This changes the overall amplitude so that, with resonance on 50%, the amplitude is the same as it was pre June
	// 2017
	filterGain = multiply_32x32_rshift32(filterGain, 1720000000) << 1;

	// HPF
	if (HPFOn) {
		if (hpfMode_ == FilterMode::HPLADDER) {
			filterGain = hpfilter.ladder.configure(hpfFrequency, hpfResonance, hpfmode, hpfMorph, filterGain);
			if (lastHPFMode_ != hpfMode_) {
				hpfilter.ladder.reset(lastHPFMode_ == FilterMode::OFF);
			}
		}
		// otherwise it's an SVF ((lpfmode == FilterMode::SVF_BAND) || (lpfmode == FilterMode::SVF_NOTCH))
		else {
			// invert the morph for the HPF so it goes high-band/notch-low
			filterGain =
			    hpfilter.svf.configure(hpfFrequency, hpfResonance, hpfmode, ((1 << 29) - 1) - hpfMorph, filterGain);
			if (lastHPFMode_ != hpfMode_) {
				hpfilter.svf.reset(lastHPFMode_ == FilterMode::OFF);
			}
		}
		lastHPFMode_ = hpfMode_;
	}
	else {
		lastHPFMode_ = FilterMode::OFF;
	}

	return filterGain;
}

void FilterSet::reset() {
	memset(&lpfilter, 0, sizeof(LowPass));
	memset(&hpfilter, 0, sizeof(HighPass));
}
} // namespace deluge::dsp::filter
