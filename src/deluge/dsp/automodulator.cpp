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

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define USE_NEON_SVF 1
#else
#define USE_NEON_SVF 0
#endif

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

float getCombMonoCollapseFromFlavor(uint16_t flavor, float phaseOffset) {
	// Normalize flavor to [0,1] and add phase offset
	double phase = static_cast<double>(flavor) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output): 0 = full stereo, 1 = mono
	// 50% duty cycle means 50% deadzone at 0 (full stereo)
	return phi::evalTriangle(phase, 1.0f, kCombMonoCollapseTriangle);
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
		// Map bipolar (-1,+1) to positive delta (0.2 to 1.0)
		// TESTING: was 0.1f + abs*0.9f - higher minimum limits max slope to reduce aliasing
		deltas[i] = 0.2f + std::abs(raw[i]) * 0.8f;
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
	// Find actual min/max of waypoints (not fixed endpoints - those are always 0)
	float minAmp = bank.amplitude[0];
	float maxAmp = bank.amplitude[0];
	for (int i = 1; i < 4; i++) {
		minAmp = std::min(minAmp, bank.amplitude[i]);
		maxAmp = std::max(maxAmp, bank.amplitude[i]);
	}
	// Include fixed endpoints at 0 in the range
	minAmp = std::min(minAmp, 0.0f);
	maxAmp = std::max(maxAmp, 0.0f);

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

	// Segment start amplitudes as bipolar q31 [-ONE_Q31, ONE_Q31]
	// Clamp to prevent overflow from floating point edge cases
	for (int i = 0; i < 5; i++) {
		float clamped = std::clamp(bank.segAmp[i], -1.0f, 1.0f);
		bank.segAmpQ[i] = static_cast<q31_t>(clamped * 2147483647.0f);
	}

	// Slopes in q31 per uint32-phase-unit (bipolar)
	// For: value = segAmpQ + multiply_32x32_rshift32(phaseOffset, segSlopeQ) << 1
	for (int i = 0; i < 5; i++) {
		bank.segSlopeQ[i] = static_cast<q31_t>(bank.segSlope[i] * 2147483647.0f);
	}

	// === Inverse segment widths for IIR-style stepping ===
	// Used to compute per-sample step without division:
	// step = ampDelta * phaseInc * invSegWidth (scaled appropriately)
	// invSegWidth is stored such that: (phaseInc * invSegWidth) >> 32 gives fraction of segment per sample
	for (int i = 0; i < 5; i++) {
		uint32_t segWidth =
		    (i < 4) ? (bank.segStartU32[i + 1] - bank.segStartU32[i]) : (0xFFFFFFFF - bank.segStartU32[4]);
		if (segWidth > 0x1000) { // Minimum width to avoid overflow
			// invSegWidth = 2^32 / segWidth (approximately)
			// Using 64-bit division for precision
			bank.invSegWidthQ[i] = static_cast<uint32_t>((0xFFFFFFFFULL << 16) / segWidth);
		}
		else {
			bank.invSegWidthQ[i] = 0x7FFFFFFF; // Max safe value for very narrow segments
		}
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

	// Evaluate wavetable - returns bipolar [-1, +1]
	float value = evalLfoWavetable(t, bank);

	// Convert to bipolar q31 [-ONE_Q31, ONE_Q31]
	return static_cast<q31_t>(value * 2147483647.0f);
}

/// Find which segment a phase falls into (pure integer)
[[gnu::always_inline]] inline int8_t findSegment(uint32_t phaseU32, const LfoWaypointBank& bank) {
	if (phaseU32 <= bank.phaseU32[0]) {
		return 0;
	}
	if (phaseU32 <= bank.phaseU32[1]) {
		return 1;
	}
	if (phaseU32 <= bank.phaseU32[2]) {
		return 2;
	}
	if (phaseU32 <= bank.phaseU32[3]) {
		return 3;
	}
	return 4;
}

/// Compute step for a segment (helper for updateLfoAccum)
[[gnu::always_inline]] inline q31_t computeSegmentStep(int8_t seg, uint32_t phaseInc, const LfoWaypointBank& bank) {
	q31_t segEnd = (seg < 4) ? bank.segAmpQ[seg + 1] : bank.segAmpQ[0];

	// Compute ampDelta in 64-bit to avoid overflow for large bipolar swings
	int64_t ampDelta64 = static_cast<int64_t>(segEnd) - static_cast<int64_t>(bank.segAmpQ[seg]);

	// step = ampDelta * phaseInc / segWidth
	int64_t partial = (ampDelta64 * static_cast<int64_t>(phaseInc)) >> 16;
	int64_t step64 = (partial * static_cast<int64_t>(bank.invSegWidthQ[seg])) >> 32;
	return static_cast<q31_t>(step64);
}

/// Compute samples remaining until next segment boundary
[[gnu::always_inline]] inline uint32_t samplesUntilSegmentEnd(int8_t seg, uint32_t phaseU32, uint32_t phaseInc,
                                                              const LfoWaypointBank& bank) {
	// Get the end phase of current segment
	uint32_t segEndPhase = (seg < 4) ? bank.segStartU32[seg + 1] : 0xFFFFFFFF;
	uint32_t phaseRemaining = segEndPhase - phaseU32;

	// Compute samples = phaseRemaining / phaseInc (with ceiling)
	if (phaseInc == 0) {
		return UINT32_MAX; // Infinite samples if LFO stopped
	}
	return (phaseRemaining + phaseInc - 1) / phaseInc;
}

/// Update LFO using pure accumulation with segment-aware stepping
/// Returns value, delta, and samples until next segment boundary
/// @param state LFO state: value=accumulated, intermediate=step, segment=current seg
/// @param phaseU32 Current phase position (for segment detection)
/// @param phaseInc Phase increment per sample
/// @param bank Wavetable configuration
/// @param samplesRemaining [out] Samples until next segment (or UINT32_MAX if stopped)
/// @return LfoIncremental with current value and per-sample step
LfoIncremental updateLfoAccum(LfoIirState& state, uint32_t phaseU32, uint32_t phaseInc, const LfoWaypointBank& bank,
                              uint32_t& samplesRemaining) {
	int8_t seg = findSegment(phaseU32, bank);

	// On segment change, reset value to actual wavetable position and compute new step
	if (seg != state.segment) {
		state.segment = seg;
		// Reset to actual wavetable value at current phase (not segment start!)
		state.value = evalLfoWavetableQ31(phaseU32, bank);
		state.intermediate = computeSegmentStep(seg, phaseInc, bank);
	}

	// Compute samples until we exit this segment
	samplesRemaining = samplesUntilSegmentEnd(seg, phaseU32, phaseInc, bank);

	// Return current accumulated value and step
	return {state.value, state.intermediate};
}

/// Legacy version without samplesRemaining (for compatibility)
LfoIncremental updateLfoAccum(LfoIirState& state, uint32_t phaseU32, uint32_t phaseInc, const LfoWaypointBank& bank) {
	uint32_t unused;
	return updateLfoAccum(state, phaseU32, phaseInc, bank, unused);
}

/// Precompute per-segment step and sample count for current rate
/// Call when rate or wavetable changes (dirty flag check)
void computeLfoSteppingParams(AutomodDspState& s, uint32_t phaseInc, const LfoWaypointBank& bank) {
	s.cachedPhaseInc = phaseInc;

	for (int seg = 0; seg < 5; seg++) {
		// Step per sample for this segment
		s.stepPerSegment[seg] = computeSegmentStep(seg, phaseInc, bank);

		// Samples to traverse entire segment
		uint32_t segWidth =
		    (seg < 4) ? (bank.segStartU32[seg + 1] - bank.segStartU32[seg]) : (0xFFFFFFFF - bank.segStartU32[4]);
		if (phaseInc > 0) {
			s.samplesPerSegment[seg] = segWidth / phaseInc;
		}
		else {
			s.samplesPerSegment[seg] = UINT32_MAX;
		}
	}
}

/// Initialize LFO state from current phase for accumulator mode
void initLfoIir(LfoIirState& state, uint32_t phaseU32, uint32_t phaseInc, const LfoWaypointBank& bank) {
	int8_t seg = findSegment(phaseU32, bank);
	state.segment = seg;
	// Set initial accumulated value from wavetable
	state.value = evalLfoWavetableQ31(phaseU32, bank);
	// Compute initial step for this segment
	q31_t segEnd = (seg < 4) ? bank.segAmpQ[seg + 1] : bank.segAmpQ[0];

	// Compute ampDelta in 64-bit to avoid overflow for large bipolar swings
	int64_t ampDelta64 = static_cast<int64_t>(segEnd) - static_cast<int64_t>(bank.segAmpQ[seg]);

	// Split multiplication to avoid 64-bit overflow
	int64_t partial = (ampDelta64 * static_cast<int64_t>(phaseInc)) >> 16;
	int64_t step64 = (partial * static_cast<int64_t>(bank.invSegWidthQ[seg])) >> 32;
	state.intermediate = static_cast<q31_t>(step64);
	state.target = segEnd;
}

/// Pure integer LFO evaluation - no float operations in fast path
/// Uses pre-computed integer segment data for efficiency
/// @param startPhaseU32 Starting phase as uint32 (full 32-bit range)
/// @param phaseInc Phase increment per sample
/// @param bufferSize Number of samples in buffer
/// @param bank Wavetable with pre-computed integer fields
/// @return LfoIncremental with start value and per-sample delta
LfoIncremental evalLfoIncremental(uint32_t startPhaseU32, uint32_t phaseInc, size_t bufferSize,
                                  const LfoWaypointBank& bank) {
	// Find segment using integer comparisons (fast path)
	int8_t seg = findSegment(startPhaseU32, bank);

	// Compute value at start phase using integer math
	// Scale phaseOffset to 31 bits to avoid overflow when cast to signed
	// (phaseOffset >> 1) fits in signed q31, then compensate with extra << 1 at end
	uint32_t phaseOffset = startPhaseU32 - bank.segStartU32[seg];
	q31_t scaledOffset = static_cast<q31_t>(phaseOffset >> 1);
	q31_t valueQ = bank.segAmpQ[seg] + (multiply_32x32_rshift32(scaledOffset, bank.segSlopeQ[seg]) << 2);

	// Check for segment crossing or phase wrap
	uint32_t endPhaseU32 = startPhaseU32 + phaseInc * static_cast<uint32_t>(bufferSize);
	bool phaseWrap = (endPhaseU32 < startPhaseU32);
	int8_t endSeg = findSegment(endPhaseU32, bank);

	q31_t deltaQ;
	if (phaseWrap || seg != endSeg) {
		// Segment crossing: compute end value and derive delta
		uint32_t endOffset = endPhaseU32 - bank.segStartU32[endSeg];
		q31_t scaledEndOffset = static_cast<q31_t>(endOffset >> 1);
		q31_t endValueQ =
		    bank.segAmpQ[endSeg] + (multiply_32x32_rshift32(scaledEndOffset, bank.segSlopeQ[endSeg]) << 2);
		// delta = (end - start) / bufferSize, using >> 7 for ~128
		deltaQ = (endValueQ - valueQ) >> 7;
	}
	else {
		// Same segment: use pre-computed slope directly
		// delta = slope * phaseInc (per sample)
		// phaseInc is small enough to fit in signed range
		deltaQ = multiply_32x32_rshift32(static_cast<q31_t>(phaseInc), bank.segSlopeQ[seg]) << 1;
	}

	return {valueQ, deltaQ};
}

// ============================================================================
// Cache update function
// ============================================================================

