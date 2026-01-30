/*
 * Copyright © 2024-2025 Owlet Records
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
 *
 * --- Additional terms under GNU GPL version 3 section 7 ---
 * This file requires preservation of the above copyright notice and author attribution
 * in all copies or substantial portions of this file.
 */

// Sine shaper DSP implementations - large functions moved here to reduce header bloat
// Small inline helpers and struct definitions remain in sine_shaper.hpp

#include "dsp/sine_shaper.hpp"

namespace deluge::dsp {

SineShaperTwistParams computeSineShaperTwistParams(q31_t twist, const SineTableShaperParams* ssParams) {
	constexpr q31_t kZone1 = ONE_Q31 / 8;       // 1/8
	constexpr q31_t kZone2 = ONE_Q31 / 4;       // 2/8
	constexpr q31_t kZone3 = (ONE_Q31 / 8) * 3; // 3/8
	constexpr q31_t kZone4 = ONE_Q31 / 2;       // 4/8

	SineShaperTwistParams result;

	// Always apply harmonicPhaseOffset (Harmonic secret menu) - works in all zones
	// This allows cycling through harmonic zones even when Twist is in zones 0-3
	float phH = ssParams ? ssParams->harmonicPhaseOffset : 0.0f;
	result.phaseHarmonic = phH;
	result.phaseHarmonicFreqMod = phH; // Base value for frequency modulation

	// Compute phase offset first to determine behavior mode
	// Use 1024x multiplier for gamma (non-overlapping zones, since range is 1024)
	double phRaw = ssParams ? static_cast<double>(ssParams->twistPhaseOffset) + 1024.0 * ssParams->gammaPhase : 0.0;

	if (phRaw != 0.0) {
		// Full range phi-triangle evolution (like meta zones, but across all 8 zones)
		float pos = static_cast<float>(twist) / static_cast<float>(ONE_Q31);
		pos = std::clamp(pos, 0.0f, 1.0f);

		// Per-effect frequency modulation using phi triangles (non-monotonic)
		float fmW = 1.0f + pos * (0.25f + 0.25f * phi::wrapPhase(phRaw * phi::kPhi025));
		float fmE = 1.0f + pos * (0.25f + 0.25f * phi::wrapPhase(phRaw * phi::kPhi033));
		float fmR = 1.0f + pos * (0.25f + 0.25f * phi::wrapPhase(phRaw * phi::kPhi067));
		float fmF = 1.0f + pos * (0.25f + 0.25f * phi::wrapPhase(phRaw * phi::kPhiN025));

		// Scale and wrap ph per-frequency to preserve irrational divergence with large ph values
		float ph025 = phi::wrapPhase(phRaw * phi::kPhi025);
		float ph033 = phi::wrapPhase(phRaw * phi::kPhi033);
		float ph050 = phi::wrapPhase(phRaw * phi::kPhi050);
		float ph067 = phi::wrapPhase(phRaw * phi::kPhi067);
		float ph075 = phi::wrapPhase(phRaw * phi::kPhi075);
		float ph100 = phi::wrapPhase(phRaw * phi::kPhi100);
		float phN025 = phi::wrapPhase(phRaw * phi::kPhiN025);
		float phN050 = phi::wrapPhase(phRaw * phi::kPhiN050);

		// Gamma rotates harmonic blend weights via phi-scaled phase
		// phaseHarmonic: includes pos * 5.0f for position shifting (existing behavior)
		// phaseHarmonicFreqMod: RAW phRaw value - computeBlendWeights4 does its own freq-scaled wrapping
		result.phaseHarmonic += pos * 5.0f + ph100;
		result.phaseHarmonicFreqMod += static_cast<float>(phRaw); // Pass raw, not pre-wrapped

		// Width: scale(φ^0.25)*2 clipped * param(φ^0.5), duty 0.8/0.7
		float wS = std::min(triangleSimpleUnipolar(pos * phi::kPhi025 * fmW + ph025 + 0.166f, 0.8f) * 2.0f, 1.0f);
		float wP = triangleSimpleUnipolar(pos * phi::kPhi050 * fmW + ph050 + 0.984f, 0.7f);
		result.stereoWidth = wS * wP;
		result.stereoPhaseOffset = triangleSimpleUnipolar(pos * phi::kPhi067 * fmW + ph067 + 0.720f, 0.5f);
		result.stereoFreqMult = 1.0f + 0.5f * triangleSimpleUnipolar(pos * phi::kPhi100 * fmW + ph100 + 0.590f);
		// Stereo LFO rate: slow evolution with φ^-0.5, 80% duty for broad coverage
		result.stereoLfoRate = triangleSimpleUnipolar(pos * phi::kPhiN050 * fmW + phN050 + 0.333f, 0.8f);

		// Evens: bipolar rectified, scale(φ^0.33) * param(φ^0.75), sign selects mode
		float eS = triangleSimpleUnipolar(pos * phi::kPhi033 * fmE + ph033 + 0.970f, 0.5f);
		float eT = triangleFloat(pos * phi::kPhi075 * fmE + ph075 + 0.896f, 0.5f);
		float eAbs = std::abs(eT);
		result.evenAmount = eS * ((eT > 0.0f) ? eAbs : 0.0f);
		result.evenDryBlend = eS * ((eT < 0.0f) ? eAbs : 0.0f);

		// Rect: bipolar rectified, scale(φ^0.67) * param(φ^1.0), sign selects mode
		float rS = triangleSimpleUnipolar(pos * phi::kPhi067 * fmR + ph067 + 0.910f, 0.5f);
		float rT = triangleFloat(pos * phi::kPhi100 * fmR + ph100 + 0.845f, 0.5f);
		float rAbs = std::abs(rT);
		result.rectAmount = rS * ((rT > 0.0f) ? rAbs : 0.0f);
		result.rect2Amount = rS * ((rT < 0.0f) ? rAbs : 0.0f);

		// Feedback: scale(φ^-0.25) * param(φ^-0.5), quadratic param for safety
		float fS = triangleSimpleUnipolar(pos * phi::kPhiN025 * fmF + phN025 + 0.001f, 0.8f);
		float fP = triangleSimpleUnipolar(pos * phi::kPhiN050 * fmF + phN050 + 0.058f, 0.8f);
		result.feedbackAmount = fS * fP * fP * 0.25f;

		return result;
	}

	// Standard discrete zone behavior (phaseOffset == 0)
	if (twist < kZone1) {
		// Zone 0: Width - stereo spread with animated phase evolution
		float pos = static_cast<float>(twist) / static_cast<float>(kZone1);
		result.stereoWidth = (pos < 0.25f) ? pos * 4.0f : (pos < 0.75f) ? 1.0f : (1.0f - pos) * 4.0f;
		result.stereoFreqMult = (pos <= 0.75f) ? 1.0f : 1.0f + (pos - 0.75f) * 12.0f;
		result.stereoPhaseOffset = pos;
	}
	else if (twist < kZone2) {
		// Zone 1: Evens - bipolar rectified triangle (duty=1.0: dry→evens1→dry→evens2)
		float pos = static_cast<float>(twist - kZone1) / static_cast<float>(kZone2 - kZone1);
		float tri = triangleFloat(pos, 1.0f);
		float absTri = std::abs(tri);
		result.evenAmount = (tri > 0.0f) ? absTri : 0.0f;
		result.evenDryBlend = (tri < 0.0f) ? absTri : 0.0f;
	}
	else if (twist < kZone3) {
		// Zone 2: Rect - bipolar rectified triangle (duty=1.0: dry→rect1→dry→rect2)
		float pos = static_cast<float>(twist - kZone2) / static_cast<float>(kZone3 - kZone2);
		float tri = triangleFloat(pos, 1.0f);
		float absTri = std::abs(tri);
		result.rectAmount = (tri > 0.0f) ? absTri : 0.0f;
		result.rect2Amount = (tri < 0.0f) ? absTri : 0.0f;
	}
	else if (twist < kZone4) {
		// Zone 3: Feedback (capped at 25%)
		float pos = static_cast<float>(twist - kZone3) / static_cast<float>(kZone4 - kZone3);
		result.feedbackAmount = pos * 0.25f;
	}
	else {
		// Zone 4+: Meta - unified triangle evolution, all shift with (pos + ph)
		// phRaw is already 0.0 here (since we checked above), so just use discrete pos
		float pos = static_cast<float>(twist - kZone4) / static_cast<float>(ONE_Q31 - kZone4);

		// Scale and wrap ph per-frequency to preserve irrational divergence with large ph values
		float ph025 = phi::wrapPhase(phRaw * phi::kPhi025);
		float ph033 = phi::wrapPhase(phRaw * phi::kPhi033);
		float ph050 = phi::wrapPhase(phRaw * phi::kPhi050);
		float ph067 = phi::wrapPhase(phRaw * phi::kPhi067);
		float ph075 = phi::wrapPhase(phRaw * phi::kPhi075);
		float ph100 = phi::wrapPhase(phRaw * phi::kPhi100);
		float phN025 = phi::wrapPhase(phRaw * phi::kPhiN025);
		float phN050 = phi::wrapPhase(phRaw * phi::kPhiN050);

		// Per-effect freqMult: ramps 1.0→(1.25-1.5), peaks at pos=1, phXXX varies the peak
		float fmW = 1.0f + pos * (0.25f + 0.25f * ph025);
		float fmE = 1.0f + pos * (0.25f + 0.25f * ph033);
		float fmR = 1.0f + pos * (0.25f + 0.25f * ph067);
		float fmF = 1.0f + pos * (0.25f + 0.25f * phN025);

		result.phaseHarmonic += pos * 5.0f;

		// Phase offsets calculated so all triangles peak at pos=0.5 (end of effective zone 5), phi=0
		// offset = duty - (freq * 0.5 * 1.125) mod 1, where 1.125 = fmX at pos=0.5, phXXX=0

		// Width: scale(φ^0.25)*2 clipped * param(φ^0.5), duty 0.8/0.7 for broad coverage
		float wS = std::min(triangleSimpleUnipolar(pos * phi::kPhi025 * fmW + ph025 + 0.166f, 0.8f) * 2.0f, 1.0f);
		float wP = triangleSimpleUnipolar(pos * phi::kPhi050 * fmW + ph050 + 0.984f, 0.7f);
		result.stereoWidth = wS * wP;
		result.stereoPhaseOffset = triangleSimpleUnipolar(pos * phi::kPhi067 * fmW + ph067 + 0.720f, 0.5f);
		result.stereoFreqMult = 1.0f + 0.5f * triangleSimpleUnipolar(pos * phi::kPhi100 * fmW + ph100 + 0.590f);
		// Stereo LFO rate: slow evolution with φ^-0.5, 80% duty for broad coverage
		result.stereoLfoRate = triangleSimpleUnipolar(pos * phi::kPhiN050 * fmW + phN050 + 0.333f, 0.8f);

		// Evens: bipolar rectified, scale(φ^0.33) * param(φ^0.75), sign selects mode
		float eS = triangleSimpleUnipolar(pos * phi::kPhi033 * fmE + ph033 + 0.970f, 0.5f);
		float eT = triangleFloat(pos * phi::kPhi075 * fmE + ph075 + 0.896f, 0.5f);
		float eAbs = std::abs(eT);
		result.evenAmount = eS * ((eT > 0.0f) ? eAbs : 0.0f);
		result.evenDryBlend = eS * ((eT < 0.0f) ? eAbs : 0.0f);

		// Rect: bipolar rectified, scale(φ^0.67) * param(φ^1.0), sign selects mode
		float rS = triangleSimpleUnipolar(pos * phi::kPhi067 * fmR + ph067 + 0.910f, 0.5f);
		float rT = triangleFloat(pos * phi::kPhi100 * fmR + ph100 + 0.845f, 0.5f);
		float rAbs = std::abs(rT);
		result.rectAmount = rS * ((rT > 0.0f) ? rAbs : 0.0f);
		result.rect2Amount = rS * ((rT < 0.0f) ? rAbs : 0.0f);

		// Feedback: scale(φ^-0.25) * param(φ^-0.5), quadratic param for safety
		float fS = triangleSimpleUnipolar(pos * phi::kPhiN025 * fmF + phN025 + 0.001f, 0.8f);
		float fP = triangleSimpleUnipolar(pos * phi::kPhiN050 * fmF + phN050 + 0.058f, 0.8f);
		result.feedbackAmount = fS * fP * fP * 0.25f;
	}

	return result;
}

q31_t sineShapeCore(q31_t input, float driveGain, int32_t zone, const ShaperWeights* zoneWeights, float evenAmount,
                    float evenDryBlend, float rectAmount, float rect2Amount) {
	// Apply drive to input
	float inputF = static_cast<float>(input) * driveGain;

	// Clamp and convert back to q31 for zones that need bounded input
	// Keep inputF unclamped for zone 3 which needs phase wrapping
	constexpr float kMaxQ31 = 2147483647.0f;
	float inputFClamped = std::clamp(inputF, -kMaxQ31, kMaxQ31);
	q31_t driven = static_cast<q31_t>(inputFClamped);

	q31_t shaped;

	if (zone == 7) {
		// === Zone 7: Polynomial waveshaping with cascade blend + self-multiplication ===
		//
		// IMPORTANT: This must remain q31-based. Float conversion was attempted but failed.
		//
		// The polynomialOscillatorApproximation function applies P(x) = 6x - 8x³ twice
		// (internal double-cascade). It relies on 32-bit integer overflow/wrap behavior
		// for its characteristic sound. When input exceeds the linear region, the
		// polynomial blows up and wraps around, creating a specific wavefolding-like
		// saturation. Float doesn't have this natural saturation - values grow unbounded,
		// causing premature clipping and different harmonic content.
		//
		// A float version would need to explicitly model the q31 overflow behavior,
		// which adds complexity without benefit. Keep this as q31.

		q31_t scaledInput = lshiftAndSaturateUnknown(multiply_32x32_rshift32_rounded(ONE_Q31, driven), 8);

		// zoneWeights repurposed for Zone 7 (Poly): c1=cascadeBlend, c3=selfMulBlend
		// Zone boundary glitches are acceptable - smoothing is per-zone, not cross-zone
		float selfMulBlend = zoneWeights->c3;

		// Self-mul = input * |input| adds even harmonics before polynomial
		if (selfMulBlend > 0.01f) {
			q31_t absInput = (scaledInput < 0) ? -scaledInput : scaledInput;
			q31_t selfMulInput = multiply_32x32_rshift32_rounded(scaledInput, absInput) << 1;
			q31_t blendQ = static_cast<q31_t>(selfMulBlend * static_cast<float>(ONE_Q31));
			scaledInput = scaledInput + multiply_32x32_rshift32_rounded(selfMulInput - scaledInput, blendQ) * 2;
		}

		shaped = polynomialOscillatorApproximation(scaledInput) >> 8;

		// zoneWeights->c1 = cascadeBlend (0-1 float)
		q31_t cascadeBlend = static_cast<q31_t>(zoneWeights->c1 * static_cast<float>(ONE_Q31));

		// Debug: log actual DSP values (throttled)
		if (cascadeBlend > 0) {
			q31_t moreHarmonics = polynomialOscillatorApproximation(shaped << 7) >> 7;
			shaped = shaped + multiply_32x32_rshift32_rounded(moreHarmonics - shaped, cascadeBlend) * 2;
		}

		// -3dB to match other zones (poly is naturally hotter)
		constexpr q31_t kMinus3dB = static_cast<q31_t>(0.707 * ONE_Q31);
		shaped = multiply_32x32_rshift32(shaped, kMinus3dB) << 1;
	}
	else if (zone == 0 || zone == 1) {
		// === Zone 0 "3579" / Zone 1 "3579wm": Blended Chebyshev T3, T5, T7, T9 ===
		// Zone 0: Raw input - clean, precise Chebyshev harmonics
		// Zone 1: Sine-preprocessed input - warmer, FM-like character
		//
		// Gain staging design:
		// - Chebyshev polynomials produce clean harmonics when |x| ≤ 1.0
		// - At |x| = 1.0: maximum harmonic energy, still mathematically clean
		// - At |x| > 1.0: polynomial explodes exponentially (not usable)
		// - Hard saturation keeps rawX bounded to ±1.0, preserving full clean range
		// - Soft clipping would compress the signal before reaching |x| = 1.0,
		//   reducing clean headroom and preventing maximum harmonic energy
		// - The "harsh" sound past threshold is flat-topped input, not polynomial
		//   explosion - it's feedback that you've exceeded the operating point
		//
		// << 9 puts sweet spot at ~80% drive knob (+8dB), giving room to
		// back off for subtle harmonics while harsh feedback indicates overdrive.
		q31_t scaledInput = lshiftAndSaturateUnknown(multiply_32x32_rshift32_rounded(ONE_Q31, driven), 9);
		float rawX = static_cast<float>(scaledInput) / static_cast<float>(ONE_Q31);

		// Zone 1 applies sine waveshaping via lookup table to bound input to [-1, 1]
		// Factor increased by 50%: sin(x * 3π/4) instead of sin(x * π/2) for more coloration
		float x;
		if (zone == 1) {
			// Convert q31 input to phase for sin(x * 3π/4) using lookup table
			// Phase = scaledInput * 0.75 (3π/4 is 3/8 of full cycle, scaled for q31 input)
			int32_t phaseInt = (scaledInput >> 1) + (scaledInput >> 2); // * 0.75
			uint32_t phase = static_cast<uint32_t>(phaseInt);

			// getSine returns ~q30 from 16-bit table interpolation
			int32_t sinValue = getSine(phase);

			// Convert to float with unity gain (getSine peak is ~ONE_Q31)
			constexpr float kInvQ31 = 1.0f / static_cast<float>(ONE_Q31);
			x = static_cast<float>(sinValue) * kInvQ31;
		}
		else {
			// Zone 0: use raw input (allows overdrive for intended harmonic character)
			x = rawX;
		}

		// Use precomputed blended coefficients (hoisted from buffer loop)
		// Shared polynomial evaluation - changes here apply to both zones
		float result = evaluate3579Polynomial(x, *zoneWeights);

		// Gain staging: match other zones' output level
		// Chebyshev peak varies by weight blend - tune empirically
		// Previous: >> 7 too quiet, 3.0 too loud, 0.5 still loud, 0.1 9dB hot, 0.035 3.5dB hot, 0.023 1.5dB quiet
		constexpr float kChebyBoost = 0.027f; // Tune empirically (+1.5dB from 0.023)
		constexpr float kMinus3dB = 0.707f;   // Match other zones
		constexpr float kChebyGain = kChebyBoost * kMinus3dB;

		double scaled = static_cast<double>(result * kChebyGain) * static_cast<double>(ONE_Q31);
		scaled = std::clamp(scaled, static_cast<double>(INT32_MIN), static_cast<double>(INT32_MAX));
		shaped = static_cast<q31_t>(scaled);
	}
	else if (zone >= 2 && zone <= 6) {
		// === Zones 2-6: Matrix-based FM synthesis (shared infrastructure) ===
		// Zone 2 (FM):   Add, Ring, FM, Fold - mixed character
		// Zone 3 (Fold): k=1,2,3,4 - wavefolder depths
		// Zone 4 (Ring): n=2,3,4,5 - ring mod frequency ratios
		// Zone 5 (Add):  n=2,3,4,5 - additive frequency ratios
		// Zone 6 (Mod):  FM depths d=0.25,0.5,0.75,1.0

		// Shared: extract weights (c3=w0, c5=w1, c7=w2, c9=w3)
		q31_t w0q = static_cast<q31_t>(zoneWeights->c3 * ONE_Q31);
		q31_t w1q = static_cast<q31_t>(zoneWeights->c5 * ONE_Q31);
		q31_t w2q = static_cast<q31_t>(zoneWeights->c7 * ONE_Q31);
		q31_t w3q = static_cast<q31_t>(zoneWeights->c9 * ONE_Q31);
		// inputGainMult (1.0-4.0) → q29 so 4.0 fits in q31
		q31_t inputGainQ = static_cast<q31_t>(zoneWeights->c1 * (ONE_Q31 >> 2));

		// Shared: apply input gain and compute base phase
		q31_t gained = multiply_32x32_rshift32(driven, inputGainQ) << 2;
		uint32_t phase1 = static_cast<uint32_t>(gained) << 8;
		q31_t sine1 = getSine(phase1);
		// Peak scaling: multiply_32x32_rshift32(ONE_Q31, ONE_Q31) = 0.5 peak
		// Ring mod naturally gives 0.5, others need kScale=1.0 to match
		constexpr q31_t kScale = ONE_Q31;

		// Zone-specific: compute 4 modes (each peaks at ~0.5)
		q31_t m0, m1, m2, m3;

		if (zone == 2) {
			// FM: Add(×2), Ring(×2), FM(×2), Fold(×2)
			uint32_t phase2 = phase1 << 1;
			q31_t sine2 = getSine(phase2);
			// Add: (s1+s2)/2 scaled to 0.5 peak
			m0 = multiply_32x32_rshift32((sine1 >> 1) + (sine2 >> 1), kScale);
			// Ring: s1 × s2(+90°) scaled to 0.5 peak
			m1 = multiply_32x32_rshift32(sine1, getSine(phase2 + 0x40000000u));
			// FM: sin(p1 + s2>>1) scaled to 0.5 peak
			m2 = multiply_32x32_rshift32(getSine(phase1 + static_cast<uint32_t>(sine2 >> 1)), kScale);
			// Fold: sin(s1<<1) scaled to 0.5 peak
			m3 = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1) << 1), kScale);
		}
		else if (zone == 3) {
			// Fold: k=1,2,3,4 (increasing fold depth) - all peak at 0.5
			m0 = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1)), kScale);
			m1 = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1) << 1), kScale);
			m2 = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1) + (static_cast<uint32_t>(sine1) << 1)),
			                             kScale);
			m3 = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1) << 2), kScale);
		}
		else if (zone == 4) {
			// Ring: sin(x) × sin(nx) for n=2,3,4,5 - ring mod peaks at 0.5
			m0 = multiply_32x32_rshift32(sine1, getSine(phase1 << 1));
			m1 = multiply_32x32_rshift32(sine1, getSine(phase1 + (phase1 << 1)));
			m2 = multiply_32x32_rshift32(sine1, getSine(phase1 << 2));
			m3 = multiply_32x32_rshift32(sine1, getSine((phase1 << 2) + phase1));
		}
		else if (zone == 5) {
			// Add: sin(x) + sin(nx) for n=2,3,4,5 - sum scaled to 0.5 peak
			q31_t s2 = getSine(phase1 << 1);
			q31_t s3 = getSine(phase1 + (phase1 << 1));
			q31_t s4 = getSine(phase1 << 2);
			q31_t s5 = getSine((phase1 << 2) + phase1);
			m0 = multiply_32x32_rshift32((sine1 >> 1) + (s2 >> 1), kScale);
			m1 = multiply_32x32_rshift32((sine1 >> 1) + (s3 >> 1), kScale);
			m2 = multiply_32x32_rshift32((sine1 >> 1) + (s4 >> 1), kScale);
			m3 = multiply_32x32_rshift32((sine1 >> 1) + (s5 >> 1), kScale);
		}
		else { // zone == 6
			// Mod: FM with depths d=0.25,0.5,0.75,1.0 (modulator at 2x) - all peak at 0.5
			q31_t mod = getSine(phase1 << 1);
			m0 = multiply_32x32_rshift32(getSine(phase1 + static_cast<uint32_t>(mod >> 2)), kScale);
			m1 = multiply_32x32_rshift32(getSine(phase1 + static_cast<uint32_t>(mod >> 1)), kScale);
			m2 = multiply_32x32_rshift32(getSine(phase1 + static_cast<uint32_t>((mod >> 1) + (mod >> 2))), kScale);
			m3 = multiply_32x32_rshift32(getSine(phase1 + static_cast<uint32_t>(mod)), kScale);
		}

		// Shared: blend with normalized weights and apply attenuation
		q31_t blended = 0;
		blended = add_saturate(blended, multiply_32x32_rshift32(m0, w0q) << 1);
		blended = add_saturate(blended, multiply_32x32_rshift32(m1, w1q) << 1);
		blended = add_saturate(blended, multiply_32x32_rshift32(m2, w2q) << 1);
		blended = add_saturate(blended, multiply_32x32_rshift32(m3, w3q) << 1);
		shaped = blended >> 7;
	}

	// === Even harmonics (Twist Zone 1) ===
	// Two phased effects like rect/rect2:
	// evenAmount: compress positive midrange → asymmetry → even harmonics
	// evenDryBlend: blend negative toward dry input → different harmonic content per half
	if (evenAmount > 0.0f || evenDryBlend > 0.0f) {
		float shapedF = static_cast<float>(shaped);
		float drivenF = static_cast<float>(driven); // Pre-zone input (dry harmonics)

		if (shapedF > 0.0f && evenAmount > 0.0f) {
			// Positive: compress midrange (peak-preserving quadratic blend)
			// Ratio = blendedX/x = 1 + (x-1)*evenAmount (eliminates division)
			constexpr float kNormFactor = 1.0f / static_cast<float>(ONE_Q31);
			constexpr float kBoost = 256.0f;
			float x = std::clamp(shapedF * kNormFactor * kBoost, 0.0f, 1.0f);

			// ratio = 1 - evenAmount + x*evenAmount = 1 + (x-1)*evenAmount
			// At x=1: ratio=1 (unchanged), at x=0: ratio=1-evenAmount (compressed)
			constexpr float kEvenGain = 0.85f;
			float ratio = 1.0f - evenAmount * (1.0f - x);
			shaped = static_cast<q31_t>(ratio * shapedF * kEvenGain);
		}
		else if (shapedF < 0.0f && evenDryBlend > 0.0f) {
			// Negative: blend magnitudes, preserve negative sign
			// Blend driven's magnitude into shaped's magnitude for different harmonic content
			float absShapedF = -shapedF; // We know shapedF < 0
			float absDrivenF = std::abs(drivenF);

			// Cap driven contribution to 2× shaped magnitude (prevents >> 7 mismatch issues)
			float cappedDrivenAbs = std::min(absDrivenF, absShapedF * 2.0f);
			float blendedAbs = absShapedF * (1.0f - evenDryBlend) + cappedDrivenAbs * evenDryBlend;
			shaped = static_cast<q31_t>(-blendedAbs); // Keep negative
		}
	}

	// === Rect + Rect2 (Twist Zone 2) - parallel processing ===
	// Both effects operate on original shaped, then combine additively
	// Rect: blend toward |shaped| for octave-up effect
	// Rect2: peak-cutting fold (subtracts peak² toward sign flip)
	if (rectAmount > 0.0f || rect2Amount > 0.0f) {
		float shapedF = static_cast<float>(shaped);
		float absVal = std::abs(shapedF);
		float result = shapedF;

		// Rect contribution: crossfade toward |shaped|
		if (rectAmount > 0.0f) {
			result = result + (absVal - shapedF) * rectAmount;
		}

		// Rect2 contribution: sine expansion on positive half (level-independent)
		// Applies sin(x*π/2)/x ratio which boosts midrange, preserves peaks
		// At x=1: ratio=1 (unchanged), at x<1: ratio>1 (boosted)
		// Boost compensates for zone attenuation (e.g., >> 7 in FM zones)
		if (rect2Amount > 0.0f && result > 0.0f) {
			constexpr float kNormFactor = 1.0f / static_cast<float>(ONE_Q31);
			constexpr float kBoost = 256.0f; // Compensate for attenuation
			float x = std::clamp(result * kNormFactor * kBoost, 0.0f, 1.0f);

			// Use fast polynomial sin approximation (~10x faster than std::sin)
			constexpr float kHalfPi = 1.5707963267949f;
			float sineX = fastSinHalfPi(x * kHalfPi);

			// Sine/linear ratio: preserves peaks (1 at x=1), boosts midrange
			// Blend between original and sine-scaled (transparent at rect2Amount=0)
			float sineRatio = (x > 0.01f) ? sineX / x : 1.0f;
			float sineResult = result * sineRatio;
			result = result * (1.0f - rect2Amount) + sineResult * rect2Amount;
		}

		shaped = static_cast<q31_t>(result);
	}

	return shaped;
}

