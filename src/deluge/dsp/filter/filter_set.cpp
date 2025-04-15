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

std::array<StereoSample, SSI_TX_BUFFER_NUM_SAMPLES> temp_render_buffer;

[[gnu::hot]] void FilterSet::renderHPFLong(std::span<q31_t> buffer) {
	if (!HPFOn) {
		return;
	}
	if ((hpfMode_ == FilterMode::SVF_BAND) || (hpfMode_ == FilterMode::SVF_NOTCH)) {
		hpfilter.svf.filterMono(buffer);
	}
	else if (hpfMode_ == FilterMode::HPLADDER) {
		hpfilter.ladder.filterMono(buffer);
	}
}

[[gnu::hot]] void FilterSet::renderHPFLongStereo(std::span<StereoSample> buffer) {
	if (!HPFOn) {
		return;
	}
	if ((hpfMode_ == FilterMode::SVF_BAND) || (hpfMode_ == FilterMode::SVF_NOTCH)) {
		hpfilter.svf.filterStereo(buffer);
	}
	else if (hpfMode_ == FilterMode::HPLADDER) {
		hpfilter.ladder.filterStereo(buffer);
	}
}

[[gnu::hot]] void FilterSet::renderLPFLong(std::span<q31_t> buffer) {
	if (!LPFOn) {
		return;
	}
	if ((lpfMode_ == FilterMode::SVF_BAND) || (lpfMode_ == FilterMode::SVF_NOTCH)) {
		lpfilter.svf.filterMono(buffer);
		return;
	}
	lpfilter.ladder.filterMono(buffer);
}

[[gnu::hot]] void FilterSet::renderLPFLongStereo(std::span<StereoSample> buffer) {
	if (!LPFOn) {
		return;
	}
	if ((lpfMode_ == FilterMode::SVF_BAND) || (lpfMode_ == FilterMode::SVF_NOTCH)) {
		lpfilter.svf.filterStereo(buffer);
		return;
	}
	lpfilter.ladder.filterStereo(buffer);
}

[[gnu::hot]] void FilterSet::renderLong(std::span<q31_t> buffer) {
	switch (routing_) {
	case FilterRoute::HIGH_TO_LOW:
		renderHPFLong(buffer);
		renderLPFLong(buffer);
		break;

	case FilterRoute::LOW_TO_HIGH:
		renderLPFLong(buffer);
		renderHPFLong(buffer);
		break;

	case FilterRoute::PARALLEL:
		// render one filter in the temp buffer so we can add
		// them together
		memcpy(temp_render_buffer.data(), buffer.data(), buffer.size_bytes());
		std::span mono_temp_render_buffer{reinterpret_cast<q31_t*>(temp_render_buffer.data()), buffer.size()};

		renderHPFLong(mono_temp_render_buffer);
		renderLPFLong(buffer);
		std::ranges::transform(buffer, mono_temp_render_buffer, buffer.begin(), std::plus{});
		break;
	}
}
// expects to receive an interleaved stereo stream
[[gnu::hot]] void FilterSet::renderLongStereo(std::span<StereoSample> buffer) {
	// Do HPF, if it's on
	switch (routing_) {
	case FilterRoute::HIGH_TO_LOW:
		renderHPFLongStereo(buffer);
		renderLPFLongStereo(buffer);
		break;

	case FilterRoute::LOW_TO_HIGH:
		renderLPFLongStereo(buffer);
		renderHPFLongStereo(buffer);
		break;

	case FilterRoute::PARALLEL:
		memcpy(temp_render_buffer.data(), buffer.data(), buffer.size_bytes());
		renderHPFLongStereo(std::span{temp_render_buffer}.first(buffer.size()));
		renderLPFLongStereo(buffer);
		std::ranges::transform(buffer, temp_render_buffer, buffer.begin(), std::plus{});
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
