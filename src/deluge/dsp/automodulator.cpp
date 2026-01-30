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

#include "dsp/automodulator.hpp"
#include "dsp/fast_math.h"
#include "io/debug/fx_benchmark.h"
#include <algorithm>
#include <cmath>

namespace deluge::dsp {

// ============================================================================
// Phi triangle helper functions (called during cache updates, not hot path)
// ============================================================================

LfoRateResult getLfoRateFromMod(uint16_t mod, float phaseOffset) {
	if (phaseOffset > 0.0f) {
		// Phi triangle mode: bipolar output
		double phase = static_cast<double>(mod) / 1023.0 + static_cast<double>(phaseOffset);
		float tri = phi::evalTriangle(phase, 1.0f, kLfoRateTriangle); // -1 to +1

		if (tri >= 0.0f) {
			// Positive: free-running Hz
			float rate = kLfoRateMin + tri * (kLfoRateMax - kLfoRateMin);
			return {rate, 0, false};
		}
		else if (tri >= -0.5f) {
			// -0.5 to 0: Triplet sync
			// Map -0.5..0 to sync levels 9..1 (256th to whole)
			float mag = -tri * 2.0f;                              // 0 to 1
			int32_t level = 1 + static_cast<int32_t>(mag * 8.0f); // 1-9
			level = std::clamp<int32_t>(level, 1, 9);
			return {0.0f, level, true};
		}
		else {
			// -1.0 to -0.5: Even sync
			// Map -1..-0.5 to sync levels 9..1 (256th to whole)
			float mag = (-tri - 0.5f) * 2.0f;                     // 0 to 1
			int32_t level = 1 + static_cast<int32_t>(mag * 8.0f); // 1-9
			level = std::clamp<int32_t>(level, 1, 9);
			return {0.0f, level, false};
		}
	}

	// Default mode: 8 zones with interpolation (always free-running)
	int32_t zone = mod >> 7;          // 0-7
	int32_t frac = (mod & 0x7F) << 1; // 0-254

	// Interpolate between zone rates
	float rate0 = kAutomodLfoRates[zone];
	float rate1 = kAutomodLfoRates[std::min<int32_t>(zone + 1, 7)];
	float t = static_cast<float>(frac) / 255.0f;
	return {rate0 + t * (rate1 - rate0), 0, false};
}

FilterMix getFilterMixFromType(uint16_t type, float phaseOffset) {
	// Normalize type to [0,1] and add phase offset
	double phase = static_cast<double>(type) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle bank
	auto raw = phi::evalTriangleBank<3>(phase, 1.0f, kFilterMixBank);

	// Add epsilon to lowpass to ensure signal always passes through
	// (prevents silent spots when all triangles are in dead zones)
	constexpr float kLpEpsilon = 0.1f;
	float lpWeight = raw[0] + kLpEpsilon;

	// Constant-power normalization: sum of squares = 1.0 for equal perceived loudness
	// When mixing: out = low*wL + band*wB + high*wH, constant power needs wL² + wB² + wH² = 1
	float sumSquares = lpWeight * lpWeight + raw[1] * raw[1] + raw[2] * raw[2];
	float invRms = 1.0f / std::sqrt(sumSquares);

	return {lpWeight * invRms, raw[1] * invRms, raw[2] * invRms};
}

FilterLfoParams getFilterLfoParamsFromFlavor(uint16_t flavor, float phaseOffset) {
	double phase = static_cast<double>(flavor) / 1023.0 + static_cast<double>(phaseOffset);

	auto response = phi::evalTriangleBank<3>(phase, 1.0f, kFilterLfoResponseBank);
	auto phaseOffsets = phi::evalTriangleBank<3>(phase, 1.0f, kFilterPhaseOffsetBank);

	return {response[0], response[1], response[2], phaseOffsets[0], phaseOffsets[1], phaseOffsets[2]};
}

float getCombLfoDepthFromFlavor(uint16_t flavor, float phaseOffset) {
	// Normalize flavor to [0,1] and add phase offset
	double phase = static_cast<double>(flavor) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output)
	return phi::evalTriangle(phase, 1.0f, kCombLfoDepthTriangle);
}

float getCombStaticOffsetFromFlavor(uint16_t flavor, float phaseOffset) {
	// Normalize flavor to [0,1] and add phase offset
	double phase = static_cast<double>(flavor) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output)
	return phi::evalTriangle(phase, 1.0f, kCombStaticOffsetTriangle);
}

uint32_t getCombLfoPhaseOffsetFromFlavor(uint16_t flavor, float phaseOffset) {
	// Normalize flavor to [0,1] and add phase offset
	double phase = static_cast<double>(flavor) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output), map to full phase range
	float tri = phi::evalTriangle(phase, 1.0f, kCombLfoPhaseOffsetTriangle);
	return static_cast<uint32_t>(tri * 4294967295.0f);
}

q31_t getFilterResonanceFromFlavor(uint16_t flavor, float phaseOffset) {
	// Normalize flavor to [0,1] and add phase offset
	double phase = static_cast<double>(flavor) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output)
	float tri = phi::evalTriangle(phase, 1.0f, kFilterResonanceTriangle);

	// Map [0,1] to [0, 0.85] resonance (capped for stability)
	return static_cast<q31_t>(tri * 0.85f * 2147483647.0f);
}

q31_t getFilterCutoffBaseFromFlavor(uint16_t flavor, float phaseOffset) {
	// Normalize flavor to [0,1] and add phase offset
	double phase = static_cast<double>(flavor) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output)
	float tri = phi::evalTriangle(phase, 1.0f, kFilterCutoffBaseTriangle);

	// Map [0,1] to [0, 0.15] base cutoff (lower = more dramatic sweep)
	return static_cast<q31_t>(tri * 0.15f * 2147483647.0f);
}

q31_t getFilterCutoffLfoDepthFromFlavor(uint16_t flavor, float phaseOffset) {
	// Normalize flavor to [0,1] and add phase offset
	double phase = static_cast<double>(flavor) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output)
	float tri = phi::evalTriangle(phase, 1.0f, kFilterCutoffLfoDepthTriangle);

	// Map [0,1] to [0.15, 0.85] depth (always some sweep, up to dramatic)
	return static_cast<q31_t>((0.15f + tri * 0.7f) * 2147483647.0f);
}

q31_t getEnvAttackFromFlavor(uint16_t flavor, float phaseOffset) {
	// Normalize flavor to [0,1] and add phase offset
	double phase = static_cast<double>(flavor) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output)
	float tri = phi::evalTriangle(phase, 1.0f, kEnvAttackTriangle);

	// Buffer-rate envelope: 128 samples @ 44.1kHz = 2.9ms per tick
	// coeff 0.25 ≈ 10ms, coeff 0.003 ≈ 1000ms
	// Map tri 0→1 to coeff 0.25→0.003 (fast to slow)
	float coeff = 0.25f * (1.0f - tri) + 0.003f;
	return static_cast<q31_t>(coeff * 2147483647.0f);
}

