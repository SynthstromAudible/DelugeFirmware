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

// The parameter scaling/patching math definitions, moved verbatim out of the gui-laden util/functions.cpp so
// the synth core links them without pulling gui. All dependencies here are gui-free: fixed-point math,
// the dsp-next table interpolator, the numeric lookup tables, and the param enums. See util/param_functions.h.

#include "util/param_functions.h"
#include "definitions_cxx.hpp"
#include "dsp-next/fixed/curves/exp_curve.h"
#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "modulation/params/param.h"
#include "util/fixedpoint.h"
#include "util/lookuptables/lookuptables.h"
#include <cstdint>

using deluge::dsp::fixed::kLinearTableInterpolator;

namespace params = deluge::modulation::params;
using namespace deluge;

int32_t getFinalParameterValueHybrid(int32_t paramNeutralValue, int32_t patchedValue) {
	// Allows for max output values of +- 1073741824, which the panning code understands as the full range from left to
	// right
	int32_t preLimits = (paramNeutralValue >> 2) + (patchedValue >> 1);
	return signed_saturate<32 - 3>(preLimits) << 2;
}

int32_t getFinalParameterValueVolume(int32_t paramNeutralValue, int32_t patchedValue) {

	// patchedValue's range is ideally +- 536870912, but may get up to 1610612736 due to multiple patch cables having
	// been multiplied

	// No need for max/min here - it's already been taken care of in patchAllCablesToParameter(),
	// ... or if we got here from patchSourceToAllExclusiveCables(), there's no way it could have been too big
	int32_t positivePatchedValue = patchedValue + 536870912;

	// positivePatchedValue's range is ideally 0 ("0") to 1073741824 ("2"), but potentially up to 2147483647 ("4").
	// 536870912 represents "1".

	// If this parameter is a volume one, then apply a parabola curve to the patched value, at this late stage
	/*
	if (isVolumeParam) {
	    // But, our output value can't get bigger than 2147483647 ("4"), which means we have to clip our input off at
	1073741824 ("2") if (positivePatchedValue >= 1073741824) positivePatchedValue = 2147483647; else {
	        //int32_t madeSmaller = positivePatchedValue >> 15;
	        //positivePatchedValue = (madeSmaller * madeSmaller) << 1;
	        positivePatchedValue = (positivePatchedValue >> 15) * (positivePatchedValue >> 14);
	    }
	}
	*/

	// This is a temporary (?) fix I've done to allow FM modulator amounts to get past where I clipped off volume
	// params. So now volumes can get higher too. Problem? Not sure.

	// But, our output value can't get bigger than 2147483647 ("4"), which means we have to clip our input off at
	// 1073741824 ("2")
	positivePatchedValue = (positivePatchedValue >> 16) * (positivePatchedValue >> 15);

	// return multiply_32x32_rshift32(positivePatchedValue, paramNeutralValue) << 5;

	// Must saturate, otherwise mod fx depth can easily overflow
	return lshift_and_saturate<5>(multiply_32x32_rshift32(positivePatchedValue, paramNeutralValue));
}

int32_t getFinalParameterValueLinear(int32_t paramNeutralValue, int32_t patchedValue) {

	// patchedValue's range is ideally +- 536870912 ('1'), but may get up to 1610612736 ('3') due to multiple patch
	// cables having been multiplied

	// No need for max/min here - it's already been taken care of in patchAllCablesToParameter(),
	// ... or if we got here from patchSourceToAllExclusiveCables(), there's no way it could have been too big
	int32_t positivePatchedValue = patchedValue + 536870912;

	// positivePatchedValue's range is ideally 0 ("0") to 1073741824 ("2"), but potentially up to 2147483647 ("4").
	// 536870912 represents "1".

	// Must saturate, otherwise sustain level can easily overflow
	return lshift_and_saturate<3>(multiply_32x32_rshift32(positivePatchedValue, paramNeutralValue));
}

int32_t getFinalParameterValueExp(int32_t paramNeutralValue, int32_t patchedValue) {
	return getExp(paramNeutralValue, patchedValue);
}

int32_t getFinalParameterValueExpWithDumbEnvelopeHack(int32_t paramNeutralValue, int32_t patchedValue, int32_t p) {
	// TODO: this is horribly hard-coded, but works for now
	if (params::LOCAL_ENV_0_DECAY <= p && p <= params::LOCAL_ENV_3_RELEASE) {
		return multiply_32x32_rshift32(paramNeutralValue, lookupReleaseRate(patchedValue));
	}
	if (params::LOCAL_ENV_0_ATTACK <= p && p <= params::LOCAL_ENV_3_ATTACK) {
		patchedValue = -patchedValue;
	}

	return getFinalParameterValueExp(paramNeutralValue, patchedValue);
}

int32_t getExp(int32_t presetValue, int32_t adjustment) {
	return deluge::dsp::fixed::exp_curve(presetValue, adjustment);
}

int32_t lookupReleaseRate(int32_t input) {
	int32_t magnitude = 24;
	int32_t whichValue = input >> magnitude;                           // 25
	int32_t howMuchFurther = (input << (31 - magnitude)) & 2147483647; // 6
	whichValue += 32;                                                  // Put it in the range 0 to 64
	if (whichValue < 0) {
		return releaseRateTable64[0];
	}
	else if (whichValue >= 64) {
		return releaseRateTable64[64];
	}
	int32_t value1 = releaseRateTable64[whichValue];
	int32_t value2 = releaseRateTable64[whichValue + 1];
	return (multiply_32x32_rshift32(value2, howMuchFurther)
	        + multiply_32x32_rshift32(value1, 2147483647 - howMuchFurther))
	       << 1;
}
