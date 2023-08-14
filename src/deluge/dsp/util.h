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

#include "definitions_cxx.hpp"
#include "dsp/filter/filter.h"
#include "dsp/filter/filter_set.h"
#include "dsp/filter/lpladder.h"
#include "dsp/filter/svf.h"
#include "dsp/timestretch/time_stretcher.h"
#include "processing/sound/sound.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
namespace deluge::dsp {
/**
 * Fold reduces the input by the amount it's over the level
*/
inline q31_t fold(q31_t input, q31_t level) {
	//no folding occurs if max is 0 or if max is greater than input
	//to keep the knob range consistent fold starts from 0 and
	//increases, decreasing would lead to a large deadspace until
	//suddenly clipping occured
	q31_t extra = 0;
	q31_t max = level >> 7;
	if (input > max) {
		extra = input - max;
	}
	else if (input < -max) {
		extra = input + max;
	}
	//this avoids inverting the wave
	return 2 * extra - input;
}
/**
 * foldBuffer folds a whole buffer. Works for stereo too
*/
void foldBuffer(q31_t* startSample, q31_t* endSample, q31_t level) {
	q31_t* currentSample = startSample;
	do {
		q31_t outs = fold(*currentSample, level);
		*currentSample = outs;

		currentSample += 1;
	} while (currentSample < endSample);
}
} // namespace deluge::dsp
