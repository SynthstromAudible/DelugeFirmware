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

	// Segment start amplitudes as bipolar q31 [-ONE_Q31, ONE_Q31]
	// Keep native bipolar range from phi triangle banks
	for (int i = 0; i < 5; i++) {
		bank.segAmpQ[i] = static_cast<q31_t>(bank.segAmp[i] * 2147483647.0f);
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

/// Update LFO using pure accumulation - add step each sample, no error correction
/// @param state LFO state: value=accumulated, intermediate=step, segment=current seg
/// @param phaseU32 Current phase position (for segment detection)
/// @param phaseInc Phase increment per sample
/// @param bank Wavetable configuration
/// @return LfoIncremental with current value and per-sample step
LfoIncremental updateLfoAccum(LfoIirState& state, uint32_t phaseU32, uint32_t phaseInc, const LfoWaypointBank& bank) {
	int8_t seg = findSegment(phaseU32, bank);

	// On segment change, compute new step
	if (seg != state.segment) {
		state.segment = seg;
		q31_t segEnd = (seg < 4) ? bank.segAmpQ[seg + 1] : bank.segAmpQ[0];

		// Compute ampDelta in 64-bit to avoid overflow for large bipolar swings
		// (e.g., -ONE_Q31 to +ONE_Q31 = ~4 billion, overflows int32)
		int64_t ampDelta64 = static_cast<int64_t>(segEnd) - static_cast<int64_t>(bank.segAmpQ[seg]);

		// step = ampDelta * phaseInc / segWidth
		// Split multiplication to avoid 64-bit overflow:
		// (ampDelta * phaseInc) >> 16, then * invSegWidthQ >> 32
		int64_t partial = (ampDelta64 * static_cast<int64_t>(phaseInc)) >> 16;
		int64_t step64 = (partial * static_cast<int64_t>(bank.invSegWidthQ[seg])) >> 32;
		state.intermediate = static_cast<q31_t>(step64);
	}

	// Return current accumulated value and step
	return {state.value, state.intermediate};
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
	// [0]=stereoOffset (unipolar), [1-4]=env influences (already bipolar in triangle config)
	auto modScalars = phi::evalTriangleBank<5>(modPhase, 1.0f, kModScalarBank);
	c.stereoPhaseOffsetRaw = modScalars[0];
	// Store env influences as q31 for integer-only per-buffer math
	c.envDepthInfluenceQ = static_cast<q31_t>(modScalars[1] * kQ31MaxFloat);
	c.envPhaseInfluenceQ = static_cast<q31_t>(modScalars[2] * kQ31MaxFloat);
	c.envDerivDepthInfluenceQ = static_cast<q31_t>(modScalars[3] * kQ31MaxFloat);
	c.envDerivPhaseInfluenceQ = static_cast<q31_t>(modScalars[4] * kQ31MaxFloat);

	// LFO rate and wavetable need special handling (multi-zone logic)
	float effectiveModPhase = params.modPhaseOffset + params.gammaPhase;
	LfoRateResult rateResult = getLfoRateFromMod(params.mod, effectiveModPhase);
	c.rateValue = rateResult.value;
	c.rateSyncLevel = rateResult.syncLevel;
	c.rateTriplet = rateResult.triplet;

	c.wavetable = getLfoWaypointBank(params.mod, effectiveModPhase);
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

	// IIR chase coefficient from LFO rate
	uint64_t rawCoeff = static_cast<uint64_t>(c.lfoInc) << 9;
	c.iirCoeff = static_cast<q31_t>(std::min(rawCoeff, static_cast<uint64_t>(0x40000000)));

	// Pre-compute stereo rate scale factor (avoids float division per buffer)
	// Slow LFOs get less stereo to avoid feeling stuck asymmetric
	float rateScaleFactor;
	if (rateResult.syncLevel > 0) {
		// Synced: Level 1→0.1, Level 9→1.0
		rateScaleFactor = std::clamp(static_cast<float>(rateResult.syncLevel) / 9.0f, 0.1f, 1.0f);
	}
	else {
		// Free: 0.5Hz→0.1, 5Hz→1.0
		rateScaleFactor = std::clamp(rateResult.value * 0.2f, 0.1f, 1.0f);
	}
	c.stereoRateScaleQ = static_cast<q31_t>(rateScaleFactor * kQ31MaxFloat);

	// === Batch evaluate flavor-derived scalar params ===
	// [0]=cutoffBase, [1]=resonance, [2]=modDepth, [3]=attack, [4]=release,
	// [5]=combStaticOffset, [6]=combLfoDepth, [7]=combPhaseOffset, [8]=combMonoCollapse
	auto flavorScalars = phi::evalTriangleBank<9>(flavorPhase, 1.0f, kFlavorScalarBank);

	// Map raw triangle outputs to param ranges
	c.filterCutoffBase = static_cast<q31_t>(flavorScalars[0] * kQ31MaxFloat);
	c.filterResonance = static_cast<q31_t>(flavorScalars[1] * 0.85f * kQ31MaxFloat);
	c.filterModDepth = static_cast<q31_t>(flavorScalars[2] * kQ31MaxFloat);
	c.envAttack = static_cast<q31_t>(std::pow(flavorScalars[3], 2.0f) * kQ31MaxFloat);
	c.envRelease = static_cast<q31_t>(std::pow(flavorScalars[4], 2.0f) * kQ31MaxFloat);
	c.combStaticOffset = flavorScalars[5];
	c.combLfoDepth = flavorScalars[6];
	c.combPhaseOffsetU32 = static_cast<uint32_t>(flavorScalars[7] * 4294967295.0f);
	c.combMonoCollapseQ = static_cast<q31_t>(flavorScalars[8] * kQ31MaxFloat);

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
	// [0]=combFeedback, [1]=combMix, [2]=tremoloDepth, [3]=tremoloPhaseOffset
	auto typeScalars = phi::evalTriangleBank<4>(typePhase, 1.0f, kTypeScalarBank);
	c.combFeedback = static_cast<q31_t>(typeScalars[0] * 0.85f * kQ31MaxFloat);
	c.combMixQ = static_cast<q31_t>(typeScalars[1] * kQ31MaxFloat);
	c.tremoloDepthQ = static_cast<q31_t>(typeScalars[2] * kQ31MaxFloat);
	c.tremoloBaseLevel = ONE_Q31 - c.tremoloDepthQ;
	c.tremPhaseOffset = static_cast<uint32_t>(typeScalars[3] * kPhaseMaxFloat);

	// Filter mix needs constant-power normalization (keep separate function)
	float effectiveTypePhase = params.typePhaseOffset + params.gammaPhase;
	FilterMix filterMix = getFilterMixFromType(params.type, effectiveTypePhase);
	c.filterMixLowQ = static_cast<q31_t>(filterMix.low * kQ31MaxFloat);
	c.filterMixBandQ = static_cast<q31_t>(filterMix.band * kQ31MaxFloat);
	c.filterMixHighQ = static_cast<q31_t>(filterMix.high * kQ31MaxFloat);

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
		wavetableChanged = true; // Need to reinit IIR states after cache is available
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

	// Envelope follower at buffer rate - one-pole filter on INPUT peaks
	if (peakL > s.envStateL) {
		s.envStateL += multiply_32x32_rshift32(peakL - s.envStateL, c.envAttack) << 1;
	}
	else {
		s.envStateL -= multiply_32x32_rshift32(s.envStateL - peakL, c.envRelease) << 1;
	}
	if (peakR > s.envStateR) {
		s.envStateR += multiply_32x32_rshift32(peakR - s.envStateR, c.envAttack) << 1;
	}
	else {
		s.envStateR -= multiply_32x32_rshift32(s.envStateR - peakR, c.envRelease) << 1;
	}

	// Derivative = change over this buffer
	q31_t rawDerivL = s.envStateL - envStartL;
	q31_t rawDerivR = s.envStateR - envStartR;
	// Smooth the derivative
	s.envDerivStateL += multiply_32x32_rshift32(rawDerivL - s.envDerivStateL, c.envAttack) << 1;
	s.envDerivStateR += multiply_32x32_rshift32(rawDerivR - s.envDerivStateR, c.envAttack) << 1;

	// Stage enables as bools (avoids float comparison per sample)
	const bool filterEnabled = (params.type > 0);
	const bool combEnabled = (params.type > 0) && params.hasCombBuffers();
	const bool tremEnabled = (params.type > 0);

	// Lazy-allocate comb buffers only when comb effect is first used
	// This saves 12KB per Sound when comb isn't needed
	if (params.type > 0 && !params.hasCombBuffers()) {
		params.allocateCombBuffers(); // May fail silently - comb just won't process
	}

	// Backup retrigger: reset LFO phase when voice count goes 0→N
	// (lastVoiceCount is reset to 0 in wontBeRenderedForAWhile when no voices)
	// Initial phase from phi triangle for varied attack character
	if (params.lastVoiceCount == 0 && voiceCount > 0) {
		float effectiveModPhase = params.modPhaseOffset + params.gammaPhase;
		s.lfoPhase = getLfoInitialPhaseFromMod(params.mod, effectiveModPhase);

		// Initialize all LFO states from the new phase
		// This ensures smooth startup without glitches
		initLfoIir(s.lfoIirL, s.lfoPhase, c.lfoInc, c.wavetable);
		initLfoIir(s.lfoIirR, s.lfoPhase, c.lfoInc, c.wavetable);
		initLfoIir(s.combLfoIirL, s.lfoPhase + c.combPhaseOffsetU32, c.lfoInc, c.wavetable);
		initLfoIir(s.combLfoIirR, s.lfoPhase + c.combPhaseOffsetU32, c.lfoInc, c.wavetable);
		initLfoIir(s.tremLfoIirL, s.lfoPhase + c.tremPhaseOffset, c.lfoInc, c.wavetable);
		initLfoIir(s.tremLfoIirR, s.lfoPhase + c.tremPhaseOffset, c.lfoInc, c.wavetable);
	}
	params.lastVoiceCount = voiceCount;

	// Scale stereo offset by LFO speed (pre-computed in cache)
	uint32_t stereoPhaseOffset = multiply_32x32_rshift32(c.stereoPhaseOffsetRaw, c.stereoRateScaleQ) << 1;

	// === Pure q31 envelope modulation math (no float conversions) ===
	// Macro scales overall modulation depth: 0.625 + 0.375 * (macro/ONE_Q31)
	constexpr q31_t kMacroBaseQ = 0x50000000;  // 0.625 in q31
	constexpr q31_t kMacroRangeQ = 0x30000000; // 0.375 in q31
	q31_t macroScaleQ = kMacroBaseQ + (multiply_32x32_rshift32(macro, kMacroRangeQ) << 1);

	// Pre-compute macro × influence products (all q31)
	q31_t macroDepthQ = multiply_32x32_rshift32(macroScaleQ, c.envDepthInfluenceQ) << 1;
	q31_t macroPhaseQ = multiply_32x32_rshift32(macroScaleQ, c.envPhaseInfluenceQ) << 1;
	q31_t macroDerivDepthQ = multiply_32x32_rshift32(macroScaleQ, c.envDerivDepthInfluenceQ) << 1;
	q31_t macroDerivPhaseQ = multiply_32x32_rshift32(macroScaleQ, c.envDerivPhaseInfluenceQ) << 1;

	// Derivative normalization: scale by 64 (matches 1/2^25 float normalization), clamp first
	constexpr q31_t kDerivClampThresh = ONE_Q31 >> 6;
	q31_t derivNormQL = std::clamp(s.envDerivStateL, -kDerivClampThresh, kDerivClampThresh) << 6;
	q31_t derivNormQR = std::clamp(s.envDerivStateR, -kDerivClampThresh, kDerivClampThresh) << 6;

	// Envelope scale contribution: macroDepth × envState × 2 (×2 via saturating add)
	q31_t envScaleL = multiply_32x32_rshift32(macroDepthQ, s.envStateL);
	q31_t envScaleR = multiply_32x32_rshift32(macroDepthQ, s.envStateR);
	envScaleL = add_saturate(envScaleL, envScaleL);
	envScaleR = add_saturate(envScaleR, envScaleR);

	// Derivative scale contribution: macroDerivDepth × derivNorm × 2
	q31_t derivScaleL = multiply_32x32_rshift32(macroDerivDepthQ, derivNormQL);
	q31_t derivScaleR = multiply_32x32_rshift32(macroDerivDepthQ, derivNormQR);
	derivScaleL = add_saturate(derivScaleL, derivScaleL);
	derivScaleR = add_saturate(derivScaleR, derivScaleR);

	// targetScale = macroScale + envScale + derivScale (saturating)
	q31_t targetScaleQL = add_saturate(add_saturate(macroScaleQ, envScaleL), derivScaleL);
	q31_t targetScaleQR = add_saturate(add_saturate(macroScaleQ, envScaleR), derivScaleR);

	// Phase push: macroPhase × envState + macroDerivPhase × derivNorm
	q31_t envPhaseL = multiply_32x32_rshift32(macroPhaseQ, s.envStateL) << 1;
	q31_t envPhaseR = multiply_32x32_rshift32(macroPhaseQ, s.envStateR) << 1;
	q31_t derivPhaseL = multiply_32x32_rshift32(macroDerivPhaseQ, derivNormQL) << 1;
	q31_t derivPhaseR = multiply_32x32_rshift32(macroDerivPhaseQ, derivNormQR) << 1;
	q31_t phasePushQL = add_saturate(envPhaseL, derivPhaseL);
	q31_t phasePushQR = add_saturate(envPhaseR, derivPhaseR);

	// Convert q31 phase push to uint32 phase offset (<<1 for full phase range)
	uint32_t targetPhasePushUL = static_cast<uint32_t>(phasePushQL) << 1;
	uint32_t targetPhasePushUR = static_cast<uint32_t>(phasePushQR) << 1;

	// Stereo scale factor from |targetScale| average
	q31_t absScaleL = (targetScaleQL >= 0) ? targetScaleQL : -targetScaleQL;
	q31_t absScaleR = (targetScaleQR >= 0) ? targetScaleQR : -targetScaleQR;
	q31_t stereoScaleFactorQ = (absScaleL >> 1) + (absScaleR >> 1);
	uint32_t targetStereoOffset = multiply_32x32_rshift32(stereoPhaseOffset, stereoScaleFactorQ) << 1;

	// Smooth toward targets (one-pole filter at buffer rate, ~12ms transition)
	s.smoothedScaleL += multiply_32x32_rshift32(targetScaleQL - s.smoothedScaleL, kModSmoothCoeffQ) << 1;
	s.smoothedScaleR += multiply_32x32_rshift32(targetScaleQR - s.smoothedScaleR, kModSmoothCoeffQ) << 1;
	// Phase push smoothing: use signed arithmetic for proper interpolation
	int32_t phaseDiffL = static_cast<int32_t>(targetPhasePushUL - s.smoothedPhasePushL);
	int32_t phaseDiffR = static_cast<int32_t>(targetPhasePushUR - s.smoothedPhasePushR);
	s.smoothedPhasePushL += static_cast<uint32_t>(multiply_32x32_rshift32(phaseDiffL, kModSmoothCoeffQ) << 1);
	s.smoothedPhasePushR += static_cast<uint32_t>(multiply_32x32_rshift32(phaseDiffR, kModSmoothCoeffQ) << 1);
	int32_t stereoDiff = static_cast<int32_t>(targetStereoOffset - s.smoothedStereoOffset);
	s.smoothedStereoOffset += static_cast<uint32_t>(multiply_32x32_rshift32(stereoDiff, kModSmoothCoeffQ) << 1);

	// Use smoothed values for the loop
	q31_t scaleQL = s.smoothedScaleL;
	q31_t scaleQR = s.smoothedScaleR;
	uint32_t phasePushL = s.smoothedPhasePushL;
	uint32_t phasePushR = s.smoothedPhasePushR;
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

	// Smooth filter mix toward targets (same ~12ms transition as other modulations)
	s.smoothedLowMixQ += multiply_32x32_rshift32(targetLowMixQ - s.smoothedLowMixQ, kModSmoothCoeffQ) << 1;
	s.smoothedBandMixQ += multiply_32x32_rshift32(targetBandMixQ - s.smoothedBandMixQ, kModSmoothCoeffQ) << 1;
	s.smoothedHighMixQ += multiply_32x32_rshift32(targetHighMixQ - s.smoothedHighMixQ, kModSmoothCoeffQ) << 1;

	// Use smoothed values in loop
	q31_t lowMixQ = s.smoothedLowMixQ;
	q31_t bandMixQ = s.smoothedBandMixQ;
	q31_t highMixQ = s.smoothedHighMixQ;

	// === Buffer-rate LFO computation using pure accumulation ===
	// Just add step each sample - no phase-based correction

	const size_t bufferSize = buffer.size();
	const uint32_t startPhase = s.lfoPhase;
	const uint32_t phaseInc = c.lfoInc;

	// Compute phases for each LFO channel (used for segment detection)
	uint32_t lfoPhaseL = startPhase + phasePushL;
	uint32_t lfoPhaseR = startPhase + scaledStereoOffset + phasePushR;
	uint32_t combPhaseL = startPhase + c.combPhaseOffsetU32;
	uint32_t combPhaseR = combPhaseL + scaledStereoOffset;
	uint32_t tremPhaseL = startPhase + c.tremPhaseOffset;
	uint32_t tremPhaseR = tremPhaseL + scaledStereoOffset;

	// Pure accumulation: get current value and step
	LfoIncremental lfoL = updateLfoAccum(s.lfoIirL, lfoPhaseL, phaseInc, c.wavetable);
	LfoIncremental lfoR = updateLfoAccum(s.lfoIirR, lfoPhaseR, phaseInc, c.wavetable);
	LfoIncremental combLfoL = updateLfoAccum(s.combLfoIirL, combPhaseL, phaseInc, c.wavetable);
	LfoIncremental combLfoR = updateLfoAccum(s.combLfoIirR, combPhaseR, phaseInc, c.wavetable);
	LfoIncremental tremLfoL = updateLfoAccum(s.tremLfoIirL, tremPhaseL, phaseInc, c.wavetable);
	LfoIncremental tremLfoR = updateLfoAccum(s.tremLfoIirR, tremPhaseR, phaseInc, c.wavetable);

	// Update phase for next buffer
	s.lfoPhase = startPhase + phaseInc * bufferSize;

	// === Pitch tracking (cached - only recompute when noteCode changes) ===
	// Scale filter cutoff and comb delay based on played note frequency
	if (noteCode != s.prevNoteCode) {
		s.prevNoteCode = noteCode;
		if (noteCode >= 0 && noteCode < 128) {
			// Filter cutoff: add pitch-relative offset (octave up → +0.125 of range)
			float pitchOctaves = (static_cast<float>(noteCode) - 60.0f) / 12.0f;
			float cutoffOffset = pitchOctaves * 0.125f;
			s.cachedPitchCutoffOffset = static_cast<q31_t>(cutoffOffset * kQ31MaxFloat);
			// Comb delay ratio in 16.16 fixed: inverse pitch (higher note = smaller ratio)
			float pitchRatio = std::clamp(fastPow2(-pitchOctaves), 0.25f, 4.0f);
			s.cachedPitchRatioQ16 = static_cast<int32_t>(pitchRatio * 65536.0f);
		}
		else {
			s.cachedPitchCutoffOffset = 0;
			s.cachedPitchRatioQ16 = 1 << 16; // 1.0 in 16.16
		}
	}
	// Apply cached pitch ratio to current base delay (16.16 × 16.16 → 16.16)
	const q31_t pitchCutoffOffset = s.cachedPitchCutoffOffset;
	int32_t pitchCombBaseDelay16 =
	    static_cast<int32_t>((static_cast<int64_t>(c.combBaseDelay16) * s.cachedPitchRatioQ16) >> 16);
	pitchCombBaseDelay16 = std::clamp(pitchCombBaseDelay16, c.combMinDelay16, c.combMaxDelay16);

	// Hoist loop-invariant filter constants
	constexpr q31_t kCutoffMin = 0x08000000;
	constexpr q31_t kCutoffMax = 0x78000000;
	const q31_t filterBasePlusPitch = add_saturate(add_saturate(kCutoffMin, c.filterCutoffBase), pitchCutoffOffset);
	const q31_t filterQ = ONE_Q31 - c.filterResonance;

	// Hoist comb mono collapse check
	const bool doCombMonoCollapse = (c.combMonoCollapseQ > 0);

#if ENABLE_FX_BENCHMARK
	if (doBench) {
		benchSetup.stop();
		benchLoop.start();
	}
#endif

	for (auto& sample : buffer) {
		q31_t outL = sample.l;
		q31_t outR = sample.r;

		// SVF Filter (auto-wah)
		if (filterEnabled) {
#if USE_NEON_SVF
			// === NEON vectorized SVF: process L/R in parallel ===
			// Pack L/R values into 2-lane vectors
			int32x2_t lfoVal = {lfoL.value, lfoR.value};
			int32x2_t scaleQ = {scaleQL, scaleQR};
			int32x2_t out = {outL, outR};
			int32x2_t svfLow = {s.svfLowL, s.svfLowR};
			int32x2_t svfBand = {s.svfBandL, s.svfBandR};
			int32x2_t filterQVec = vdup_n_s32(filterQ);
			int32x2_t modDepth = vdup_n_s32(c.filterModDepth);
			int32x2_t basePlusPitch = vdup_n_s32(filterBasePlusPitch);

			// LFO scaling: (lfoVal * scaleQ * 2) >> 32
			int32x2_t lfoScaled = vqdmulh_s32(lfoVal, scaleQ);

			// LFO contribution: (lfoScaled * modDepth * 2) >> 32
			int32x2_t lfoContrib = vqdmulh_s32(lfoScaled, modDepth);

			// Cutoff = clamp(basePlusPitch + lfoContrib)
			int32x2_t cutoff = vqadd_s32(basePlusPitch, lfoContrib);
			cutoff = vmax_s32(cutoff, vdup_n_s32(kCutoffMin));
			cutoff = vmin_s32(cutoff, vdup_n_s32(kCutoffMax));

			// f = cutoff >> 2
			int32x2_t f = vshr_n_s32(cutoff, 2);

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

			// Blend LP/BP/HP: filtered = (low * lowMix + band * bandMix + high * highMix) << 1
			int32x2_t lowMix = vdup_n_s32(lowMixQ);
			int32x2_t bandMix = vdup_n_s32(bandMixQ);
			int32x2_t highMix = vdup_n_s32(highMixQ);

			// vqdmulh gives (a*b*2)>>32, accumulate all three
			int32x2_t filtered = vqdmulh_s32(svfLow, lowMix);
			filtered = vadd_s32(filtered, vqdmulh_s32(svfBand, bandMix));
			filtered = vadd_s32(filtered, vqdmulh_s32(high, highMix));

			outL = vget_lane_s32(filtered, 0);
			outR = vget_lane_s32(filtered, 1);
#else
			// === Scalar fallback for non-NEON platforms ===
			// Apply envelope scale to raw LFO
			q31_t lfoScaledL = multiply_32x32_rshift32(lfoL.value, scaleQL) << 1;
			q31_t lfoScaledR = multiply_32x32_rshift32(lfoR.value, scaleQR) << 1;

			// Compute filter cutoff: base + pitch + LFO contribution
			q31_t lfoContribL = multiply_32x32_rshift32(lfoScaledL, c.filterModDepth) << 1;
			q31_t lfoContribR = multiply_32x32_rshift32(lfoScaledR, c.filterModDepth) << 1;
			q31_t cutoffL = std::clamp(add_saturate(filterBasePlusPitch, lfoContribL), kCutoffMin, kCutoffMax);
			q31_t cutoffR = std::clamp(add_saturate(filterBasePlusPitch, lfoContribR), kCutoffMin, kCutoffMax);

			// SVF processing (simplified 2-pole)
			q31_t fL = cutoffL >> 2;
			q31_t fR = cutoffR >> 2;

			// Left channel
			q31_t highL = outL - s.svfLowL - multiply_32x32_rshift32(s.svfBandL, filterQ);
			s.svfBandL += multiply_32x32_rshift32(highL, fL) << 1;
			s.svfLowL += multiply_32x32_rshift32(s.svfBandL, fL) << 1;

			// Right channel
			q31_t highR = outR - s.svfLowR - multiply_32x32_rshift32(s.svfBandR, filterQ);
			s.svfBandR += multiply_32x32_rshift32(highR, fR) << 1;
			s.svfLowR += multiply_32x32_rshift32(s.svfBandR, fR) << 1;

			// Blend LP/BP/HP using buffer-level mix weights
			q31_t filteredL = multiply_32x32_rshift32(s.svfLowL, lowMixQ)
			                  + multiply_32x32_rshift32(s.svfBandL, bandMixQ)
			                  + multiply_32x32_rshift32(highL, highMixQ);
			q31_t filteredR = multiply_32x32_rshift32(s.svfLowR, lowMixQ)
			                  + multiply_32x32_rshift32(s.svfBandR, bandMixQ)
			                  + multiply_32x32_rshift32(highR, highMixQ);

			outL = filteredL << 1;
			outR = filteredR << 1;
#endif
		}

		// Comb filter (flanger effect)
		if (combEnabled) {
			constexpr int32_t kCombSize = static_cast<int32_t>(AutomodulatorParams::kCombBufferSize);

			// Delay calculation in 16.16 fixed-point
			int32_t lfo16L = combLfoL.value >> 15;
			int32_t lfo16R = combLfoR.value >> 15;
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

			// Linear interpolation: out = sample0 + frac * (sample1 - sample0)
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

		// Tremolo (VCA)
		if (tremEnabled) {
#if USE_NEON_SVF
			// NEON vectorized tremolo
			int32x2_t tremLfo = {tremLfoL.value, tremLfoR.value};
			int32x2_t depthQ = vdup_n_s32(c.tremoloDepthQ);
			int32x2_t baseLevel = vdup_n_s32(c.tremoloBaseLevel);

			// tremMod = (lfo * depth * 2) >> 32
			int32x2_t tremMod = vqdmulh_s32(tremLfo, depthQ);
			// tremolo = saturate(baseLevel + tremMod)
			int32x2_t tremolo = vqadd_s32(baseLevel, tremMod);
			// out = (out * tremolo * 2) >> 32
			int32x2_t outVec = {outL, outR};
			outVec = vqdmulh_s32(outVec, tremolo);

			outL = vget_lane_s32(outVec, 0);
			outR = vget_lane_s32(outVec, 1);
#else
			q31_t tremModL = multiply_32x32_rshift32(tremLfoL.value, c.tremoloDepthQ) << 1;
			q31_t tremModR = multiply_32x32_rshift32(tremLfoR.value, c.tremoloDepthQ) << 1;
			q31_t tremoloL = add_saturate(c.tremoloBaseLevel, tremModL);
			q31_t tremoloR = add_saturate(c.tremoloBaseLevel, tremModR);

			outL = multiply_32x32_rshift32(outL, tremoloL) << 1;
			outR = multiply_32x32_rshift32(outR, tremoloR) << 1;
#endif
		}

		sample.l = outL;
		sample.r = outR;

#if USE_NEON_SVF
		// NEON vectorized LFO increment (4 at a time, then 2)
		int32x4_t lfoVals = {lfoL.value, lfoR.value, combLfoL.value, combLfoR.value};
		int32x4_t lfoDeltas = {lfoL.delta, lfoR.delta, combLfoL.delta, combLfoR.delta};
		lfoVals = vqaddq_s32(lfoVals, lfoDeltas);
		lfoL.value = vgetq_lane_s32(lfoVals, 0);
		lfoR.value = vgetq_lane_s32(lfoVals, 1);
		combLfoL.value = vgetq_lane_s32(lfoVals, 2);
		combLfoR.value = vgetq_lane_s32(lfoVals, 3);

		int32x2_t tremVals = {tremLfoL.value, tremLfoR.value};
		int32x2_t tremDeltas = {tremLfoL.delta, tremLfoR.delta};
		tremVals = vqadd_s32(tremVals, tremDeltas);
		tremLfoL.value = vget_lane_s32(tremVals, 0);
		tremLfoR.value = vget_lane_s32(tremVals, 1);
#else
		// Increment LFO values using saturating add to prevent overflow
		// qadd saturates to signed q31 range - allows small negative drift that self-corrects
		lfoL.value = add_saturate(lfoL.value, lfoL.delta);
		lfoR.value = add_saturate(lfoR.value, lfoR.delta);
		combLfoL.value = add_saturate(combLfoL.value, combLfoL.delta);
		combLfoR.value = add_saturate(combLfoR.value, combLfoR.delta);
		tremLfoL.value = add_saturate(tremLfoL.value, tremLfoL.delta);
		tremLfoR.value = add_saturate(tremLfoR.value, tremLfoR.delta);
#endif
	}

	// Write back accumulated LFO values for next buffer
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