StereoShaped sineShapeCoreStereo(q31_t inputL, q31_t inputR, float driveGainL, float driveGainR, int32_t zone,
                                 const ShaperWeights* zoneWeightsL, const ShaperWeights* zoneWeightsR, float evenAmount,
                                 float evenDryBlend, float rectAmount, float rect2Amount) {
	// Apply drive to inputs
	float inputFL = static_cast<float>(inputL) * driveGainL;
	float inputFR = static_cast<float>(inputR) * driveGainR;

	// Clamp and convert back to q31
	constexpr float kMaxQ31 = 2147483647.0f;
	float inputFClampedL = std::clamp(inputFL, -kMaxQ31, kMaxQ31);
	float inputFClampedR = std::clamp(inputFR, -kMaxQ31, kMaxQ31);
	q31_t drivenL = static_cast<q31_t>(inputFClampedL);
	q31_t drivenR = static_cast<q31_t>(inputFClampedR);

	q31_t shapedL, shapedR;

	// Single zone branch for both channels
	if (zone == 7) {
		// === Zone 7: Polynomial waveshaping with cascade blend + self-multiplication ===
		q31_t scaledInputL = lshiftAndSaturateUnknown(multiply_32x32_rshift32_rounded(ONE_Q31, drivenL), 8);
		q31_t scaledInputR = lshiftAndSaturateUnknown(multiply_32x32_rshift32_rounded(ONE_Q31, drivenR), 8);

		float selfMulBlendL = zoneWeightsL->c3;
		float selfMulBlendR = zoneWeightsR->c3;

		// Self-mul for L
		if (selfMulBlendL > 0.01f) {
			q31_t absInput = (scaledInputL < 0) ? -scaledInputL : scaledInputL;
			q31_t selfMulInput = multiply_32x32_rshift32_rounded(scaledInputL, absInput) << 1;
			q31_t blendQ = static_cast<q31_t>(selfMulBlendL * static_cast<float>(ONE_Q31));
			scaledInputL = scaledInputL + multiply_32x32_rshift32_rounded(selfMulInput - scaledInputL, blendQ) * 2;
		}
		// Self-mul for R
		if (selfMulBlendR > 0.01f) {
			q31_t absInput = (scaledInputR < 0) ? -scaledInputR : scaledInputR;
			q31_t selfMulInput = multiply_32x32_rshift32_rounded(scaledInputR, absInput) << 1;
			q31_t blendQ = static_cast<q31_t>(selfMulBlendR * static_cast<float>(ONE_Q31));
			scaledInputR = scaledInputR + multiply_32x32_rshift32_rounded(selfMulInput - scaledInputR, blendQ) * 2;
		}

		shapedL = polynomialOscillatorApproximation(scaledInputL) >> 8;
		shapedR = polynomialOscillatorApproximation(scaledInputR) >> 8;

		q31_t cascadeBlendL = static_cast<q31_t>(zoneWeightsL->c1 * static_cast<float>(ONE_Q31));
		q31_t cascadeBlendR = static_cast<q31_t>(zoneWeightsR->c1 * static_cast<float>(ONE_Q31));

		if (cascadeBlendL > 0) {
			q31_t moreHarmonics = polynomialOscillatorApproximation(shapedL << 7) >> 7;
			shapedL = shapedL + multiply_32x32_rshift32_rounded(moreHarmonics - shapedL, cascadeBlendL) * 2;
		}
		if (cascadeBlendR > 0) {
			q31_t moreHarmonics = polynomialOscillatorApproximation(shapedR << 7) >> 7;
			shapedR = shapedR + multiply_32x32_rshift32_rounded(moreHarmonics - shapedR, cascadeBlendR) * 2;
		}

		constexpr q31_t kMinus3dB = static_cast<q31_t>(0.707 * ONE_Q31);
		shapedL = multiply_32x32_rshift32(shapedL, kMinus3dB) << 1;
		shapedR = multiply_32x32_rshift32(shapedR, kMinus3dB) << 1;
	}
	else if (zone == 0 || zone == 1) {
		// === Zones 0-1: Chebyshev polynomial waveshaping ===
		q31_t scaledInputL = lshiftAndSaturateUnknown(multiply_32x32_rshift32_rounded(ONE_Q31, drivenL), 9);
		q31_t scaledInputR = lshiftAndSaturateUnknown(multiply_32x32_rshift32_rounded(ONE_Q31, drivenR), 9);
		float rawXL = static_cast<float>(scaledInputL) / static_cast<float>(ONE_Q31);
		float rawXR = static_cast<float>(scaledInputR) / static_cast<float>(ONE_Q31);

		float xL, xR;
		if (zone == 1) {
			// Sine preprocessing for warm character
			int32_t phaseIntL = (scaledInputL >> 1) + (scaledInputL >> 2);
			int32_t phaseIntR = (scaledInputR >> 1) + (scaledInputR >> 2);
			constexpr float kInvQ31 = 1.0f / static_cast<float>(ONE_Q31);
			xL = static_cast<float>(getSine(static_cast<uint32_t>(phaseIntL))) * kInvQ31;
			xR = static_cast<float>(getSine(static_cast<uint32_t>(phaseIntR))) * kInvQ31;
		}
		else {
			xL = rawXL;
			xR = rawXR;
		}

		float resultL = evaluate3579Polynomial(xL, *zoneWeightsL);
		float resultR = evaluate3579Polynomial(xR, *zoneWeightsR);

		constexpr float kChebyBoost = 0.027f;
		constexpr float kMinus3dB = 0.707f;
		constexpr float kChebyGain = kChebyBoost * kMinus3dB;

		double scaledL = static_cast<double>(resultL * kChebyGain) * static_cast<double>(ONE_Q31);
		double scaledR = static_cast<double>(resultR * kChebyGain) * static_cast<double>(ONE_Q31);
		scaledL = std::clamp(scaledL, static_cast<double>(INT32_MIN), static_cast<double>(INT32_MAX));
		scaledR = std::clamp(scaledR, static_cast<double>(INT32_MIN), static_cast<double>(INT32_MAX));
		shapedL = static_cast<q31_t>(scaledL);
		shapedR = static_cast<q31_t>(scaledR);
	}
	else {
		// === Zones 2-6: FM synthesis ===
		// Extract weights (shared structure for both channels)
		q31_t w0qL = static_cast<q31_t>(zoneWeightsL->c3 * ONE_Q31);
		q31_t w1qL = static_cast<q31_t>(zoneWeightsL->c5 * ONE_Q31);
		q31_t w2qL = static_cast<q31_t>(zoneWeightsL->c7 * ONE_Q31);
		q31_t w3qL = static_cast<q31_t>(zoneWeightsL->c9 * ONE_Q31);
		q31_t inputGainQL = static_cast<q31_t>(zoneWeightsL->c1 * (ONE_Q31 >> 2));

		q31_t w0qR = static_cast<q31_t>(zoneWeightsR->c3 * ONE_Q31);
		q31_t w1qR = static_cast<q31_t>(zoneWeightsR->c5 * ONE_Q31);
		q31_t w2qR = static_cast<q31_t>(zoneWeightsR->c7 * ONE_Q31);
		q31_t w3qR = static_cast<q31_t>(zoneWeightsR->c9 * ONE_Q31);
		q31_t inputGainQR = static_cast<q31_t>(zoneWeightsR->c1 * (ONE_Q31 >> 2));

		// Compute base phases
		q31_t gainedL = multiply_32x32_rshift32(drivenL, inputGainQL) << 2;
		q31_t gainedR = multiply_32x32_rshift32(drivenR, inputGainQR) << 2;
		uint32_t phase1L = static_cast<uint32_t>(gainedL) << 8;
		uint32_t phase1R = static_cast<uint32_t>(gainedR) << 8;
		q31_t sine1L = getSine(phase1L);
		q31_t sine1R = getSine(phase1R);
		constexpr q31_t kScale = ONE_Q31;

		q31_t m0L, m1L, m2L, m3L;
		q31_t m0R, m1R, m2R, m3R;

		// Zone-specific mode computation (same zone for both channels)
		if (zone == 2) {
			// FM: Add, Ring, FM, Fold at 2x
			uint32_t phase2L = phase1L << 1;
			uint32_t phase2R = phase1R << 1;
			q31_t sine2L = getSine(phase2L);
			q31_t sine2R = getSine(phase2R);
			m0L = multiply_32x32_rshift32((sine1L >> 1) + (sine2L >> 1), kScale);
			m0R = multiply_32x32_rshift32((sine1R >> 1) + (sine2R >> 1), kScale);
			m1L = multiply_32x32_rshift32(sine1L, getSine(phase2L + 0x40000000u));
			m1R = multiply_32x32_rshift32(sine1R, getSine(phase2R + 0x40000000u));
			m2L = multiply_32x32_rshift32(getSine(phase1L + static_cast<uint32_t>(sine2L >> 1)), kScale);
			m2R = multiply_32x32_rshift32(getSine(phase1R + static_cast<uint32_t>(sine2R >> 1)), kScale);
			m3L = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1L) << 1), kScale);
			m3R = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1R) << 1), kScale);
		}
		else if (zone == 3) {
			// Fold: k=1,2,3,4
			m0L = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1L)), kScale);
			m0R = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1R)), kScale);
			m1L = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1L) << 1), kScale);
			m1R = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1R) << 1), kScale);
			m2L = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1L) + (static_cast<uint32_t>(sine1L) << 1)),
			                              kScale);
			m2R = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1R) + (static_cast<uint32_t>(sine1R) << 1)),
			                              kScale);
			m3L = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1L) << 2), kScale);
			m3R = multiply_32x32_rshift32(getSine(static_cast<uint32_t>(sine1R) << 2), kScale);
		}
		else if (zone == 4) {
			// Ring: n=2,3,4,5
			m0L = multiply_32x32_rshift32(sine1L, getSine(phase1L << 1));
			m0R = multiply_32x32_rshift32(sine1R, getSine(phase1R << 1));
			m1L = multiply_32x32_rshift32(sine1L, getSine(phase1L + (phase1L << 1)));
			m1R = multiply_32x32_rshift32(sine1R, getSine(phase1R + (phase1R << 1)));
			m2L = multiply_32x32_rshift32(sine1L, getSine(phase1L << 2));
			m2R = multiply_32x32_rshift32(sine1R, getSine(phase1R << 2));
			m3L = multiply_32x32_rshift32(sine1L, getSine((phase1L << 2) + phase1L));
			m3R = multiply_32x32_rshift32(sine1R, getSine((phase1R << 2) + phase1R));
		}
		else if (zone == 5) {
			// Add: n=2,3,4,5
			q31_t s2L = getSine(phase1L << 1);
			q31_t s2R = getSine(phase1R << 1);
			q31_t s3L = getSine(phase1L + (phase1L << 1));
			q31_t s3R = getSine(phase1R + (phase1R << 1));
			q31_t s4L = getSine(phase1L << 2);
			q31_t s4R = getSine(phase1R << 2);
			q31_t s5L = getSine((phase1L << 2) + phase1L);
			q31_t s5R = getSine((phase1R << 2) + phase1R);
			m0L = multiply_32x32_rshift32((sine1L >> 1) + (s2L >> 1), kScale);
			m0R = multiply_32x32_rshift32((sine1R >> 1) + (s2R >> 1), kScale);
			m1L = multiply_32x32_rshift32((sine1L >> 1) + (s3L >> 1), kScale);
			m1R = multiply_32x32_rshift32((sine1R >> 1) + (s3R >> 1), kScale);
			m2L = multiply_32x32_rshift32((sine1L >> 1) + (s4L >> 1), kScale);
			m2R = multiply_32x32_rshift32((sine1R >> 1) + (s4R >> 1), kScale);
			m3L = multiply_32x32_rshift32((sine1L >> 1) + (s5L >> 1), kScale);
			m3R = multiply_32x32_rshift32((sine1R >> 1) + (s5R >> 1), kScale);
		}
		else { // zone == 6
			// Mod: FM depths
			q31_t modL = getSine(phase1L << 1);
			q31_t modR = getSine(phase1R << 1);
			m0L = multiply_32x32_rshift32(getSine(phase1L + static_cast<uint32_t>(modL >> 2)), kScale);
			m0R = multiply_32x32_rshift32(getSine(phase1R + static_cast<uint32_t>(modR >> 2)), kScale);
			m1L = multiply_32x32_rshift32(getSine(phase1L + static_cast<uint32_t>(modL >> 1)), kScale);
			m1R = multiply_32x32_rshift32(getSine(phase1R + static_cast<uint32_t>(modR >> 1)), kScale);
			m2L = multiply_32x32_rshift32(getSine(phase1L + static_cast<uint32_t>((modL >> 1) + (modL >> 2))), kScale);
			m2R = multiply_32x32_rshift32(getSine(phase1R + static_cast<uint32_t>((modR >> 1) + (modR >> 2))), kScale);
			m3L = multiply_32x32_rshift32(getSine(phase1L + static_cast<uint32_t>(modL)), kScale);
			m3R = multiply_32x32_rshift32(getSine(phase1R + static_cast<uint32_t>(modR)), kScale);
		}

		// Blend modes for both channels
		q31_t blendedL = 0;
		blendedL = add_saturate(blendedL, multiply_32x32_rshift32(m0L, w0qL) << 1);
		blendedL = add_saturate(blendedL, multiply_32x32_rshift32(m1L, w1qL) << 1);
		blendedL = add_saturate(blendedL, multiply_32x32_rshift32(m2L, w2qL) << 1);
		blendedL = add_saturate(blendedL, multiply_32x32_rshift32(m3L, w3qL) << 1);
		shapedL = blendedL >> 7;

		q31_t blendedR = 0;
		blendedR = add_saturate(blendedR, multiply_32x32_rshift32(m0R, w0qR) << 1);
		blendedR = add_saturate(blendedR, multiply_32x32_rshift32(m1R, w1qR) << 1);
		blendedR = add_saturate(blendedR, multiply_32x32_rshift32(m2R, w2qR) << 1);
		blendedR = add_saturate(blendedR, multiply_32x32_rshift32(m3R, w3qR) << 1);
		shapedR = blendedR >> 7;
	}

	// === Even harmonics (Twist Zone 1) - process both channels ===
	if (evenAmount > 0.0f || evenDryBlend > 0.0f) {
		// Left channel
		float shapedFL = static_cast<float>(shapedL);
		float drivenFL = static_cast<float>(drivenL);
		if (shapedFL > 0.0f && evenAmount > 0.0f) {
			constexpr float kNormFactor = 1.0f / static_cast<float>(ONE_Q31);
			constexpr float kBoost = 256.0f;
			float x = std::clamp(shapedFL * kNormFactor * kBoost, 0.0f, 1.0f);
			constexpr float kEvenGain = 0.85f;
			float ratio = 1.0f - evenAmount * (1.0f - x);
			shapedL = static_cast<q31_t>(ratio * shapedFL * kEvenGain);
		}
		else if (shapedFL < 0.0f && evenDryBlend > 0.0f) {
			float absShapedF = -shapedFL;
			float absDrivenF = std::abs(drivenFL);
			float cappedDrivenAbs = std::min(absDrivenF, absShapedF * 2.0f);
			float blendedAbs = absShapedF * (1.0f - evenDryBlend) + cappedDrivenAbs * evenDryBlend;
			shapedL = static_cast<q31_t>(-blendedAbs);
		}

		// Right channel
		float shapedFR = static_cast<float>(shapedR);
		float drivenFR = static_cast<float>(drivenR);
		if (shapedFR > 0.0f && evenAmount > 0.0f) {
			constexpr float kNormFactor = 1.0f / static_cast<float>(ONE_Q31);
			constexpr float kBoost = 256.0f;
			float x = std::clamp(shapedFR * kNormFactor * kBoost, 0.0f, 1.0f);
			constexpr float kEvenGain = 0.85f;
			float ratio = 1.0f - evenAmount * (1.0f - x);
			shapedR = static_cast<q31_t>(ratio * shapedFR * kEvenGain);
		}
		else if (shapedFR < 0.0f && evenDryBlend > 0.0f) {
			float absShapedF = -shapedFR;
			float absDrivenF = std::abs(drivenFR);
			float cappedDrivenAbs = std::min(absDrivenF, absShapedF * 2.0f);
			float blendedAbs = absShapedF * (1.0f - evenDryBlend) + cappedDrivenAbs * evenDryBlend;
			shapedR = static_cast<q31_t>(-blendedAbs);
		}
	}

	// === Rect + Rect2 (Twist Zone 2) - process both channels ===
	if (rectAmount > 0.0f || rect2Amount > 0.0f) {
		// Left channel
		float shapedFL = static_cast<float>(shapedL);
		float absValL = std::abs(shapedFL);
		float resultL = shapedFL;
		if (rectAmount > 0.0f) {
			resultL = resultL + (absValL - shapedFL) * rectAmount;
		}
		if (rect2Amount > 0.0f && resultL > 0.0f) {
			constexpr float kNormFactor = 1.0f / static_cast<float>(ONE_Q31);
			constexpr float kBoost = 256.0f;
			float x = std::clamp(resultL * kNormFactor * kBoost, 0.0f, 1.0f);
			constexpr float kHalfPi = 1.5707963267949f;
			float sineX = fastSinHalfPi(x * kHalfPi);
			float sineRatio = (x > 0.01f) ? sineX / x : 1.0f;
			float sineResult = resultL * sineRatio;
			resultL = resultL * (1.0f - rect2Amount) + sineResult * rect2Amount;
		}
		shapedL = static_cast<q31_t>(resultL);

		// Right channel
		float shapedFR = static_cast<float>(shapedR);
		float absValR = std::abs(shapedFR);
		float resultR = shapedFR;
		if (rectAmount > 0.0f) {
			resultR = resultR + (absValR - shapedFR) * rectAmount;
		}
		if (rect2Amount > 0.0f && resultR > 0.0f) {
			constexpr float kNormFactor = 1.0f / static_cast<float>(ONE_Q31);
			constexpr float kBoost = 256.0f;
			float x = std::clamp(resultR * kNormFactor * kBoost, 0.0f, 1.0f);
			constexpr float kHalfPi = 1.5707963267949f;
			float sineX = fastSinHalfPi(x * kHalfPi);
			float sineRatio = (x > 0.01f) ? sineX / x : 1.0f;
			float sineResult = resultR * sineRatio;
			resultR = resultR * (1.0f - rect2Amount) + sineResult * rect2Amount;
		}
		shapedR = static_cast<q31_t>(resultR);
	}

	return {shapedL, shapedR};
}

