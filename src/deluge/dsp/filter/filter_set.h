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
#include "dsp/filter/hpladder.h"
#include "dsp/filter/ladder_components.h"
#include "dsp/filter/lpladder.h"
#include "dsp/filter/svf.h"
#include "util/fixedpoint.h"
#include <cstdint>

class Sound;

namespace deluge::dsp::filter {
class FilterSet {
public:
	FilterSet();
	void reset();
	q31_t setConfig(q31_t lpfFrequency, q31_t lpfResonance, bool doLPF, FilterMode lpfmode, q31_t lpfMorph,
	                q31_t hpfFrequency, q31_t hpfResonance, bool doHPF, FilterMode hpfmode, q31_t hpfMorph,
	                q31_t filterGain, FilterRoute routing, bool adjustVolumeForHPFResonance = true,
	                q31_t* overallOscAmplitude = NULL);

	void renderLong(q31_t* startSample, q31_t* endSample, int32_t numSamples, int32_t sampleIncrememt = 1);

	// expects to receive an interleaved stereo stream
	void renderLongStereo(q31_t* startSample, q31_t* endSample);

	// used to check whether the filter is used at all
	inline bool isLPFOn() { return LPFOn; }
	inline bool isHPFOn() { return HPFOn; }
	inline bool isOn() { return HPFOn || LPFOn; }

private:
	q31_t noiseLastValue;
	FilterMode lpfMode_;
	FilterMode lastLPFMode_;
	FilterMode hpfMode_;
	FilterMode lastHPFMode_;
	FilterRoute routing_;

	void renderLPFLong(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement = 1);
	void renderLPFLongStereo(q31_t* startSample, q31_t* endSample);
	void renderHPFLongStereo(q31_t* startSample, q31_t* endSample);
	void renderHPFLong(q31_t* startSample, q31_t* endSample, int32_t sampleIncrement = 1);
	void renderLadderHPF(q31_t* outputSample);

	SVFilter lpsvf;
	LpLadderFilter lpladder;
	HpLadderFilter hpladder;
	SVFilter hpsvf;
	bool LPFOn;
	bool HPFOn;
};
} // namespace deluge::dsp::filter
