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
#include "dsp-next/fixed/interpolation/table_interpolation.h"
#include "modulation/params/param.h"
#include "util/fixedpoint.h"
#include "util/lookuptables/lookuptables.h"
#include <cstdint>

using deluge::dsp::fixed::linear_table_interpolator;

namespace params = deluge::modulation::params;
using namespace deluge;
using params::kNumParams;

int32_t paramRanges[kNumParams];
int32_t paramNeutralValues[kNumParams];

// This is just the range of the user-defined "preset" value, it doesn't apply to the outcome of patch cables
int32_t getParamRange(int32_t p) {
	switch (p) {
	case params::LOCAL_ENV_0_ATTACK:
	case params::LOCAL_ENV_1_ATTACK:
	case params::LOCAL_ENV_2_ATTACK:
	case params::LOCAL_ENV_3_ATTACK:
		return 536870912 * 1.5;

	case params::GLOBAL_DELAY_RATE:
		return 536870912;

	case params::LOCAL_PITCH_ADJUST:
	case params::LOCAL_OSC_A_PITCH_ADJUST:
	case params::LOCAL_OSC_B_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_0_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_1_PITCH_ADJUST:
		return 536870912;

	case params::LOCAL_LPF_FREQ:
		return 536870912 * 1.4;

		// For phase width, we have this higher (than I previously did) because these are hibrid params, meaning that
		// with a source (e.g. LFO) patched to them, the might have up to 1073741824 added to them
		// - which would take us to the max user "preset value", which is what we want for phase width
	default:
		return 1073741824;
	}
}

int32_t getParamNeutralValue(int32_t p) {
	switch (p) {
	case params::LOCAL_OSC_A_VOLUME:
	case params::LOCAL_OSC_B_VOLUME:
	case params::GLOBAL_VOLUME_POST_REVERB_SEND:
	case params::LOCAL_NOISE_VOLUME:
	case params::GLOBAL_REVERB_AMOUNT:
	case params::GLOBAL_VOLUME_POST_FX:
	case params::LOCAL_VOLUME:
		return 134217728;

	case params::LOCAL_MODULATOR_0_VOLUME:
	case params::LOCAL_MODULATOR_1_VOLUME:
		return 33554432;

	case params::LOCAL_LPF_FREQ:
		return 2000000;
	case params::LOCAL_HPF_FREQ:
		return 2672947;

	case params::GLOBAL_LFO_FREQ_1:
	case params::GLOBAL_LFO_FREQ_2:
	case params::LOCAL_LFO_LOCAL_FREQ_1:
	case params::LOCAL_LFO_LOCAL_FREQ_2:
	case params::GLOBAL_MOD_FX_RATE:
		return 121739; // lfoRateTable[userValue];

	case params::LOCAL_LPF_RESONANCE:
	case params::LOCAL_HPF_RESONANCE:
	case params::LOCAL_LPF_MORPH:
	case params::LOCAL_HPF_MORPH:
	case params::LOCAL_FOLD:
		return 25 * 10737418; // Room to be quadrupled

	case params::LOCAL_PAN:
	case params::LOCAL_OSC_A_PHASE_WIDTH:
	case params::LOCAL_OSC_B_PHASE_WIDTH:
		return 0;

	case params::LOCAL_ENV_0_ATTACK:
	case params::LOCAL_ENV_1_ATTACK:
	case params::LOCAL_ENV_2_ATTACK:
	case params::LOCAL_ENV_3_ATTACK:
		return 4096; // attackRateTable[userValue];

	case params::LOCAL_ENV_0_RELEASE:
	case params::LOCAL_ENV_1_RELEASE:
	case params::LOCAL_ENV_2_RELEASE:
	case params::LOCAL_ENV_3_RELEASE:
		return 140 << 9; // releaseRateTable[userValue];

	case params::LOCAL_ENV_0_DECAY:
	case params::LOCAL_ENV_1_DECAY:
	case params::LOCAL_ENV_2_DECAY:
	case params::LOCAL_ENV_3_DECAY:
		return 70 << 9; // releaseRateTable[userValue] >> 1;

	case params::LOCAL_ENV_0_SUSTAIN:
	case params::LOCAL_ENV_1_SUSTAIN:
	case params::LOCAL_ENV_2_SUSTAIN:
	case params::LOCAL_ENV_3_SUSTAIN:
	case params::GLOBAL_DELAY_FEEDBACK:
		return 1073741824; // 536870912;

	case params::LOCAL_MODULATOR_0_FEEDBACK:
	case params::LOCAL_MODULATOR_1_FEEDBACK:
	case params::LOCAL_CARRIER_0_FEEDBACK:
	case params::LOCAL_CARRIER_1_FEEDBACK:
		return 5931642;

	case params::GLOBAL_DELAY_RATE:
	case params::GLOBAL_ARP_RATE:
	case params::LOCAL_PITCH_ADJUST:
	case params::LOCAL_OSC_A_PITCH_ADJUST:
	case params::LOCAL_OSC_B_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_0_PITCH_ADJUST:
	case params::LOCAL_MODULATOR_1_PITCH_ADJUST:
		return kMaxSampleValue; // Means we have space to 8x (3-octave-shift) the pitch if we want... (wait, I've since
		                        // made it 16x smaller)

	case params::GLOBAL_MOD_FX_DEPTH:
		return 526133494; // 2% lower than 536870912

	default:
		return 0;
	}
}

void functionsInit() {

	for (int32_t p = 0; p < kNumParams; p++) {
		paramRanges[p] = getParamRange(p);
	}

	for (int32_t p = 0; p < kNumParams; p++) {
		paramNeutralValues[p] = getParamNeutralValue(p);
	}
}

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
	return lshiftAndSaturate<5>(multiply_32x32_rshift32(positivePatchedValue, paramNeutralValue));
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
	return lshiftAndSaturate<3>(multiply_32x32_rshift32(positivePatchedValue, paramNeutralValue));
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

	int32_t magnitudeIncrease = (adjustment >> 26) + 2;

	// Do "fine" adjustment - change less than one doubling
	int32_t adjustedPresetValue = multiply_32x32_rshift32(
	    presetValue, linear_table_interpolator.interpolate(adjustment & 67108863, 26, expTableSmall));

	return increaseMagnitudeAndSaturate(adjustedPresetValue, magnitudeIncrease);
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