void sineShapeBuffer(std::span<q31_t> buffer, q31_t drive, q31_t* smoothedDrive, SineShaperVoiceState* voiceState,
                     q31_t harmonic, q31_t mix, const SineShaperTwistParams& twist, SineTableShaperParams* params,
                     bool* wasBypassed, q31_t filterGain, bool hasFilters) {
	// Early out - if mix is 0, do nothing (important CPU optimization)
	if (mix <= 0 || buffer.empty()) {
		if (wasBypassed) {
			*wasBypassed = true;
		}
		return;
	}

	// Per-sample drive interpolation for zipper-free parameter changes
	auto driveCtx = prepareSmoothing(*smoothedDrive, drive, buffer.size());
	// Snap to target on first use (INT32_MIN sentinel means uninitialized)
	if (driveCtx.current == INT32_MIN) {
		driveCtx.current = driveCtx.target;
		*smoothedDrive = driveCtx.target;
	}
	// Write-back happens at end of function after per-sample IIR updates

	// Buffer-rate harmonic smoothing commented out - now relying only on per-sample IIR
	// for coefficients. This eliminates the "chasing" behavior where two IIR stages fight.
	// q31_t smoothedHarmonic = smoothParam(&params->smoothedHarmonic, harmonic);
	q31_t smoothedHarmonic = harmonic; // Use raw harmonic, let coefficient IIR do smoothing

	// Mark as no longer bypassed if transitioning
	if (wasBypassed && *wasBypassed) {
		*wasBypassed = false;
	}

	// Determine zone from raw harmonic (coefficients will smooth the transition)
	auto zoneInfo = computeZoneQ31(smoothedHarmonic, kNumHarmonicZones);
	int32_t zone = zoneInfo.index;
	float posInZone = zoneInfo.position;
	// Poly (zone 7) uses different crossfade, all others use Chebyshev-style
	bool useChebyshevCrossfade = (zone != 7);

	// Benchmarking with zone tag and sub-aggregations (mono version)
	// Single flag check to minimize cache pollution (otherwise 8 voices × 6 checks = constant overhead)
#if ENABLE_FX_BENCHMARK
	const bool doBench = Debug::FxBenchGlobal::sampleThisBuffer;
	static Debug::FxBenchmark benchTotal("sine_shaper", "total");
	static Debug::FxBenchmark benchSetup("sine_shaper", "setup");
	static Debug::FxBenchmark benchLoop("sine_shaper", "loop");
	if (doBench) {
		benchTotal.setTag(1, kSineShaperZoneNames[zone]);
		benchSetup.setTag(1, kSineShaperZoneNames[zone]);
		benchLoop.setTag(1, kSineShaperZoneNames[zone]);
		benchTotal.start();
		benchSetup.start();
	}
#endif

	// Hoist zone weight calculation with per-sample coefficient interpolation
	// Per-sample smoothing eliminates zipper noise from triangle weight changes
	// Zone boundary glitches are acceptable - smoothing is per-zone, not cross-zone
	// All zones use same smoothing path and repurpose ShaperWeights:
	//   Zone 7 (Poly): c1=cascadeBlend, c3=selfMulBlend
	//   Zone 0/1 (Chebyshev): c1-c9 = Chebyshev coefficients
	//   Zones 2-6 (FM): c1=inputGainMult, c3=w0, c5=w1, c7=w2, c9=w3
	ShaperWeights shaperWeights{0, 0, 0, 0, 0};
	ShaperWeights targetWeights{0, 0, 0, 0, 0};

	if (zone == 7) {
		// Zone 7 (Poly): c1=cascadeBlend, c3=selfMulBlend
		targetWeights.c1 = posInZone;
		targetWeights.c3 = std::fmod(twist.phaseHarmonic + 0.5f + 1.0f, 1.0f);
		// Debug: throttled logging for zone 7 - also show what blend weights would be
	}
	else if (zone == 0 || zone == 1) {
		float pos = std::fmod(posInZone + twist.phaseHarmonic + 1.0f, 1.0f);
		targetWeights = computeShaperWeightsFromPos(pos, twist.phaseHarmonicFreqMod);
	}
	else if (zone >= 2 && zone <= 6) {
		// Zones 2-6 (FM): c1=inputGainMult, c3=w0, c5=w1, c7=w2, c9=w3
		// All use same weight structure, differ only in DSP applied in sineShapeCore
		float pos = std::fmod(posInZone + twist.phaseHarmonic + 1.0f, 1.0f);
		BlendWeights4 w = computeBlendWeights4(pos, twist.phaseHarmonicFreqMod);
		targetWeights.c1 = 1.0f + pos * 3.0f; // inputGainMult 1.0-4.0
		targetWeights.c3 = w.w0;
		targetWeights.c5 = w.w1;
		targetWeights.c7 = w.w2;
		targetWeights.c9 = w.w3;
	}

	// Single smoothing path for all zones
	FloatSmoothingContext c1Ctx = prepareSmoothingFloat(params->smoothedC1, targetWeights.c1, buffer.size());
	FloatSmoothingContext c3Ctx = prepareSmoothingFloat(params->smoothedC3L, targetWeights.c3, buffer.size());
	FloatSmoothingContext c5Ctx = prepareSmoothingFloat(params->smoothedC5L, targetWeights.c5, buffer.size());
	FloatSmoothingContext c7Ctx = prepareSmoothingFloat(params->smoothedC7L, targetWeights.c7, buffer.size());
	FloatSmoothingContext c9Ctx = prepareSmoothingFloat(params->smoothedC9L, targetWeights.c9, buffer.size());

	shaperWeights = ShaperWeights{c1Ctx.current, c3Ctx.current, c5Ctx.current, c7Ctx.current, c9Ctx.current};

	// Debug: log smoothed weights for zone 7
	// Check if all coefficients have converged - if so, skip per-sample smoothing entirely
	// This saves ~1000 cycles/buffer when harmonic/twist params are static (common case)
	bool coeffsConverged =
	    isConverged(c1Ctx) && isConverged(c3Ctx) && isConverged(c5Ctx) && isConverged(c7Ctx) && isConverged(c9Ctx);

	// Local copy of state for efficient per-sample update
	// If voiceState is null, use local zeros (no persistent state across buffers)
	q31_t dcState = voiceState ? voiceState->dcBlockerL : 0;
	q31_t fbState = voiceState ? voiceState->feedbackL : 0;

	// Scale feedback amount for moderate self-oscillation range
	// At max (1.0), feedback is ~0.9 of output (just below self-oscillation)
	// In Zone 7 (Poly), feedback is inversely proportional to harmonic position (more cascade = less feedback)
	// Also reduce feedback by up to 10% as drive increases (tames high-drive feedback)
	constexpr float kFeedbackScale = 0.25f;
	float fbScale = kFeedbackScale;
	if (zoneInfo.index == kNumHarmonicZones - 1) {
		fbScale *= (1.0f - zoneInfo.position); // 100% at start, 0% at end of Zone 7
		fbScale *= 0.707f;                     // -3dB for Zone 7 (poly is hotter)
	}
	// Drive-based reduction: up to 10% at max drive (tames high-drive feedback)
	float driveNorm = static_cast<float>(drive) / static_cast<float>(ONE_Q31);
	fbScale *= (1.0f - 0.1f * driveNorm);
	q31_t fbAmount = static_cast<q31_t>(twist.feedbackAmount * fbScale * static_cast<float>(ONE_Q31));

	// Compute gain adjustment for subtractive mode (matches table shaper pattern)
	// filterGain=0 means FM mode (no adjustment needed)
	// filterGain>0 means subtractive: compensate for resonance-induced level changes
	// At neutral filterGain (2^28), boostGain = 1.0 (no adjustment)
	// High resonance (low filterGain) → boost; low resonance (high filterGain) → attenuate
	bool needsGainAdjust = (filterGain > 0) && hasFilters;
	float boostGain = 1.0f;
	float attenGain = 1.0f;

	if (needsGainAdjust) {
		boostGain = static_cast<float>(kSineShaperNeutralFilterGain) / static_cast<float>(filterGain);
		attenGain = 1.0f / boostGain;
		// Skip per-sample multiply if effectively unity
		needsGainAdjust = (filterGain != kSineShaperNeutralFilterGain);
	}

#if ENABLE_FX_BENCHMARK
	if (doBench) {
		benchSetup.stop();
		benchLoop.start();
	}
#endif

	// Compute target driveGain and initialize current from voice state (for buffer continuity)
	float targetDriveGain = computeDriveGain(driveCtx.target);
	float currentDriveGain;
	if (voiceState && voiceState->smoothedDriveGain >= 0.0f) {
		// Resume from where we left off last buffer
		currentDriveGain = voiceState->smoothedDriveGain;
	}
	else {
		// First use or no voice state: snap to current (no smoothing artifact)
		currentDriveGain = computeDriveGain(driveCtx.current);
	}

	// Sample counter for strided coefficient updates (only used when not converged)
	int32_t strideCounter = 0;

	for (auto& sample : buffer) {
		// Apply feedback to input (before shaping)
		// Pre-boost for subtractive mode to normalize operating point with FM
		q31_t inputWithFb = sample;
		if (needsGainAdjust) {
			// Clamp to prevent overflow when boostGain > 1 and sample is near INT32_MAX
			inputWithFb =
			    static_cast<q31_t>(std::clamp(static_cast<float>(sample) * boostGain, -2147483648.0f, 2147483647.0f));
		}
		if (fbAmount > 0) {
			// Apply feedback directly (HPF on tap prevents fundamental buildup)
			q31_t fb = signed_saturate<22>(multiply_32x32_rshift32(fbState, fbAmount) << 1);
			inputWithFb = add_saturate(inputWithFb, fb);
			// Clamp combined signal to q31 range
			inputWithFb = std::clamp(inputWithFb, -ONE_Q31, ONE_Q31);
		}

		// Get shaped (wet) signal with all modifiers (drive + weights interpolated per-sample)
		// All zones use shaperWeights (repurposed per zone, see comment above)
		q31_t shaped = sineShapeCore(inputWithFb, currentDriveGain, zone, &shaperWeights, twist.evenAmount,
		                             twist.evenDryBlend, twist.rectAmount, twist.rect2Amount);
		// Per-sample IIR update for driveGain (only float smoothing needed for DSP)
		currentDriveGain += (targetDriveGain - currentDriveGain) * kPerSampleAlpha;

		// Update weights: skip entirely when converged, otherwise stride every 4 samples
		// Saves ~1000 cycles/buffer when params are static (common case)
		if (!coeffsConverged) {
			if (++strideCounter >= kSmoothingStride) {
				strideCounter = 0;
				// Strided update (alpha already scaled 4x via kStridedAlpha)
				c1Ctx.current += (c1Ctx.target - c1Ctx.current) * kStridedAlpha;
				c3Ctx.current += (c3Ctx.target - c3Ctx.current) * kStridedAlpha;
				c5Ctx.current += (c5Ctx.target - c5Ctx.current) * kStridedAlpha;
				c7Ctx.current += (c7Ctx.target - c7Ctx.current) * kStridedAlpha;
				c9Ctx.current += (c9Ctx.target - c9Ctx.current) * kStridedAlpha;
				shaperWeights.c1 = c1Ctx.current;
				shaperWeights.c3 = c3Ctx.current;
				shaperWeights.c5 = c5Ctx.current;
				shaperWeights.c7 = c7Ctx.current;
				shaperWeights.c9 = c9Ctx.current;
			}
		}

		// 100Hz HPF on wet signal only - removes sub-bass rumble without affecting dry
		dcState += multiply_32x32_rshift32(shaped - dcState, kOutputHpfAlpha) * 2;
		q31_t hpfWet = shaped - dcState;

		// Feedback tap with 1-pole LPF (~4.9kHz) to tame harsh harmonics
		// Uses shifts for efficiency: α=0.5 → fbState = 0.5*old + 0.5*new
		fbState = (fbState + hpfWet) >> 1;

		// Post-attenuate wet signal for subtractive mode (inverse of pre-boost)
		// This normalizes the waveshaper operating point without changing output level
		// Applied AFTER feedback tap so feedback operates at internal waveshaper level
		if (needsGainAdjust) {
			hpfWet = static_cast<q31_t>(static_cast<float>(hpfWet) * attenGain);
		}

		q31_t mixed;
		if (useChebyshevCrossfade) {
			// Chebyshev zones: traditional wet/dry crossfade
			// At mix=0: pure dry, mix=1: pure wet
			q31_t dryCoeff = ONE_Q31 - multiply_32x32_rshift32(mix, ONE_Q31) * 2; // 1 - mix
			q31_t dryPart = multiply_32x32_rshift32(sample, dryCoeff) << 1;
			q31_t wetPart = multiply_32x32_rshift32(hpfWet, mix) << 1;
			mixed = add_saturate(dryPart, wetPart);
		}
		else {
			// Zone 7 (Poly): Traditional wet/dry crossfade
			q31_t invMix = ONE_Q31 - mix;
			q31_t dryPart = multiply_32x32_rshift32(sample, invMix) << 1;
			q31_t wetPart = multiply_32x32_rshift32(hpfWet, mix) << 1;
			mixed = add_saturate(dryPart, wetPart);
		}

		sample = mixed;
	}

#if ENABLE_FX_BENCHMARK
	if (doBench) {
		benchLoop.stop();
		benchTotal.stop();
	}
#endif

	// Write back state if voiceState is not null
	if (voiceState) {
		voiceState->dcBlockerL = dcState;
		voiceState->feedbackL = fbState;
		voiceState->smoothedDriveGain = currentDriveGain; // Persist float smoothing for continuity
	}
	// Write back smoothed params (shared across zones, zone boundary glitches acceptable)
	// Per-sample IIR: write final current, not target (current is where we actually got to)
	params->smoothedC1 = c1Ctx.current;
	params->smoothedC3L = c3Ctx.current;
	params->smoothedC5L = c5Ctx.current;
	params->smoothedC7L = c7Ctx.current;
	params->smoothedC9L = c9Ctx.current;
	*smoothedDrive = driveCtx.target; // q31 state for prepareSmoothing next buffer
}

