/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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

#include "deluge/util/fixedpoint.h"
#include "deluge/util/functions.h"

namespace deluge::dsp {

/// UI-facing scale for drive/tweak, matches kMaxMenuValue used by every other 0-50 style parameter
constexpr int32_t kSaturationUiMax = 50;

/// Number of distinct drive steps the original (pre 0-50) Tanh saturation had. Rescaling drive down
/// to this range keeps the tone identical to before, just addressable with the finer 0-50 UI
/// resolution (same tradeoff Bitcrush/SRR already make: fine UI range, few real DSP steps).
constexpr int32_t kTanhLegacySteps = 15;

/// Maps the 0-50 drive range onto the original 0-15 Tanh curve-steepness steps.
constexpr uint8_t tanhLegacyDriveSteps(uint8_t driveAmount) {
	return static_cast<uint8_t>((driveAmount * kTanhLegacySteps) / kSaturationUiMax);
}

namespace saturation_detail {

/// multiply_32x32_rshift32() intentionally returns half the true product so callers can restore it
/// with "* 2" (see fixedpoint.h). That doubling can overflow int32 by exactly one step when the true
/// product is ~1.0 in q31 terms - most notably at the negative full-scale rail (NEGATIVE_ONE_Q31). A
/// plain "*2" then wraps that overflow into a huge sign-flipped value instead of clamping, which was
/// the cause of a real bug (hard, unexpected clipping/noise at low drive/level settings). Use
/// saturating doubling instead.
[[gnu::always_inline]] inline q31_t doubleSaturate(q31_t halfValue) {
	return add_saturate(halfValue, halfValue);
}

/// tweakAmount (0-50) as a q31 fraction, used for the Mix and Asymmetry controls.
[[gnu::always_inline]] inline q31_t tweakToFraction(uint8_t tweakAmount) {
	constexpr q31_t stepQ31 = ONE_Q31 / kSaturationUiMax;
	return static_cast<q31_t>(tweakAmount) * stepQ31;
}

/// Scales `x` up by `octaves` bits, applies `curve`, then scales the result back down by the same
/// amount - i.e. runs `curve` at a point further along its own shape without changing the overall
/// output level. This is how the Asymmetry tweak below reaches the compression/knee region of the
/// tanh curve at a lower input level for the negative half than the positive half, in a way that
/// survives at any Drive setting (unlike boosting the signal's amplitude and letting a later clip
/// stage sort it out, which stops doing anything once that stage is already fully saturated).
template <typename Curve>
[[gnu::always_inline]] inline q31_t curveAtSteeperPoint(q31_t x, int32_t octaves, Curve curve) {
	q31_t scaledUp = octaves ? lshiftAndSaturateUnknown(x, octaves) : x;
	q31_t result = curve(scaledUp);
	return octaves ? (result >> octaves) : result;
}

} // namespace saturation_detail

/// Runs a one-pole lowpass (tracked in *lowpassState) over the DRY input and returns it - this is
/// the clean, undistorted low-end that bypasses saturation entirely, letting the caller build a
/// "clean lows, dirty highs" signal flow: split the dry signal into low (this function's return
/// value) and high (dry minus this), saturate ONLY the high part, then sum low + saturated-high back
/// together. That's a deliberately different flow from a plain post-saturation tone filter: filtering
/// *after* the fact only ever removes low-frequency content that has ALREADY been distorted (and, in
/// the case of a highpass, throws away signal rather than keeping it clean), whereas splitting first
/// keeps the low end's original, undistorted waveform intact and merely never sends it through the
/// saturation curve at all.
/// toneAmount (0-50) is the crossover point: 0 excludes nothing (the whole signal is distorted, same
/// as if Tone didn't exist), rising towards 50 pushes the crossover up towards roughly 1.5kHz,
/// keeping progressively more of the low end clean.
[[gnu::always_inline]] inline q31_t saturationCleanLowEnd(q31_t input, q31_t* lowpassState, uint8_t toneAmount) {
	using namespace saturation_detail;
	if (toneAmount == 0) {
		*lowpassState = 0;
		return 0;
	}
	constexpr q31_t kMaxAlpha = static_cast<q31_t>(0.2136 * ONE_Q31); // ~1.5kHz one-pole crossover @ 44.1kHz
	q31_t alpha = doubleSaturate(multiply_32x32_rshift32(tweakToFraction(toneAmount), kMaxAlpha));
	q31_t diff = subtract_saturate(input, *lowpassState);
	*lowpassState = add_saturate(*lowpassState, doubleSaturate(multiply_32x32_rshift32(diff, alpha)));
	return *lowpassState;
}

/// Tanh saturation: the antialiased lookup-table curve, where `driveAmount` controls the STEEPNESS of
/// the curve itself (via the LUT's saturationAmount parameter), not just a pre-gain into a fixed
/// shape - which is why this has real, audible character at low/moderate drive rather than needing
/// to be driven to the rails before anything happens.
/// tweakAmount ("Asymmetry") adds even harmonics on top of tanh's odd ones via curveAtSteeperPoint()
/// on the negative half only, so tweak=0 stays the plain symmetric tanh and higher tweak reads as
/// progressively dirtier/more analog. Only ever calls the antialiased lookup once per sample - it
/// carries state between samples (`lastWorkingValue`), so calling it twice in the same sample would
/// corrupt that state.
[[gnu::always_inline]] inline q31_t saturateTanh(q31_t input, uint32_t* lastWorkingValue, uint8_t driveAmount,
                                                 uint8_t tweakAmount) {
	using namespace saturation_detail;
	uint32_t saturationAmount = 5 + tanhLegacyDriveSteps(driveAmount);
	if (input >= 0 || tweakAmount == 0) {
		return getTanHAntialiased(input, lastWorkingValue, saturationAmount);
	}
	constexpr int32_t kMaxAsymOctaveTenths = 25; // up to ~2.5 octaves at tweak=50
	int32_t octaves = (static_cast<int32_t>(tweakAmount) * kMaxAsymOctaveTenths) / (kSaturationUiMax * 10);
	return curveAtSteeperPoint(input, octaves,
	                           [&](q31_t x) { return getTanHAntialiased(x, lastWorkingValue, saturationAmount); });
}

/// Dry/wet blend for the Mix control (0-50, 50 = fully wet). Caller supplies the pre-saturation
/// (dry) sample.
[[gnu::always_inline]] inline q31_t blendSaturationMix(q31_t dry, q31_t wet, uint8_t mixAmount) {
	q31_t wetFraction = saturation_detail::tweakToFraction(mixAmount);
	q31_t diff = subtract_saturate(wet, dry);
	return add_saturate(dry, saturation_detail::doubleSaturate(multiply_32x32_rshift32(diff, wetFraction)));
}

} // namespace deluge::dsp