void updateAutomodPhiCache(AutomodulatorParams& params, uint32_t timePerTickInverse) {
	// Cache must be allocated before calling this function (ensureStateAllocated in processAutomodulator)
	AutomodPhiCache& c = *params.cache;

	// Compute effective phases (one calculation per source)
	double modPhase = static_cast<double>(params.mod) / 1023.0 + params.modPhaseOffset + params.gammaPhase;
	double flavorPhase = static_cast<double>(params.flavor) / 1023.0 + params.flavorPhaseOffset + params.gammaPhase;
	double typePhase = static_cast<double>(params.type) / 1023.0 + params.typePhaseOffset + params.gammaPhase;

	// === Batch evaluate mod-derived scalar params ===
	// [0]=stereoOffset, [1-4]=env influences, [5]=envValue, [6-7]=springFreq/Damp, [8-9]=tremSpringFreq/Damp
	auto modScalars = phi::evalTriangleBank<10>(modPhase, 1.0f, kModScalarBank);
	c.stereoPhaseOffsetRaw = modScalars[0];
	// Store env influences as q31 for integer-only per-buffer math
	c.envDepthInfluenceQ = static_cast<q31_t>(modScalars[1] * kQ31MaxFloat);
	c.envPhaseInfluenceQ = static_cast<q31_t>(modScalars[2] * kQ31MaxFloat);
	c.envDerivDepthInfluenceQ = static_cast<q31_t>(modScalars[3] * kQ31MaxFloat);
	c.envDerivPhaseInfluenceQ = static_cast<q31_t>(modScalars[4] * kQ31MaxFloat);
	c.envValueInfluenceQ = static_cast<q31_t>(modScalars[5] * kQ31MaxFloat);

	// Spring coefficients computed later after LFO rate is known (for rate-proportional scaling)
	float springModFreq = modScalars[6];     // Save for later
	float springModDamp = modScalars[7];     // Save for later
	float tremSpringModFreq = modScalars[8]; // Tremolo spring freq
	float tremSpringModDamp = modScalars[9]; // Tremolo spring damping

	// LFO rate and wavetable need special handling (multi-zone logic)
	float effectiveModPhase = params.modPhaseOffset + params.gammaPhase;

	// Rate control now uses lfoMode enum for stop/once/retrig/free
	LfoRateResult rateResult;
	c.rateStopped = (params.lfoMode == AutomodLfoMode::STOP);
	c.rateOnce = (params.lfoMode == AutomodLfoMode::ONCE);

	// Reset Once mode state when switching TO Once mode
	if (c.rateOnce && params.dspState != nullptr && params.prevLfoMode != params.lfoMode) {
		params.dspState->onceStartPhase = params.dspState->lfoPhase;
		params.dspState->oneCycleComplete = false;
	}

	if (c.rateStopped) {
		// Stop mode: LFO frozen, Manual knob used directly
		rateResult.value = 0.0f;
		rateResult.syncLevel = 0;
		rateResult.slowShift = 0;
		rateResult.triplet = false;
	}
	else {
		// Direct rate control from Rate knob (rate 1+ maps to index 0+)
		if (params.rateSynced) {
			// Synced mode: use sync rate table (ordered by frequency, slow to fast)
			// Labels are honest: actual LFO cycle matches display
			// slowShift adds extra right-shift for ultra-slow rates (8/1, 4/1)
			static constexpr struct {
				uint8_t syncLevel;
				uint8_t slowShift;
				bool triplet;
			} kDspSyncRates[] = {
			    {1, 2, false}, // 8/1 (8 whole notes)
			    {1, 1, false}, // 4/1 (4 whole notes)
			    {1, 0, false}, // 2/1 (2 whole notes)
			    {2, 0, false}, // 1/1
			    {2, 0, true},  // 1/1T
			    {3, 0, false}, // 1/2
			    {3, 0, true},  // 1/2T
			    {4, 0, false}, // 1/4
			    {4, 0, true},  // 1/4T
			    {5, 0, false}, // 1/8
			    {5, 0, true},  // 1/8T
			    {6, 0, false}, // 1/16
			    {6, 0, true},  // 1/16T
			    {7, 0, false}, // 1/32
			    {7, 0, true},  // 1/32T
			    {8, 0, false}, // 1/64 (max speed)
			    {8, 0, true},  // 1/64T
			};
			constexpr int32_t kNumDspSyncRates = sizeof(kDspSyncRates) / sizeof(kDspSyncRates[0]);
			int32_t idx = std::clamp<int32_t>(static_cast<int32_t>(params.rate) - 1, 0, kNumDspSyncRates - 1);
			rateResult.syncLevel = kDspSyncRates[idx].syncLevel;
			rateResult.slowShift = kDspSyncRates[idx].slowShift;
			rateResult.triplet = kDspSyncRates[idx].triplet;
			rateResult.value = 1.0f; // Not used for synced
		}
		else {
			// Unsynced mode: log scale from 0.01Hz to 20Hz
			// Formula: hz = 0.01 * 2000^((rate-1)/127)
			// rate 1 = 0.01Hz (100s period), rate 128 = 20Hz (50ms period)
			float hz = 0.01f * powf(2000.0f, static_cast<float>(params.rate - 1) / 127.0f);
			rateResult.value = hz;
			rateResult.syncLevel = 0;
			rateResult.slowShift = 0;
			rateResult.triplet = false;
		}
	}
	c.rateValue = rateResult.value;
	c.rateSyncLevel = rateResult.syncLevel;
	c.rateTriplet = rateResult.triplet;

	c.wavetable = getLfoWaypointBank(params.mod, effectiveModPhase);
	c.lastSegmentPhaseU32 = static_cast<uint32_t>(c.wavetable.phase[3] * 4294967295.0f);

	// Invalidate cached stepping params so they're recomputed with new wavetable
	if (params.dspState) {
		params.dspState->cachedPhaseInc = 0;
	}

	// Compute LFO increment
	if (rateResult.syncLevel > 0) {
		if (timePerTickInverse > 0) {
			// Transport running: use tempo-synced rate
			// slowShift adds extra right-shift for ultra-slow rates (8/1, 4/1)
			c.lfoInc = timePerTickInverse >> (9 - rateResult.syncLevel + rateResult.slowShift);
			if (rateResult.triplet) {
				c.lfoInc = c.lfoInc * 3 / 2;
			}
		}
		else {
			// Transport stopped: use fallback Hz based on sync level
			// Level 1=1/1 (~0.5Hz), Level 9=1/256 (~128Hz at 120bpm baseline)
			// Use 120bpm as reference: level 1=0.5Hz, each level doubles
			// slowShift divides by 2^slowShift for ultra-slow rates
			float fallbackHz = 0.5f * static_cast<float>(1 << (rateResult.syncLevel - 1));
			fallbackHz /= static_cast<float>(1 << rateResult.slowShift);
			if (rateResult.triplet) {
				fallbackHz *= 1.5f;
			}
			c.lfoInc = static_cast<uint32_t>(fallbackHz * 97391.263f);
		}
	}
	else {
		c.lfoInc = static_cast<uint32_t>(rateResult.value * 97391.263f);
	}

	// IIR chase coefficient from LFO rate
	uint64_t rawCoeff = static_cast<uint64_t>(c.lfoInc) << 9;
	c.iirCoeff = static_cast<q31_t>(std::min(rawCoeff, static_cast<uint64_t>(0x40000000)));

	// Stereo offset uses full range (no rate-based scaling)

	// === Spring filter coefficients (buffer-rate 2nd-order LPF) ===
	// Now computed after LFO rate is known for rate-proportional scaling
	// Spring freq scales with LFO rate so bounce count per cycle stays consistent
	constexpr float kBufferRate = 44100.0f / 128.0f; // ~344 Hz
	constexpr float kDt = 1.0f / kBufferRate;
	constexpr float kPhaseToHz = 1.0f / 97391.263f; // lfoInc to Hz conversion

	// Convert LFO rate to Hz for spring scaling
	float lfoHz = static_cast<float>(c.lfoInc) * kPhaseToHz;
	lfoHz = std::max(lfoHz, 0.01f); // Floor to prevent division issues

	// Linear spring scaling for constant bounces/cycle regardless of LFO rate
	// At 1 Hz LFO with 12 bounces/cycle = 12 Hz spring; at 10 Hz LFO = 120 Hz spring
	float bouncesPerCycle = 0.5f + springModFreq * 11.5f; // 0.5 to 12 bounces/cycle
	float springFreqHz = bouncesPerCycle * lfoHz;

	// Clamp spring freq: max ~150 Hz (below buffer Nyquist of 172 Hz)
	constexpr float kMinSpringHz = 0.5f;
	constexpr float kMaxSpringHz = 150.0f;
	springFreqHz = std::clamp(springFreqHz, kMinSpringHz, kMaxSpringHz);

	// For discrete spring, correct coefficient for desired frequency:
	// k = 2 * (1 - cos(2π * f / sampleRate))
	constexpr float kTwoPi = 2.0f * 3.14159265f;
	float omega0 = kTwoPi * springFreqHz;

	// Bounciness from phi triangle: 0 in deadzone (critically damped), up to 0.9 (very bouncy)
	float bounciness = springModDamp * 0.9f;

	// Rate-dependent compensation: low rates need MORE bounciness for visible overshoot
	// Calibrated at 20 Hz: 0.75 power scaling
	constexpr float kCalibrationRate = 20.0f;
	float rateCompensation = std::pow(lfoHz / kCalibrationRate, 0.75f);
	rateCompensation = std::clamp(rateCompensation, 0.02f, 2.0f);

	// Frequency-dependent compensation: loose springs (low freq) need LESS bounciness to settle
	// High freq springs can handle more bounciness without instability
	float freqNorm = std::clamp((springFreqHz - kMinSpringHz) / (kMaxSpringHz - kMinSpringHz), 0.0f, 1.0f);
	float freqCompensation = 0.5f + (1.0f - freqNorm) * 3.5f; // 4x at low freq, 0.5x at high freq

	// Compensation scales bounciness, NOT the critical damping baseline
	// This preserves zeta=1.0 when springModDamp=0 (deadzone = critically damped)
	float compensatedBounciness = bounciness / (rateCompensation * freqCompensation);
	float zeta = std::max(1.0f - compensatedBounciness, 0.05f); // Floor at 0.05 for stability

	// Correct discrete-time spring coefficient for desired oscillation frequency
	// k = 2 * (1 - cos(2π * f / sampleRate)) gives exact frequency in discrete system
	float normalizedFreq = springFreqHz / kBufferRate; // f / sampleRate
	float springK = 2.0f * (1.0f - std::cos(kTwoPi * normalizedFreq));
	// Damping scales with sqrt(k) for consistent damping ratio behavior
	float springC = 2.0f * zeta * std::sqrt(springK);
	// Clamp to stability limit (k < 4 for stability)
	c.springOmega2Q = static_cast<q31_t>(std::min(springK, 3.9f) * (kQ31MaxFloat / 4.0f));
	c.springDampingCoeffQ = static_cast<q31_t>(std::min(springC, 3.9f) * (kQ31MaxFloat / 4.0f));

	// === Tremolo spring coefficients (same math, different phi triangles) ===
	// Smoother defaults than filter spring - tremolo benefits from less bounce
	float tremBouncesPerCycle = 0.5f + tremSpringModFreq * 11.5f;
	float tremSpringFreqHz = tremBouncesPerCycle * lfoHz;
	tremSpringFreqHz = std::clamp(tremSpringFreqHz, kMinSpringHz, kMaxSpringHz);

	float tremBounciness = tremSpringModDamp * 0.9f;
	float tremRateCompensation = std::pow(lfoHz / kCalibrationRate, 0.75f);
	tremRateCompensation = std::clamp(tremRateCompensation, 0.02f, 2.0f);
	float tremFreqNorm = std::clamp((tremSpringFreqHz - kMinSpringHz) / (kMaxSpringHz - kMinSpringHz), 0.0f, 1.0f);
	float tremFreqCompensation = 0.5f + (1.0f - tremFreqNorm) * 3.5f;
	float tremCompensatedBounciness = tremBounciness / (tremRateCompensation * tremFreqCompensation);
	float tremZeta = std::max(1.0f - tremCompensatedBounciness, 0.05f);

	float tremNormalizedFreq = tremSpringFreqHz / kBufferRate;
	float tremSpringK = 2.0f * (1.0f - std::cos(kTwoPi * tremNormalizedFreq));
	float tremSpringC = 2.0f * tremZeta * std::sqrt(tremSpringK);
	c.tremSpringOmega2Q = static_cast<q31_t>(std::min(tremSpringK, 3.9f) * (kQ31MaxFloat / 4.0f));
	c.tremSpringDampingCoeffQ = static_cast<q31_t>(std::min(tremSpringC, 3.9f) * (kQ31MaxFloat / 4.0f));

	// === Batch evaluate flavor-derived scalar params ===
	// [0]=cutoffBase, [1]=resonance, [2]=filterModDepth, [3]=attack, [4]=release,
	// [5]=combStaticOffset, [6]=combLfoDepth, [7]=combPhaseOffset, [8]=combMonoCollapse,
	// [9]=tremoloDepth, [10]=tremoloPhaseOffset, [11-13]=tremRectify LP/BP/HP
	auto flavorScalars = phi::evalTriangleBank<14>(flavorPhase, 1.0f, kFlavorScalarBank);

	// Map raw triangle outputs to param ranges
	// Note: freqOffset is applied dynamically in the DSP loop to support mod matrix routing
	c.filterCutoffBase = static_cast<q31_t>(flavorScalars[0] * kQ31MaxFloat);
	c.filterResonance = static_cast<q31_t>(flavorScalars[1] * 0.85f * kQ31MaxFloat);
	c.filterModDepth = static_cast<q31_t>(flavorScalars[2] * kQ31MaxFloat);
	c.envAttack = static_cast<q31_t>(std::pow(flavorScalars[3], 2.0f) * kQ31MaxFloat);
	c.envRelease = static_cast<q31_t>(std::pow(flavorScalars[4], 2.0f) * kQ31MaxFloat);
	c.combStaticOffset = flavorScalars[5];
	c.combLfoDepth = flavorScalars[6];
	c.combPhaseOffsetU32 = static_cast<uint32_t>(flavorScalars[7] * 4294967295.0f);
	c.combMonoCollapseQ = static_cast<q31_t>(flavorScalars[8] * kQ31MaxFloat);
	c.tremoloDepthQ = static_cast<q31_t>(flavorScalars[9] * kQ31MaxFloat * 0.5f); // Halved to reduce scratchiness
	c.tremPhaseOffset = static_cast<uint32_t>(flavorScalars[10] * kPhaseMaxFloat);
	// Per-band tremolo rectification: 0=half-wave, 1=full-wave
	c.tremRectifyLpQ = static_cast<q31_t>(flavorScalars[11] * kQ31MaxFloat);
	c.tremRectifyBpQ = static_cast<q31_t>(flavorScalars[12] * kQ31MaxFloat);
	c.tremRectifyHpQ = static_cast<q31_t>(flavorScalars[13] * kQ31MaxFloat);

	// Pre-compute comb delay constants in 16.16 fixed-point
	constexpr int32_t kMinDelay = 4;
	constexpr int32_t kMaxDelay = 735;
	constexpr int32_t kMaxModRange = 200;
	constexpr int32_t kCombSize = static_cast<int32_t>(AutomodulatorParams::kCombBufferSize);
	int32_t delayRange = kMaxDelay - kMinDelay - kMaxModRange;
	int32_t baseDelaySamples = kMinDelay + static_cast<int32_t>(c.combStaticOffset * static_cast<float>(delayRange));
	c.combBaseDelay16 = baseDelaySamples << 16;
	c.combModRangeSamples = static_cast<int32_t>(c.combLfoDepth * static_cast<float>(kMaxModRange));
	c.combMinDelay16 = 2 << 16;
	c.combMaxDelay16 = (kCombSize - 2) << 16;

	// Filter LFO banks (already batched - 2 banks of 3)
	auto filterResponse = phi::evalTriangleBank<3>(flavorPhase, 1.0f, kFilterLfoResponseBank);
	auto filterPhaseOffsets = phi::evalTriangleBank<3>(flavorPhase, 1.0f, kFilterPhaseOffsetBank);
	c.lpResponse = filterResponse[0];
	c.bpResponse = filterResponse[1];
	c.hpResponse = filterResponse[2];
	c.lpPhaseOffset = filterPhaseOffsets[0];
	c.bpPhaseOffset = filterPhaseOffsets[1];
	c.hpPhaseOffset = filterPhaseOffsets[2];
	c.lpPhaseOffsetU32 = static_cast<uint32_t>(filterPhaseOffsets[0] * kPhaseMaxFloat);
	c.bpPhaseOffsetU32 = static_cast<uint32_t>(filterPhaseOffsets[1] * kPhaseMaxFloat);
	c.hpPhaseOffsetU32 = static_cast<uint32_t>(filterPhaseOffsets[2] * kPhaseMaxFloat);
	c.lpResponseQ = static_cast<q31_t>(filterResponse[0] * kQ31MaxFloat);
	c.bpResponseQ = static_cast<q31_t>(filterResponse[1] * kQ31MaxFloat);
	c.hpResponseQ = static_cast<q31_t>(filterResponse[2] * kQ31MaxFloat);

	constexpr float kResponseThreshold = 0.01f;
	c.useStaticFilterMix = (filterResponse[0] < kResponseThreshold && filterResponse[1] < kResponseThreshold
	                        && filterResponse[2] < kResponseThreshold);

	// === Batch evaluate type-derived scalar params ===
	// [0]=combFeedback, [1]=combMix, [2]=svfFeedback (bipolar)
	auto typeScalars = phi::evalTriangleBank<3>(typePhase, 1.0f, kTypeScalarBank);
	c.combFeedback = static_cast<q31_t>(typeScalars[0] * 0.85f * kQ31MaxFloat);
	c.combMixQ = static_cast<q31_t>(typeScalars[1] * kQ31MaxFloat);
	c.svfFeedbackQ = static_cast<q31_t>(typeScalars[2] * kQ31MaxFloat);

	// Filter mix needs constant-power normalization (keep separate function)
	float effectiveTypePhase = params.typePhaseOffset + params.gammaPhase;
	FilterMix filterMix = getFilterMixFromType(params.type, effectiveTypePhase);
	c.filterMixLowQ = static_cast<q31_t>(filterMix.low * kQ31MaxFloat);
	c.filterMixBandQ = static_cast<q31_t>(filterMix.band * kQ31MaxFloat);
	c.filterMixHighQ = static_cast<q31_t>(filterMix.high * kQ31MaxFloat);

	// Update cache keys
	params.prevRate = params.rate;
	params.prevRateSynced = params.rateSynced;
	params.prevLfoMode = params.lfoMode;
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

void processAutomodulator(std::span<StereoSample> buffer, AutomodulatorParams& params, q31_t depth, q31_t freqOffset,
                          q31_t manual, bool useInternalOsc, uint8_t voiceCount, uint32_t timePerTickInverse,
                          int32_t noteCode, bool isLegato) {
	if (!params.isEnabled() || buffer.empty()) {
		return;
	}

	// Ensure lazily-allocated state is ready
	if (!params.ensureStateAllocated()) {
		return; // Allocation failed, skip processing
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

	// Local references to lazily-allocated state (minimizes pointer dereferences)
	AutomodPhiCache& c = *params.cache;
	AutomodDspState& s = *params.dspState;

	// === Envelope tracking on INPUT (before processing) ===
	// Sample at buffer boundaries instead of scanning entire buffer (~400 cycles saved)
	// TODO: verify this doesn't regress envelope tracking feel compared to full peak scan
	q31_t peakL = std::max(std::abs(buffer.front().l), std::abs(buffer.back().l));
	q31_t peakR = std::max(std::abs(buffer.front().r), std::abs(buffer.back().r));

	// Update phi triangle cache only when params change (big perf win)
	bool wavetableChanged = false;
	if (params.needsCacheUpdate(timePerTickInverse)) {
#if ENABLE_FX_BENCHMARK
		if (doBench) {
			benchCache.start();
		}
#endif
		updateAutomodPhiCache(params, timePerTickInverse);
		wavetableChanged = true;
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

	// Reinitialize LFO states if wavetable changed
	if (wavetableChanged) {
		initLfoIir(s.lfoIirL, s.lfoPhase, c.lfoInc, c.wavetable);
		initLfoIir(s.lfoIirR, s.lfoPhase, c.lfoInc, c.wavetable);
		initLfoIir(s.combLfoIirL, s.lfoPhase + c.combPhaseOffsetU32, c.lfoInc, c.wavetable);
		initLfoIir(s.combLfoIirR, s.lfoPhase + c.combPhaseOffsetU32, c.lfoInc, c.wavetable);
		initLfoIir(s.tremLfoIirL, s.lfoPhase + c.tremPhaseOffset, c.lfoInc, c.wavetable);
		initLfoIir(s.tremLfoIirR, s.lfoPhase + c.tremPhaseOffset, c.lfoInc, c.wavetable);
	}

	// Store initial envelope state for derivative calculation
	q31_t envStartL = s.envStateL;
	q31_t envStartR = s.envStateR;

	// Envelope follower at buffer rate - one-pole filter on INPUT peaks (saturating arithmetic)
	if (peakL > s.envStateL) {
		q31_t deltaL = multiply_32x32_rshift32(add_saturate(peakL, -s.envStateL), c.envAttack) << 1;
		s.envStateL = add_saturate(s.envStateL, deltaL);
	}
	else {
		q31_t deltaL = multiply_32x32_rshift32(add_saturate(s.envStateL, -peakL), c.envRelease) << 1;
		s.envStateL = add_saturate(s.envStateL, -deltaL);
	}
	if (peakR > s.envStateR) {
		q31_t deltaR = multiply_32x32_rshift32(add_saturate(peakR, -s.envStateR), c.envAttack) << 1;
		s.envStateR = add_saturate(s.envStateR, deltaR);
	}
	else {
		q31_t deltaR = multiply_32x32_rshift32(add_saturate(s.envStateR, -peakR), c.envRelease) << 1;
		s.envStateR = add_saturate(s.envStateR, -deltaR);
	}

	// Derivative = change over this buffer (saturating to prevent overflow)
	q31_t rawDerivL = add_saturate(s.envStateL, -envStartL);
	q31_t rawDerivR = add_saturate(s.envStateR, -envStartR);
	// Smooth the derivative (saturating arithmetic)
	q31_t derivDeltaL = multiply_32x32_rshift32(add_saturate(rawDerivL, -s.envDerivStateL), c.envAttack) << 1;
	q31_t derivDeltaR = multiply_32x32_rshift32(add_saturate(rawDerivR, -s.envDerivStateR), c.envAttack) << 1;
	s.envDerivStateL = add_saturate(s.envDerivStateL, derivDeltaL);
	s.envDerivStateR = add_saturate(s.envDerivStateR, derivDeltaR);

	// Stage enables as bools - all stages active when effect is on (mix > 0 gates entry via isEnabled())
	const bool filterEnabled = true;
	const bool combEnabled = params.hasCombBuffers();
	// Note: Tremolo is now always active via per-band processing in filter mixing

	// Lazy-allocate comb buffers only when comb effect is first used
	// This saves 12KB per Sound when comb isn't needed
	if (!params.hasCombBuffers()) {
		params.allocateCombBuffers(); // May fail silently - comb just won't process
	}

	// Wet/dry mix: convert mix (0-127) to q31 blend factor
	// mix << 24 gives approximate q31 range (mix=127 → ~0x7F000000)
	const q31_t wetMixQ = std::min(static_cast<int32_t>(params.mix) << 24, static_cast<int32_t>(ONE_Q31));

	// Retrigger LFO based on note/voice activity and LFO mode:
	// - FREE mode: never retrigger (free-running LFO ignores note triggers)
	// - STOP mode: never retrigger (LFO frozen)
	// - ONCE/RETRIG modes: retrigger on note activity
	//   - Legato: retrigger on 0→N held notes only (no retrigger during legato overlap)
	//   - Non-legato: retrigger on any note increase OR voice increase
	bool doRetrigger = false;
	bool modeAllowsRetrigger = (params.lfoMode == AutomodLfoMode::ONCE || params.lfoMode == AutomodLfoMode::RETRIG);
	if (modeAllowsRetrigger) {
		if (isLegato) {
			// Legato: retrigger only when first note played after all released
			doRetrigger = (params.lastHeldNotesCount == 0 && params.heldNotesCount > 0);
		}
		else {
			// Non-legato: retrigger on any new note OR voice increase
			bool noteIncreaseRetrigger = (params.heldNotesCount > params.lastHeldNotesCount);
			bool voiceRetrigger = (voiceCount > params.lastVoiceCount) && (params.lastVoiceCount > 0);
			doRetrigger = noteIncreaseRetrigger || voiceRetrigger;
		}
	}

	if (doRetrigger) {
		float effectiveModPhase = params.modPhaseOffset + params.gammaPhase;
		s.lfoPhase = getLfoInitialPhaseFromMod(params.mod, effectiveModPhase);

		// For Once mode: track start phase for cycle detection
		if (c.rateOnce) {
			s.onceStartPhase = s.lfoPhase;
			s.oneCycleComplete = false;
		}

		// Initialize all LFO states from the new phase
		initLfoIir(s.lfoIirL, s.lfoPhase, c.lfoInc, c.wavetable);
		initLfoIir(s.lfoIirR, s.lfoPhase, c.lfoInc, c.wavetable);
		initLfoIir(s.combLfoIirL, s.lfoPhase + c.combPhaseOffsetU32, c.lfoInc, c.wavetable);
		initLfoIir(s.combLfoIirR, s.lfoPhase + c.combPhaseOffsetU32, c.lfoInc, c.wavetable);
		initLfoIir(s.tremLfoIirL, s.lfoPhase + c.tremPhaseOffset, c.lfoInc, c.wavetable);
		initLfoIir(s.tremLfoIirR, s.lfoPhase + c.tremPhaseOffset, c.lfoInc, c.wavetable);
	}
	params.lastVoiceCount = voiceCount;
	params.lastHeldNotesCount = params.heldNotesCount;

	// Stereo offset directly from mod (no rate/depth scaling)
	uint32_t stereoPhaseOffset = c.stereoPhaseOffsetRaw;

	// === Pure q31 envelope modulation math (no float conversions) ===
	// Depth is bipolar q31: -ONE_Q31 = -100%, 0 = 0%, +ONE_Q31 = +100%
	// Param defaults to ONE_Q31 so knob center = 100%
	// Negative depth inverts the LFO polarity
	q31_t depthScaleQ31 = depth;

	// Pre-compute depth × influence products (all q31)
	// Use absolute scale for envelope influences (envelope modulates magnitude)
	// Saturating abs: INT32_MIN (-2^31) negates to ONE_Q31 (avoids overflow)
	q31_t absScale = (depthScaleQ31 >= 0) ? depthScaleQ31 : (depthScaleQ31 > -ONE_Q31) ? -depthScaleQ31 : ONE_Q31;
	q31_t depthEnvQ = multiply_32x32_rshift32(absScale, c.envDepthInfluenceQ) << 1;
	q31_t depthPhaseQ = multiply_32x32_rshift32(absScale, c.envPhaseInfluenceQ) << 1;
	q31_t depthDerivEnvQ = multiply_32x32_rshift32(absScale, c.envDerivDepthInfluenceQ) << 1;
	q31_t depthDerivPhaseQ = multiply_32x32_rshift32(absScale, c.envDerivPhaseInfluenceQ) << 1;

	// Derivative normalization: scale by 64 (matches 1/2^25 float normalization), clamp first
	constexpr q31_t kDerivClampThresh = ONE_Q31 >> 6;
	q31_t derivNormQL = std::clamp(s.envDerivStateL, -kDerivClampThresh, kDerivClampThresh) << 6;
	q31_t derivNormQR = std::clamp(s.envDerivStateR, -kDerivClampThresh, kDerivClampThresh) << 6;

	// Envelope scale contribution: depthEnv × envState × 2 (×2 via saturating add)
	q31_t envScaleL = multiply_32x32_rshift32(depthEnvQ, s.envStateL);
	q31_t envScaleR = multiply_32x32_rshift32(depthEnvQ, s.envStateR);
	envScaleL = add_saturate(envScaleL, envScaleL);
	envScaleR = add_saturate(envScaleR, envScaleR);

	// Derivative scale contribution: depthDerivEnv × derivNorm × 2
	q31_t derivScaleL = multiply_32x32_rshift32(depthDerivEnvQ, derivNormQL);
	q31_t derivScaleR = multiply_32x32_rshift32(depthDerivEnvQ, derivNormQR);
	derivScaleL = add_saturate(derivScaleL, derivScaleL);
	derivScaleR = add_saturate(derivScaleR, derivScaleR);

	// targetScale = depthScale + envScale + derivScale (saturating)
	// All values in q31 format, negative scale inverts LFO polarity
	q31_t targetScaleQL = add_saturate(add_saturate(depthScaleQ31, envScaleL), derivScaleL);
	q31_t targetScaleQR = add_saturate(add_saturate(depthScaleQ31, envScaleR), derivScaleR);

	// Phase push: depthPhase × envState + depthDerivPhase × derivNorm
	q31_t envPhaseL = multiply_32x32_rshift32(depthPhaseQ, s.envStateL) << 1;
	q31_t envPhaseR = multiply_32x32_rshift32(depthPhaseQ, s.envStateR) << 1;
	q31_t derivPhaseL = multiply_32x32_rshift32(depthDerivPhaseQ, derivNormQL) << 1;
	q31_t derivPhaseR = multiply_32x32_rshift32(depthDerivPhaseQ, derivNormQR) << 1;
	q31_t phasePushQL = add_saturate(envPhaseL, derivPhaseL);
	q31_t phasePushQR = add_saturate(envPhaseR, derivPhaseR);

	// Convert q31 phase push to uint32 phase offset (<<1 for full phase range)
	uint32_t targetPhasePushUL = static_cast<uint32_t>(phasePushQL) << 1;
	uint32_t targetPhasePushUR = static_cast<uint32_t>(phasePushQR) << 1;

	// Stereo offset applied directly (no depth scaling)
	uint32_t targetStereoOffset = stereoPhaseOffset;

	// Smooth toward targets (one-pole filter at buffer rate, ~12ms transition, saturating)
	q31_t scaleDeltaL = multiply_32x32_rshift32(add_saturate(targetScaleQL, -s.smoothedScaleL), kModSmoothCoeffQ) << 1;
	q31_t scaleDeltaR = multiply_32x32_rshift32(add_saturate(targetScaleQR, -s.smoothedScaleR), kModSmoothCoeffQ) << 1;
	s.smoothedScaleL = add_saturate(s.smoothedScaleL, scaleDeltaL);
	s.smoothedScaleR = add_saturate(s.smoothedScaleR, scaleDeltaR);
	// Phase push smoothing: use signed arithmetic for proper interpolation
	int32_t phaseDiffL = static_cast<int32_t>(targetPhasePushUL - s.smoothedPhasePushL);
	int32_t phaseDiffR = static_cast<int32_t>(targetPhasePushUR - s.smoothedPhasePushR);
	s.smoothedPhasePushL += static_cast<uint32_t>(multiply_32x32_rshift32(phaseDiffL, kModSmoothCoeffQ) << 1);
	s.smoothedPhasePushR += static_cast<uint32_t>(multiply_32x32_rshift32(phaseDiffR, kModSmoothCoeffQ) << 1);
	int32_t stereoDiff = static_cast<int32_t>(targetStereoOffset - s.smoothedStereoOffset);
	s.smoothedStereoOffset += static_cast<uint32_t>(multiply_32x32_rshift32(stereoDiff, kModSmoothCoeffQ) << 1);

	// Use smoothed values for the loop (q31 bipolar: -ONE_Q31 to +ONE_Q31)
	q31_t scaleQL = s.smoothedScaleL;
	q31_t scaleQR = s.smoothedScaleR;
	// DISABLED FOR TESTING: all phase push
	uint32_t phasePushL = 0; // was: s.smoothedPhasePushL;
	uint32_t phasePushR = 0; // was: s.smoothedPhasePushR;
	uint32_t scaledStereoOffset = s.smoothedStereoOffset;

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
		uint32_t lpPhase = s.lfoPhase + c.lpPhaseOffsetU32;
		uint32_t bpPhase = s.lfoPhase + c.bpPhaseOffsetU32;
		uint32_t hpPhase = s.lfoPhase + c.hpPhaseOffsetU32;
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

	// Save previous mix values for per-sample interpolation
	q31_t prevLowMixQ = s.smoothedLowMixQ;
	q31_t prevBandMixQ = s.smoothedBandMixQ;
	q31_t prevHighMixQ = s.smoothedHighMixQ;

	// Smooth filter mix toward targets (same ~12ms transition as other modulations, saturating)
	q31_t lowMixSmoothDelta = multiply_32x32_rshift32(add_saturate(targetLowMixQ, -s.smoothedLowMixQ), kModSmoothCoeffQ)
	                          << 1;
	q31_t bandMixSmoothDelta =
	    multiply_32x32_rshift32(add_saturate(targetBandMixQ, -s.smoothedBandMixQ), kModSmoothCoeffQ) << 1;
	q31_t highMixSmoothDelta =
	    multiply_32x32_rshift32(add_saturate(targetHighMixQ, -s.smoothedHighMixQ), kModSmoothCoeffQ) << 1;
	s.smoothedLowMixQ = add_saturate(s.smoothedLowMixQ, lowMixSmoothDelta);
	s.smoothedBandMixQ = add_saturate(s.smoothedBandMixQ, bandMixSmoothDelta);
	s.smoothedHighMixQ = add_saturate(s.smoothedHighMixQ, highMixSmoothDelta);

	// === Buffer-rate LFO computation using pure accumulation ===
	// Just add step each sample - no phase-based correction

	const size_t bufferSize = buffer.size();
	const uint32_t startPhase = s.lfoPhase;
	const uint32_t phaseInc = c.lfoInc;

	// Recompute stepping params if rate changed (wavetable changes trigger cache rebuild)
	if (phaseInc != s.cachedPhaseInc) {
		computeLfoSteppingParams(s, phaseInc, c.wavetable);
	}

	// Compute phases for each LFO channel (used for segment detection)
	// DISABLED FOR TESTING: phase push from envelope feedback
	uint32_t lfoPhaseL = startPhase;                      // was: + phasePushL
	uint32_t lfoPhaseR = startPhase + scaledStereoOffset; // was: + phasePushR
	uint32_t combPhaseL = startPhase + c.combPhaseOffsetU32;
	uint32_t combPhaseR = combPhaseL + scaledStereoOffset;
	uint32_t tremPhaseL = startPhase + c.tremPhaseOffset;
	uint32_t tremPhaseR = tremPhaseL + scaledStereoOffset;

	// Get initial LFO values and samples remaining until segment boundary
	// Use precomputed step from s.stepPerSegment instead of computing per-channel
	uint32_t lfoLRemaining, lfoRRemaining, combLRemaining, combRRemaining, tremLRemaining, tremRRemaining;
	LfoIncremental lfoL = updateLfoAccum(s.lfoIirL, lfoPhaseL, phaseInc, c.wavetable, lfoLRemaining);
	LfoIncremental lfoR = updateLfoAccum(s.lfoIirR, lfoPhaseR, phaseInc, c.wavetable, lfoRRemaining);
	LfoIncremental combLfoL = updateLfoAccum(s.combLfoIirL, combPhaseL, phaseInc, c.wavetable, combLRemaining);
	LfoIncremental combLfoR = updateLfoAccum(s.combLfoIirR, combPhaseR, phaseInc, c.wavetable, combRRemaining);
	LfoIncremental tremLfoL = updateLfoAccum(s.tremLfoIirL, tremPhaseL, phaseInc, c.wavetable, tremLRemaining);
	LfoIncremental tremLfoR = updateLfoAccum(s.tremLfoIirR, tremPhaseR, phaseInc, c.wavetable, tremRRemaining);

	// Override deltas with precomputed values from stepping params
	lfoL.delta = s.stepPerSegment[s.lfoIirL.segment];
	lfoR.delta = s.stepPerSegment[s.lfoIirR.segment];
	combLfoL.delta = s.stepPerSegment[s.combLfoIirL.segment];
	combLfoR.delta = s.stepPerSegment[s.combLfoIirR.segment];
	tremLfoL.delta = s.stepPerSegment[s.tremLfoIirL.segment];
	tremLfoR.delta = s.stepPerSegment[s.tremLfoIirR.segment];

	// === Manual offset handling ===
	// IMPORTANT: Do NOT add manual to .value fields - those are used for accumulation
	// and IIR state tracking. Instead, compute separate processed values for DSP use.
	// This prevents manual offset from corrupting the IIR state (which caused LFO to
	// get "stuck" when manual was negative and caused saturation).

	// Compute manual offset to apply for processing (varies by mode)
	q31_t manualOffset = 0;
	bool freezeLfo = false;

	if (c.rateStopped) {
		// Stop mode: manual IS the LFO value, freeze phase and delta
		// Set values to manual directly (no raw tracking needed when stopped)
		lfoL.value = manual;
		lfoL.delta = 0;
		lfoR.value = manual;
		lfoR.delta = 0;
		combLfoL.value = manual;
		combLfoL.delta = 0;
		combLfoR.value = manual;
		combLfoR.delta = 0;
		tremLfoL.value = manual;
		tremLfoL.delta = 0;
		tremLfoR.value = manual;
		tremLfoR.delta = 0;
		freezeLfo = true;
		// manualOffset stays 0 since manual is already in .value
	}
	else if (c.rateOnce && s.oneCycleComplete) {
		// Once mode with cycle complete: freeze at final position
		lfoL.delta = 0;
		lfoR.delta = 0;
		combLfoL.delta = 0;
		combLfoR.delta = 0;
		tremLfoL.delta = 0;
		tremLfoR.delta = 0;
		freezeLfo = true;
		manualOffset = manual; // Add manual to frozen position for processing
	}
	else {
		// Running mode: .value tracks raw waveform, manual added for processing only
		manualOffset = manual;

		// Update phase for next buffer
		uint32_t newPhase = startPhase + phaseInc * bufferSize;

		// Once mode: stop when we've traveled one full cycle from start phase
		if (c.rateOnce && !s.oneCycleComplete) {
			// Distance from start (unsigned arithmetic handles wrap correctly)
			uint32_t prevDist = startPhase - s.onceStartPhase;
			uint32_t newDist = newPhase - s.onceStartPhase;
			// If distance decreased, we wrapped past the start phase
			if (newDist < prevDist && prevDist > 0x40000000) {
				s.oneCycleComplete = true;
				// Freeze at current end position - IIRs and phase stay where they are
			}
		}

		s.lfoPhase = newPhase;
	}

	// Apply global depth scaling to tremolo and comb LFOs
	// (Filter LFO uses spring filter below instead of per-sample scaling)
	// Use absolute depth (no inversion for trem/comb), cap at ONE_Q31
	// Note: trem/comb .value fields are overwritten here for processing, not preserved for IIR
	q31_t depthMultQ31 = std::min(absScale, ONE_Q31);

	// For trem/comb, add manual offset THEN apply depth scaling (for processing only)
	// The raw .value is used for accumulation but gets depth-scaled here for DSP use
	q31_t processedTremL = add_saturate(tremLfoL.value, manualOffset);
	q31_t processedTremR = add_saturate(tremLfoR.value, manualOffset);
	q31_t processedCombL = add_saturate(combLfoL.value, manualOffset);
	q31_t processedCombR = add_saturate(combLfoR.value, manualOffset);

	// Apply depth scaling to processed values (not to raw tracking values)
	processedTremL = multiply_32x32_rshift32(processedTremL, depthMultQ31) << 1;
	processedTremR = multiply_32x32_rshift32(processedTremR, depthMultQ31) << 1;
	processedCombL = multiply_32x32_rshift32(processedCombL, depthMultQ31) << 1;
	processedCombR = multiply_32x32_rshift32(processedCombR, depthMultQ31) << 1;

	// Depth-scale the deltas too (for per-sample accumulation in DSP loop)
	q31_t tremDeltaL = multiply_32x32_rshift32(tremLfoL.delta, depthMultQ31) << 1;
	q31_t tremDeltaR = multiply_32x32_rshift32(tremLfoR.delta, depthMultQ31) << 1;
	q31_t combDeltaL = multiply_32x32_rshift32(combLfoL.delta, depthMultQ31) << 1;
	q31_t combDeltaR = multiply_32x32_rshift32(combLfoR.delta, depthMultQ31) << 1;

	// === Spring filter on filter LFO modulation signal (buffer-rate 2nd-order LPF) ===
	// Signal flow: (lfoL.value + manualOffset) + envValue → × scaleQL → spring → filter cutoff
	// Spring output is separate from LFO state to avoid corrupting segment tracking
	//
	// FUTURE: Alternative "impulse-excited spring" LFO mode could replace multi-segment triangle
	// with periodic impulses that excite the spring directly. The spring's natural resonance
	// would create the waveform (like plucked strings). Impulse rate = LFO rate, spring freq/damp
	// control timbre. Would give organic, emergent shapes with built-in anti-aliasing.

	// Compute spring input: LFO + manual + envValue contribution
	// Scale each down by 8 before adding to prevent saturation (max sum = 0.375)
	q31_t springTargetL = add_saturate(lfoL.value >> 3, manualOffset >> 3);
	q31_t springTargetR = add_saturate(lfoR.value >> 3, manualOffset >> 3);
	if (c.envValueInfluenceQ != 0) {
		// Env contrib: multiply gives ~1/2 scale, >> 2 more = 1/8 scale to match
		q31_t envContribL = multiply_32x32_rshift32(s.envStateL, c.envValueInfluenceQ) >> 2;
		q31_t envContribR = multiply_32x32_rshift32(s.envStateR, c.envValueInfluenceQ) >> 2;
		springTargetL = add_saturate(springTargetL, envContribL);
		springTargetR = add_saturate(springTargetR, envContribR);
	}

	// Apply depth scaling at buffer rate (use raw targetScale, spring handles smoothing)
	// springTargetL is at 1/8 scale, multiply halves again = 1/16 scale
	// << 1 restores to 1/8 scale = 8x headroom for spring overshoot
	q31_t scaledModL = multiply_32x32_rshift32(springTargetL, targetScaleQL) << 1;
	q31_t scaledModR = multiply_32x32_rshift32(springTargetR, targetScaleQR) << 1;

	// Save previous spring positions for interpolation
	q31_t prevSpringPosL = s.springPosL;
	q31_t prevSpringPosR = s.springPosR;

	// Spring filter update (2nd-order LPF with resonance)
	// Semi-implicit Euler: vel += k*error - c*vel, pos += vel
	// Coefficients already include dt scaling
	{
		q31_t errorL = add_saturate(scaledModL, -s.springPosL); // Saturating subtract
		q31_t forceL = multiply_32x32_rshift32(errorL, c.springOmega2Q) << 1;
		q31_t dampL = multiply_32x32_rshift32(s.springVelL, c.springDampingCoeffQ) << 1;
		forceL = add_saturate(forceL, -dampL); // Saturating subtract to prevent overflow
		s.springVelL = add_saturate(s.springVelL, forceL);
		s.springPosL = add_saturate(s.springPosL, s.springVelL);

		q31_t errorR = add_saturate(scaledModR, -s.springPosR); // Saturating subtract
		q31_t forceR = multiply_32x32_rshift32(errorR, c.springOmega2Q) << 1;
		q31_t dampR = multiply_32x32_rshift32(s.springVelR, c.springDampingCoeffQ) << 1;
		forceR = add_saturate(forceR, -dampR); // Saturating subtract to prevent overflow
		s.springVelR = add_saturate(s.springVelR, forceR);
		s.springPosR = add_saturate(s.springPosR, s.springVelR);
	}

	// Compute per-sample delta for smooth interpolation within buffer
	// Scale up by 8 to compensate for input scaling (gives 8x headroom for overshoot)
	// For buffer size N: delta = (newPos - oldPos) * 8 / N = (diff) >> (log2(N) - 3)
	int bufferLog2 = 31 - __builtin_clz(std::max(static_cast<unsigned>(bufferSize), 1u));
	int deltaShift = bufferLog2 - 3;                           // Combine /N and *8 into single shift
	q31_t diffL = add_saturate(s.springPosL, -prevSpringPosL); // Saturating subtract
	q31_t diffR = add_saturate(s.springPosR, -prevSpringPosR); // Saturating subtract
	q31_t springDeltaL = (deltaShift >= 0) ? (diffL >> deltaShift) : (diffL << (-deltaShift));
	q31_t springDeltaR = (deltaShift >= 0) ? (diffR >> deltaShift) : (diffR << (-deltaShift));

	// Spring output for filter modulation (separate from lfoL/lfoR to preserve LFO state)
	// Scale up by 8 to restore original amplitude (spring operates at 1/8 scale for headroom)
	// Use saturating adds to prevent overflow (3 doublings = 8x)
	q31_t springOutL = add_saturate(prevSpringPosL, prevSpringPosL);
	springOutL = add_saturate(springOutL, springOutL);
	springOutL = add_saturate(springOutL, springOutL);
	q31_t springOutR = add_saturate(prevSpringPosR, prevSpringPosR);
	springOutR = add_saturate(springOutR, springOutR);
	springOutR = add_saturate(springOutR, springOutR);

	// Capture spring input for DEBUG_SPRING output (scale up to match output scale)
	q31_t springInputL = add_saturate(scaledModL, scaledModL);
	springInputL = add_saturate(springInputL, springInputL);
	springInputL = add_saturate(springInputL, springInputL);

	// === Tremolo spring filter (smooths tremolo LFO, same structure as filter spring) ===
	// Input: processedTremL/R (already has manual offset and depth scaling)
	// Scale down by 8 for headroom
	q31_t tremSpringTargetL = processedTremL >> 3;
	q31_t tremSpringTargetR = processedTremR >> 3;

	// Save previous positions for interpolation
	q31_t prevTremSpringPosL = s.tremSpringPosL;
	q31_t prevTremSpringPosR = s.tremSpringPosR;

	// Spring filter update (same semi-implicit Euler as filter spring)
	{
		q31_t errorL = add_saturate(tremSpringTargetL, -s.tremSpringPosL); // Saturating subtract
		q31_t forceL = multiply_32x32_rshift32(errorL, c.tremSpringOmega2Q) << 1;
		q31_t dampL = multiply_32x32_rshift32(s.tremSpringVelL, c.tremSpringDampingCoeffQ) << 1;
		forceL = add_saturate(forceL, -dampL); // Saturating subtract to prevent overflow
		s.tremSpringVelL = add_saturate(s.tremSpringVelL, forceL);
		s.tremSpringPosL = add_saturate(s.tremSpringPosL, s.tremSpringVelL);

		q31_t errorR = add_saturate(tremSpringTargetR, -s.tremSpringPosR); // Saturating subtract
		q31_t forceR = multiply_32x32_rshift32(errorR, c.tremSpringOmega2Q) << 1;
		q31_t dampR = multiply_32x32_rshift32(s.tremSpringVelR, c.tremSpringDampingCoeffQ) << 1;
		forceR = add_saturate(forceR, -dampR); // Saturating subtract to prevent overflow
		s.tremSpringVelR = add_saturate(s.tremSpringVelR, forceR);
		s.tremSpringPosR = add_saturate(s.tremSpringPosR, s.tremSpringVelR);
	}

	// Compute per-sample delta for smooth interpolation
	q31_t tremDiffL = add_saturate(s.tremSpringPosL, -prevTremSpringPosL); // Saturating subtract
	q31_t tremDiffR = add_saturate(s.tremSpringPosR, -prevTremSpringPosR); // Saturating subtract
	q31_t tremSpringDeltaL = (deltaShift >= 0) ? (tremDiffL >> deltaShift) : (tremDiffL << (-deltaShift));
	q31_t tremSpringDeltaR = (deltaShift >= 0) ? (tremDiffR >> deltaShift) : (tremDiffR << (-deltaShift));

	// Spring output for tremolo (scale up 8x to restore amplitude)
	q31_t tremSpringOutL = add_saturate(prevTremSpringPosL, prevTremSpringPosL);
	tremSpringOutL = add_saturate(tremSpringOutL, tremSpringOutL);
	tremSpringOutL = add_saturate(tremSpringOutL, tremSpringOutL);
	q31_t tremSpringOutR = add_saturate(prevTremSpringPosR, prevTremSpringPosR);
	tremSpringOutR = add_saturate(tremSpringOutR, tremSpringOutR);
	tremSpringOutR = add_saturate(tremSpringOutR, tremSpringOutR);

	// Replace processedTrem with spring-filtered version
	processedTremL = tremSpringOutL;
	processedTremR = tremSpringOutR;

	// Update tremolo deltas to use spring interpolation instead of LFO delta
	// Note: springDelta already accounts for 8x scaling via deltaShift = bufferLog2 - 3
	// The delta is: (newPos - prevPos) * 8 / bufferSize, which gives correct interpolation
	// from prevPos*8 to newPos*8 over the buffer
	tremDeltaL = tremSpringDeltaL;
	tremDeltaR = tremSpringDeltaR;

	// Compute per-sample filter mix deltas for smooth interpolation
	// Use bufferLog2 directly (no 8x scaling like spring)
	q31_t lowMixDiffQ = add_saturate(s.smoothedLowMixQ, -prevLowMixQ);
	q31_t bandMixDiffQ = add_saturate(s.smoothedBandMixQ, -prevBandMixQ);
	q31_t highMixDiffQ = add_saturate(s.smoothedHighMixQ, -prevHighMixQ);
	q31_t lowMixDeltaQ = lowMixDiffQ >> bufferLog2;
	q31_t bandMixDeltaQ = bandMixDiffQ >> bufferLog2;
	q31_t highMixDeltaQ = highMixDiffQ >> bufferLog2;

	// Start filter mix interpolation from previous buffer values
	q31_t lowMixQ = prevLowMixQ;
	q31_t bandMixQ = prevBandMixQ;
	q31_t highMixQ = prevHighMixQ;

	// === Pitch tracking (cached - only recompute when noteCode changes) ===
	// Scale filter cutoff and comb delay based on played note frequency
	// Both use multiplicative scaling to maintain harmonic relationships
	// TODO: Currently uses target noteCode which jumps instantly. During portamento, the actual
	// sounding pitch glides but tracking doesn't follow. To fix: pass interpolated pitch from
	// Voice (using portaEnvelopePos and portaEnvelopeMaxAmplitude) instead of target noteCode.
	// For poly mode, use last triggered voice's pitch (matches standard synth behavior).
	if (noteCode != s.prevNoteCode) {
		s.prevNoteCode = noteCode;
		if (noteCode >= 0 && noteCode < 128) {
			float pitchOctaves = (static_cast<float>(noteCode) - 60.0f) / 12.0f;
			// Filter cutoff ratio: higher note = higher cutoff (positive octaves)
			float filterRatio = std::clamp(fastPow2(pitchOctaves), 0.25f, 4.0f);
			s.cachedFilterPitchRatioQ16 = static_cast<int32_t>(filterRatio * 65536.0f);
			// Comb delay ratio: higher note = shorter delay (negative octaves)
			float combRatio = std::clamp(fastPow2(-pitchOctaves), 0.25f, 4.0f);
			s.cachedCombPitchRatioQ16 = static_cast<int32_t>(combRatio * 65536.0f);
		}
		else {
			s.cachedFilterPitchRatioQ16 = 1 << 16; // 1.0 in 16.16
			s.cachedCombPitchRatioQ16 = 1 << 16;   // 1.0 in 16.16
		}
	}
	// Apply cached pitch ratios
	const int32_t filterPitchRatioQ16 = s.cachedFilterPitchRatioQ16;

	// Hoist loop-invariant filter constants
	// freqOffset applied here dynamically to support mod matrix routing
	// kCutoffMax must be < 0x40000000 to avoid overflow when shifted left by 1
	// Range is approximately 20Hz to 8kHz
	constexpr q31_t kCutoffMin = 0x00200000;                    // ~20 Hz (deep bass)
	constexpr q31_t kCutoffMax = 0x3FFFFFFF;                    // ~8 kHz (max safe before << 1 overflow)
	constexpr q31_t kCutoffMid = (kCutoffMin + kCutoffMax) / 2; // ~0x20800000
	constexpr q31_t kCutoffHalfRange = kCutoffMax - kCutoffMid; // ~0x1F7FFFFF
	// Scale freqOffset from full q31 range to filter half-range
	// This makes the knob span the full filter range (negative = low freq, positive = high freq)
	q31_t scaledFreqOffset = multiply_32x32_rshift32(freqOffset, kCutoffHalfRange) << 1;

	// Convert freqOffset to comb delay ratio (inverse: higher freq = shorter delay)
	// Full range (±0x80000000) = ±2 octaves
	float freqOctaves = static_cast<float>(freqOffset) / static_cast<float>(0x40000000);
	float combFreqRatio = std::clamp(fastPow2(-freqOctaves), 0.25f, 4.0f);
	int32_t combFreqRatioQ16 = static_cast<int32_t>(combFreqRatio * 65536.0f);

	// Apply both pitch tracking and freq offset to comb delay
	int32_t pitchCombBaseDelay16 =
	    static_cast<int32_t>((static_cast<int64_t>(c.combBaseDelay16) * s.cachedCombPitchRatioQ16) >> 16);
	pitchCombBaseDelay16 = static_cast<int32_t>((static_cast<int64_t>(pitchCombBaseDelay16) * combFreqRatioQ16) >> 16);
	pitchCombBaseDelay16 = std::clamp(pitchCombBaseDelay16, c.combMinDelay16, c.combMaxDelay16);
	// Scale filterCutoffBase similarly (it's 0 to 0x7FFFFFFF, we want it to add modest offset)
	q31_t scaledCutoffBase = multiply_32x32_rshift32(c.filterCutoffBase, kCutoffHalfRange >> 1) << 1;
	// Calculate base cutoff without pitch tracking
	q31_t filterBaseNoPitch = add_saturate(add_saturate(kCutoffMid, scaledCutoffBase), scaledFreqOffset);
	// Apply pitch tracking multiplicatively (16.16 × q31 → q31)
	// This maintains harmonic relationships: 1 octave up = 2× cutoff frequency
	const q31_t filterBasePlusPitch =
	    std::clamp(static_cast<q31_t>((static_cast<int64_t>(filterBaseNoPitch) * filterPitchRatioQ16) >> 16),
	               kCutoffMin, kCutoffMax);
	const q31_t filterQ = ONE_Q31 - c.filterResonance;

	// Hoist comb mono collapse check
	const bool doCombMonoCollapse = (c.combMonoCollapseQ > 0);

	// Slew-limit comb LFO delta to prevent Doppler aliasing from rapid delay changes
	// 0x00040000 limits to ~370ms peak-to-peak, ~1.35Hz max full-depth modulation
	// Note: Clamp the depth-scaled deltas (used for processing), not raw tracking deltas
	constexpr q31_t kMaxCombDelta = 0x00040000;
	if (combEnabled) {
		combDeltaL = std::clamp(combDeltaL, -kMaxCombDelta, kMaxCombDelta);
		combDeltaR = std::clamp(combDeltaR, -kMaxCombDelta, kMaxCombDelta);
	}

#if ENABLE_FX_BENCHMARK
	if (doBench) {
		benchSetup.stop();
		benchLoop.start();
	}
#endif

	for (auto& sample : buffer) {
		// Store dry signal for wet/dry blend
		const q31_t dryL = sample.l;
		const q31_t dryR = sample.r;
		q31_t outL = dryL;
		q31_t outR = dryR;

		// SVF Filter (auto-wah)
		if (filterEnabled) {
#if USE_NEON_SVF
			// === NEON vectorized SVF: process L/R in parallel ===
			// Pack L/R values into 2-lane vectors
			// springOutL/R are spring-filtered, depth-scaled modulation signals
			int32x2_t springVal = {springOutL, springOutR};
			int32x2_t out = {outL, outR};
			int32x2_t svfLow = {s.svfLowL, s.svfLowR};
			int32x2_t svfBand = {s.svfBandL, s.svfBandR};
			int32x2_t filterQVec = vdup_n_s32(filterQ);
			int32x2_t modDepth = vdup_n_s32(c.filterModDepth);
			int32x2_t basePlusPitch = vdup_n_s32(filterBasePlusPitch);

			// LFO contribution: spring output × modDepth (depth already applied by spring)
			int32x2_t lfoContrib = vqdmulh_s32(springVal, modDepth);

			// Cutoff = clamp(basePlusPitch + lfoContrib)
			int32x2_t cutoff = vqadd_s32(basePlusPitch, lfoContrib);

			// SVF feedback: LP output → cutoff (creates self-oscillation at high feedback)
			// svfFeedbackQ is bipolar: positive = cutoff feedback, negative = inverted
			if (c.svfFeedbackQ != 0) {
				int32x2_t feedbackVec = vdup_n_s32(c.svfFeedbackQ);
				// Scale LP output by feedback amount: (svfLow * feedback * 2) >> 32
				int32x2_t feedbackContrib = vqdmulh_s32(svfLow, feedbackVec);
				cutoff = vqadd_s32(cutoff, feedbackContrib);
			}

			cutoff = vmax_s32(cutoff, vdup_n_s32(kCutoffMin));
			cutoff = vmin_s32(cutoff, vdup_n_s32(kCutoffMax));

			// f = cutoff << 1 (extends frequency range, max ~8kHz at kCutoffMax)
			int32x2_t f = vshl_n_s32(cutoff, 1);

			// SVF processing:
			// high = out - svfLow - (svfBand * filterQ * 2) >> 32
			int32x2_t bandTimesQ = vqdmulh_s32(svfBand, filterQVec);
			int32x2_t high = vsub_s32(vsub_s32(out, svfLow), bandTimesQ);

			// svfBand += (high * f * 2) >> 32
			svfBand = vadd_s32(svfBand, vqdmulh_s32(high, f));

			// svfLow += (svfBand * f * 2) >> 32
			svfLow = vadd_s32(svfLow, vqdmulh_s32(svfBand, f));

			// Store SVF state back
			s.svfLowL = vget_lane_s32(svfLow, 0);
			s.svfLowR = vget_lane_s32(svfLow, 1);
			s.svfBandL = vget_lane_s32(svfBand, 0);
			s.svfBandR = vget_lane_s32(svfBand, 1);
			q31_t highL = vget_lane_s32(high, 0);
			q31_t highR = vget_lane_s32(high, 1);

			// Per-band stereo width: cross-blend each band toward mono
			// LP gets narrow stereo (focused bass), BP moderate, HP full width
			q31_t lowMono = (s.svfLowL >> 1) + (s.svfLowR >> 1);
			q31_t lowStereoL = lowMono + (multiply_32x32_rshift32(s.svfLowL - lowMono, c.lpStereoWidthQ) << 1);
			q31_t lowStereoR = lowMono + (multiply_32x32_rshift32(s.svfLowR - lowMono, c.lpStereoWidthQ) << 1);

			q31_t bandMono = (s.svfBandL >> 1) + (s.svfBandR >> 1);
			q31_t bandStereoL = bandMono + (multiply_32x32_rshift32(s.svfBandL - bandMono, c.bpStereoWidthQ) << 1);
			q31_t bandStereoR = bandMono + (multiply_32x32_rshift32(s.svfBandR - bandMono, c.bpStereoWidthQ) << 1);

			q31_t highMono = (highL >> 1) + (highR >> 1);
			q31_t highStereoL = highMono + (multiply_32x32_rshift32(highL - highMono, c.hpStereoWidthQ) << 1);
			q31_t highStereoR = highMono + (multiply_32x32_rshift32(highR - highMono, c.hpStereoWidthQ) << 1);

			// Per-band tremolo with rectification (cut-only, stereo varies by band)
			// Split bipolar LFO into positive and rectified negative parts
			// Use saturating negation to avoid overflow when value is INT32_MIN
			q31_t tremPosL = (processedTremL > 0) ? processedTremL : 0;
			q31_t tremNegL = (processedTremL < 0) ? (processedTremL == INT32_MIN ? INT32_MAX : -processedTremL) : 0;
			q31_t tremPosR = (processedTremR > 0) ? processedTremR : 0;
			q31_t tremNegR = (processedTremR < 0) ? (processedTremR == INT32_MIN ? INT32_MAX : -processedTremR) : 0;

			// Per-band unipolar LFO: positive + (rectified negative * rectifyAmount)
			// rectify=1: full-wave (smooth), rectify=0: half-wave (choppy)
			q31_t uniLpL = add_saturate(tremPosL, multiply_32x32_rshift32(tremNegL, c.tremRectifyLpQ) << 1);
			q31_t uniLpR = add_saturate(tremPosR, multiply_32x32_rshift32(tremNegR, c.tremRectifyLpQ) << 1);
			q31_t uniBpL = add_saturate(tremPosL, multiply_32x32_rshift32(tremNegL, c.tremRectifyBpQ) << 1);
			q31_t uniBpR = add_saturate(tremPosR, multiply_32x32_rshift32(tremNegR, c.tremRectifyBpQ) << 1);
			q31_t uniHpL = add_saturate(tremPosL, multiply_32x32_rshift32(tremNegL, c.tremRectifyHpQ) << 1);
			q31_t uniHpR = add_saturate(tremPosR, multiply_32x32_rshift32(tremNegR, c.tremRectifyHpQ) << 1);

			// Cut-only tremolo: 1 - depth * unipolar (uni=0: no cut, uni=max: max cut)
			q31_t tremLpL = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniLpL) << 1);
			q31_t tremLpR = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniLpR) << 1);
			q31_t tremBpL = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniBpL) << 1);
			q31_t tremBpR = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniBpR) << 1);
			q31_t tremHpL = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniHpL) << 1);
			q31_t tremHpR = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniHpR) << 1);

			// LP: mono tremolo (no stereo pulsing in bass)
			q31_t tremLpMono = (tremLpL >> 1) + (tremLpR >> 1);
			lowStereoL = multiply_32x32_rshift32(lowStereoL, tremLpMono) << 1;
			lowStereoR = multiply_32x32_rshift32(lowStereoR, tremLpMono) << 1;

			// BP: full stereo tremolo
			bandStereoL = multiply_32x32_rshift32(bandStereoL, tremBpL) << 1;
			bandStereoR = multiply_32x32_rshift32(bandStereoR, tremBpR) << 1;

			// HP: full stereo tremolo
			highStereoL = multiply_32x32_rshift32(highStereoL, tremHpL) << 1;
			highStereoR = multiply_32x32_rshift32(highStereoR, tremHpR) << 1;

			// Blend LP/BP/HP using buffer-level mix weights (scalar after stereo width)
			q31_t filteredL = multiply_32x32_rshift32(lowStereoL, lowMixQ)
			                  + multiply_32x32_rshift32(bandStereoL, bandMixQ)
			                  + multiply_32x32_rshift32(highStereoL, highMixQ);
			q31_t filteredR = multiply_32x32_rshift32(lowStereoR, lowMixQ)
			                  + multiply_32x32_rshift32(bandStereoR, bandMixQ)
			                  + multiply_32x32_rshift32(highStereoR, highMixQ);

			outL = filteredL << 1;
			outR = filteredR << 1;
