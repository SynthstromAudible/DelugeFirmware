/*
 * Copyright © 2026
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

/*
 * Heat: a cubic soft-clipper followed by a one-pole tilt tone control.
 *
 * Deliberately shaped like foldBufferPolyApproximation() in util.hpp — same q31
 * conventions, same (startSample, endSample) pointer-range walk, same do/while. Read
 * fold() there first if the fixed-point idiom is unfamiliar.
 *
 * FIXED-POINT RULE used throughout: multiply_32x32_rshift32(a, b) yields a*b/2, not
 * a*b, because it drops 32 bits of a 64-bit product held in q31. Every multiply below
 * is therefore followed by a shift that puts the result back on scale. Getting this
 * wrong is silent — the stage just ends up 6 dB down — so each transfer curve is stated
 * in a comment and was checked numerically against its ideal before committing.
 *
 * WHY CUBIC AND NOT TANH. Sound::saturate() already gives a tanh curve via lookup, so a
 * second tanh would duplicate an existing character. The cubic has a harder knee and a
 * true ceiling at ±1, which reads as "pedal" beside the softer saturate(). It is also
 * three multiplies with no table lookup, which matters when this runs per voice.
 *
 * ALIASING — a genuine limitation, not an oversight. There is no oversampling, and the
 * cubic generates harmonics to 3f, so bright material driven hard WILL alias.
 * Oversampling per-voice on a 400 MHz Cortex-A9 was judged too expensive. The call site
 * puts Heat AFTER the ladder filter, so rolling the LPF down also tames the aliasing.
 */

// ---------------------------------------------------------------------------
// Soft clipper
// ---------------------------------------------------------------------------

/// Transfer curve: y = 1.5x - 0.5x^3, evaluated on [-1, 1].
///
/// Slope at the origin is 1.5, and y(±1) = ±1 exactly. Monotonic over the whole domain —
/// which matters, because the very similar 1.5x - x^3 folds back above x = 0.707 and
/// would turn this into a second wavefolder rather than a clipper.
///
/// Callers must saturate x into [-1, 1] first; the curve is only monotonic there.
inline q31_t softClipCubic(q31_t x) {
	constexpr q31_t THREE_QUARTERS_Q31 = 0.75 * ONE_Q31;
	constexpr q31_t ONE_QUARTER_Q31 = 0.25 * ONE_Q31;

	// Each multiply_32x32_rshift32 halves the scale, so double after each to keep
	// x2 and x3 as true q31 powers of x.
	q31_t x2 = 2 * multiply_32x32_rshift32(x, x);
	q31_t x3 = 2 * multiply_32x32_rshift32(x2, x);

	// 4 * (0.75x/2 - 0.25x^3/2) == 1.5x - 0.5x^3. The <<2 is the put-it-back-on-scale
	// step; it saturates because y(±1) lands exactly on the rail.
	return lshiftAndSaturateUnknown(
	    multiply_32x32_rshift32(THREE_QUARTERS_Q31, x) - multiply_32x32_rshift32(ONE_QUARTER_Q31, x3), 2);
}

/// Makeup gain. Anchors the stage's PEAK response at unity: sweeps 1.0 down to 0.75 as
/// `level` runs 0..2^29.
///
/// WHY NOT ANCHOR SMALL-SIGNAL GAIN. The obvious choice is makeup = 2/3, which cancels the
/// cubic's slope of 1.5 at the origin and makes quiet signals pass at exactly unity. That was
/// the original design, and it is wrong for this call site. softClipCubic saturates at y(±1)
/// = ±1, so any sample loud enough to reach the knee leaves the curve at the ceiling and is
/// then still multiplied by 0.667 — a flat 3.5 dB drop. Heat runs on oscBuffer BEFORE
/// overallOscAmplitude is applied (see voice.cpp), where the signal is typically hot, so in
/// practice almost everything hit that drop. With drive below about 2x there are not yet
/// enough harmonics to mask it, so knob 1-6 audibly ducked the level instead of dirtying it.
/// Reported from hardware 2026-08-05.
///
/// A saturating curve cannot be transparent at both ends — preserving peaks costs you
/// small-signal gain and vice versa. Peaks win here because the call site is hot.
///
/// CONSEQUENCE, so it is not misdiagnosed later: quiet material now gets up to +3.5 dB as Heat
/// comes off its stop, because small signals see the cubic's full 1.5 slope. That step is
/// expected. If it ever needs splitting, start the constant near 0.8 * ONE_Q31 instead of
/// ONE_Q31 — that trades ~1.9 dB of peak drop for ~1.6 dB of small-signal lift.
inline q31_t heatMakeup(q31_t level) {
	// level tops out just under 2^29 (see heatBuffer), so this lands on 0.75 at full drive —
	// a 2.5 dB fall across the sweep, enough that Heat reads as drive rather than as volume.
	return ONE_Q31 - level;
}

