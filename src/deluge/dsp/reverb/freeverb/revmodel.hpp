// Reverb model declaration
//
// Written by Jezar at Dreampoint, June 2000
// http://www.dreampoint.co.uk
// This code is public domain

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

#include "dsp/reverb/freeverb/allpass.hpp"
#include "dsp/reverb/freeverb/comb.hpp"
#include "dsp/reverb/freeverb/tuning.h"

class revmodel {
public:
	revmodel();
	void mute();
	// void	process(int32_t input, int32_t *outputL, int32_t *outputR);
	void setroomsize(float value);
	float getroomsize();
	void setdamp(float value);
	float getdamp();
	void setwet(float value);
	float getwet();
	void setdry(float value);
	float getdry();
	void setwidth(float value);
	float getwidth();
	void setmode(float value);
	float getmode();

	inline void process(int32_t input, int32_t* outputL, int32_t* outputR) {
		int32_t outL, outR;

		outL = outR = 0;

		// Accumulate comb filters in parallel
		for (int32_t i = 0; i < numcombs; i++) {
			outL += combL[i].process(input);
			outR += combR[i].process(input);
		}

		// Feed through allpasses in series
		for (int32_t i = 0; i < numallpasses; i++) {
			outL = allpassL[i].process(outL);
			outR = allpassR[i].process(outR);
		}

		// Calculate output
		*outputL = outL + (multiply_32x32_rshift32_rounded(outR, wet2)) << 1;
		*outputR = outR + (multiply_32x32_rshift32_rounded(outL, wet2)) << 1;
	}

private:
	void update();

private:
	int32_t gain;
	float roomsize;
	float damp;
	float wet;
	float wet1;
	int32_t wet2;
	float dry;
	float width;
	float mode;

	// The following are all declared inline
	// to remove the need for dynamic allocation
	// with its subsequent error-checking messiness

	// Comb filters
	comb combL[numcombs];
	comb combR[numcombs];

	// Allpass filters
	allpass allpassL[numallpasses];
	allpass allpassR[numallpasses];

	// Buffers for the combs
	int32_t bufcombL1[combtuningL1];
	int32_t bufcombR1[combtuningR1];
	int32_t bufcombL2[combtuningL2];
	int32_t bufcombR2[combtuningR2];
	int32_t bufcombL3[combtuningL3];
	int32_t bufcombR3[combtuningR3];
	int32_t bufcombL4[combtuningL4];
	int32_t bufcombR4[combtuningR4];
	int32_t bufcombL5[combtuningL5];
	int32_t bufcombR5[combtuningR5];
	int32_t bufcombL6[combtuningL6];
	int32_t bufcombR6[combtuningR6];
	int32_t bufcombL7[combtuningL7];
	int32_t bufcombR7[combtuningR7];
	int32_t bufcombL8[combtuningL8];
	int32_t bufcombR8[combtuningR8];

	// Buffers for the allpasses
	int32_t bufallpassL1[allpasstuningL1];
	int32_t bufallpassR1[allpasstuningR1];
	int32_t bufallpassL2[allpasstuningL2];
	int32_t bufallpassR2[allpasstuningR2];
	int32_t bufallpassL3[allpasstuningL3];
	int32_t bufallpassR3[allpasstuningR3];
	int32_t bufallpassL4[allpasstuningL4];
	int32_t bufallpassR4[allpasstuningR4];
};

// ends