#else
			// === Scalar fallback for non-NEON platforms ===
			// springOutL/R are spring-filtered, depth-scaled modulation signals
			// Compute filter cutoff: base + pitch + spring contribution
			q31_t lfoContribL = multiply_32x32_rshift32(springOutL, c.filterModDepth) << 1;
			q31_t lfoContribR = multiply_32x32_rshift32(springOutR, c.filterModDepth) << 1;
			q31_t cutoffL = add_saturate(filterBasePlusPitch, lfoContribL);
			q31_t cutoffR = add_saturate(filterBasePlusPitch, lfoContribR);

			// SVF feedback: LP output → cutoff (creates self-oscillation at high feedback)
			if (c.svfFeedbackQ != 0) {
				cutoffL = add_saturate(cutoffL, multiply_32x32_rshift32(s.svfLowL, c.svfFeedbackQ) << 1);
				cutoffR = add_saturate(cutoffR, multiply_32x32_rshift32(s.svfLowR, c.svfFeedbackQ) << 1);
			}

			cutoffL = std::clamp(cutoffL, kCutoffMin, kCutoffMax);
			cutoffR = std::clamp(cutoffR, kCutoffMin, kCutoffMax);

			// SVF processing (simplified 2-pole)
			// f = cutoff << 1 (extends frequency range, max ~8kHz at kCutoffMax)
			q31_t fL = cutoffL << 1;
			q31_t fR = cutoffR << 1;

			// Left channel
			q31_t highL = outL - s.svfLowL - multiply_32x32_rshift32(s.svfBandL, filterQ);
			s.svfBandL += multiply_32x32_rshift32(highL, fL) << 1;
			s.svfLowL += multiply_32x32_rshift32(s.svfBandL, fL) << 1;

			// Right channel
			q31_t highR = outR - s.svfLowR - multiply_32x32_rshift32(s.svfBandR, filterQ);
			s.svfBandR += multiply_32x32_rshift32(highR, fR) << 1;
			s.svfLowR += multiply_32x32_rshift32(s.svfBandR, fR) << 1;

			// Per-band stereo width: cross-blend each band toward mono
			// LP gets narrow stereo (focused bass), BP moderate, HP full width
			q31_t lowMono = (s.svfLowL >> 1) + (s.svfLowR >> 1);
			q31_t lowStereoL = lowMono + (multiply_32x32_rshift32(s.svfLowL - lowMono, c.lpStereoWidthQ) << 1);
			q31_t lowStereoR = lowMono + (multiply_32x32_rshift32(s.svfLowR - lowMono, c.lpStereoWidthQ) << 1);

			q31_t bandMono = (s.svfBandL >> 1) + (s.svfBandR >> 1);
			q31_t bandStereoL = bandMono + (multiply_32x32_rshift32(s.svfBandL - bandMono, c.bpStereoWidthQ) << 1);
			q31_t bandStereoR = bandMono + (multiply_32x32_rshift32(s.svfBandR - bandMono, c.bpStereoWidthQ) << 1);

			q31_t highMono = (highL >> 1) + (highR >> 1);
			q31_t highStereoL = highMono + (multiply_32x32_rshift32(highL - highMono, c.hpStereoWidthQ) << 1);
			q31_t highStereoR = highMono + (multiply_32x32_rshift32(highR - highMono, c.hpStereoWidthQ) << 1);

			// Per-band tremolo with rectification (cut-only, stereo varies by band)
			// Split bipolar LFO into positive and rectified negative parts
			// Use saturating negation to avoid overflow when value is INT32_MIN
			q31_t tremPosL = (processedTremL > 0) ? processedTremL : 0;
			q31_t tremNegL = (processedTremL < 0) ? (processedTremL == INT32_MIN ? INT32_MAX : -processedTremL) : 0;
			q31_t tremPosR = (processedTremR > 0) ? processedTremR : 0;
			q31_t tremNegR = (processedTremR < 0) ? (processedTremR == INT32_MIN ? INT32_MAX : -processedTremR) : 0;

			// Per-band unipolar LFO: positive + (rectified negative * rectifyAmount)
			// rectify=1: full-wave (smooth), rectify=0: half-wave (choppy)
			q31_t uniLpL = add_saturate(tremPosL, multiply_32x32_rshift32(tremNegL, c.tremRectifyLpQ) << 1);
			q31_t uniLpR = add_saturate(tremPosR, multiply_32x32_rshift32(tremNegR, c.tremRectifyLpQ) << 1);
			q31_t uniBpL = add_saturate(tremPosL, multiply_32x32_rshift32(tremNegL, c.tremRectifyBpQ) << 1);
			q31_t uniBpR = add_saturate(tremPosR, multiply_32x32_rshift32(tremNegR, c.tremRectifyBpQ) << 1);
			q31_t uniHpL = add_saturate(tremPosL, multiply_32x32_rshift32(tremNegL, c.tremRectifyHpQ) << 1);
			q31_t uniHpR = add_saturate(tremPosR, multiply_32x32_rshift32(tremNegR, c.tremRectifyHpQ) << 1);

			// Cut-only tremolo: 1 - depth * unipolar (uni=0: no cut, uni=max: max cut)
			q31_t tremLpL = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniLpL) << 1);
			q31_t tremLpR = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniLpR) << 1);
			q31_t tremBpL = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniBpL) << 1);
			q31_t tremBpR = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniBpR) << 1);
			q31_t tremHpL = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniHpL) << 1);
			q31_t tremHpR = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniHpR) << 1);

			// LP: mono tremolo (no stereo pulsing in bass)
			q31_t tremLpMono = (tremLpL >> 1) + (tremLpR >> 1);
			lowStereoL = multiply_32x32_rshift32(lowStereoL, tremLpMono) << 1;
			lowStereoR = multiply_32x32_rshift32(lowStereoR, tremLpMono) << 1;

			// BP: full stereo tremolo
			bandStereoL = multiply_32x32_rshift32(bandStereoL, tremBpL) << 1;
			bandStereoR = multiply_32x32_rshift32(bandStereoR, tremBpR) << 1;

			// HP: full stereo tremolo
			highStereoL = multiply_32x32_rshift32(highStereoL, tremHpL) << 1;
			highStereoR = multiply_32x32_rshift32(highStereoR, tremHpR) << 1;

			// Blend LP/BP/HP using buffer-level mix weights
			q31_t filteredL = multiply_32x32_rshift32(lowStereoL, lowMixQ)
			                  + multiply_32x32_rshift32(bandStereoL, bandMixQ)
			                  + multiply_32x32_rshift32(highStereoL, highMixQ);
			q31_t filteredR = multiply_32x32_rshift32(lowStereoR, lowMixQ)
			                  + multiply_32x32_rshift32(bandStereoR, bandMixQ)
			                  + multiply_32x32_rshift32(highStereoR, highMixQ);

			outL = filteredL << 1;
			outR = filteredR << 1;