/*
 * Heat (drive stage).
 *
 * Pre-gain sweeps 1x to 256x on an EXPONENTIAL taper — gain = 2^(8 * level).
 *
 * The first hardware test used a linear 1x..33x law and was, in George's words, "not very
 * audible". Two things were wrong with it. It was ~4x weaker than the wavefolder sitting
 * next to it — foldBufferPolyApproximation() shifts its product left by 8, this shifted by
 * 6, which is 12 dB less drive:
 *
 *     level 0.25   fold x32    old heat x9
 *     level 0.50   fold x64    old heat x17
 *     level 1.00   fold x128   old heat x33
 *
 * And a linear taper wastes the knob: by 25% you are already most of the way to the
 * maximum in perceptual terms, so the top three quarters of the travel all sound alike.
 *
 * The exponential taper fixes both. `s` is the integer part and `f` the fraction; 2^s * (1 + f)
 * is the standard linear-interpolation-in-the-exponent approximation, and it is continuous at
 * every octave boundary because 2^s * 2 == 2^(s+1).
 *
 * Measured against the REAL param pipeline (not the DSP in isolation — that was the earlier
 * mistake), knob position 0..50:
 *
 *     knob   0    3    5   10   15   20   25   30   35    40    45    50
 *     gain  1.0  1.5  1.8  3.2  5.6  9.6   16   29   51    90   154   256
 *
 * Roughly a constant ratio per step, audible by knob 3-5, and no dead zone. Getting here needed
 * BOTH the >>26 above and moving LOCAL_HEAT out of the patcher's volume block (see param.h) —
 * the volume block's parabola alone held the entire bottom half between 1.0x and 2.0x.
 *
 * Memoryless, so an interleaved stereo buffer can be passed straight through as one
 * long range — exactly the shortcut foldBufferPolyApproximation() takes.
 */
inline void heatBuffer(q31_t* startSample, q31_t* endSample, q31_t level) {
	if (level <= 0) {
		return; // bypass — mirrors how LOCAL_FOLD is gated at the call site
	}

	const q31_t makeup = heatMakeup(level);
	// Split `level` into integer octaves and a fraction. The cast matters: shifting a signed
	// q31_t left would overflow into the sign bit.
	//
	// THE SHIFT IS TIED TO THE PARAM PIPELINE, not to q31. `level` is
	// paramFinalValues[LOCAL_HEAT], which does NOT span the full q31 range: with neutral value
	// 25*10737418 and getFinalParameterValueLinear's `<<3`, it tops out just under 2^29. So >>26
	// is what yields 0..7 octaves. An earlier version used >>28 on the assumption that level was
	// full-scale q31 — that capped the whole control at 16x instead of 256x. If the neutral value
	// or the param's patcher block ever changes, recompute this.
	const int32_t octaves = level >> 26;                                        // 0..7
	const q31_t frac = static_cast<q31_t>((static_cast<uint32_t>(level) << 5) & 0x7FFFFFFF);
	q31_t* currentSample = startSample;

	do {
		q31_t c = *currentSample;

		// c * (1 + frac), already saturated into q31 by add_saturation.
		// NOTE: on 1.2.1 (Chopin) this helper is named add_saturation; it was renamed to
		// add_saturate upstream after this release. Same `qadd` instruction, same semantics.
		// The 2* undoes the halving in multiply_32x32_rshift32.
		const q31_t driven = add_saturation(c, 2 * multiply_32x32_rshift32(c, frac));

		// THE octaves == 0 GUARD IS LOAD-BEARING. DO NOT REMOVE IT.
		//
		// lshiftAndSaturateUnknown(val, 0) is documented in functions.h as forbidden —
		// "lshift must be greater than 0! Not 0" — and it fails silently rather than loudly.
		// It calls signed_saturate_operand_unknown(val, 32 - 0), whose switch only covers 31
		// down to 13 and whose default is signed_saturate<12>. So a shift of zero clamps the
		// sample to TWELVE BITS and shifts by nothing: about 120 dB of attenuation, i.e.
		// silence, not merely a level drop.
		//
		// `octaves` is zero across the bottom of the knob (level < 2^26, so k < 6.25 of 50),
		// which is precisely the range that was reported as muting on hardware — twice, at two
		// different widths, because earlier param laws put the octaves==0 boundary in a
		// different place. It was misdiagnosed as insufficient drive and then as makeup gain
		// before the real cause was found. See HANDOFF.md.
		//
		// At octaves == 0 no shift is wanted anyway, and `driven` is already saturated, so the
		// correct behaviour is simply to pass it through.
		const q31_t x = (octaves > 0) ? lshiftAndSaturateUnknown(driven, octaves) : driven;
		q31_t y = softClipCubic(x);

		*currentSample = lshiftAndSaturateUnknown(multiply_32x32_rshift32(y, makeup), 1);

		currentSample += 1;
	} while (currentSample < endSample);
}