void sineShapeBuffer(std::span<StereoSample> buffer, q31_t drive, q31_t* smoothedDrive,
                     SineShaperVoiceState* voiceState, q31_t harmonic, q31_t mix, const SineShaperTwistParams& twist,
                     SineTableShaperParams* params, bool* wasBypassed, q31_t filterGain, bool hasFilters) {
	// Early out - if mix is 0, do nothing (important CPU optimization)
	if (mix <= 0 || buffer.empty()) {
		if (wasBypassed) {
			*wasBypassed = true;
		}
		return;
	}

	// Per-sample drive interpolation for zipper-free parameter changes
	auto driveCtx = prepareSmoothing(*smoothedDrive, drive, buffer.size());
	// Snap to target on first use (INT32_MIN sentinel means uninitialized)
	if (driveCtx.current == INT32_MIN) {
		driveCtx.current = driveCtx.target;
		*smoothedDrive = driveCtx.target;
	}
	// Write-back happens at end of function after per-sample IIR updates

	// Buffer-rate harmonic smoothing commented out - now relying only on per-sample IIR
	// for coefficients. This eliminates the "chasing" behavior where two IIR stages fight.
	// q31_t smoothedHarmonic = smoothParam(&params->smoothedHarmonic, harmonic);
	q31_t smoothedHarmonic = harmonic; // Use raw harmonic, let coefficient IIR do smoothing

	// Mark as no longer bypassed if transitioning
	if (wasBypassed && *wasBypassed) {
		*wasBypassed = false;
	}

	// Determine zone from raw harmonic (coefficients will smooth the transition)
	auto zoneInfo = computeZoneQ31(smoothedHarmonic, kNumHarmonicZones);
	int32_t zone = zoneInfo.index;
	float posInZone = zoneInfo.position;
	// Poly (zone 7) uses different crossfade, all others use Chebyshev-style
	bool useChebyshevCrossfade = (zone != 7);

	// Benchmarking with zone tag and sub-aggregations (stereo version)
	// Single flag check to minimize cache pollution (otherwise 8 voices × 6 checks = constant overhead)
#if ENABLE_FX_BENCHMARK
	const bool doBench = Debug::FxBenchGlobal::sampleThisBuffer;
	static Debug::FxBenchmark benchTotal("sine_shaper", "total");
	static Debug::FxBenchmark benchSetup("sine_shaper", "setup");
	static Debug::FxBenchmark benchLoop("sine_shaper", "loop");
	if (doBench) {
		benchTotal.setTag(1, kSineShaperZoneNames[zone]);
		benchSetup.setTag(1, kSineShaperZoneNames[zone]);
		benchLoop.setTag(1, kSineShaperZoneNames[zone]);
		benchTotal.start();
		benchSetup.start();
	}
#endif

	// Hoist zone weight calculation with per-sample coefficient interpolation
	// Per-sample smoothing eliminates zipper noise from triangle weight changes
	// Zone boundary glitches are acceptable - smoothing is per-zone, not cross-zone
	// All zones use same smoothing path and repurpose ShaperWeights:
	//   Zone 7 (Poly): c1=cascadeBlend, c3=selfMulBlend
	//   Zone 0/1 (Chebyshev): c1-c9 = Chebyshev coefficients
	//   Zones 2-6 (FM): c1=inputGainMult, c3=w0, c5=w1, c7=w2, c9=w3
	ShaperWeights shaperWeightsL{0, 0, 0, 0, 0};
	ShaperWeights shaperWeightsR{0, 0, 0, 0, 0};
	ShaperWeights targetWeightsL{0, 0, 0, 0, 0};
	ShaperWeights targetWeightsR{0, 0, 0, 0, 0};

	// Read LFO phase early for stereo mode selection
	// Bipolar LFO: positive = position stereo, negative = drive stereo
	// 50% duty cycle shapes the waveform to spend more time near zero (subtler effect)
	// Stereo effect strength follows LFO amplitude directly
	constexpr float kStereoLfoDuty = 0.5f;
	float lfoPhaseForStereo = voiceState ? voiceState->stereoLfoPhase : 0.0f;
	float stereoLfoValue = triangleFloat(lfoPhaseForStereo, kStereoLfoDuty); // [-1, 1]
	bool positionStereoActive = (twist.stereoWidth > 0.0f && stereoLfoValue > 0.0f);

	// Check weight cache - skip expensive computation if params unchanged
	// Force cache miss when position stereo is active (LFO positive, weights differ per-buffer)
	bool cacheHit = !positionStereoActive
	                && (params->cachedZone == zone && params->cachedHarmonic == harmonic
	                    && params->cachedPhaseHarmonic == twist.phaseHarmonic
	                    && params->cachedPhaseHarmonicFreqMod == twist.phaseHarmonicFreqMod
	                    && params->cachedStereoWidth == twist.stereoWidth
	                    && params->cachedStereoPhaseOffset == twist.stereoPhaseOffset
	                    && params->cachedStereoFreqMult == twist.stereoFreqMult);

	if (cacheHit) {
		// Use cached target weights
		targetWeightsL = {params->cachedTargetC1, params->cachedTargetC3L, params->cachedTargetC5L,
		                  params->cachedTargetC7L, params->cachedTargetC9L};
		targetWeightsR = {params->cachedTargetC1, params->cachedTargetC3R, params->cachedTargetC5R,
		                  params->cachedTargetC7R, params->cachedTargetC9R};
	}
	else {
		// Compute stereo position offset for zones that use it
		// Use LFO value when positive (position stereo), otherwise 0 (drive stereo handles negative)
		float stereoMod = positionStereoActive ? stereoLfoValue : 0.0f;

		if (zone == 7) {
			// Zone 7 (Poly): c1=cascadeBlend, c3=selfMulBlend (L/R identical)
			targetWeightsL.c1 = posInZone;
			targetWeightsL.c3 = std::fmod(twist.phaseHarmonic + 0.5f + 1.0f, 1.0f);
			targetWeightsR = targetWeightsL;
		}
		else if (zone == 0 || zone == 1) {
			float pos = std::fmod(posInZone + twist.phaseHarmonic + 1.0f, 1.0f);

			// Position-based stereo: L and R at different positions
			constexpr float kMaxPosOffset = 0.06f;
			float posOffset = twist.stereoWidth * stereoMod * kMaxPosOffset;
			float posL = std::clamp(pos - posOffset, 0.0f, 1.0f);
			float posR = std::clamp(pos + posOffset, 0.0f, 1.0f);

			targetWeightsL = computeShaperWeightsFromPos(posL, twist.phaseHarmonicFreqMod);
			targetWeightsR = computeShaperWeightsFromPos(posR, twist.phaseHarmonicFreqMod);
		}
		else if (zone >= 2 && zone <= 6) {
			// Zones 2-6 (FM): c1=inputGainMult, c3=w0, c5=w1, c7=w2, c9=w3
			// All use same weight structure, differ only in DSP applied in sineShapeCore
			float pos = std::fmod(posInZone + twist.phaseHarmonic + 1.0f, 1.0f);

			// Position-based stereo
			constexpr float kMaxPosOffset = 0.08f;
			float posOffset = twist.stereoWidth * stereoMod * kMaxPosOffset;
			float posL = std::clamp(pos - posOffset, 0.0f, 1.0f);
			float posR = std::clamp(pos + posOffset, 0.0f, 1.0f);

			BlendWeights4 wL = computeBlendWeights4(posL, twist.phaseHarmonicFreqMod);
			BlendWeights4 wR = computeBlendWeights4(posR, twist.phaseHarmonicFreqMod);
			float inputGain = 1.0f + pos * 3.0f;

			targetWeightsL = {inputGain, wL.w0, wL.w1, wL.w2, wL.w3};
			targetWeightsR = {inputGain, wR.w0, wR.w1, wR.w2, wR.w3};
		}

		// Update cache
		params->cachedZone = zone;
		params->cachedHarmonic = harmonic;
		params->cachedPhaseHarmonic = twist.phaseHarmonic;
		params->cachedPhaseHarmonicFreqMod = twist.phaseHarmonicFreqMod;
		params->cachedStereoWidth = twist.stereoWidth;
		params->cachedStereoPhaseOffset = twist.stereoPhaseOffset;
		params->cachedStereoFreqMult = twist.stereoFreqMult;
		params->cachedTargetC1 = targetWeightsL.c1;
		params->cachedTargetC3L = targetWeightsL.c3;
		params->cachedTargetC5L = targetWeightsL.c5;
		params->cachedTargetC7L = targetWeightsL.c7;
		params->cachedTargetC9L = targetWeightsL.c9;
		params->cachedTargetC3R = targetWeightsR.c3;
		params->cachedTargetC5R = targetWeightsR.c5;
		params->cachedTargetC7R = targetWeightsR.c7;
		params->cachedTargetC9R = targetWeightsR.c9;
	}

	// Single smoothing path for all zones (L channel)
	FloatSmoothingContext c1Ctx = prepareSmoothingFloat(params->smoothedC1, targetWeightsL.c1, buffer.size());
	FloatSmoothingContext c3LCtx = prepareSmoothingFloat(params->smoothedC3L, targetWeightsL.c3, buffer.size());
	FloatSmoothingContext c5LCtx = prepareSmoothingFloat(params->smoothedC5L, targetWeightsL.c5, buffer.size());
	FloatSmoothingContext c7LCtx = prepareSmoothingFloat(params->smoothedC7L, targetWeightsL.c7, buffer.size());
	FloatSmoothingContext c9LCtx = prepareSmoothingFloat(params->smoothedC9L, targetWeightsL.c9, buffer.size());

	// R channel (c1 shared)
	FloatSmoothingContext c3RCtx = prepareSmoothingFloat(params->smoothedC3R, targetWeightsR.c3, buffer.size());
	FloatSmoothingContext c5RCtx = prepareSmoothingFloat(params->smoothedC5R, targetWeightsR.c5, buffer.size());
	FloatSmoothingContext c7RCtx = prepareSmoothingFloat(params->smoothedC7R, targetWeightsR.c7, buffer.size());
	FloatSmoothingContext c9RCtx = prepareSmoothingFloat(params->smoothedC9R, targetWeightsR.c9, buffer.size());

	shaperWeightsL = ShaperWeights{c1Ctx.current, c3LCtx.current, c5LCtx.current, c7LCtx.current, c9LCtx.current};
	shaperWeightsR = ShaperWeights{c1Ctx.current, c3RCtx.current, c5RCtx.current, c7RCtx.current, c9RCtx.current};

	// NEON vectorized smoothing for L/R coefficients (more efficient than 8 scalar updates)
	// Use strided alpha (4x) since we only update every kSmoothingStride samples
	// Left: c3L, c5L, c7L, c9L
	NeonSmoothingContext neonCtxL =
	    prepareNeonSmoothing(c3LCtx.current, c5LCtx.current, c7LCtx.current, c9LCtx.current, c3LCtx.target,
	                         c5LCtx.target, c7LCtx.target, c9LCtx.target, kStridedAlpha);
	// Right: c3R, c5R, c7R, c9R
	NeonSmoothingContext neonCtxR =
	    prepareNeonSmoothing(c3RCtx.current, c5RCtx.current, c7RCtx.current, c9RCtx.current, c3RCtx.target,
	                         c5RCtx.target, c7RCtx.target, c9RCtx.target, kStridedAlpha);

	// Check if all coefficients have converged - if so, skip per-sample smoothing entirely
	// This saves ~2000 cycles/buffer when harmonic/twist params are static (common case)
	bool coeffsConverged = isConverged(c1Ctx) && isNeonConverged(neonCtxL) && isNeonConverged(neonCtxR);

	// Stereo LFO: bipolar mode selection
	// Positive LFO → position stereo (L/R at different positions, computed per-buffer in weights)
	// Negative LFO → drive stereo (L/R at different drive gains, applied per-sample)
	// This gives alternating timbral (position) and amplitude (drive) stereo effects
	constexpr float kStereoDriveScale = 0.5f; // Up to 50% drive reduction at full width
	constexpr float kMinLfoHz = 0.05f;        // Slow minimum (~20s period)
	constexpr float kMaxLfoHz = 6.0f;         // Moderate max for subtle stereo motion
	constexpr float kSampleRate = 44100.0f;

	// Compute per-sample LFO phase increment (0 if stereo disabled)
	float lfoPhaseIncrement = 0.0f;
	float lfoPhase = voiceState ? voiceState->stereoLfoPhase : 0.0f;
	float driveStereoScale = 0.0f; // Per-sample drive stereo when LFO negative

	// First-use detection: phi-based initial phase from twist.phaseHarmonic
	// Each twist position gives a unique starting phase, creating per-note variation
	bool isFirstUse = voiceState && voiceState->smoothedDriveGain < 0.0f;
	if (isFirstUse && twist.stereoWidth > 0.0f) {
		// Use phaseHarmonic (which includes twist position * 5.0f + gamma effects) for decorrelation
		lfoPhase = phi::wrapPhase(static_cast<double>(twist.phaseHarmonic) * phi::kPhi);
	}

	if (twist.stereoWidth > 0.0f) {
		float lfoHz = kMinLfoHz + twist.stereoLfoRate * (kMaxLfoHz - kMinLfoHz);
		lfoHz *= twist.stereoFreqMult; // Additional modulation from Width zone
		lfoPhaseIncrement = lfoHz / kSampleRate;
		driveStereoScale = twist.stereoWidth * kStereoDriveScale;
	}

	// Local copy of state for efficient per-sample update
	// If voiceState is null, use local zeros (no persistent state across buffers)
	q31_t dcStateL = voiceState ? voiceState->dcBlockerL : 0;
	q31_t dcStateR = voiceState ? voiceState->dcBlockerR : 0;
	q31_t fbStateL = voiceState ? voiceState->feedbackL : 0;
	q31_t fbStateR = voiceState ? voiceState->feedbackR : 0;

	// Scale feedback amount for moderate self-oscillation range
	// At max (1.0), feedback is ~0.9 of output (just below self-oscillation)
	// In Zone 7 (Poly), feedback is inversely proportional to harmonic position (more cascade = less feedback)
	// Also reduce feedback by up to 10% as drive increases (tames high-drive feedback)
	constexpr float kFeedbackScale = 0.25f;
	float fbScale = kFeedbackScale;
	if (zoneInfo.index == kNumHarmonicZones - 1) {
		fbScale *= (1.0f - zoneInfo.position); // 100% at start, 0% at end of Zone 7
		fbScale *= 0.707f;                     // -3dB for Zone 7 (poly is hotter)
	}
	// Drive-based reduction: -10% at max drive
	float driveNorm = static_cast<float>(drive) / static_cast<float>(ONE_Q31);
	fbScale *= (1.0f - 0.1f * driveNorm);
	q31_t fbAmount = static_cast<q31_t>(twist.feedbackAmount * fbScale * static_cast<float>(ONE_Q31));

	// Compute gain adjustment for subtractive mode (matches table shaper pattern)
	// filterGain=0 means FM mode (no adjustment needed)
	// filterGain>0 means subtractive: compensate for resonance-induced level changes
	// At neutral filterGain (2^28), boostGain = 1.0 (no adjustment)
	// High resonance (low filterGain) → boost; low resonance (high filterGain) → attenuate
	bool needsGainAdjust = (filterGain > 0) && hasFilters;
	float boostGain = 1.0f;
	float attenGain = 1.0f;

	if (needsGainAdjust) {
		boostGain = static_cast<float>(kSineShaperNeutralFilterGain) / static_cast<float>(filterGain);
		attenGain = 1.0f / boostGain;
		// Skip per-sample multiply if effectively unity
		needsGainAdjust = (filterGain != kSineShaperNeutralFilterGain);
	}

#if ENABLE_FX_BENCHMARK
	if (doBench) {
		benchSetup.stop();
		benchLoop.start();
	}
#endif

	// Compute target driveGain and initialize current from voice state (for buffer continuity)
	float targetDriveGain = computeDriveGain(driveCtx.target);
	float currentDriveGain;
	if (voiceState && voiceState->smoothedDriveGain >= 0.0f) {
		// Resume from where we left off last buffer
		currentDriveGain = voiceState->smoothedDriveGain;
	}
	else {
		// First use or no voice state: snap to current (no smoothing artifact)
		currentDriveGain = computeDriveGain(driveCtx.current);
	}

	// Sample counter for strided coefficient updates (only used when not converged)
	int32_t strideCounter = 0;

	for (auto& sample : buffer) {
		// Bipolar stereo LFO with 50% duty cycle:
		// - LFO > 0: position stereo (handled in weights, no drive mod here)
		// - LFO < 0: drive stereo (reduce R drive)
		// - LFO == 0: dead zone (duty cycle returns 0 for ~50% of cycle)
		float lfoValue = triangleFloat(lfoPhase, kStereoLfoDuty);
		float driveGainL = currentDriveGain;
		float driveGainR = currentDriveGain;

		if (lfoValue < 0.0f) {
			// Negative LFO: drive stereo - reduce R drive proportionally
			float driveReduction = (-lfoValue) * driveStereoScale;
			driveGainR = currentDriveGain * (1.0f - driveReduction);
		}
		// When lfoValue == 0: dead zone from duty cycle, no stereo
		// When lfoValue > 0: position stereo (weights differ), no drive mod

		// Advance LFO phase for next sample
		lfoPhase += lfoPhaseIncrement;
		if (lfoPhase >= 1.0f) {
			lfoPhase -= 1.0f;
		}

		// Apply feedback to input (before shaping)
		// Pre-boost for subtractive mode to normalize operating point with FM
		q31_t inputL = sample.l;
		q31_t inputR = sample.r;
		if (needsGainAdjust) {
			// Clamp to prevent overflow when boostGain > 1 and sample is near INT32_MAX
			inputL =
			    static_cast<q31_t>(std::clamp(static_cast<float>(sample.l) * boostGain, -2147483648.0f, 2147483647.0f));
			inputR =
			    static_cast<q31_t>(std::clamp(static_cast<float>(sample.r) * boostGain, -2147483648.0f, 2147483647.0f));
		}
		if (fbAmount > 0) {
			// Apply feedback directly (HPF on tap prevents fundamental buildup)
			q31_t fbL = signed_saturate<22>(multiply_32x32_rshift32(fbStateL, fbAmount) << 1);
			q31_t fbR = signed_saturate<22>(multiply_32x32_rshift32(fbStateR, fbAmount) << 1);
			inputL = add_saturate(inputL, fbL);
			inputR = add_saturate(inputR, fbR);
			// Clamp combined signal to q31 range
			inputL = std::clamp(inputL, -ONE_Q31, ONE_Q31);
			inputR = std::clamp(inputR, -ONE_Q31, ONE_Q31);
		}

		// Process both channels together (single zone branch, more efficient)
		StereoShaped shaped =
		    sineShapeCoreStereo(inputL, inputR, driveGainL, driveGainR, zone, &shaperWeightsL, &shaperWeightsR,
		                        twist.evenAmount, twist.evenDryBlend, twist.rectAmount, twist.rect2Amount);
		q31_t shapedL = shaped.l;
		q31_t shapedR = shaped.r;

		// Per-sample IIR update for driveGain (only float smoothing needed for DSP)
		currentDriveGain += (targetDriveGain - currentDriveGain) * kPerSampleAlpha;

		// Update weights: skip entirely when converged, otherwise stride every 4 samples
		// Saves ~2000 cycles/buffer when params are static (common case)
		if (!coeffsConverged) {
			if (++strideCounter >= kSmoothingStride) {
				strideCounter = 0;
				// Strided update: c1 scalar, L/R vectorized via NEON (alpha already scaled 4x)
				c1Ctx.current += (c1Ctx.target - c1Ctx.current) * kStridedAlpha;
				updateNeonSmoothing(neonCtxL);
				updateNeonSmoothing(neonCtxR);

				// Extract smoothed values to weights
				shaperWeightsL.c1 = c1Ctx.current;
				shaperWeightsL.c3 = getNeonLane0(neonCtxL.current);
				shaperWeightsL.c5 = getNeonLane1(neonCtxL.current);
				shaperWeightsL.c7 = getNeonLane2(neonCtxL.current);
				shaperWeightsL.c9 = getNeonLane3(neonCtxL.current);
				shaperWeightsR.c1 = c1Ctx.current;
				shaperWeightsR.c3 = getNeonLane0(neonCtxR.current);
				shaperWeightsR.c5 = getNeonLane1(neonCtxR.current);
				shaperWeightsR.c7 = getNeonLane2(neonCtxR.current);
				shaperWeightsR.c9 = getNeonLane3(neonCtxR.current);
			}
		}

		// 100Hz HPF on wet signal only - removes sub-bass rumble without affecting dry
		dcStateL += multiply_32x32_rshift32(shapedL - dcStateL, kOutputHpfAlpha) * 2;
		dcStateR += multiply_32x32_rshift32(shapedR - dcStateR, kOutputHpfAlpha) * 2;
		q31_t hpfWetL = shapedL - dcStateL;
		q31_t hpfWetR = shapedR - dcStateR;

		// Feedback tap with 1-pole LPF (~4.9kHz) to tame harsh harmonics
		// Uses shifts for efficiency: α=0.5 → fbState = 0.5*old + 0.5*new
		fbStateL = (fbStateL + hpfWetL) >> 1;
		fbStateR = (fbStateR + hpfWetR) >> 1;

		// Post-attenuate wet signal for subtractive mode (inverse of pre-boost)
		// This normalizes the waveshaper operating point without changing output level
		// Applied AFTER feedback tap so feedback operates at internal waveshaper level
		if (needsGainAdjust) {
			hpfWetL = static_cast<q31_t>(static_cast<float>(hpfWetL) * attenGain);
			hpfWetR = static_cast<q31_t>(static_cast<float>(hpfWetR) * attenGain);
		}

		q31_t mixedL, mixedR;
		if (useChebyshevCrossfade) {
			// Chebyshev zones: traditional wet/dry crossfade
			// At mix=0: pure dry, mix=1: pure wet
			q31_t dryCoeff = ONE_Q31 - multiply_32x32_rshift32(mix, ONE_Q31) * 2; // 1 - mix
			mixedL = add_saturate(multiply_32x32_rshift32(sample.l, dryCoeff) << 1,
			                      multiply_32x32_rshift32(hpfWetL, mix) << 1);
			mixedR = add_saturate(multiply_32x32_rshift32(sample.r, dryCoeff) << 1,
			                      multiply_32x32_rshift32(hpfWetR, mix) << 1);
		}
		else {
			// Zone 7 (Poly): Traditional wet/dry crossfade
			q31_t invMix = ONE_Q31 - mix;
			mixedL = add_saturate(multiply_32x32_rshift32(sample.l, invMix) << 1, multiply_32x32_rshift32(hpfWetL, mix)
			                                                                          << 1);
			mixedR = add_saturate(multiply_32x32_rshift32(sample.r, invMix) << 1, multiply_32x32_rshift32(hpfWetR, mix)
			                                                                          << 1);
		}

		sample.l = mixedL;
		sample.r = mixedR;
	}

#if ENABLE_FX_BENCHMARK
	if (doBench) {
		benchLoop.stop();
		benchTotal.stop();
	}
#endif

	// Write back state if voiceState is not null
	if (voiceState) {
		voiceState->dcBlockerL = dcStateL;
		voiceState->dcBlockerR = dcStateR;
		voiceState->feedbackL = fbStateL;
		voiceState->feedbackR = fbStateR;
		voiceState->smoothedDriveGain = currentDriveGain; // Persist float smoothing for continuity
		voiceState->stereoLfoPhase = lfoPhase;            // Persist LFO phase for continuity
	}
	// Write back smoothed params (shared across zones, zone boundary glitches acceptable)
	// Per-sample IIR: write final current, not target (current is where we actually got to)
	params->smoothedC1 = c1Ctx.current;
	params->smoothedC3L = getNeonLane0(neonCtxL.current);
	params->smoothedC5L = getNeonLane1(neonCtxL.current);
	params->smoothedC7L = getNeonLane2(neonCtxL.current);
	params->smoothedC9L = getNeonLane3(neonCtxL.current);
	params->smoothedC3R = getNeonLane0(neonCtxR.current);
	params->smoothedC5R = getNeonLane1(neonCtxR.current);
	params->smoothedC7R = getNeonLane2(neonCtxR.current);
	params->smoothedC9R = getNeonLane3(neonCtxR.current);
	*smoothedDrive = driveCtx.target; // q31 state for prepareSmoothing next buffer
}

