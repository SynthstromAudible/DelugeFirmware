/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include <AudioEngine.h>
#include <SampleControls.h>
#include "definitions.h"
#include "functions.h"

SampleControls::SampleControls() {
	interpolationMode = INTERPOLATION_MODE_SMOOTH;
	pitchAndSpeedAreIndependent = false;
	reversed = false;
}

int SampleControls::getInterpolationBufferSize(int32_t phaseIncrement) {
	if (interpolationMode == INTERPOLATION_MODE_LINEAR) {
useLinearInterpolation:
		return 2;
	}
	else {

		// If CPU dire...
		if (AudioEngine::cpuDireness) {
			int octave =
			    getMagnitudeOld(phaseIncrement); // Unstretched, and the first octave going up from that, would be '25'
			if (octave
			    >= 26
			           - (AudioEngine::cpuDireness
			              >> 2)) { // So, under max direness (14), everything from octave 23 up will be linearly interpolated. That's from 2 octaves down, upward
				goto useLinearInterpolation;
			}
		}

		return INTERPOLATION_MAX_NUM_SAMPLES;
	}
}
