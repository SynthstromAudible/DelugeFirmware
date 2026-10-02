/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

// PatchCableSet::getModifiedPatchCableAmount lives in its own TU (split from the app-coupled
// patch_cable_set.cpp): it reads only patch-cable value/source state + fixed-point math + param enums, so it
// is gui/model-free and links into the standalone synth core (the patcher's one PatchCableSet runtime dep).

#include "modulation/params/param.h"
#include "modulation/patch/patch_cable_set.h"
#include "util/fixedpoint.h"

namespace params = deluge::modulation::params;

int32_t PatchCableSet::getModifiedPatchCableAmount(int32_t c, int32_t p) {
	// For some params, we square the cable strength, to make it slope up more slowly at first, so we have access
	// to small effects as well as big
	int32_t output;
	int32_t amount = patchCables[c].param.getCurrentValue();
	switch (p) {
	case params::LOCAL_PITCH_ADJUST:
	case params::LOCAL_OSC_A_PITCH_ADJUST:
	case params::LOCAL_OSC_B_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_0_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_1_PITCH_ADJUST:
	case params::GLOBAL_DELAY_RATE:
		output = (amount >> 15) * (amount >> 16);
		if (amount < 0) {
			output = -output;
		}

		if (p == params::LOCAL_PITCH_ADJUST) {
			// If patching to master pitch, adjust range so that, on max range, the velocity-editing steps correspond
			// with whole semitones
			if (patchCables[c].from == PatchSource::VELOCITY) {
				// output = (output / 3 << 1);
				output = multiply_32x32_rshift32_rounded(output, 1431655765) << 1;
			}
			else {
				output = multiply_32x32_rshift32_rounded(output, 1518500250)
				         << 1; // Divides by square root of 2. Gives us 3 octaves of shifting rather than 4
			}
		}

		return output;
	default:
		return amount;
	}
}