q31_t getEnvReleaseFromFlavor(uint16_t flavor, float phaseOffset) {
	// Normalize flavor to [0,1] and add phase offset
	double phase = static_cast<double>(flavor) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output)
	float tri = phi::evalTriangle(phase, 1.0f, kEnvReleaseTriangle);

	// Buffer-rate envelope: 128 samples @ 44.1kHz = 2.9ms per tick
	// coeff 0.25 ≈ 10ms, coeff 0.003 ≈ 1000ms
	// Map tri 0→1 to coeff 0.25→0.003 (fast to slow)
	float coeff = 0.25f * (1.0f - tri) + 0.003f;
	return static_cast<q31_t>(coeff * 2147483647.0f);
}

q31_t getCombFeedbackFromType(uint16_t type, float phaseOffset) {
	// Normalize type to [0,1] and add phase offset
	double phase = static_cast<double>(type) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output)
	float tri = phi::evalTriangle(phase, 1.0f, kCombFeedbackTriangle);

	// Map [0,1] to [0, 0.85] feedback (capped for stability)
	return static_cast<q31_t>(tri * 0.85f * 2147483647.0f);
}

float getCombMixFromType(uint16_t type, float phaseOffset) {
	// Normalize type to [0,1] and add phase offset
	double phase = static_cast<double>(type) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output)
	return phi::evalTriangle(phase, 1.0f, kCombMixTriangle);
}

float getTremoloDepthFromType(uint16_t type, float phaseOffset) {
	// Normalize type to [0,1] and add phase offset
	double phase = static_cast<double>(type) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output), map to 0-0.8 depth
	return phi::evalTriangle(phase, 1.0f, kTremoloDepthTriangle) * 0.8f;
}

uint32_t getTremoloPhaseOffsetFromType(uint16_t type, float phaseOffset) {
	// Normalize type to [0,1] and add phase offset
	double phase = static_cast<double>(type) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output), map to full phase range
	float tri = phi::evalTriangle(phase, 1.0f, kTremoloPhaseOffsetTriangle);
	return static_cast<uint32_t>(tri * 4294967295.0f);
}

uint32_t getStereoOffsetFromMod(uint16_t mod, float phaseOffset) {
	// Normalize mod to [0,1] and add phase offset
	double phase = static_cast<double>(mod) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output)
	float tri = phi::evalTriangle(phase, 1.0f, kStereoOffsetTriangle);

	// Map [0,1] to [0, 0x80000000] (0-180 degrees range)
	// 0 = no offset (mono), 0.5 = 90 degrees (max width), 1 = 180 degrees (opposite phase)
	return static_cast<uint32_t>(tri * 2147483647.0f);
}

uint32_t getLfoInitialPhaseFromMod(uint16_t mod, float phaseOffset) {
	// Normalize mod to [0,1] and add phase offset
	double phase = static_cast<double>(mod) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output)
	float tri = phi::evalTriangle(phase, 1.0f, kLfoInitialPhaseTriangle);

	// Map [0,1] to full 32-bit phase range
	return static_cast<uint32_t>(tri * 4294967295.0f);
}

float getEnvDepthInfluenceFromMod(uint16_t mod, float phaseOffset) {
	// Normalize mod to [0,1] and add phase offset
	double phase = static_cast<double>(mod) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate bipolar triangle (-1 to +1 output)
	return phi::evalTriangle(phase, 1.0f, kEnvDepthInfluenceTriangle);
}

float getEnvPhaseInfluenceFromMod(uint16_t mod, float phaseOffset) {
	// Normalize mod to [0,1] and add phase offset
	double phase = static_cast<double>(mod) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate bipolar triangle (-1 to +1 output)
	return phi::evalTriangle(phase, 1.0f, kEnvPhaseInfluenceTriangle);
}

float getEnvDerivDepthInfluenceFromMod(uint16_t mod, float phaseOffset) {
	// Normalize mod to [0,1] and add phase offset
	double phase = static_cast<double>(mod) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate bipolar triangle (-1 to +1 output)
	return phi::evalTriangle(phase, 1.0f, kEnvDerivDepthInfluenceTriangle);
}

float getEnvDerivPhaseInfluenceFromMod(uint16_t mod, float phaseOffset) {
	// Normalize mod to [0,1] and add phase offset
	double phase = static_cast<double>(mod) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate bipolar triangle (-1 to +1 output)
	return phi::evalTriangle(phase, 1.0f, kEnvDerivPhaseInfluenceTriangle);
}

