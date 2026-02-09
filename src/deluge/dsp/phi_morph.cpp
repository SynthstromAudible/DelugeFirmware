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

	// Guarantee at least 1 waypoint (scale ensures this, but clamp defensively)
	if (activeWaypoints == 0) {
		table.phases[0] = (kPhiMorphPhaseMin + kPhiMorphPhaseMax) * 0.5f;
		phaseAccum = table.phases[0];
		activeWaypoints = 1;
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

	// First two waypoints fixed at +1/-1 for strong fundamental,
	// remaining waypoints from phi triangle bank normalized to [-1, +1]
	if (activeWaypoints >= 1) {
		table.amplitudes[0] = 1.0f;
	}
	if (activeWaypoints >= 2) {
		table.amplitudes[1] = -1.0f;
	}
	if (activeWaypoints > 2) {
		float minAmp = 0.0f, maxAmp = 0.0f;
		for (int i = 2; i < activeWaypoints; i++) {
			minAmp = std::min(minAmp, rawAmplitudes[i]);
			maxAmp = std::max(maxAmp, rawAmplitudes[i]);
		}
		float ampRange = maxAmp - minAmp;
		if (ampRange > 0.01f) {
			float scale = 2.0f / ampRange;
			float center = (maxAmp + minAmp) * 0.5f;
			for (int i = 2; i < activeWaypoints; i++) {
				table.amplitudes[i] = std::clamp((rawAmplitudes[i] - center) * scale, -1.0f, 1.0f);
			}
		}
		else {
			for (int i = 2; i < activeWaypoints; i++) {
				table.amplitudes[i] = 0.0f;
			}
		}
	}

	// Inactive waypoints: zero amplitude
	for (int i = activeWaypoints; i < kPhiMorphMovableWaypoints; i++) {
		table.amplitudes[i] = 0.0f;
	}

	// Energy normalization: equalize perceived loudness across zone positions.
	// Approximate energy as sum of (segWidth × (a0² + a0·a1 + a1²) / 3) per segment.
	float energy = 0.0f;
	{
		float prevPhase = 0.0f;
		float prevAmp = 0.0f;
		for (int i = 0; i < kPhiMorphMovableWaypoints; i++) {
			float segWidth = table.phases[i] - prevPhase;
			float a0 = prevAmp;
			float a1 = table.amplitudes[i];
			energy += segWidth * (a0 * a0 + a0 * a1 + a1 * a1);
			prevPhase = table.phases[i];
			prevAmp = a1;
		}
		float lastWidth = 1.0f - prevPhase;
		energy += lastWidth * (prevAmp * prevAmp);
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
	}

	// Gain: strictly positive [1.0, 2.0] — drives normalized waveform into Q31 clipper
	// for flat-top / square-like harmonics. Applied after energy normalization.
	float gainValue = 1.0f + rawGain[0];
	table.gain = gainValue;
	for (int i = 0; i < activeWaypoints; i++) {
		table.amplitudes[i] *= gainValue;
	}

	// Curvatures: active segments + return-to-zero get phi curvatures, tail gets zero
	for (int i = 0; i <= activeWaypoints && i < kPhiMorphMaxSegments; i++) {
		table.curvatures[i] = rawCurvatures[i];
	}
	for (int i = activeWaypoints + 1; i < kPhiMorphMaxSegments; i++) {
		table.curvatures[i] = 0.0f;
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

	// 9 points: (0,0), P1..P7, (1,0) — endpoints always at zero
	float segStartPhase[kPhiMorphMaxSegments + 1];
	float segStartAmp[kPhiMorphMaxSegments + 1];

	segStartPhase[0] = 0.0f;
	segStartAmp[0] = 0.0f;
	for (int i = 0; i < kPhiMorphMovableWaypoints; i++) {
		segStartPhase[i + 1] = phases[i];
		segStartAmp[i + 1] = amplitudes[i]; // Already gain-scaled, may exceed [-1,1]
	}
	segStartPhase[kPhiMorphMaxSegments] = 1.0f;
	segStartAmp[kPhiMorphMaxSegments] = 0.0f;

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
                    int32_t amplitudeIncrement, bool applyAmplitude, q31_t crossfade) {

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

		float effPhases[kPhiMorphMovableWaypoints];
		float effAmplitudes[kPhiMorphMovableWaypoints];
		float effCurvatures[kPhiMorphMaxSegments];

		for (int i = 0; i < kPhiMorphMovableWaypoints; i++) {
			effPhases[i] = cfInv * cache.bankA.phases[i] + cf * cache.bankB.phases[i];
			effAmplitudes[i] = cfInv * cache.bankA.amplitudes[i] + cf * cache.bankB.amplitudes[i];
		}
		for (int i = 0; i < kPhiMorphMaxSegments; i++) {
			effCurvatures[i] = cfInv * cache.bankA.curvatures[i] + cf * cache.bankB.curvatures[i];
		}
		cache.effective = PhiMorphWavetable{};
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

	if (applyAmplitude) {
		for (int32_t n = 0; n < numSamples; n++) {
			phase += phaseIncrement;
			evalPhase = phase + retriggerPhase;

			// Advance segment if phase passed boundary
			while (segIdx < kPhiMorphMaxSegments - 1 && evalPhase >= segs[segIdx].endPhase) {
				segIdx++;
			}

			const PhiMorphSegment& s = segs[segIdx];
			float frac = static_cast<float>(evalPhase - s.startPhase) * s.invWidth;

			q31_t waveform = s.startAmp + static_cast<q31_t>(s.ampDeltaF * frac);
			float quadTerm = frac * (1.0f - frac) * 4.0f;
			waveform = add_saturate(waveform, static_cast<q31_t>(s.curvatureF * quadTerm));

			amplitude += amplitudeIncrement;
			*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, waveform, amplitude);
			thisSample++;
		}
	}
	else {
		for (int32_t n = 0; n < numSamples; n++) {
			phase += phaseIncrement;
			evalPhase = phase + retriggerPhase;

			while (segIdx < kPhiMorphMaxSegments - 1 && evalPhase >= segs[segIdx].endPhase) {
				segIdx++;
			}

			const PhiMorphSegment& s = segs[segIdx];
			float frac = static_cast<float>(evalPhase - s.startPhase) * s.invWidth;

			q31_t waveform = s.startAmp + static_cast<q31_t>(s.ampDeltaF * frac);
			float quadTerm = frac * (1.0f - frac) * 4.0f;
			waveform = add_saturate(waveform, static_cast<q31_t>(s.curvatureF * quadTerm));

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
