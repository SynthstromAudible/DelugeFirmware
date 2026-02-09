/*
 * Copyright © 2025 Owlet Records
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

#include "dsp/phi_morph.hpp"
#include <algorithm>
#include <cmath>

namespace deluge::dsp {

// ============================================================================
// Wavetable Builder
// ============================================================================

PhiMorphWavetable buildPhiMorphWavetable(uint16_t zone, float phaseOffset) {
	PhiMorphWavetable table{};
	table.numSegments = kPhiMorphMaxSegments;

	double phase = static_cast<double>(zone) / 1023.0 + static_cast<double>(phaseOffset);

	auto rawPhaseDeltas = phi::evalTriangleBank<kPhiMorphMaxSegments>(phase, 1.0f, kPhiMorphPhaseBank);
	auto rawAmplitudes = phi::evalTriangleBank<kPhiMorphMovableWaypoints>(phase, 1.0f, kPhiMorphAmplitudeBank);
	auto rawCurvatures = phi::evalTriangleBank<kPhiMorphMaxSegments>(phase, 1.0f, kPhiMorphCurvatureBank);
	auto rawGain = phi::evalTriangleBank<1>(phase, 1.0f, kPhiMorphGainBank);
	auto rawEndpoint = phi::evalTriangleBank<1>(phase, 1.0f, kPhiMorphEndpointBank);
	auto rawAmpOvershoot = phi::evalTriangleBank<1>(phase, 1.0f, kPhiMorphAmpOvershootBank);
	auto rawCurvBoost = phi::evalTriangleBank<1>(phase, 1.0f, kPhiMorphCurvBoostBank);
	auto rawPhaseDistort = phi::evalTriangleBank<1>(phase, 1.0f, kPhiMorphPhaseDistortBank);
	auto rawSineBlend = phi::evalTriangleBank<1>(phase, 1.0f, kPhiMorphSineBlendBank);
	auto rawOddSym = phi::evalTriangleBank<1>(phase, 1.0f, kPhiMorphOddSymBank);
	auto rawWindow = phi::evalTriangleBank<1>(phase, 1.0f, kPhiMorphWindowBank);
	auto rawSlopeMatch = phi::evalTriangleBank<1>(phase, 1.0f, kPhiMorphSlopeMatchBank);
	auto rawPhaseJitter = phi::evalTriangleBank<1>(phase, 1.0f, kPhiMorphPhaseJitterBank);
	auto rawAmpNoise = phi::evalTriangleBank<1>(phase, 1.0f, kPhiMorphAmpNoiseBank);
	auto rawAsymGain = phi::evalTriangleBank<1>(phase, 1.0f, kPhiMorphAsymGainBank);

	// Phase deltas: take abs of bipolar, add floor
	float deltas[kPhiMorphMaxSegments];
	for (int i = 0; i < kPhiMorphMaxSegments; i++) {
		deltas[i] = 0.3f + std::abs(rawPhaseDeltas[i]) * 0.7f;
	}

	// Accumulate phases WITHOUT normalizing — absolute delta magnitude determines
	// how many waypoints fit. Short segments → all 8 fit (complex shape).
	// Long segments → fewer fit, simpler shape with longer return-to-zero tail.
	float phaseAccum = kPhiMorphPhaseMin;
	int activeWaypoints = 0;

	for (int i = 0; i < kPhiMorphMovableWaypoints; i++) {
		float nextPhase = phaseAccum + deltas[i] * kPhiMorphPhaseScale;
		if (nextPhase >= kPhiMorphPhaseMax) {
			break;
		}
		table.phases[i] = nextPhase;
		phaseAccum = nextPhase;
		activeWaypoints = i + 1;
	}

	// Guarantee at least 2 waypoints so normalization always has +/- peaks
	if (activeWaypoints < 2) {
		constexpr float kRange = kPhiMorphPhaseMax - kPhiMorphPhaseMin;
		table.phases[0] = kPhiMorphPhaseMin + kRange * 0.33f;
		table.phases[1] = kPhiMorphPhaseMin + kRange * 0.66f;
		phaseAccum = table.phases[1];
		activeWaypoints = 2;
	}

	// Spread remaining waypoints from truncation to kPhiMorphPhaseMax with zero amplitude.
	// The first inactive waypoint defines the return-to-zero transition.
	if (activeWaypoints < kPhiMorphMovableWaypoints) {
		int remaining = kPhiMorphMovableWaypoints - activeWaypoints;
		float spreadRange = kPhiMorphPhaseMax - phaseAccum;
		for (int i = activeWaypoints; i < kPhiMorphMovableWaypoints; i++) {
			int idx = i - activeWaypoints + 1;
			table.phases[i] = phaseAccum + spreadRange * static_cast<float>(idx) / static_cast<float>(remaining + 1);
		}
	}

	// Peak-symmetric normalization: all waypoints from phi bank, centered and scaled
	// so max → +1, min → -1. Guarantees no DC asymmetry in the fundamental.
	// (activeWaypoints is always >= 2)
	float minAmp = rawAmplitudes[0], maxAmp = rawAmplitudes[0];
	for (int i = 1; i < activeWaypoints; i++) {
		minAmp = std::min(minAmp, rawAmplitudes[i]);
		maxAmp = std::max(maxAmp, rawAmplitudes[i]);
	}
	float ampRange = maxAmp - minAmp;
	if (ampRange > 0.01f) {
		float scale = 2.0f / ampRange;
		float center = (maxAmp + minAmp) * 0.5f;
		for (int i = 0; i < activeWaypoints; i++) {
			table.amplitudes[i] = std::clamp((rawAmplitudes[i] - center) * scale, -1.0f, 1.0f);
		}
	}
	else {
		// Degenerate: all similar values → alternating ±1 for a basic waveform
		for (int i = 0; i < activeWaypoints; i++) {
			table.amplitudes[i] = (i % 2 == 0) ? 1.0f : -1.0f;
		}
	}

	// Inactive waypoints: zero amplitude
	for (int i = activeWaypoints; i < kPhiMorphMovableWaypoints; i++) {
		table.amplitudes[i] = 0.0f;
	}

	// Odd symmetry: blend waypoints toward f(x) = -f(1-x) to eliminate even harmonics.
	// Pairs waypoints from both ends, symmetrizing phases and anti-symmetrizing amplitudes.
	float oddSym = rawOddSym[0];
	if (oddSym > 0.01f && activeWaypoints >= 2) {
		int pairs = activeWaypoints / 2;
		for (int i = 0; i < pairs; i++) {
			int j = activeWaypoints - 1 - i;
			float symPhase = (table.phases[i] + (1.0f - table.phases[j])) * 0.5f;
			table.phases[i] += (symPhase - table.phases[i]) * oddSym;
			table.phases[j] += ((1.0f - symPhase) - table.phases[j]) * oddSym;
			float antiSym = (table.amplitudes[i] - table.amplitudes[j]) * 0.5f;
			table.amplitudes[i] += (antiSym - table.amplitudes[i]) * oddSym;
			table.amplitudes[j] += (-antiSym - table.amplitudes[j]) * oddSym;
		}
		if (activeWaypoints % 2 == 1) {
			int mid = activeWaypoints / 2;
			table.phases[mid] += (0.5f - table.phases[mid]) * oddSym;
			table.amplitudes[mid] *= (1.0f - oddSym);
		}
	}

	// Sine blend: lerp active waypoint amplitudes toward sin(2π × phase).
	// Strengthens fundamental by smoothing toward the purest possible tone.
	constexpr float kTwoPi = 6.283185307f;
	float sineBlend = rawSineBlend[0];
	if (sineBlend > 0.01f) {
		for (int i = 0; i < activeWaypoints; i++) {
			float sineTarget = std::sin(kTwoPi * table.phases[i]);
			table.amplitudes[i] += (sineTarget - table.amplitudes[i]) * sineBlend;
		}
	}

	// Amplitude windowing: raised cosine taper toward cycle extremes.
	// w(p) = 0.5 × (1 + cos(2π × (p - 0.5))): 1.0 at center, 0.0 at edges.
	float windowAmt = rawWindow[0];
	if (windowAmt > 0.01f) {
		for (int i = 0; i < activeWaypoints; i++) {
			float w = 0.5f * (1.0f + std::cos(kTwoPi * (table.phases[i] - 0.5f)));
			table.amplitudes[i] *= (1.0f - windowAmt) + windowAmt * w;
		}
	}

	// Endpoint amplitude: 30% duty cycle (non-zero only for ~30% of zone positions).
	// Unipolar triangle [0,1] → threshold at 0.7 → scale remaining 30% to [0, 0.8].
	float endpointRaw = std::max(0.0f, rawEndpoint[0] - 0.7f) / 0.3f;
	table.endpointAmp = endpointRaw * 0.8f;

	// Morph excitation factors: zone-dependent strengths for crossfade effects
	table.morphAmpOvershoot = rawAmpOvershoot[0] * 1.5f;
	table.morphCurvBoost = rawCurvBoost[0] * 6.0f;
	// Bipolar phase distortion: clamped to ±0.15 to guarantee monotonic warping
	// (derivative 1 + 4*pd*(1-2p) > 0 when |pd| < 0.25)
	table.morphPhaseDistort = std::clamp(rawPhaseDistort[0] * 0.15f, -0.15f, 0.15f);

	// Per-sample render modifiers
	table.phaseJitter = rawPhaseJitter[0];
	table.ampNoise = rawAmpNoise[0] * 0.25f;

	// Energy normalization: equalize perceived loudness across zone positions.
	// Approximate energy as sum of (segWidth × (a0² + a0·a1 + a1²) / 3) per segment.
	// Includes endpoint amplitude at both start (phase=0) and end (phase=1).
	float energy = 0.0f;
	{
		float prevPhase = 0.0f;
		float prevAmp = table.endpointAmp;
		for (int i = 0; i < kPhiMorphMovableWaypoints; i++) {
			float segWidth = table.phases[i] - prevPhase;
			float a0 = prevAmp;
			float a1 = table.amplitudes[i];
			energy += segWidth * (a0 * a0 + a0 * a1 + a1 * a1);
			prevPhase = table.phases[i];
			prevAmp = a1;
		}
		float lastWidth = 1.0f - prevPhase;
		float aEnd = table.endpointAmp;
		energy += lastWidth * (prevAmp * prevAmp + prevAmp * aEnd + aEnd * aEnd);
		energy /= 3.0f;
	}

	// Scale to match triangle wave energy (RMS² = 1/3 for peak ±1 over full cycle)
	constexpr float kTargetEnergy = 0.333f;
	if (energy > 0.001f) {
		float normScale = std::sqrt(kTargetEnergy / energy);
		normScale = std::min(normScale, 2.0f);
		for (int i = 0; i < kPhiMorphMovableWaypoints; i++) {
			table.amplitudes[i] *= normScale;
		}
		table.endpointAmp *= normScale;
	}

	// Gain: strictly positive [1.0, 2.0] — drives normalized waveform into Q31 clipper
	// for flat-top / square-like harmonics. Applied after energy normalization.
	float gainValue = 1.0f + rawGain[0];
	table.gain = gainValue;
	for (int i = 0; i < activeWaypoints; i++) {
		table.amplitudes[i] *= gainValue;
	}
	table.endpointAmp *= gainValue;

	// Asymmetric gain: break waveform symmetry to introduce even harmonics.
	// Negative phi triangle → reduce gain on positive-amplitude waypoints only.
	// Positive phi triangle → symmetric gain boost on all waypoints.
	float asymGain = rawAsymGain[0];
	if (asymGain < -0.01f) {
		float posFactor = 1.0f + asymGain; // [0, 1] when asymGain in [-1, 0]
		for (int i = 0; i < activeWaypoints; i++) {
			if (table.amplitudes[i] > 0.0f) {
				table.amplitudes[i] *= posFactor;
			}
		}
		if (table.endpointAmp > 0.0f) {
			table.endpointAmp *= posFactor;
		}
	}
	else if (asymGain > 0.01f) {
		float allFactor = 1.0f + asymGain * 0.5f; // [1.0, 1.5]
		for (int i = 0; i < activeWaypoints; i++) {
			table.amplitudes[i] *= allFactor;
		}
		table.endpointAmp *= allFactor;
	}

	// Curvatures: blend between phi-triangle curvatures and C1 slope-matched curvatures.
	// C1 matching uses Catmull-Rom tangents at waypoint boundaries to compute curvatures
	// that minimize slope discontinuities, producing smoother waveforms.
	float slopeMatch = rawSlopeMatch[0];
	{
		// Build boundary arrays (same layout as buildSegmentsFromWaypoints)
		float bPhase[kPhiMorphMaxSegments + 1];
		float bAmp[kPhiMorphMaxSegments + 1];
		bPhase[0] = 0.0f;
		bAmp[0] = table.endpointAmp;
		for (int i = 0; i < kPhiMorphMovableWaypoints; i++) {
			bPhase[i + 1] = table.phases[i];
			bAmp[i + 1] = table.amplitudes[i];
		}
		bPhase[kPhiMorphMaxSegments] = 1.0f;
		bAmp[kPhiMorphMaxSegments] = table.endpointAmp;

		for (int i = 0; i < kPhiMorphMaxSegments; i++) {
			float w = bPhase[i + 1] - bPhase[i];
			float ampDelta = bAmp[i + 1] - bAmp[i];
			float linearSlope = (w > 0.001f) ? ampDelta / w : 0.0f;

			// Catmull-Rom tangent at entry boundary i
			float tEntry;
			if (i == 0) {
				tEntry = linearSlope;
			}
			else {
				float dp = bPhase[i + 1] - bPhase[i - 1];
				tEntry = (dp > 0.001f) ? (bAmp[i + 1] - bAmp[i - 1]) / dp : linearSlope;
			}

			// Catmull-Rom tangent at exit boundary i+1
			float tExit;
			if (i >= kPhiMorphMaxSegments - 1) {
				tExit = linearSlope;
			}
			else {
				float dp = bPhase[i + 2] - bPhase[i];
				tExit = (dp > 0.001f) ? (bAmp[i + 2] - bAmp[i]) / dp : linearSlope;
			}

			// Natural curvature from averaged entry/exit slope matching.
			// In float amp units: curvature contribution = stored_curv * 2 * frac*(1-frac)*4
			// So stored_curv = naturalCurv_amp / 2.
			float naturalCurvAmp = (tEntry - tExit) * w / 4.0f;
			float naturalCurv = std::clamp(naturalCurvAmp * 0.5f, -1.0f, 1.0f);

			float phiCurv = (i <= activeWaypoints) ? rawCurvatures[i] : 0.0f;
			table.curvatures[i] = phiCurv * (1.0f - slopeMatch) + naturalCurv * slopeMatch;
		}
	}

	buildSegmentsFromWaypoints(table.phases, table.amplitudes, table.curvatures, table);

	return table;
}

// ============================================================================
// Segment Builder
// ============================================================================

void buildSegmentsFromWaypoints(const float* phases, const float* amplitudes, const float* curvatures,
                                PhiMorphWavetable& out) {
	constexpr float kPhaseToU32 = 4294967295.0f;
	constexpr float kQ31Max = 2147483647.0f;

	// 9 points: (0,endpoint), P1..P7, (1,endpoint) — endpoints from phi triangle
	float segStartPhase[kPhiMorphMaxSegments + 1];
	float segStartAmp[kPhiMorphMaxSegments + 1];

	segStartPhase[0] = 0.0f;
	segStartAmp[0] = out.endpointAmp;
	for (int i = 0; i < kPhiMorphMovableWaypoints; i++) {
		segStartPhase[i + 1] = phases[i];
		segStartAmp[i + 1] = amplitudes[i]; // Already gain-scaled, may exceed [-1,1]
	}
	segStartPhase[kPhiMorphMaxSegments] = 1.0f;
	segStartAmp[kPhiMorphMaxSegments] = out.endpointAmp;

	for (int i = 0; i < kPhiMorphMaxSegments; i++) {
		PhiMorphSegment& seg = out.segments[i];

		float pStart = segStartPhase[i];
		float pEnd = segStartPhase[i + 1];
		float aStart = segStartAmp[i];
		float aEnd = segStartAmp[i + 1];

		seg.startPhase = static_cast<uint32_t>(pStart * kPhaseToU32);
		seg.endPhase = static_cast<uint32_t>(pEnd * kPhaseToU32);

		// Scale to reference amplitude level, clamp at Q31 max for flat-top squares
		seg.startAmp = static_cast<q31_t>(std::clamp(aStart * kPhiMorphRefAmplitude, -kQ31Max, kQ31Max));
		q31_t endAmp = static_cast<q31_t>(std::clamp(aEnd * kPhiMorphRefAmplitude, -kQ31Max, kQ31Max));

		// Precompute float delta and reciprocal width for per-sample interpolation
		seg.ampDeltaF = static_cast<float>(endAmp) - static_cast<float>(seg.startAmp);

		uint32_t segWidth = seg.endPhase - seg.startPhase;
		seg.invWidth = (segWidth > 0) ? (1.0f / static_cast<float>(segWidth)) : 0.0f;

		// Curvature as float for branch-free per-sample evaluation
		float width = pEnd - pStart;
		if (width > 0.001f) {
			float curv = std::clamp(curvatures[i] * 2.0f, -2.0f, 2.0f);
			seg.curvatureF = curv * (kQ31Max / 2.0f);
		}
		else {
			seg.curvatureF = 0.0f;
		}
	}

	out.numSegments = kPhiMorphMaxSegments;
}

// ============================================================================
// Direct Waveform Evaluation
// ============================================================================

q31_t evaluateWaveformDirect(const PhiMorphWavetable& table, uint32_t phase) {
	// Find segment containing this phase
	int seg = kPhiMorphMaxSegments - 1;
	for (int i = 0; i < kPhiMorphMaxSegments; i++) {
		if (phase < table.segments[i].endPhase) {
			seg = i;
			break;
		}
	}

	const PhiMorphSegment& s = table.segments[seg];
	uint32_t offset = phase - s.startPhase;
	float frac = static_cast<float>(offset) * s.invWidth;

	// Linear interpolation from precomputed float delta
	q31_t value = s.startAmp + static_cast<q31_t>(s.ampDeltaF * frac);

	// Quadratic curvature (branch-free: curvatureF is 0.0 for straight segments)
	float quadTerm = frac * (1.0f - frac) * 4.0f;
	q31_t curvContrib = static_cast<q31_t>(s.curvatureF * quadTerm);
	value = add_saturate(value, curvContrib);

	return value;
}

// ============================================================================
// Main Render Function
//
// Benchmark at daad4d5c (mono, single voice, no osc sync):
//   phi_morph render: median 1,048 cycles
//   wavetable render: median 2,173 cycles
// ============================================================================

void renderPhiMorph(PhiMorphCache& cache, int32_t* bufferStart, int32_t* bufferEnd, int32_t numSamples,
                    uint32_t phaseIncrement, uint32_t* startPhase, uint32_t retriggerPhase, int32_t amplitude,
                    int32_t amplitudeIncrement, bool applyAmplitude, q31_t crossfade, uint32_t pulseWidth) {

#if ENABLE_FX_BENCHMARK
	FX_BENCH_DECLARE(bench_render, "phi_morph", "render");
	FX_BENCH_START(bench_render);
#endif

	uint32_t phase = *startPhase;

	// Build crossfaded effective wavetable — cached across unison voices.
	// Only rebuilds when crossfade changes (once per buffer, not per unison voice).
	if (crossfade != cache.prevCrossfade) {
		float cf = static_cast<float>(crossfade) / 2147483648.0f + 0.5f;
		cf = std::clamp(cf, 0.0f, 1.0f);
		float cfInv = 1.0f - cf;

		// Morph excitation: peaks at crossfade center (cf=0.5), zero at endpoints.
		// Creates "virtual intermediate frames" — waveform shapes that exist only during morphing.
		// Strengths derived from zone B's phi triangles (morph target defines transition character).
		float morphExcite = cf * cfInv * 4.0f; // 0 at endpoints, 1.0 at center
		float ampOvershoot = cache.bankB.morphAmpOvershoot;
		float curvBoost = cache.bankB.morphCurvBoost;
		float pd = cache.bankB.morphPhaseDistort * morphExcite;

		float effPhases[kPhiMorphMovableWaypoints];
		float effAmplitudes[kPhiMorphMovableWaypoints];
		float effCurvatures[kPhiMorphMaxSegments];

		for (int i = 0; i < kPhiMorphMovableWaypoints; i++) {
			float p = cfInv * cache.bankA.phases[i] + cf * cache.bankB.phases[i];
			// Quadratic phase distortion: compresses/stretches cycle halves during morph
			effPhases[i] = p + pd * p * (1.0f - p) * 4.0f;
			float ampLerp = cfInv * cache.bankA.amplitudes[i] + cf * cache.bankB.amplitudes[i];
			// Amplitude overshoot: push beyond both banks at midpoint, hitting clipper
			float ampDelta = cache.bankB.amplitudes[i] - cache.bankA.amplitudes[i];
			effAmplitudes[i] = ampLerp + morphExcite * ampDelta * ampOvershoot;
		}
		for (int i = 0; i < kPhiMorphMaxSegments; i++) {
			float curvLerp = cfInv * cache.bankA.curvatures[i] + cf * cache.bankB.curvatures[i];
			// Curvature boost: amplify nonlinearity at midpoint for richer harmonics
			effCurvatures[i] = curvLerp * (1.0f + morphExcite * curvBoost);
		}
		cache.effective = PhiMorphWavetable{};
		cache.effective.endpointAmp = cfInv * cache.bankA.endpointAmp + cf * cache.bankB.endpointAmp;
		cache.effective.phaseJitter = cfInv * cache.bankA.phaseJitter + cf * cache.bankB.phaseJitter;
		cache.effective.ampNoise = cfInv * cache.bankA.ampNoise + cf * cache.bankB.ampNoise;
		buildSegmentsFromWaypoints(effPhases, effAmplitudes, effCurvatures, cache.effective);
		cache.effective.numSegments = kPhiMorphMaxSegments;
		cache.prevCrossfade = crossfade;
	}
	const PhiMorphWavetable& effectiveTable = cache.effective;

	// Advance phase to account for the full buffer
	uint32_t phaseAtEnd = phase + phaseIncrement * static_cast<uint32_t>(numSamples);

	int32_t* thisSample = bufferStart;

	// Double amplitude to match triangle oscillator convention:
	// Triangle outputs at full Q31 range with amplitude <<= 1
	amplitude <<= 1;
	amplitudeIncrement <<= 1;

	// Find initial segment for first sample's phase
	uint32_t evalPhase = phase + phaseIncrement + retriggerPhase;
	const PhiMorphSegment* segs = effectiveTable.segments;
	int segIdx = kPhiMorphMaxSegments - 1;
	for (int i = 0; i < kPhiMorphMaxSegments; i++) {
		if (evalPhase < segs[i].endPhase) {
			segIdx = i;
			break;
		}
	}

	// Pulse width deadzone: phase beyond phaseWidth outputs zero
	const uint32_t phaseWidth = pulseWidth ? (0xFFFFFFFF - (pulseWidth << 1)) : 0xFFFFFFFF;

	// Per-sample modifiers: phase jitter and amplitude-dependent noise
	// Phase jitter: [0,1] → max ~5% of phase cycle
	int32_t jitterRange = static_cast<int32_t>(effectiveTable.phaseJitter * 0.02f * 4294967295.0f);
	// Amp noise: [0, 0.5] → Q31 multiplier
	q31_t ampNoiseQ31 = static_cast<q31_t>(effectiveTable.ampNoise * 2147483647.0f);
	// LCG noise state, seeded from current phase
	uint32_t noiseState = phase ^ 0x5DEECE66Du;

	if (applyAmplitude) {
		for (int32_t n = 0; n < numSamples; n++) {
			phase += phaseIncrement;
			evalPhase = phase + retriggerPhase;

			amplitude += amplitudeIncrement;

			// Phase jitter: add noise to evalPhase for analog drift
			noiseState = noiseState * 1664525u + 1013904223u;
			evalPhase +=
			    static_cast<uint32_t>(multiply_32x32_rshift32(static_cast<int32_t>(noiseState), jitterRange) << 1);

			if (evalPhase > phaseWidth) {
				thisSample++;
				continue;
			}

			// Handle phase wrap-around: when phase overflows uint32, evalPhase jumps
			// from near UINT32_MAX to near 0, but segIdx is stuck at a late segment.
			// Re-scan from segment 0 if evalPhase is before current segment's start.
			if (evalPhase < segs[segIdx].startPhase) {
				segIdx = 0;
			}
			while (segIdx < kPhiMorphMaxSegments - 1 && evalPhase >= segs[segIdx].endPhase) {
				segIdx++;
			}

			const PhiMorphSegment& s = segs[segIdx];
			float frac = static_cast<float>(evalPhase - s.startPhase) * s.invWidth;

			q31_t waveform = s.startAmp + static_cast<q31_t>(s.ampDeltaF * frac);
			float quadTerm = frac * (1.0f - frac) * 4.0f;
			waveform = add_saturate(waveform, static_cast<q31_t>(s.curvatureF * quadTerm));

			// Amplitude-dependent noise: grit scaled by |waveform|
			noiseState = noiseState * 1664525u + 1013904223u;
			q31_t noiseContrib =
			    multiply_32x32_rshift32(multiply_32x32_rshift32(static_cast<int32_t>(noiseState), ampNoiseQ31),
			                            std::abs(waveform))
			    << 2;
			waveform = add_saturate(waveform, noiseContrib);

			*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, waveform, amplitude);
			thisSample++;
		}
	}
	else {
		for (int32_t n = 0; n < numSamples; n++) {
			phase += phaseIncrement;
			evalPhase = phase + retriggerPhase;

			// Phase jitter
			noiseState = noiseState * 1664525u + 1013904223u;
			evalPhase +=
			    static_cast<uint32_t>(multiply_32x32_rshift32(static_cast<int32_t>(noiseState), jitterRange) << 1);

			if (evalPhase > phaseWidth) {
				*thisSample = 0;
				thisSample++;
				continue;
			}

			if (evalPhase < segs[segIdx].startPhase) {
				segIdx = 0;
			}
			while (segIdx < kPhiMorphMaxSegments - 1 && evalPhase >= segs[segIdx].endPhase) {
				segIdx++;
			}

			const PhiMorphSegment& s = segs[segIdx];
			float frac = static_cast<float>(evalPhase - s.startPhase) * s.invWidth;

			q31_t waveform = s.startAmp + static_cast<q31_t>(s.ampDeltaF * frac);
			float quadTerm = frac * (1.0f - frac) * 4.0f;
			waveform = add_saturate(waveform, static_cast<q31_t>(s.curvatureF * quadTerm));

			// Amplitude-dependent noise
			noiseState = noiseState * 1664525u + 1013904223u;
			q31_t noiseContrib =
			    multiply_32x32_rshift32(multiply_32x32_rshift32(static_cast<int32_t>(noiseState), ampNoiseQ31),
			                            std::abs(waveform))
			    << 2;
			waveform = add_saturate(waveform, noiseContrib);

			*thisSample = waveform;
			thisSample++;
		}
	}

	*startPhase = phaseAtEnd;

#if ENABLE_FX_BENCHMARK
	FX_BENCH_STOP(bench_render);
#endif
}

} // namespace deluge::dsp