LfoWaypointBank getLfoWaypointBank(uint16_t mod, float phaseOffset) {
	// Normalize mod to [0,1] and add phase offset
	double phase = static_cast<double>(mod) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate all 9 triangles (5 phase deltas + 4 amplitudes)
	auto raw = phi::evalTriangleBank<9>(phase, 1.0f, kLfoWaypointBank);

	// Phase deltas: take abs of bipolar values, accumulate, then normalize
	// 5 deltas for 5 segments: 0→P1, P1→P2, P2→P3, P3→P4, P4→1
	// This guarantees monotonically increasing phases
	float deltas[5];
	for (int i = 0; i < 5; i++) {
		// Map bipolar (-1,+1) to positive delta (0.1 to 1.0)
		// Using abs + offset ensures we always have positive spacing
		deltas[i] = 0.1f + std::abs(raw[i]) * 0.9f;
	}

	// Accumulate phases for the 4 waypoints
	// P1 is after delta[0], P2 after delta[0]+delta[1], etc.
	float cumulative[4];
	cumulative[0] = deltas[0];
	cumulative[1] = cumulative[0] + deltas[1];
	cumulative[2] = cumulative[1] + deltas[2];
	cumulative[3] = cumulative[2] + deltas[3];

	// Total includes all 5 deltas (P4→1 segment)
	float total = cumulative[3] + deltas[4];

	// Normalize to [kWaypointPhaseMin, kWaypointPhaseMax] range
	float phaseRange = kWaypointPhaseMax - kWaypointPhaseMin;

	LfoWaypointBank bank;
	for (int i = 0; i < 4; i++) {
		bank.phase[i] = kWaypointPhaseMin + (cumulative[i] / total) * phaseRange;
		bank.amplitude[i] = raw[5 + i]; // Amplitudes start at index 5
	}

	// Normalize amplitudes to ensure consistent peak-to-peak range
	// The LFO strength multipliers (scaleQL/scaleQR) expect normalized output
	// Consider all 6 points: (0,0), 4 waypoints, (1,0)
	float minAmp = 0.0f; // Fixed endpoints are at 0
	float maxAmp = 0.0f;
	for (int i = 0; i < 4; i++) {
		minAmp = std::min(minAmp, bank.amplitude[i]);
		maxAmp = std::max(maxAmp, bank.amplitude[i]);
	}

	// Normalize to [-1, +1] range if there's any amplitude variation
	float ampRange = maxAmp - minAmp;
	if (ampRange > 0.01f) {
		// Scale so peak-to-peak spans 2.0 (-1 to +1)
		// Then center around 0
		float scale = 2.0f / ampRange;
		float center = (maxAmp + minAmp) * 0.5f;
		for (int i = 0; i < 4; i++) {
			bank.amplitude[i] = (bank.amplitude[i] - center) * scale;
		}
	}
	else {
		// All amplitudes nearly equal - output flat line at 0
		for (int i = 0; i < 4; i++) {
			bank.amplitude[i] = 0.0f;
		}
	}

	// Pre-compute segment boundaries, start amplitudes, and slopes for fast runtime evaluation
	// 6 points: (0, 0), P1, P2, P3, P4, (1, 0)
	// 5 segments with pre-computed values (avoids division at runtime)
	bank.segStart[0] = 0.0f;
	bank.segStart[1] = bank.phase[0];
	bank.segStart[2] = bank.phase[1];
	bank.segStart[3] = bank.phase[2];
	bank.segStart[4] = bank.phase[3];

	// Segment start amplitudes: (0, A1, A2, A3, A4)
	bank.segAmp[0] = 0.0f;
	bank.segAmp[1] = bank.amplitude[0];
	bank.segAmp[2] = bank.amplitude[1];
	bank.segAmp[3] = bank.amplitude[2];
	bank.segAmp[4] = bank.amplitude[3];

	// Segment 0: (0,0) → P1
	float width0 = bank.phase[0];
	bank.segSlope[0] = (width0 > 0.001f) ? bank.amplitude[0] / width0 : 0.0f;

	// Segment 1: P1 → P2
	float width1 = bank.phase[1] - bank.phase[0];
	bank.segSlope[1] = (width1 > 0.001f) ? (bank.amplitude[1] - bank.amplitude[0]) / width1 : 0.0f;

	// Segment 2: P2 → P3
	float width2 = bank.phase[2] - bank.phase[1];
	bank.segSlope[2] = (width2 > 0.001f) ? (bank.amplitude[2] - bank.amplitude[1]) / width2 : 0.0f;

	// Segment 3: P3 → P4
	float width3 = bank.phase[3] - bank.phase[2];
	bank.segSlope[3] = (width3 > 0.001f) ? (bank.amplitude[3] - bank.amplitude[2]) / width3 : 0.0f;

	// Segment 4: P4 → (1,0)
	float width4 = 1.0f - bank.phase[3];
	bank.segSlope[4] = (width4 > 0.001f) ? (0.0f - bank.amplitude[3]) / width4 : 0.0f;

	// === Pre-compute integer fields for fast runtime evaluation ===
	// Phase boundaries as uint32 (for fast comparison without float conversion)
	constexpr float kPhaseToU32 = 4294967295.0f;
	for (int i = 0; i < 4; i++) {
		bank.phaseU32[i] = static_cast<uint32_t>(bank.phase[i] * kPhaseToU32);
	}

	// Segment start phases as uint32
	for (int i = 0; i < 5; i++) {
		bank.segStartU32[i] = static_cast<uint32_t>(bank.segStart[i] * kPhaseToU32);
	}

	// Segment start amplitudes as unipolar q31 [0, ONE_Q31]
	// Map [-1,+1] to [0, ONE_Q31]
	for (int i = 0; i < 5; i++) {
		float unipolar = (bank.segAmp[i] + 1.0f) * 0.5f;
		bank.segAmpQ[i] = static_cast<q31_t>(unipolar * 2147483647.0f);
	}

	// Slopes in q31 per uint32-phase-unit
	// For: value = segAmpQ + multiply_32x32_rshift32(phaseOffset, segSlopeQ) << 1
	// The slope needs to be scaled so that over the full uint32 range of a segment,
	// the amplitude change equals the float slope * segment width (in 0-1 range)
	// Since phaseOffset is uint32, we need: slopeQ = floatSlope * 0.5 * ONE_Q31 (bipolar to unipolar, then q31)
	for (int i = 0; i < 5; i++) {
		// Float slope is in bipolar amplitude per float phase unit
		// Convert to unipolar: divide by 2 (since [-1,1] → [0,1] is /2)
		// Then scale to q31
		float unipolarSlope = bank.segSlope[i] * 0.5f;
		bank.segSlopeQ[i] = static_cast<q31_t>(unipolarSlope * 2147483647.0f);
	}

	return bank;
}

float evalLfoWavetable(float t, const LfoWaypointBank& bank) {
	// Clamp t to [0, 1]
	t = std::clamp(t, 0.0f, 1.0f);

	// 6 points total: (0,0), P1, P2, P3, P4, (1,0)
	// Find segment and use pre-computed slope (no division!)

	// Amplitudes at segment starts: 0, A1, A2, A3, A4
	// Using slope: value = startAmp + (t - segStart) * slope

	if (t <= bank.phase[0]) {
		// Segment 0: (0,0) to P1, startAmp = 0
		return t * bank.segSlope[0];
	}
	else if (t <= bank.phase[1]) {
		// Segment 1: P1 to P2, startAmp = A1
		return bank.amplitude[0] + (t - bank.phase[0]) * bank.segSlope[1];
	}
	else if (t <= bank.phase[2]) {
		// Segment 2: P2 to P3, startAmp = A2
		return bank.amplitude[1] + (t - bank.phase[1]) * bank.segSlope[2];
	}
	else if (t <= bank.phase[3]) {
		// Segment 3: P3 to P4, startAmp = A3
		return bank.amplitude[2] + (t - bank.phase[2]) * bank.segSlope[3];
	}
	else {
		// Segment 4: P4 to (1,0), startAmp = A4
		return bank.amplitude[3] + (t - bank.phase[3]) * bank.segSlope[4];
	}
}

q31_t evalLfoWavetableQ31(uint32_t phaseU32, const LfoWaypointBank& bank) {
	// Convert uint32 phase to float [0,1]
	float t = static_cast<float>(phaseU32) * (1.0f / 4294967295.0f);

	// Evaluate wavetable
	float value = evalLfoWavetable(t, bank);

	// Convert to unipolar q31 [0, ONE_Q31] for compatibility with existing code
	// Map [-1,+1] to [0,1] then scale to q31
	float unipolar = (value + 1.0f) * 0.5f;
	return static_cast<q31_t>(unipolar * 2147483647.0f);
}