// ---------------------------------------------------------------------------
// Tone (tilt)
// ---------------------------------------------------------------------------

/*
 * A one-pole lowpass splits the signal into lp and hp = x - lp, and the output is a
 * weighted sum of the two. The weights are chosen so that at centre both are unity and
 * the halves sum back to the input EXACTLY — measured flat to 0.00 dB. That is the whole
 * point of the tilt topology here: centre is a true bypass, so a user who never touches
 * Tone hears the drive character unaltered.
 *
 * Measured response relative to input:
 *            100 Hz    1 kHz    8 kHz
 *   dark     +5.8 dB  -1.7 dB  -18.5 dB
 *   centre    0.0 dB   0.0 dB    0.0 dB
 *   bright   -7.6 dB  +4.9 dB   +5.7 dB
 *
 * The pivot sits near 1 kHz, where a guitar-pedal tone stack usually puts it.
 */
constexpr int32_t kHeatToneCoefficientShift = 4; // lp += (x - lp) >> 4

/// tone: q31. 0 = fully dark, ONE_Q31/2 = flat, ONE_Q31 = fully bright.
/// `state` is the lowpass memory and must persist across calls, one per channel.
inline void heatToneBuffer(q31_t* startSample, q31_t* endSample, q31_t tone, q31_t* state) {
	const q31_t gHigh = tone;
	const q31_t gLow = ONE_Q31 - tone;
	q31_t lp = *state;
	q31_t* currentSample = startSample;

	do {
		q31_t c = *currentSample;
		lp += (c - lp) >> kHeatToneCoefficientShift;
		q31_t hp = c - lp;

		*currentSample =
		    lshiftAndSaturateUnknown(multiply_32x32_rshift32(lp, gLow) + multiply_32x32_rshift32(hp, gHigh), 2);

		currentSample += 1;
	} while (currentSample < endSample);

	*state = lp;
}

/*
 * Stereo tone MUST NOT reuse the mono path.
 *
 * The tone stage holds state, so walking an interleaved buffer with a single filter
 * would feed L into R's history and back — a comb filter, not a tone control. Hence the
 * strided loop and two independent states. This is the single easiest thing to get wrong
 * in this file, and the drive stage above is safe only because it is memoryless.
 */
inline void heatToneBufferStereo(q31_t* startSample, q31_t* endSample, q31_t tone, q31_t* stateL, q31_t* stateR) {
	const q31_t gHigh = tone;
	const q31_t gLow = ONE_Q31 - tone;
	q31_t lpL = *stateL;
	q31_t lpR = *stateR;
	q31_t* currentSample = startSample;

	while (currentSample < endSample) {
		q31_t l = currentSample[0];
		q31_t r = currentSample[1];

		lpL += (l - lpL) >> kHeatToneCoefficientShift;
		lpR += (r - lpR) >> kHeatToneCoefficientShift;
		q31_t hpL = l - lpL;
		q31_t hpR = r - lpR;

		currentSample[0] =
		    lshiftAndSaturateUnknown(multiply_32x32_rshift32(lpL, gLow) + multiply_32x32_rshift32(hpL, gHigh), 2);
		currentSample[1] =
		    lshiftAndSaturateUnknown(multiply_32x32_rshift32(lpR, gLow) + multiply_32x32_rshift32(hpR, gHigh), 2);

		currentSample += 2;
	}

	*stateL = lpL;
	*stateR = lpR;
}

/// Convert an unpatched param's signed q31 (-2^31 .. 2^31-1) to the unsigned 0..ONE_Q31
/// that heatToneBuffer expects, with the param's centre landing on tone centre.
/// Matches the codebase idiom — see mod_controllable_audio.cpp, where UNPATCHED_BITCRUSHING
/// and UNPATCHED_SAMPLE_RATE_REDUCTION are both converted by adding 2147483648.
inline q31_t heatToneFromUnpatched(int32_t unpatchedValue) {
	return static_cast<q31_t>((static_cast<int64_t>(unpatchedValue) + 2147483648LL) >> 1);
}

} // namespace deluge::dsp