#endif
		}

		// Comb filter (flanger effect)
		if (combEnabled) {
			constexpr int32_t kCombSize = static_cast<int32_t>(AutomodulatorParams::kCombBufferSize);

			// Delay calculation in 16.16 fixed-point (LFO delta already slew-limited)
			// Use processed values (includes manual offset and depth scaling)
			int32_t lfo16L = processedCombL >> 15;
			int32_t lfo16R = processedCombR >> 15;
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
			int32_t combIdx = static_cast<int32_t>(s.combIdx);

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

			// Linear interpolation between adjacent samples
			// sample0 is at delayInt, sample1 is at delayInt+1
			// frac=0 → sample0, frac=1 → sample1
			q31_t combOutL = sample0L + (multiply_32x32_rshift32(sample1L - sample0L, fracQL) << 1);
			q31_t combOutR = sample0R + (multiply_32x32_rshift32(sample1R - sample0R, fracQR) << 1);

			// Mono collapse (hoisted check)
			if (doCombMonoCollapse) {
				q31_t combMonoOut = (combOutL >> 1) + (combOutR >> 1);
				combOutL += multiply_32x32_rshift32(combMonoOut - combOutL, c.combMonoCollapseQ) << 1;
				combOutR += multiply_32x32_rshift32(combMonoOut - combOutR, c.combMonoCollapseQ) << 1;
			}

			// Feedback comb: write input + scaled delayed back to buffer
			q31_t feedbackL = multiply_32x32_rshift32(combOutL, c.combFeedback) << 1;
			q31_t feedbackR = multiply_32x32_rshift32(combOutR, c.combFeedback) << 1;
			params.combBufferL[s.combIdx] = outL + feedbackL;
			params.combBufferR[s.combIdx] = outR + feedbackR;
			if (++s.combIdx >= AutomodulatorParams::kCombBufferSize) {
				s.combIdx = 0;
			}

			// Mix comb output with dry signal
			outL = outL + (multiply_32x32_rshift32(combOutL, c.combMixQ) << 1);
			outR = outR + (multiply_32x32_rshift32(combOutR, c.combMixQ) << 1);
		}

		// Note: Tremolo is now applied per-band in the filter mixing section above
		// (with per-band rectification and frequency-dependent stereo width)

		// Wet/dry blend: out = dry + (wet - dry) * mixFactor
		sample.l = dryL + (multiply_32x32_rshift32(outL - dryL, wetMixQ) << 1);
		sample.r = dryR + (multiply_32x32_rshift32(outR - dryR, wetMixQ) << 1);