/// Hybrid LFO evaluation - uses float for wavetable eval, returns q31 for DSP
/// Fast path uses segment slope when staying in same segment
/// Slow path evaluates both endpoints when crossing segment boundary or phase wrap
LfoIncremental evalLfoIncremental(uint32_t startPhaseU32, uint32_t phaseInc, size_t bufferSize,
                                  const LfoWaypointBank& bank) {
	constexpr float kPhaseToFloat = 1.0f / 4294967295.0f;

	// Convert phases to [0,1] range
	float tStart = static_cast<float>(startPhaseU32) * kPhaseToFloat;
	uint32_t endPhaseU32 = startPhaseU32 + phaseInc * static_cast<uint32_t>(bufferSize);
	float tEnd = static_cast<float>(endPhaseU32) * kPhaseToFloat;

	// Check for phase wrap (end < start means we crossed 1.0 -> 0.0)
	bool phaseWrap = (endPhaseU32 < startPhaseU32);

	// Find which segment start is in
	int startSeg = (tStart <= bank.phase[0])   ? 0
	               : (tStart <= bank.phase[1]) ? 1
	               : (tStart <= bank.phase[2]) ? 2
	               : (tStart <= bank.phase[3]) ? 3
	                                           : 4;

	// Find which segment end is in (after potential wrap)
	float tEndClamped = std::clamp(tEnd, 0.0f, 1.0f);
	int endSeg = (tEndClamped <= bank.phase[0])   ? 0
	             : (tEndClamped <= bank.phase[1]) ? 1
	             : (tEndClamped <= bank.phase[2]) ? 2
	             : (tEndClamped <= bank.phase[3]) ? 3
	                                              : 4;

	// Evaluate start value
	float startValue = evalLfoWavetable(tStart, bank);

	// If we cross a segment boundary or phase wrap, do accurate two-eval method
	// Otherwise use the fast single-eval with segment slope
	float delta;
	if (phaseWrap || startSeg != endSeg) {
		// Accurate: evaluate both endpoints
		float endValue = evalLfoWavetable(tEndClamped, bank);
		delta = (endValue - startValue) / static_cast<float>(bufferSize);
	}
	else {
		// Fast: use pre-computed segment slope
		float phaseIncFloat = static_cast<float>(phaseInc) * kPhaseToFloat;
		delta = bank.segSlope[startSeg] * phaseIncFloat;
	}

	// Convert to unipolar q31 [0, ONE_Q31]
	float unipolarValue = (startValue + 1.0f) * 0.5f;
	q31_t valueQ = static_cast<q31_t>(unipolarValue * 2147483647.0f);

	float unipolarDelta = delta * 0.5f;
	q31_t deltaQ = static_cast<q31_t>(unipolarDelta * 2147483647.0f);

	return {valueQ, deltaQ};
}

// ============================================================================
// Cache update function
// ============================================================================

void updateAutomodPhiCache(AutomodulatorParams& params, uint32_t timePerTickInverse) {
	AutomodPhiCache& c = params.cache;

	// Compute effective phases
	float effectiveModPhase = params.modPhaseOffset + params.gammaPhase;
	float effectiveFlavorPhase = params.flavorPhaseOffset + params.gammaPhase;
	float effectiveTypePhase = params.typePhaseOffset + params.gammaPhase;

	// === From mod ===
	LfoRateResult rateResult = getLfoRateFromMod(params.mod, effectiveModPhase);
	c.rateValue = rateResult.value;
	c.rateSyncLevel = rateResult.syncLevel;
	c.rateTriplet = rateResult.triplet;

	// LFO wavetable (4-point XY shape)
	c.wavetable = getLfoWaypointBank(params.mod, effectiveModPhase);

	// P4 phase as uint32 for last-segment detection (do full eval in last segment for clean wrap)
	c.lastSegmentPhaseU32 = static_cast<uint32_t>(c.wavetable.phase[3] * 4294967295.0f);

	// Compute LFO increment
	if (rateResult.syncLevel > 0 && timePerTickInverse > 0) {
		c.lfoInc = timePerTickInverse >> (9 - rateResult.syncLevel);
		if (rateResult.triplet) {
			c.lfoInc = c.lfoInc * 3 / 2;
		}
	}
	else {
		c.lfoInc = static_cast<uint32_t>(rateResult.value * 97391.263f);
	}

	c.stereoPhaseOffsetRaw = getStereoOffsetFromMod(params.mod, effectiveModPhase);
	c.envDepthInfluence = getEnvDepthInfluenceFromMod(params.mod, effectiveModPhase);
	c.envPhaseInfluence = getEnvPhaseInfluenceFromMod(params.mod, effectiveModPhase);
	c.envDerivDepthInfluence = getEnvDerivDepthInfluenceFromMod(params.mod, effectiveModPhase);
	c.envDerivPhaseInfluence = getEnvDerivPhaseInfluenceFromMod(params.mod, effectiveModPhase);

	// === From flavor ===
	c.filterCutoffBase = getFilterCutoffBaseFromFlavor(params.flavor, effectiveFlavorPhase);
	c.filterResonance = getFilterResonanceFromFlavor(params.flavor, effectiveFlavorPhase);
	c.filterModDepth = getFilterCutoffLfoDepthFromFlavor(params.flavor, effectiveFlavorPhase);
	c.envAttack = getEnvAttackFromFlavor(params.flavor, effectiveFlavorPhase);
	c.envRelease = getEnvReleaseFromFlavor(params.flavor, effectiveFlavorPhase);
	c.combStaticOffset = getCombStaticOffsetFromFlavor(params.flavor, effectiveFlavorPhase);
	c.combLfoDepth = getCombLfoDepthFromFlavor(params.flavor, effectiveFlavorPhase);
	c.combPhaseOffsetU32 = getCombLfoPhaseOffsetFromFlavor(params.flavor, effectiveFlavorPhase);

	// Pre-compute comb delay constants in 16.16 fixed-point (avoids float conversions in loop)
	constexpr int32_t kMinDelay = 4;
	constexpr int32_t kMaxDelay = 1470;
	constexpr int32_t kMaxModRange = 400;
	constexpr int32_t kCombSize = static_cast<int32_t>(AutomodulatorParams::kCombBufferSize);
	// baseDelay = minDelay + staticOffset * (maxDelay - minDelay - maxModRange)
	int32_t delayRange = kMaxDelay - kMinDelay - kMaxModRange; // 1066 samples
	int32_t baseDelaySamples = kMinDelay + static_cast<int32_t>(c.combStaticOffset * static_cast<float>(delayRange));
	c.combBaseDelay16 = baseDelaySamples << 16;
	c.combModRangeSamples = static_cast<int32_t>(c.combLfoDepth * static_cast<float>(kMaxModRange)); // 0-400 samples
	c.combMinDelay16 = 2 << 16;
	c.combMaxDelay16 = (kCombSize - 2) << 16;

	// Filter LFO params
	FilterLfoParams filterLfo = getFilterLfoParamsFromFlavor(params.flavor, effectiveFlavorPhase);
	c.lpResponse = filterLfo.lpResponse;
	c.bpResponse = filterLfo.bpResponse;
	c.hpResponse = filterLfo.hpResponse;
	c.lpPhaseOffset = filterLfo.lpPhaseOffset;
	c.bpPhaseOffset = filterLfo.bpPhaseOffset;
	c.hpPhaseOffset = filterLfo.hpPhaseOffset;
	c.lpPhaseOffsetU32 = static_cast<uint32_t>(filterLfo.lpPhaseOffset * kPhaseMaxFloat);
	c.bpPhaseOffsetU32 = static_cast<uint32_t>(filterLfo.bpPhaseOffset * kPhaseMaxFloat);
	c.hpPhaseOffsetU32 = static_cast<uint32_t>(filterLfo.hpPhaseOffset * kPhaseMaxFloat);

	// Pre-compute response as Q31 for pure fixed-point loop math
	c.lpResponseQ = static_cast<q31_t>(filterLfo.lpResponse * kQ31MaxFloat);
	c.bpResponseQ = static_cast<q31_t>(filterLfo.bpResponse * kQ31MaxFloat);
	c.hpResponseQ = static_cast<q31_t>(filterLfo.hpResponse * kQ31MaxFloat);

	constexpr float kResponseThreshold = 0.01f;
	c.useStaticFilterMix = (filterLfo.lpResponse < kResponseThreshold && filterLfo.bpResponse < kResponseThreshold
	                        && filterLfo.hpResponse < kResponseThreshold);

	// === From type ===
	FilterMix filterMix = getFilterMixFromType(params.type, effectiveTypePhase);
	c.filterMixLowQ = static_cast<q31_t>(filterMix.low * kQ31MaxFloat);
	c.filterMixBandQ = static_cast<q31_t>(filterMix.band * kQ31MaxFloat);
	c.filterMixHighQ = static_cast<q31_t>(filterMix.high * kQ31MaxFloat);

	c.combFeedback = getCombFeedbackFromType(params.type, effectiveTypePhase);
	float combMix = getCombMixFromType(params.type, effectiveTypePhase);
	c.combMixQ = static_cast<q31_t>(combMix * kQ31MaxFloat);

	c.tremPhaseOffset = getTremoloPhaseOffsetFromType(params.type, effectiveTypePhase);
	float tremoloDepth = getTremoloDepthFromType(params.type, effectiveTypePhase);
	c.tremoloDepthQ = static_cast<q31_t>(tremoloDepth * kQ31MaxFloat);
	c.tremoloBaseLevel = ONE_Q31 - c.tremoloDepthQ;

	// Update cache keys
	params.prevType = params.type;
	params.prevFlavor = params.flavor;
	params.prevMod = params.mod;
	params.prevGammaPhase = params.gammaPhase;
	params.prevTypePhaseOffset = params.typePhaseOffset;
	params.prevFlavorPhaseOffset = params.flavorPhaseOffset;
	params.prevModPhaseOffset = params.modPhaseOffset;
	params.prevTimePerTickInverse = timePerTickInverse;
}