void processSineShaper(std::span<StereoSample> buffer, SineTableShaperParams* params, SineShaperVoiceState* state,
                       q31_t driveFinal, q31_t harmonicPreset, q31_t harmonicCables, q31_t twistPreset,
                       q31_t twistCables, q31_t filterGain, bool hasFilters) {
	// Combine preset + cables using zone-aware scaling
	q31_t sineHarmonic = params->harmonic.combinePresetAndCables(harmonicPreset, harmonicCables);
	q31_t sineTwist = params->twist.combinePresetAndCables(twistPreset, twistCables);

	// Compute twist-derived params (coefficient IIR smoothing handles transitions)
	auto twistParams = computeSineShaperTwistParams(sineTwist, params);

	// Process buffer
	sineShapeBuffer(buffer, driveFinal, &params->smoothedDrive, state, sineHarmonic, params->getMixQ31(), twistParams,
	                params, nullptr, filterGain, hasFilters);
}

void processSineShaper(std::span<q31_t> buffer, SineTableShaperParams* params, SineShaperVoiceState* state,
                       q31_t driveFinal, q31_t harmonicPreset, q31_t harmonicCables, q31_t twistPreset,
                       q31_t twistCables, q31_t filterGain, bool hasFilters) {
	q31_t sineHarmonic = params->harmonic.combinePresetAndCables(harmonicPreset, harmonicCables);
	q31_t sineTwist = params->twist.combinePresetAndCables(twistPreset, twistCables);
	// Coefficient IIR smoothing handles transitions (no buffer-rate smoothing needed)
	auto twistParams = computeSineShaperTwistParams(sineTwist, params);
	sineShapeBuffer(buffer, driveFinal, &params->smoothedDrive, state, sineHarmonic, params->getMixQ31(), twistParams,
	                params, nullptr, filterGain, hasFilters);
}

} // namespace deluge::dsp