// DEBUG_SPRING: Set to 1 to output spring input/output for oscilloscope testing
// L = spring input (pre-spring modulation signal), R = spring output (post-spring)
// DEBUG_SPRING_RAW: Shows raw 1/8 scale values without scale-up (to check for clipping source)
// DEBUG_COMB_LFO: Shows raw LFO values (full scale, no depth)
#define DEBUG_SPRING 0
#define DEBUG_SPRING_RAW 0
#define DEBUG_COMB_LFO 0

#if DEBUG_SPRING
		// Debug: L = spring input (scaled up 8x), R = spring output (scaled up 8x)
		sample.l = springInputL >> 2;
		sample.r = springOutL >> 2;
#elif DEBUG_SPRING_RAW
		// Debug: L = raw scaledModL (1/8 scale), R = raw spring pos (1/8 scale)
		// No scale-up - if these clip, the math is wrong
		sample.l = scaledModL;
		sample.r = s.springPosL;
#elif DEBUG_COMB_LFO
		// Debug: output raw LFO (before spring)
		sample.l = lfoL.value >> 2;
		sample.r = lfoR.value >> 2;
#endif

		// Increment spring output for per-sample interpolation
		springOutL = add_saturate(springOutL, springDeltaL);
		springOutR = add_saturate(springOutR, springDeltaR);

		// Increment LFO values and check for segment crossings
		// When a channel exhausts its segment, refresh via updateLfoAccum
		// Raw values (for IIR tracking) use raw deltas
		lfoL.value = add_saturate(lfoL.value, lfoL.delta);
		lfoR.value = add_saturate(lfoR.value, lfoR.delta);
		combLfoL.value = add_saturate(combLfoL.value, combLfoL.delta);
		combLfoR.value = add_saturate(combLfoR.value, combLfoR.delta);
		tremLfoL.value = add_saturate(tremLfoL.value, tremLfoL.delta);
		tremLfoR.value = add_saturate(tremLfoR.value, tremLfoR.delta);

		// Processed values (for DSP) use depth-scaled deltas
		processedCombL = add_saturate(processedCombL, combDeltaL);
		processedCombR = add_saturate(processedCombR, combDeltaR);
		processedTremL = add_saturate(processedTremL, tremDeltaL);
		processedTremR = add_saturate(processedTremR, tremDeltaR);

		// Interpolate filter mix values
		lowMixQ = add_saturate(lowMixQ, lowMixDeltaQ);
		bandMixQ = add_saturate(bandMixQ, bandMixDeltaQ);
		highMixQ = add_saturate(highMixQ, highMixDeltaQ);

		// Advance phases
		lfoPhaseL += phaseInc;
		lfoPhaseR += phaseInc;
		combPhaseL += phaseInc;
		combPhaseR += phaseInc;
		tremPhaseL += phaseInc;
		tremPhaseR += phaseInc;

		// Decrement remaining counters, use precomputed values on segment crossing
		if (--lfoLRemaining == 0) {
			int8_t newSeg = (s.lfoIirL.segment + 1) % 5;
			s.lfoIirL.segment = newSeg;
			lfoL.value = c.wavetable.segAmpQ[newSeg]; // Reset to segment start
			lfoL.delta = s.stepPerSegment[newSeg];
			lfoLRemaining = s.samplesPerSegment[newSeg];
		}
		if (--lfoRRemaining == 0) {
			int8_t newSeg = (s.lfoIirR.segment + 1) % 5;
			s.lfoIirR.segment = newSeg;
			lfoR.value = c.wavetable.segAmpQ[newSeg];
			lfoR.delta = s.stepPerSegment[newSeg];
			lfoRRemaining = s.samplesPerSegment[newSeg];
		}
		if (--combLRemaining == 0) {
			int8_t newSeg = (s.combLfoIirL.segment + 1) % 5;
			s.combLfoIirL.segment = newSeg;
			combLfoL.value = c.wavetable.segAmpQ[newSeg];
			combLfoL.delta = s.stepPerSegment[newSeg];
			combLRemaining = s.samplesPerSegment[newSeg];
			// Also reset processed value: (raw + manual) * depth
			q31_t rawVal = c.wavetable.segAmpQ[newSeg];
			processedCombL = multiply_32x32_rshift32(add_saturate(rawVal, manualOffset), depthMultQ31) << 1;
			combDeltaL = multiply_32x32_rshift32(s.stepPerSegment[newSeg], depthMultQ31) << 1;
		}
		if (--combRRemaining == 0) {
			int8_t newSeg = (s.combLfoIirR.segment + 1) % 5;
			s.combLfoIirR.segment = newSeg;
			combLfoR.value = c.wavetable.segAmpQ[newSeg];
			combLfoR.delta = s.stepPerSegment[newSeg];
			combRRemaining = s.samplesPerSegment[newSeg];
			q31_t rawVal = c.wavetable.segAmpQ[newSeg];
			processedCombR = multiply_32x32_rshift32(add_saturate(rawVal, manualOffset), depthMultQ31) << 1;
			combDeltaR = multiply_32x32_rshift32(s.stepPerSegment[newSeg], depthMultQ31) << 1;
		}
		if (--tremLRemaining == 0) {
			int8_t newSeg = (s.tremLfoIirL.segment + 1) % 5;
			s.tremLfoIirL.segment = newSeg;
			tremLfoL.value = c.wavetable.segAmpQ[newSeg];
			tremLfoL.delta = s.stepPerSegment[newSeg];
			tremLRemaining = s.samplesPerSegment[newSeg];
			// NOTE: Don't reset processedTremL here - it's now spring-filtered and interpolated
			// The raw LFO state (tremLfoL) is still updated for IIR tracking, but the spring
			// filter provides smoothing so we don't need to reset the processed output
		}
		if (--tremRRemaining == 0) {
			int8_t newSeg = (s.tremLfoIirR.segment + 1) % 5;
			s.tremLfoIirR.segment = newSeg;
			tremLfoR.value = c.wavetable.segAmpQ[newSeg];
			tremLfoR.delta = s.stepPerSegment[newSeg];
			tremRRemaining = s.samplesPerSegment[newSeg];
			// NOTE: Don't reset processedTremR here - spring filter provides smoothing
		}
	}

	// Write back RAW accumulated LFO values for next buffer (no manual offset, no depth scaling)
	// This preserves correct waveform tracking - manual offset is applied to separate processed
	// variables for DSP use only. Segment crossing handling keeps raw values clean.
	s.lfoIirL.value = lfoL.value;
	s.lfoIirR.value = lfoR.value;
	s.combLfoIirL.value = combLfoL.value;
	s.combLfoIirR.value = combLfoR.value;
	s.tremLfoIirL.value = tremLfoL.value;
	s.tremLfoIirR.value = tremLfoR.value;

#if ENABLE_FX_BENCHMARK
	if (doBench) {
		benchLoop.stop();
		benchTotal.stop();
	}
#endif
}

} // namespace deluge::dsp