// ============================================================================
// Main DSP processing function
// ============================================================================

void processAutomodulator(std::span<StereoSample> buffer, AutomodulatorParams& params, q31_t macro, bool useInternalOsc,
                          uint8_t voiceCount, uint32_t timePerTickInverse, int32_t noteCode) {
	if (!params.isEnabled() || buffer.empty()) {
		return;
	}

#if ENABLE_FX_BENCHMARK
	const bool doBench = Debug::FxBenchGlobal::sampleThisBuffer;
	static Debug::FxBenchmark benchTotal("automod", "total"); // Total cycles for entire effect
	static Debug::FxBenchmark benchCache("automod", "cache"); // Phi triangle cache update (rare)
	static Debug::FxBenchmark benchSetup("automod", "setup"); // Per-buffer setup (envelope, smoothing)
	static Debug::FxBenchmark benchLoop("automod", "loop");   // Main DSP loop
	if (doBench) {
		benchTotal.start();
	}
#endif

	// === Envelope tracking on INPUT (before processing) ===
	// Sample at buffer boundaries instead of scanning entire buffer (~400 cycles saved)
	// TODO: verify this doesn't regress envelope tracking feel compared to full peak scan
	q31_t peakL = std::max(std::abs(buffer.front().l), std::abs(buffer.back().l));
	q31_t peakR = std::max(std::abs(buffer.front().r), std::abs(buffer.back().r));

	// Update phi triangle cache only when params change (big perf win)
	if (params.needsCacheUpdate(timePerTickInverse)) {
#if ENABLE_FX_BENCHMARK
		if (doBench) {
			benchCache.start();
		}
#endif
		updateAutomodPhiCache(params, timePerTickInverse);
		params.prevLfoPhase = 0xFFFFFFFF; // Force LFO recalc (wavetable changed)
#if ENABLE_FX_BENCHMARK
		if (doBench) {
			benchCache.stop();
		}
#endif
	}

#if ENABLE_FX_BENCHMARK
	if (doBench) {
		benchSetup.start();
	}
#endif

	// Reference to cache for cleaner code
	const AutomodPhiCache& c = params.cache;

	// Store initial envelope state for derivative calculation
	q31_t envStartL = params.envStateL;
	q31_t envStartR = params.envStateR;

	// Envelope follower at buffer rate - one-pole filter on INPUT peaks
	if (peakL > params.envStateL) {
		params.envStateL += multiply_32x32_rshift32(peakL - params.envStateL, c.envAttack) << 1;
	}
	else {
		params.envStateL -= multiply_32x32_rshift32(params.envStateL - peakL, c.envRelease) << 1;
	}
	if (peakR > params.envStateR) {
		params.envStateR += multiply_32x32_rshift32(peakR - params.envStateR, c.envAttack) << 1;
	}
	else {
		params.envStateR -= multiply_32x32_rshift32(params.envStateR - peakR, c.envRelease) << 1;
	}

	// Derivative = change over this buffer
	q31_t rawDerivL = params.envStateL - envStartL;
	q31_t rawDerivR = params.envStateR - envStartR;
	// Smooth the derivative
	params.envDerivStateL += multiply_32x32_rshift32(rawDerivL - params.envDerivStateL, c.envAttack) << 1;
	params.envDerivStateR += multiply_32x32_rshift32(rawDerivR - params.envDerivStateR, c.envAttack) << 1;

	TypeMix mix = getTypeMix(params.type);

	// Lazy-allocate comb buffers only when comb effect is first used
	// This saves 12KB per Sound when comb isn't needed
	if (mix.comb > 0.01f && !params.hasCombBuffers()) {
		params.allocateCombBuffers(); // May fail silently - comb just won't process
	}

	// Backup retrigger: reset LFO phase when voice count goes 0→N
	// (lastVoiceCount is reset to 0 in wontBeRenderedForAWhile when no voices)
	// Initial phase from phi triangle for varied attack character
	if (params.lastVoiceCount == 0 && voiceCount > 0) {
		float effectiveModPhase = params.modPhaseOffset + params.gammaPhase;
		params.lfoPhase = getLfoInitialPhaseFromMod(params.mod, effectiveModPhase);
	}
	params.lastVoiceCount = voiceCount;

	// Scale stereo offset by LFO speed - slow LFOs get less stereo to avoid feeling stuck asymmetric
	// At 0.5 Hz: 10% stereo, at 2 Hz: 40% stereo, at 5 Hz+: 100% stereo
	float rateScaleFactor;
	if (c.rateSyncLevel > 0) {
		// Synced mode: faster subdivisions (higher levels) get more stereo
		// Level 1 (whole) → 0.1, Level 5 (16th) → 0.5, Level 9 (256th) → 1.0
		rateScaleFactor = std::clamp(static_cast<float>(c.rateSyncLevel) / 9.0f, 0.1f, 1.0f);
	}
	else {
		// Free-running: scale by rate in Hz (0.1-20 Hz range)
		// 0.5 Hz → 0.1, 2 Hz → 0.4, 5 Hz → 1.0
		rateScaleFactor = std::clamp(c.rateValue * 0.2f, 0.1f, 1.0f);
	}
	uint32_t stereoPhaseOffset = static_cast<uint32_t>(static_cast<float>(c.stereoPhaseOffsetRaw) * rateScaleFactor);

	// Macro scales overall modulation depth (bipolar -> 0.25-1.0 range)
	// At macro=0 (center), depth is 0.625. At max, depth is 1.0. At min, depth is 0.25.
	float macroScale = static_cast<float>(macro) * kOneOverQ31Max;
	macroScale = 0.625f + macroScale * 0.375f; // Ensure always audible

	// Pre-compute depth influence coefficients for simplified per-sample math
	// finalScale = scaleBase + scaleEnvCoeff * envNorm + scaleDerivCoeff * derivNorm
	// (instead of: depthMod = 1 + 2*(envDepth*env + derivDepth*deriv), then finalScale = macro*depthMod)
	float scaleBase = macroScale;
	float scaleEnvCoeff = macroScale * 2.0f * c.envDepthInfluence;
	float scaleDerivCoeff = macroScale * 2.0f * c.envDerivDepthInfluence;

	// Pre-compute phase influence coefficients
	float phaseEnvCoeff = macroScale * c.envPhaseInfluence;
	float phaseDerivCoeff = macroScale * c.envDerivPhaseInfluence;

	// === Compute buffer-level modulation targets from current envelope state ===
	float envNormL = static_cast<float>(params.envStateL) * kOneOverQ31Max;
	float envNormR = static_cast<float>(params.envStateR) * kOneOverQ31Max;
	float derivNormL = std::clamp(static_cast<float>(params.envDerivStateL) * kDerivNormScale, -1.0f, 1.0f);
	float derivNormR = std::clamp(static_cast<float>(params.envDerivStateR) * kDerivNormScale, -1.0f, 1.0f);

	// Target values for this buffer
	float targetScaleL = std::clamp(scaleBase + scaleEnvCoeff * envNormL + scaleDerivCoeff * derivNormL, -1.0f, 1.0f);
	float targetScaleR = std::clamp(scaleBase + scaleEnvCoeff * envNormR + scaleDerivCoeff * derivNormR, -1.0f, 1.0f);
	float targetPhasePushL = phaseEnvCoeff * envNormL + phaseDerivCoeff * derivNormL;
	float targetPhasePushR = phaseEnvCoeff * envNormR + phaseDerivCoeff * derivNormR;
	float stereoScaleFactor = (std::abs(targetScaleL) + std::abs(targetScaleR)) * 0.5f;

	// Convert targets to fixed-point
	q31_t targetScaleQL = static_cast<q31_t>(targetScaleL * kQ31MaxFloat);
	q31_t targetScaleQR = static_cast<q31_t>(targetScaleR * kQ31MaxFloat);

	// Phase push conversion: wrap to [0,1) range for safe uint32_t cast
	// Fast wrap using integer truncation instead of std::floor (~30 cycles saved)
	// Integer truncation goes toward zero, so adjust for negatives to get floor behavior
	auto fastFractional = [](float x) -> float {
		int32_t intPart = static_cast<int32_t>(x);
		if (x < 0.0f && x != static_cast<float>(intPart)) {
			intPart--; // Floor for negatives
		}
		return x - static_cast<float>(intPart);
	};
	float wrappedPhasePushL = fastFractional(targetPhasePushL);
	float wrappedPhasePushR = fastFractional(targetPhasePushR);
	uint32_t targetPhasePushUL = static_cast<uint32_t>(wrappedPhasePushL * kPhaseMaxFloat);
	uint32_t targetPhasePushUR = static_cast<uint32_t>(wrappedPhasePushR * kPhaseMaxFloat);
	uint32_t targetStereoOffset = static_cast<uint32_t>(static_cast<float>(stereoPhaseOffset) * stereoScaleFactor);

	// Smooth toward targets (one-pole filter at buffer rate, ~12ms transition)
	params.smoothedScaleL += multiply_32x32_rshift32(targetScaleQL - params.smoothedScaleL, kModSmoothCoeffQ) << 1;
	params.smoothedScaleR += multiply_32x32_rshift32(targetScaleQR - params.smoothedScaleR, kModSmoothCoeffQ) << 1;
	// Phase push smoothing: use signed arithmetic for proper interpolation
	int32_t phaseDiffL = static_cast<int32_t>(targetPhasePushUL - params.smoothedPhasePushL);
	int32_t phaseDiffR = static_cast<int32_t>(targetPhasePushUR - params.smoothedPhasePushR);
	params.smoothedPhasePushL += static_cast<uint32_t>(multiply_32x32_rshift32(phaseDiffL, kModSmoothCoeffQ) << 1);
	params.smoothedPhasePushR += static_cast<uint32_t>(multiply_32x32_rshift32(phaseDiffR, kModSmoothCoeffQ) << 1);
	int32_t stereoDiff = static_cast<int32_t>(targetStereoOffset - params.smoothedStereoOffset);
	params.smoothedStereoOffset += static_cast<uint32_t>(multiply_32x32_rshift32(stereoDiff, kModSmoothCoeffQ) << 1);

	// Use smoothed values for the loop
	q31_t scaleQL = params.smoothedScaleL;
	q31_t scaleQR = params.smoothedScaleR;
	uint32_t phasePushL = params.smoothedPhasePushL;
	uint32_t phasePushR = params.smoothedPhasePushR;
	uint32_t scaledStereoOffset = params.smoothedStereoOffset;

	// Lambda for unipolar triangle (used in multiple places)
	auto makeUnipolarTriangle = [](uint32_t phase) -> q31_t {
		if (phase < 0x80000000) {
			return static_cast<q31_t>(phase >> 0); // Rising: 0 to ONE_Q31
		}
		else {
			return static_cast<q31_t>(~phase); // Falling: ONE_Q31 to 0
		}
	};

	// === Buffer-level filter mix calculation (hoisted from per-sample loop) ===
	// Compute target mix weights, then smooth toward them to avoid clicks
	q31_t targetLowMixQ, targetBandMixQ, targetHighMixQ;
	if (c.useStaticFilterMix) {
		targetLowMixQ = c.filterMixLowQ;
		targetBandMixQ = c.filterMixBandQ;
		targetHighMixQ = c.filterMixHighQ;
	}
	else {
		// Use current LFO phase for mix calculation (will drift slightly over buffer - acceptable)
		uint32_t lpPhase = params.lfoPhase + c.lpPhaseOffsetU32;
		uint32_t bpPhase = params.lfoPhase + c.bpPhaseOffsetU32;
		uint32_t hpPhase = params.lfoPhase + c.hpPhaseOffsetU32;
		q31_t lpLfo = makeUnipolarTriangle(lpPhase);
		q31_t bpLfo = makeUnipolarTriangle(bpPhase);
		q31_t hpLfo = makeUnipolarTriangle(hpPhase);
		// Filter mix = base + (base * response * lfo) - use saturating add to prevent overflow
		q31_t lpMod = multiply_32x32_rshift32(multiply_32x32_rshift32(c.filterMixLowQ, c.lpResponseQ) << 1, lpLfo) << 1;
		q31_t bpMod = multiply_32x32_rshift32(multiply_32x32_rshift32(c.filterMixBandQ, c.bpResponseQ) << 1, bpLfo)
		              << 1;
		q31_t hpMod = multiply_32x32_rshift32(multiply_32x32_rshift32(c.filterMixHighQ, c.hpResponseQ) << 1, hpLfo)
		              << 1;
		targetLowMixQ = add_saturate(c.filterMixLowQ, lpMod);
		targetBandMixQ = add_saturate(c.filterMixBandQ, bpMod);
		targetHighMixQ = add_saturate(c.filterMixHighQ, hpMod);
	}

	// Smooth filter mix toward targets (same ~12ms transition as other modulations)
	params.smoothedLowMixQ += multiply_32x32_rshift32(targetLowMixQ - params.smoothedLowMixQ, kModSmoothCoeffQ) << 1;
	params.smoothedBandMixQ += multiply_32x32_rshift32(targetBandMixQ - params.smoothedBandMixQ, kModSmoothCoeffQ) << 1;
	params.smoothedHighMixQ += multiply_32x32_rshift32(targetHighMixQ - params.smoothedHighMixQ, kModSmoothCoeffQ) << 1;

	// Use smoothed values in loop
	q31_t lowMixQ = params.smoothedLowMixQ;
	q31_t bandMixQ = params.smoothedBandMixQ;
	q31_t highMixQ = params.smoothedHighMixQ;

	// === Buffer-rate LFO computation ===
	// Always do fresh wavetable eval each buffer for now (simpler, no drift issues)
	// TODO: Re-add caching optimization once glitches are resolved

	const size_t bufferSize = buffer.size();
	const uint32_t startPhase = params.lfoPhase;
	const uint32_t endPhase = startPhase + c.lfoInc * bufferSize;

	LfoIncremental lfoL, lfoR, combLfoL, combLfoR, tremLfoL, tremLfoR;

	// Always do full wavetable eval (no caching for now)
	uint32_t lfoPhaseL = startPhase + phasePushL;
	uint32_t lfoPhaseR = startPhase + scaledStereoOffset + phasePushR;
	lfoL = evalLfoIncremental(lfoPhaseL, c.lfoInc, bufferSize, c.wavetable);
	lfoR = evalLfoIncremental(lfoPhaseR, c.lfoInc, bufferSize, c.wavetable);

	uint32_t combPhaseL = startPhase + c.combPhaseOffsetU32;
	uint32_t combPhaseR = combPhaseL + scaledStereoOffset;
	combLfoL = evalLfoIncremental(combPhaseL, c.lfoInc, bufferSize, c.wavetable);
	combLfoR = evalLfoIncremental(combPhaseR, c.lfoInc, bufferSize, c.wavetable);

	uint32_t tremPhaseL = startPhase + c.tremPhaseOffset;
	uint32_t tremPhaseR = tremPhaseL + scaledStereoOffset;
	tremLfoL = evalLfoIncremental(tremPhaseL, c.lfoInc, bufferSize, c.wavetable);
	tremLfoR = evalLfoIncremental(tremPhaseR, c.lfoInc, bufferSize, c.wavetable);

	// Update phase for next buffer
	params.lfoPhase = endPhase;

	// === Pitch tracking (cached - only recompute when noteCode changes) ===
	// Scale filter cutoff and comb delay based on played note frequency
	if (noteCode != params.prevNoteCode) {
		params.prevNoteCode = noteCode;
		if (noteCode >= 0 && noteCode < 128) {
			// Filter cutoff: add pitch-relative offset (octave up → +0.125 of range)
			float pitchOctaves = (static_cast<float>(noteCode) - 60.0f) / 12.0f;
			float cutoffOffset = pitchOctaves * 0.125f;
			params.cachedPitchCutoffOffset = static_cast<q31_t>(cutoffOffset * kQ31MaxFloat);
			// Comb delay ratio: inverse pitch (higher note = smaller ratio)
			params.cachedPitchRatio = std::clamp(fastPow2(-pitchOctaves), 0.25f, 4.0f);
		}
		else {
			params.cachedPitchCutoffOffset = 0;
			params.cachedPitchRatio = 1.0f;
		}
	}
	// Apply cached pitch ratio to current base delay (base delay changes with flavor)
	q31_t pitchCutoffOffset = params.cachedPitchCutoffOffset;
	int32_t pitchCombBaseDelay16 =
	    static_cast<int32_t>(static_cast<float>(c.combBaseDelay16) * params.cachedPitchRatio);
	pitchCombBaseDelay16 = std::clamp(pitchCombBaseDelay16, c.combMinDelay16, c.combMaxDelay16);

#if ENABLE_FX_BENCHMARK
	if (doBench) {
		benchSetup.stop();
		benchLoop.start();
	}
#endif

	for (auto& sample : buffer) {
		// Use q31 LFO values directly (computed from incremental evaluation)
		// Clamp to valid unipolar range to handle segment boundary drift
		q31_t lfoRawL = std::clamp(lfoL.value, q31_t{0}, ONE_Q31);
		q31_t lfoRawR = std::clamp(lfoR.value, q31_t{0}, ONE_Q31);
		q31_t combLfoValL = std::clamp(combLfoL.value, q31_t{0}, ONE_Q31);
		q31_t combLfoValR = std::clamp(combLfoR.value, q31_t{0}, ONE_Q31);
		q31_t tremLfoValL = std::clamp(tremLfoL.value, q31_t{0}, ONE_Q31);
		q31_t tremLfoValR = std::clamp(tremLfoR.value, q31_t{0}, ONE_Q31);

		q31_t outL = sample.l;
		q31_t outR = sample.r;

		// SVF Filter (auto-wah) - phi triangle bank controls LP/BP/HP mix
		if (mix.filter > 0.01f) {
			// Apply envelope scale to raw LFO (scale can be negative for inverted wah)
			q31_t lfoL = multiply_32x32_rshift32(lfoRawL, scaleQL) << 1;
			q31_t lfoR = multiply_32x32_rshift32(lfoRawR, scaleQR) << 1;

			// Compute filter cutoff: base + pitch offset + LFO contribution
			// NOTE: Use saturating addition to prevent overflow wrap-around!
			// With negative scale, LFO can go negative → inverted wah (filter closes on attack)
			// Pitch tracking shifts cutoff center to track played note
			constexpr q31_t kCutoffMin = 0x08000000;
			constexpr q31_t kCutoffMax = 0x78000000;
			q31_t lfoContribL = multiply_32x32_rshift32(lfoL, c.filterModDepth) << 1;
			q31_t lfoContribR = multiply_32x32_rshift32(lfoR, c.filterModDepth) << 1;
			q31_t basePlusPitch = add_saturate(c.filterCutoffBase, pitchCutoffOffset);
			q31_t cutoffL = add_saturate(add_saturate(kCutoffMin, basePlusPitch), lfoContribL);
			q31_t cutoffR = add_saturate(add_saturate(kCutoffMin, basePlusPitch), lfoContribR);

			// Clamp to valid range (saturating add handles overflow, clamp handles range)
			cutoffL = std::clamp(cutoffL, kCutoffMin, kCutoffMax);
			cutoffR = std::clamp(cutoffR, kCutoffMin, kCutoffMax);

			// SVF processing (simplified 2-pole)
			// f = cutoff coefficient (higher = higher frequency)
			q31_t fL = cutoffL >> 2; // Scale for stability
			q31_t fR = cutoffR >> 2;
			q31_t q = ONE_Q31 - c.filterResonance;

			// Left channel
			q31_t highL = outL - params.svfLowL - multiply_32x32_rshift32(params.svfBandL, q);
			params.svfBandL += multiply_32x32_rshift32(highL, fL) << 1;
			params.svfLowL += multiply_32x32_rshift32(params.svfBandL, fL) << 1;

			// Right channel
			q31_t highR = outR - params.svfLowR - multiply_32x32_rshift32(params.svfBandR, q);
			params.svfBandR += multiply_32x32_rshift32(highR, fR) << 1;
			params.svfLowR += multiply_32x32_rshift32(params.svfBandR, fR) << 1;

			// Blend LP/BP/HP using buffer-level mix weights (hoisted above loop)
			q31_t filteredL = multiply_32x32_rshift32(params.svfLowL, lowMixQ)
			                  + multiply_32x32_rshift32(params.svfBandL, bandMixQ)
			                  + multiply_32x32_rshift32(highL, highMixQ);
			q31_t filteredR = multiply_32x32_rshift32(params.svfLowR, lowMixQ)
			                  + multiply_32x32_rshift32(params.svfBandR, bandMixQ)
			                  + multiply_32x32_rshift32(highR, highMixQ);

			// Scale up (rshift32 reduces amplitude, weights sum to 1.0)
			outL = filteredL << 1;
			outR = filteredR << 1;
		}

		// Comb filter (flanger effect) - feedback comb for pronounced resonance
		// All delay math in 16.16 fixed-point (no float conversions)
		// LFO values computed at stride boundaries above
		// Pitch tracking: pitchCombBaseDelay16 scales base delay to track played note
		// User's relative tuning (e.g., "F+1 octave") transposes with the note
		// LAZY ALLOCATION: Buffers only allocated when comb effect is first used
		if (mix.comb > 0.01f && params.hasCombBuffers()) {
			constexpr int32_t kCombSize = static_cast<int32_t>(AutomodulatorParams::kCombBufferSize);

			// Pure 32-bit delay calculation (no 64-bit multiply!)
			// lfo >> 15 gives 16-bit (0-0xFFFF), × modRangeSamples (9-bit) = 25 bits, result is 16.16
			// Use pitch-tracked base delay for note-relative comb tuning
			int32_t lfo16L = combLfoValL >> 15;
			int32_t lfo16R = combLfoValR >> 15;
			int32_t delay16L = pitchCombBaseDelay16 + lfo16L * c.combModRangeSamples;
			int32_t delay16R = pitchCombBaseDelay16 + lfo16R * c.combModRangeSamples;

			// Clamp to valid range
			delay16L = std::clamp(delay16L, c.combMinDelay16, c.combMaxDelay16);
			delay16R = std::clamp(delay16R, c.combMinDelay16, c.combMaxDelay16);

			// Extract integer (samples) and fractional (16-bit) parts
			int32_t delayIntL = delay16L >> 16;
			int32_t delayIntR = delay16R >> 16;
			// Convert 16-bit frac to q31 for interpolation: (frac16 << 15) gives 0 to 0x7FFF8000
			q31_t fracQL = (delay16L & 0xFFFF) << 15;
			q31_t fracQR = (delay16R & 0xFFFF) << 15;

			// Linear interpolation for smooth delay modulation
			int32_t combIdx = static_cast<int32_t>(params.combIdx);

			// Read two adjacent samples and interpolate
			// Use conditional subtraction instead of modulo (cheaper on ARM)
			int32_t readIdx0L = combIdx - delayIntL + kCombSize;
			int32_t readIdx1L = combIdx - delayIntL - 1 + kCombSize;
			int32_t readIdx0R = combIdx - delayIntR + kCombSize;
			int32_t readIdx1R = combIdx - delayIntR - 1 + kCombSize;
			if (readIdx0L >= kCombSize) {
				readIdx0L -= kCombSize;
			}
			if (readIdx1L >= kCombSize) {
				readIdx1L -= kCombSize;
			}
			if (readIdx0R >= kCombSize) {
				readIdx0R -= kCombSize;
			}
			if (readIdx1R >= kCombSize) {
				readIdx1R -= kCombSize;
			}

			q31_t sample0L = params.combBufferL[readIdx0L];
			q31_t sample1L = params.combBufferL[readIdx1L];
			q31_t sample0R = params.combBufferR[readIdx0R];
			q31_t sample1R = params.combBufferR[readIdx1R];

			// Linear interpolation: out = sample0 + frac * (sample1 - sample0)
			q31_t combOutL = sample0L + (multiply_32x32_rshift32(sample1L - sample0L, fracQL) << 1);
			q31_t combOutR = sample0R + (multiply_32x32_rshift32(sample1R - sample0R, fracQR) << 1);

			// Feedback comb: write input + scaled delayed back to buffer
			q31_t feedbackL = multiply_32x32_rshift32(combOutL, c.combFeedback) << 1;
			q31_t feedbackR = multiply_32x32_rshift32(combOutR, c.combFeedback) << 1;
			params.combBufferL[params.combIdx] = outL + feedbackL;
			params.combBufferR[params.combIdx] = outR + feedbackR;
			if (++params.combIdx >= AutomodulatorParams::kCombBufferSize) {
				params.combIdx = 0;
			}

			// Mix comb output with dry signal
			outL = outL + (multiply_32x32_rshift32(combOutL, c.combMixQ) << 1);
			outR = outR + (multiply_32x32_rshift32(combOutR, c.combMixQ) << 1);
		}

		// Tremolo (VCA) - amplitude modulation with phase offset from type
		// LFO values computed at stride boundaries above
		if (mix.tremolo > 0.01f) {
			// Scale to (1-depth) to 1.0 range: e.g. depth=0.8 → volume goes 0.2-1.0
			// Use saturating add for safety (prevents overflow if LFO drifted out of range)
			q31_t tremModL = multiply_32x32_rshift32(tremLfoValL, c.tremoloDepthQ) << 1;
			q31_t tremModR = multiply_32x32_rshift32(tremLfoValR, c.tremoloDepthQ) << 1;
			q31_t tremoloL = add_saturate(c.tremoloBaseLevel, tremModL);
			q31_t tremoloR = add_saturate(c.tremoloBaseLevel, tremModR);

			outL = multiply_32x32_rshift32(outL, tremoloL) << 1;
			outR = multiply_32x32_rshift32(outR, tremoloR) << 1;
		}

		sample.l = outL;
		sample.r = outR;

		// Increment LFO values using pre-computed segment slopes (just integer addition!)
		lfoL.value += lfoL.delta;
		lfoR.value += lfoR.delta;
		combLfoL.value += combLfoL.delta;
		combLfoR.value += combLfoR.delta;
		tremLfoL.value += tremLfoL.delta;
		tremLfoR.value += tremLfoR.delta;
	}

#if ENABLE_FX_BENCHMARK
	if (doBench) {
		benchLoop.stop();
		benchTotal.stop();
	}
#endif
}

} // namespace deluge::dsp
