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

uint32_t getLfoInitialPhaseFromMod(uint16_t mod, float phaseOffset) {
	// Normalize mod to [0,1] and add phase offset
	double phase = static_cast<double>(mod) / 1023.0 + static_cast<double>(phaseOffset);

	// Evaluate triangle (0-1 output)
	float tri = phi::evalTriangle(phase, 1.0f, kLfoInitialPhaseTriangle);

	// Map [0,1] to full 32-bit phase range
	return static_cast<uint32_t>(tri * 4294967295.0f);
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
		// Map bipolar (-1,+1) to positive delta (0.4 to 1.0)
		// Higher minimum limits max slope to reduce aliasing
		deltas[i] = 0.4f + std::abs(raw[i]) * 0.6f;
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

q31_t evalLfoWavetableQ31(uint32_t phaseU32, const LfoWaypointBank& bank) {
	// Pure integer evaluation using precomputed segment data
	// Inline segment finding (4 comparisons)
	int8_t seg = (phaseU32 <= bank.phaseU32[0])   ? 0
	             : (phaseU32 <= bank.phaseU32[1]) ? 1
	             : (phaseU32 <= bank.phaseU32[2]) ? 2
	             : (phaseU32 <= bank.phaseU32[3]) ? 3
	                                              : 4;
	uint32_t phaseOffset = phaseU32 - bank.segStartU32[seg];
	// Scale phaseOffset to fit signed q31 range, compensate with extra shift at end
	q31_t scaledOffset = static_cast<q31_t>(phaseOffset >> 1);
	return bank.segAmpQ[seg] + (multiply_32x32_rshift32(scaledOffset, bank.segSlopeQ[seg]) << 2);
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
	// Clamp to q31 range to prevent overflow in accumulation
	return static_cast<q31_t>(std::clamp(step64, static_cast<int64_t>(INT32_MIN), static_cast<int64_t>(INT32_MAX)));
}

/// Update LFO using pure accumulation with segment-aware stepping
/// Returns value, delta, and samples until next segment boundary
/// @param state LFO state: value=accumulated, intermediate=step, segment=current seg, samplesRemaining=count
/// @param phaseU32 Current phase position (for segment detection)
/// @param phaseInc Phase increment per sample
/// @param bank Wavetable configuration
/// @param samplesPerSegment Precomputed samples per segment array (from AutomodDspState)
/// @return LfoIncremental with current value and per-sample step
LfoIncremental updateLfoAccum(LfoIirState& state, uint32_t phaseU32, uint32_t phaseInc, const LfoWaypointBank& bank,
                              const uint32_t* samplesPerSegment) {
	// Only do segment detection on first init (samplesRemaining==0)
	// Normal operation tracks segment via per-sample decrement in loop
	if (state.samplesRemaining == 0) {
		int8_t seg = findSegment(phaseU32, bank);
		state.segment = seg;
		state.value = evalLfoWavetableQ31(phaseU32, bank);
		state.intermediate = computeSegmentStep(seg, phaseInc, bank);
		state.samplesRemaining = samplesPerSegment[seg];
	}

	return {state.value, state.intermediate};
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
			s.samplesPerSegment[seg] = std::max<uint32_t>(segWidth / phaseInc, 1);
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
	state.intermediate =
	    static_cast<q31_t>(std::clamp(step64, static_cast<int64_t>(INT32_MIN), static_cast<int64_t>(INT32_MAX)));
	state.target = segEnd;
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
	auto modScalars = phi::evalTriangleBank<8>(modPhase, 1.0f, kModScalarBank);
	c.stereoPhaseOffsetRaw = static_cast<uint32_t>(modScalars[0] * kPhaseMaxFloat);
	// Store env influences as q31 for integer-only per-buffer math
	c.envDepthInfluenceQ = static_cast<q31_t>(modScalars[1] * kQ31MaxFloat);
	c.envPhaseInfluenceQ = static_cast<q31_t>(modScalars[2] * kQ31MaxFloat);
	c.envDerivDepthInfluenceQ = static_cast<q31_t>(modScalars[3] * kQ31MaxFloat);
	c.envDerivPhaseInfluenceQ = static_cast<q31_t>(modScalars[4] * kQ31MaxFloat);
	c.envValueInfluenceQ = static_cast<q31_t>(modScalars[5] * kQ31MaxFloat);

	// Spring coefficients computed later after LFO rate is known (for rate-proportional scaling)
	float springModFreq = modScalars[6]; // Save for later
	float springModDamp = modScalars[7]; // Save for later

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
			constexpr float kLog2_2000 = 10.9657843f; // log2(2000)
			float hz = 0.01f * exp2f(static_cast<float>(params.rate - 1) / 127.0f * kLog2_2000);
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
	float rateRatio = lfoHz / kCalibrationRate;
	float rateCompensation = sqrtf(rateRatio * sqrtf(rateRatio));
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

	// === Batch evaluate flavor-derived scalar params ===
	// [0]=cutoffBase, [1]=resonance, [2]=filterModDepth, [3]=attack, [4]=release,
	// [5]=combStaticOffset, [6]=combLfoDepth, [7]=combPhaseOffset, [8]=combMonoCollapse,
	// [9]=tremoloDepth, [10]=tremoloPhaseOffset
	auto flavorScalars = phi::evalTriangleBank<11>(flavorPhase, 1.0f, kFlavorScalarBank);

	// Map raw triangle outputs to param ranges
	// Note: freqOffset is applied dynamically in the DSP loop to support mod matrix routing
	c.filterCutoffBase = static_cast<q31_t>(flavorScalars[0] * kQ31MaxFloat);
	c.filterResonance = static_cast<q31_t>(flavorScalars[1] * 0.85f * kQ31MaxFloat);
	c.filterModDepth = static_cast<q31_t>(flavorScalars[2] * kQ31MaxFloat);
	c.envAttack = static_cast<q31_t>(flavorScalars[3] * flavorScalars[3] * kQ31MaxFloat);
	c.envRelease = static_cast<q31_t>(flavorScalars[4] * flavorScalars[4] * kQ31MaxFloat);
	c.combStaticOffset = flavorScalars[5];
	c.combLfoDepth = flavorScalars[6];
	c.combPhaseOffsetU32 = static_cast<uint32_t>(flavorScalars[7] * 4294967295.0f);
	c.combMonoCollapseQ = static_cast<q31_t>(flavorScalars[8] * kQ31MaxFloat);
	c.tremoloDepthQ = static_cast<q31_t>(flavorScalars[9] * kQ31MaxFloat * 0.5f); // Halved to reduce scratchiness
	c.tremPhaseOffset = static_cast<uint32_t>(flavorScalars[10] * kPhaseMaxFloat);

	// Pre-compute comb delay constants in 16.16 fixed-point
	constexpr int32_t kMinDelay = 4;
	constexpr int32_t kCombSize = static_cast<int32_t>(AutomodulatorParams::kCombBufferSize);
	constexpr int32_t kMaxDelay = kCombSize - 2; // Use full buffer (1534 samples at 1536 buffer)
	constexpr int32_t kMaxModRange = 400;
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
		// Track disabled buffers for deferred deallocation
		if (params.hasCombBuffers() && params.disabledBufferCount < AutomodulatorParams::kDeallocDelayBuffers) {
			params.disabledBufferCount++;
			if (params.disabledBufferCount >= AutomodulatorParams::kDeallocDelayBuffers) {
				params.deallocateCombBuffers();
			}
		}
		return;
	}

	// Reset disabled counter when active
	params.disabledBufferCount = 0;

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
	// R channels use stereo offset for L/R phase separation
	if (wavetableChanged) {
		uint32_t stereoOff = c.stereoPhaseOffsetRaw;
		initLfoIir(s.lfoIirL, s.lfoPhase, c.lfoInc, c.wavetable);
		initLfoIir(s.lfoIirR, s.lfoPhase + stereoOff, c.lfoInc, c.wavetable);
		initLfoIir(s.combLfoIirL, s.lfoPhase + c.combPhaseOffsetU32, c.lfoInc, c.wavetable);
		initLfoIir(s.combLfoIirR, s.lfoPhase + c.combPhaseOffsetU32 + stereoOff, c.lfoInc, c.wavetable);
		initLfoIir(s.tremLfoIirL, s.lfoPhase + c.tremPhaseOffset, c.lfoInc, c.wavetable);
		initLfoIir(s.tremLfoIirR, s.lfoPhase + c.tremPhaseOffset + stereoOff, c.lfoInc, c.wavetable);
		// Initialize processed values from raw LFO to prevent startup transient
		s.smoothedCombLfoL = s.combLfoIirL.value;
		s.smoothedCombLfoR = s.combLfoIirR.value;
		s.smoothedTremLfoL = s.tremLfoIirL.value;
		s.smoothedTremLfoR = s.tremLfoIirR.value;
	}

	// Store initial envelope state for derivative calculation
	q31_t envStartL = s.envStateL;
	q31_t envStartR = s.envStateR;

	// Envelope follower at buffer rate - one-pole filter on INPUT peaks
	if (peakL > s.envStateL) {
		q31_t deltaL = multiply_32x32_rshift32(peakL - s.envStateL, c.envAttack) << 1;
		s.envStateL += deltaL;
	}
	else {
		q31_t deltaL = multiply_32x32_rshift32(s.envStateL - peakL, c.envRelease) << 1;
		s.envStateL -= deltaL;
	}
	if (peakR > s.envStateR) {
		q31_t deltaR = multiply_32x32_rshift32(peakR - s.envStateR, c.envAttack) << 1;
		s.envStateR += deltaR;
	}
	else {
		q31_t deltaR = multiply_32x32_rshift32(s.envStateR - peakR, c.envRelease) << 1;
		s.envStateR -= deltaR;
	}

	// Derivative = change over this buffer
	q31_t rawDerivL = s.envStateL - envStartL;
	q31_t rawDerivR = s.envStateR - envStartR;
	// Smooth the derivative
	q31_t derivDeltaL = multiply_32x32_rshift32(rawDerivL - s.envDerivStateL, c.envAttack) << 1;
	q31_t derivDeltaR = multiply_32x32_rshift32(rawDerivR - s.envDerivStateR, c.envAttack) << 1;
	s.envDerivStateL += derivDeltaL;
	s.envDerivStateR += derivDeltaR;

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
		// R channels use stereo offset for L/R phase separation
		uint32_t stereoOff = c.stereoPhaseOffsetRaw;
		initLfoIir(s.lfoIirL, s.lfoPhase, c.lfoInc, c.wavetable);
		initLfoIir(s.lfoIirR, s.lfoPhase + stereoOff, c.lfoInc, c.wavetable);
		initLfoIir(s.combLfoIirL, s.lfoPhase + c.combPhaseOffsetU32, c.lfoInc, c.wavetable);
		initLfoIir(s.combLfoIirR, s.lfoPhase + c.combPhaseOffsetU32 + stereoOff, c.lfoInc, c.wavetable);
		initLfoIir(s.tremLfoIirL, s.lfoPhase + c.tremPhaseOffset, c.lfoInc, c.wavetable);
		initLfoIir(s.tremLfoIirR, s.lfoPhase + c.tremPhaseOffset + stereoOff, c.lfoInc, c.wavetable);
		// Initialize processed values from raw LFO to prevent startup transient
		s.smoothedCombLfoL = s.combLfoIirL.value;
		s.smoothedCombLfoR = s.combLfoIirR.value;
		s.smoothedTremLfoL = s.tremLfoIirL.value;
		s.smoothedTremLfoR = s.tremLfoIirR.value;
	}
	params.lastVoiceCount = voiceCount;
	params.lastHeldNotesCount = params.heldNotesCount;

	// Stereo offset directly from mod (no rate/depth scaling)
	uint32_t stereoPhaseOffset = c.stereoPhaseOffsetRaw;

	// === Pure q31 envelope modulation math (no float conversions) ===
	// Depth is bipolar q31: -ONE_Q31 = -100%, 0 = 0%, +ONE_Q31 = +100%
	// Param defaults to ONE_Q31 so knob center = 100%
	// Negative depth inverts the LFO polarity
	// Cap depth to ±(1-epsilon) to leave headroom for spring overshoot
	constexpr q31_t kMaxDepth = ONE_Q31 - (ONE_Q31 >> 4); // ~93.75% max
	q31_t depthScaleQ31 = std::clamp(depth, -kMaxDepth, kMaxDepth);

// Phase push feature flag - can be set via compiler: -DAUTOMOD_ENABLE_PHASE_PUSH=1
#ifndef AUTOMOD_ENABLE_PHASE_PUSH
#define AUTOMOD_ENABLE_PHASE_PUSH 0
#endif

	// Pre-compute depth × influence products (all q31)
	// Use absolute scale for envelope influences (envelope modulates magnitude)
	// Saturating abs: INT32_MIN (-2^31) negates to ONE_Q31 (avoids overflow)
	q31_t absScale = (depthScaleQ31 >= 0) ? depthScaleQ31 : (depthScaleQ31 > -ONE_Q31) ? -depthScaleQ31 : ONE_Q31;
	q31_t depthEnvQ = multiply_32x32_rshift32(absScale, c.envDepthInfluenceQ) << 1;
	q31_t depthDerivEnvQ = multiply_32x32_rshift32(absScale, c.envDerivDepthInfluenceQ) << 1;

	// Derivative normalization: scale by 64 (matches 1/2^25 float normalization), clamp first
	constexpr q31_t kDerivClampThresh = ONE_Q31 >> 6;
	q31_t derivNormQL = std::clamp(s.envDerivStateL, -kDerivClampThresh, kDerivClampThresh) << 6;
	q31_t derivNormQR = std::clamp(s.envDerivStateR, -kDerivClampThresh, kDerivClampThresh) << 6;

	// Envelope scale contribution: depthEnv × envState × 2
	q31_t envScaleL = multiply_32x32_rshift32(depthEnvQ, s.envStateL) << 1;
	q31_t envScaleR = multiply_32x32_rshift32(depthEnvQ, s.envStateR) << 1;

	// Derivative scale contribution: depthDerivEnv × derivNorm × 2
	q31_t derivScaleL = multiply_32x32_rshift32(depthDerivEnvQ, derivNormQL) << 1;
	q31_t derivScaleR = multiply_32x32_rshift32(depthDerivEnvQ, derivNormQR) << 1;

	// targetScale = depthScale + envScale + derivScale
	// All values in q31 format, negative scale inverts LFO polarity
	// Use saturating adds to prevent overflow when all contributions are large
	q31_t targetScaleQL = add_saturate(add_saturate(depthScaleQ31, envScaleL), derivScaleL);
	q31_t targetScaleQR = add_saturate(add_saturate(depthScaleQ31, envScaleR), derivScaleR);

	// Phase push computation (guarded - skip when disabled)
#if AUTOMOD_ENABLE_PHASE_PUSH
	q31_t depthPhaseQ = multiply_32x32_rshift32(absScale, c.envPhaseInfluenceQ) << 1;
	q31_t depthDerivPhaseQ = multiply_32x32_rshift32(absScale, c.envDerivPhaseInfluenceQ) << 1;
	q31_t envPhaseL = multiply_32x32_rshift32(depthPhaseQ, s.envStateL) << 1;
	q31_t envPhaseR = multiply_32x32_rshift32(depthPhaseQ, s.envStateR) << 1;
	q31_t derivPhaseL = multiply_32x32_rshift32(depthDerivPhaseQ, derivNormQL) << 1;
	q31_t derivPhaseR = multiply_32x32_rshift32(depthDerivPhaseQ, derivNormQR) << 1;
	uint32_t targetPhasePushUL = static_cast<uint32_t>(add_saturate(envPhaseL, derivPhaseL)) << 1;
	uint32_t targetPhasePushUR = static_cast<uint32_t>(add_saturate(envPhaseR, derivPhaseR)) << 1;
	int32_t phaseDiffL = static_cast<int32_t>(targetPhasePushUL - s.smoothedPhasePushL);
	int32_t phaseDiffR = static_cast<int32_t>(targetPhasePushUR - s.smoothedPhasePushR);
	s.smoothedPhasePushL += static_cast<uint32_t>(multiply_32x32_rshift32(phaseDiffL, kModSmoothCoeffQ) << 1);
	s.smoothedPhasePushR += static_cast<uint32_t>(multiply_32x32_rshift32(phaseDiffR, kModSmoothCoeffQ) << 1);
	uint32_t phasePushL = s.smoothedPhasePushL;
	uint32_t phasePushR = s.smoothedPhasePushR;
#else
	uint32_t phasePushL = 0;
	uint32_t phasePushR = 0;
#endif

	// Stereo offset smoothing
	uint32_t targetStereoOffset = stereoPhaseOffset;
	int32_t stereoDiff = static_cast<int32_t>(targetStereoOffset - s.smoothedStereoOffset);
	s.smoothedStereoOffset += static_cast<uint32_t>(multiply_32x32_rshift32(stereoDiff, kModSmoothCoeffQ) << 1);

	// Scale smoothing
	q31_t scaleDeltaL = multiply_32x32_rshift32(targetScaleQL - s.smoothedScaleL, kModSmoothCoeffQ) << 1;
	q31_t scaleDeltaR = multiply_32x32_rshift32(targetScaleQR - s.smoothedScaleR, kModSmoothCoeffQ) << 1;
	s.smoothedScaleL += scaleDeltaL;
	s.smoothedScaleR += scaleDeltaR;

	// Use smoothed values for the loop
	q31_t scaleQL = s.smoothedScaleL;
	q31_t scaleQR = s.smoothedScaleR;
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
	// Use single smoothed value per buffer (no per-sample interpolation needed - changes slowly)
	s.smoothedLowMixQ += multiply_32x32_rshift32(targetLowMixQ - s.smoothedLowMixQ, kModSmoothCoeffQ) << 1;
	s.smoothedBandMixQ += multiply_32x32_rshift32(targetBandMixQ - s.smoothedBandMixQ, kModSmoothCoeffQ) << 1;
	s.smoothedHighMixQ += multiply_32x32_rshift32(targetHighMixQ - s.smoothedHighMixQ, kModSmoothCoeffQ) << 1;

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

	// Get initial LFO values - remaining counts stored in state, use precomputed step from s.stepPerSegment
	LfoIncremental lfoL = updateLfoAccum(s.lfoIirL, lfoPhaseL, phaseInc, c.wavetable, s.samplesPerSegment);
	LfoIncremental lfoR = updateLfoAccum(s.lfoIirR, lfoPhaseR, phaseInc, c.wavetable, s.samplesPerSegment);
	LfoIncremental combLfoL = updateLfoAccum(s.combLfoIirL, combPhaseL, phaseInc, c.wavetable, s.samplesPerSegment);
	LfoIncremental combLfoR = updateLfoAccum(s.combLfoIirR, combPhaseR, phaseInc, c.wavetable, s.samplesPerSegment);
	LfoIncremental tremLfoL = updateLfoAccum(s.tremLfoIirL, tremPhaseL, phaseInc, c.wavetable, s.samplesPerSegment);
	LfoIncremental tremLfoR = updateLfoAccum(s.tremLfoIirR, tremPhaseR, phaseInc, c.wavetable, s.samplesPerSegment);

	// Copy remaining counts to locals for per-sample loop (written back at end)
	uint32_t lfoLRemaining = s.lfoIirL.samplesRemaining;
	uint32_t lfoRRemaining = s.lfoIirR.samplesRemaining;
	uint32_t combLRemaining = s.combLfoIirL.samplesRemaining;
	uint32_t combRRemaining = s.combLfoIirR.samplesRemaining;
	uint32_t tremLRemaining = s.tremLfoIirL.samplesRemaining;
	uint32_t tremRRemaining = s.tremLfoIirR.samplesRemaining;

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

	// For trem/comb, use persisted processed values (smoothed LFO state) to avoid
	// discontinuities at buffer boundaries when segment resets cause raw LFO jumps.
	// Values are stored in RAW (unscaled) space; depth scaling applied at use points.
	// This allows depth changes without rescaling stored state.
	q31_t processedTremL = s.smoothedTremLfoL;
	q31_t processedTremR = s.smoothedTremLfoR;
	q31_t processedCombL = s.smoothedCombLfoL;
	q31_t processedCombR = s.smoothedCombLfoR;

	// Deltas are RAW (unscaled) - slew limiting operates in raw space for consistent feel
	// Depth scaling applied at point of use (tremolo gain, comb delay calculation)
	q31_t tremDeltaL = tremLfoL.delta;
	q31_t tremDeltaR = tremLfoR.delta;
	q31_t combDeltaL = combLfoL.delta;
	q31_t combDeltaR = combLfoR.delta;

	// === Spring filter on filter LFO modulation signal (buffer-rate 2nd-order LPF) ===
	// Signal flow: (lfoL.value + manualOffset) + envValue → × scaleQL → spring → filter cutoff
	// Spring output is separate from LFO state to avoid corrupting segment tracking
	//
	// FUTURE: Alternative "impulse-excited spring" LFO mode could replace multi-segment triangle
	// with periodic impulses that excite the spring directly. The spring's natural resonance
	// would create the waveform (like plucked strings). Impulse rate = LFO rate, spring freq/damp
	// control timbre. Would give organic, emergent shapes with built-in anti-aliasing.

	// Compute spring input: LFO + envValue, then depth, then manual (post-depth override)
	// Scale each down by 16 before adding (max sum ~0.19, safe without saturation)
	q31_t springTargetL = lfoL.value >> 4;
	q31_t springTargetR = lfoR.value >> 4;
	if (c.envValueInfluenceQ != 0) {
		// Env contrib: multiply gives ~1/2 scale, >> 3 more = 1/16 scale to match
		q31_t envContribL = multiply_32x32_rshift32(s.envStateL, c.envValueInfluenceQ) >> 3;
		q31_t envContribR = multiply_32x32_rshift32(s.envStateR, c.envValueInfluenceQ) >> 3;
		springTargetL += envContribL;
		springTargetR += envContribR;
	}

	// Apply depth scaling at buffer rate (use raw targetScale, spring handles smoothing)
	// springTargetL is at 1/16 scale, multiply halves again = 1/32 scale
	// << 1 restores to 1/16 scale = 16x headroom for spring overshoot
	q31_t scaledModL = multiply_32x32_rshift32(springTargetL, targetScaleQL) << 1;
	q31_t scaledModR = multiply_32x32_rshift32(springTargetR, targetScaleQR) << 1;

	// Add manual offset POST depth - allows manual to override regardless of depth setting
	// Use saturating add capped at 1/8 scale (spring's designed input range with 8x headroom)
	constexpr q31_t kSpringInputLimit = ONE_Q31 >> 3;
	scaledModL = std::clamp(add_saturate(scaledModL, manualOffset >> 4), -kSpringInputLimit, kSpringInputLimit);
	scaledModR = std::clamp(add_saturate(scaledModR, manualOffset >> 4), -kSpringInputLimit, kSpringInputLimit);

	// Save previous spring positions for interpolation
	q31_t prevSpringPosL = s.springPosL;
	q31_t prevSpringPosR = s.springPosR;

	// Spring filter update (2nd-order LPF with resonance)
	// Semi-implicit Euler: vel += k*error - c*vel, pos += vel
	// Coefficients already include dt scaling
	// Input is at 1/8 scale with 8x headroom - spring bounded by damping
	// Use 64-bit arithmetic to prevent overflow (negation of INT32_MIN overflows)
	{
		// Safe subtraction via 64-bit
		auto sub64 = [](q31_t a, q31_t b) -> q31_t {
			int64_t result = static_cast<int64_t>(a) - static_cast<int64_t>(b);
			return static_cast<q31_t>(
			    std::clamp(result, static_cast<int64_t>(INT32_MIN), static_cast<int64_t>(INT32_MAX)));
		};

		q31_t errorL = sub64(scaledModL, s.springPosL);
		q31_t forceL = multiply_32x32_rshift32(errorL, c.springOmega2Q) << 1;
		q31_t dampL = multiply_32x32_rshift32(s.springVelL, c.springDampingCoeffQ) << 1;
		q31_t netForceL = sub64(forceL, dampL);
		s.springVelL = add_saturate(s.springVelL, netForceL);
		s.springPosL = add_saturate(s.springPosL, s.springVelL);

		q31_t errorR = sub64(scaledModR, s.springPosR);
		q31_t forceR = multiply_32x32_rshift32(errorR, c.springOmega2Q) << 1;
		q31_t dampR = multiply_32x32_rshift32(s.springVelR, c.springDampingCoeffQ) << 1;
		q31_t netForceR = sub64(forceR, dampR);
		s.springVelR = add_saturate(s.springVelR, netForceR);
		s.springPosR = add_saturate(s.springPosR, s.springVelR);
	}

	// Compute per-sample delta for smooth interpolation within buffer
	// Scale up by 16 to compensate for input scaling (gives 16x headroom for overshoot)
	// For buffer size N: delta = (newPos - oldPos) * 16 / N = (diff) >> (log2(N) - 4)
	int bufferLog2 = 31 - __builtin_clz(std::max(static_cast<unsigned>(bufferSize), 1u));
	int deltaShift = bufferLog2 - 4; // Combine /N and *16 into single shift
	// Use 64-bit subtraction to avoid overflow from negation
	q31_t diffL =
	    static_cast<q31_t>(std::clamp(static_cast<int64_t>(s.springPosL) - static_cast<int64_t>(prevSpringPosL),
	                                  static_cast<int64_t>(INT32_MIN), static_cast<int64_t>(INT32_MAX)));
	q31_t diffR =
	    static_cast<q31_t>(std::clamp(static_cast<int64_t>(s.springPosR) - static_cast<int64_t>(prevSpringPosR),
	                                  static_cast<int64_t>(INT32_MIN), static_cast<int64_t>(INT32_MAX)));
	// Use saturating shift for left shift case (small buffers)
	q31_t springDeltaL, springDeltaR;
	if (deltaShift >= 0) {
		springDeltaL = diffL >> deltaShift;
		springDeltaR = diffR >> deltaShift;
	}
	else {
		// Left shift - clamp to prevent overflow
		springDeltaL = std::clamp(diffL << (-deltaShift), -ONE_Q31, ONE_Q31);
		springDeltaR = std::clamp(diffR << (-deltaShift), -ONE_Q31, ONE_Q31);
	}

	// Spring output for filter modulation (separate from lfoL/lfoR to preserve LFO state)
	// Scale up by 16 to restore original amplitude (spring operates at 1/16 scale for headroom)
	// Clip values exceeding 1/16 scale range before scaling up
	auto clipAndScale = [](q31_t x) -> q31_t {
		constexpr q31_t kClipLimit = ONE_Q31 >> 4;
		return std::clamp(x, -kClipLimit, kClipLimit) << 4;
	};
	q31_t springOutL = clipAndScale(prevSpringPosL);
	q31_t springOutR = clipAndScale(prevSpringPosR);

	// Tremolo uses processedTremL/R directly with tremDeltaL/R (no spring smoothing)

	// Use smoothed filter mix values directly (no per-sample interpolation - changes slowly)
	const q31_t lowMixQ = s.smoothedLowMixQ;
	const q31_t bandMixQ = s.smoothedBandMixQ;
	const q31_t highMixQ = s.smoothedHighMixQ;

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
	// Raw deltas clamped here; depth scaling applied at use time
	// At max depth: ~1.35Hz max full-depth modulation; proportionally slower at lower depths
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

	// Hoist NEON constants and checks outside loop
#if USE_NEON_SVF
	const int32x2_t filterQVec = vdup_n_s32(filterQ);
	const int32x2_t modDepthVec = vdup_n_s32(c.filterModDepth);
	const int32x2_t basePlusPitchVec = vdup_n_s32(filterBasePlusPitch);
	const int32x2_t feedbackVec = vdup_n_s32(c.svfFeedbackQ);
	const int32x2_t cutoffMinVec = vdup_n_s32(kCutoffMin);
	const int32x2_t cutoffMaxVec = vdup_n_s32(kCutoffMax);
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
			int32x2_t springVal = {springOutL, springOutR};
			int32x2_t out = {outL, outR};
			int32x2_t svfLow = {s.svfLowL, s.svfLowR};
			int32x2_t svfBand = {s.svfBandL, s.svfBandR};

			// LFO contribution: spring output × modDepth
			int32x2_t lfoContrib = vqdmulh_s32(springVal, modDepthVec);

			// Cutoff = basePlusPitch + lfoContrib + feedbackContrib
			// (when feedbackVec=0, feedbackContrib=0 - no branch needed)
			int32x2_t cutoff = vqadd_s32(basePlusPitchVec, lfoContrib);
			int32x2_t feedbackContrib = vqdmulh_s32(svfLow, feedbackVec);
			cutoff = vqadd_s32(cutoff, feedbackContrib);

			cutoff = vmax_s32(cutoff, cutoffMinVec);
			cutoff = vmin_s32(cutoff, cutoffMaxVec);

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

			// Apply depth first, then manual offset (post-depth override)
			// Use saturating add: depth-scaled LFO and manual are both full-scale q31
			q31_t scaledTremL = add_saturate(multiply_32x32_rshift32(processedTremL, depthMultQ31) << 1, manualOffset);
			q31_t scaledTremR = add_saturate(multiply_32x32_rshift32(processedTremR, depthMultQ31) << 1, manualOffset);
			q31_t uniTremL = (scaledTremL >> 1) + (ONE_Q31 >> 1);
			q31_t uniTremR = (scaledTremR >> 1) + (ONE_Q31 >> 1);

			// Tremolo gain: 1 - depth * unipolar
			q31_t tremGainL = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniTremL) << 1);
			q31_t tremGainR = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniTremR) << 1);

			// LP: mono tremolo (no stereo pulsing in bass)
			q31_t tremMono = (tremGainL >> 1) + (tremGainR >> 1);
			q31_t lowTremL = multiply_32x32_rshift32(s.svfLowL, tremMono) << 1;
			q31_t lowTremR = multiply_32x32_rshift32(s.svfLowR, tremMono) << 1;

			// BP/HP: full stereo tremolo
			q31_t bandTremL = multiply_32x32_rshift32(s.svfBandL, tremGainL) << 1;
			q31_t bandTremR = multiply_32x32_rshift32(s.svfBandR, tremGainR) << 1;
			q31_t highTremL = multiply_32x32_rshift32(highL, tremGainL) << 1;
			q31_t highTremR = multiply_32x32_rshift32(highR, tremGainR) << 1;

			// Blend LP/BP/HP using buffer-level mix weights
			q31_t filteredL = multiply_32x32_rshift32(lowTremL, lowMixQ) + multiply_32x32_rshift32(bandTremL, bandMixQ)
			                  + multiply_32x32_rshift32(highTremL, highMixQ);
			q31_t filteredR = multiply_32x32_rshift32(lowTremR, lowMixQ) + multiply_32x32_rshift32(bandTremR, bandMixQ)
			                  + multiply_32x32_rshift32(highTremR, highMixQ);

			outL = filteredL << 1;
			outR = filteredR << 1;
#else
			// === Scalar fallback for non-NEON platforms ===
			// springOutL/R are spring-filtered, depth-scaled modulation signals
			// Compute filter cutoff: base + pitch + spring contribution (clamped below)
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
			q31_t highL = outL - s.svfLowL - (multiply_32x32_rshift32(s.svfBandL, filterQ) << 1);
			s.svfBandL += multiply_32x32_rshift32(highL, fL) << 1;
			s.svfLowL += multiply_32x32_rshift32(s.svfBandL, fL) << 1;

			// Right channel
			q31_t highR = outR - s.svfLowR - (multiply_32x32_rshift32(s.svfBandR, filterQ) << 1);
			s.svfBandR += multiply_32x32_rshift32(highR, fR) << 1;
			s.svfLowR += multiply_32x32_rshift32(s.svfBandR, fR) << 1;

			// Apply depth first, then manual offset (post-depth override)
			// Use saturating add: depth-scaled LFO and manual are both full-scale q31
			q31_t scaledTremL = add_saturate(multiply_32x32_rshift32(processedTremL, depthMultQ31) << 1, manualOffset);
			q31_t scaledTremR = add_saturate(multiply_32x32_rshift32(processedTremR, depthMultQ31) << 1, manualOffset);
			q31_t uniTremL = (scaledTremL >> 1) + (ONE_Q31 >> 1);
			q31_t uniTremR = (scaledTremR >> 1) + (ONE_Q31 >> 1);

			// Tremolo gain: 1 - depth * unipolar
			q31_t tremGainL = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniTremL) << 1);
			q31_t tremGainR = ONE_Q31 - (multiply_32x32_rshift32(c.tremoloDepthQ, uniTremR) << 1);

			// LP: mono tremolo (no stereo pulsing in bass)
			q31_t tremMono = (tremGainL >> 1) + (tremGainR >> 1);
			q31_t lowTremL = multiply_32x32_rshift32(s.svfLowL, tremMono) << 1;
			q31_t lowTremR = multiply_32x32_rshift32(s.svfLowR, tremMono) << 1;

			// BP/HP: full stereo tremolo
			q31_t bandTremL = multiply_32x32_rshift32(s.svfBandL, tremGainL) << 1;
			q31_t bandTremR = multiply_32x32_rshift32(s.svfBandR, tremGainR) << 1;
			q31_t highTremL = multiply_32x32_rshift32(highL, tremGainL) << 1;
			q31_t highTremR = multiply_32x32_rshift32(highR, tremGainR) << 1;

			// Blend LP/BP/HP using buffer-level mix weights
			q31_t filteredL = multiply_32x32_rshift32(lowTremL, lowMixQ) + multiply_32x32_rshift32(bandTremL, bandMixQ)
			                  + multiply_32x32_rshift32(highTremL, highMixQ);
			q31_t filteredR = multiply_32x32_rshift32(lowTremR, lowMixQ) + multiply_32x32_rshift32(bandTremR, bandMixQ)
			                  + multiply_32x32_rshift32(highTremR, highMixQ);

			outL = filteredL << 1;
			outR = filteredR << 1;
#endif
		}

		// Comb filter (flanger effect)
		if (combEnabled) {
			constexpr int32_t kCombSize = static_cast<int32_t>(AutomodulatorParams::kCombBufferSize);

			// Delay calculation in 16.16 fixed-point (LFO delta already slew-limited)
			// Apply depth first, then manual offset (post-depth override)
			// Use saturating add: depth-scaled LFO and manual are both full-scale q31
			q31_t scaledCombL = add_saturate(multiply_32x32_rshift32(processedCombL, depthMultQ31) << 1, manualOffset);
			q31_t scaledCombR = add_saturate(multiply_32x32_rshift32(processedCombR, depthMultQ31) << 1, manualOffset);
			int32_t lfo16L = scaledCombL >> 15;
			int32_t lfo16R = scaledCombR >> 15;
			int32_t delay16L = pitchCombBaseDelay16 + lfo16L * c.combModRangeSamples;
			int32_t delay16R = pitchCombBaseDelay16 + lfo16R * c.combModRangeSamples;

			// Wrap delay at power-of-2 buffer size for harmonic consistency
			// Use bitmask for fast modulo (buffer size is 2048 = 2^11)
			// Fold delay into valid range [minDelay, maxDelay] with reflection at boundaries
			// This avoids discontinuities from wrapping while keeping delay in bounds
			constexpr int32_t kBufferSize16 = kCombSize << 16;
			constexpr int32_t kMaxDelay16 = kBufferSize16 - (2 << 16); // Leave room for interpolation
			auto foldDelay = [&](int32_t d) -> int32_t {
				// Reflect off min boundary
				if (d < c.combMinDelay16) {
					d = 2 * c.combMinDelay16 - d;
				}
				// Reflect off max boundary
				if (d > kMaxDelay16) {
					d = 2 * kMaxDelay16 - d;
				}
				// Clamp as safety (handles extreme cases)
				return std::clamp(d, c.combMinDelay16, kMaxDelay16);
			};
			delay16L = foldDelay(delay16L);
			delay16R = foldDelay(delay16R);

			// Extract integer (samples) and fractional (16-bit) parts
			int32_t delayIntL = delay16L >> 16;
			int32_t delayIntR = delay16R >> 16;
			// Convert 16-bit frac to q31 for interpolation: (frac16 << 15) gives 0 to 0x7FFF8000
			q31_t fracQL = (delay16L & 0xFFFF) << 15;
			q31_t fracQR = (delay16R & 0xFFFF) << 15;

			// Linear interpolation for smooth delay modulation
			int32_t combIdx = static_cast<int32_t>(s.combIdx);

			// Read two adjacent samples and interpolate
			// Use bitmask for wrap (buffer is power of 2)
			constexpr int32_t kCombMask = kCombSize - 1;
			int32_t readIdx0L = (combIdx - delayIntL) & kCombMask;
			int32_t readIdx1L = (combIdx - delayIntL - 1) & kCombMask;
			int32_t readIdx0R = (combIdx - delayIntR) & kCombMask;
			int32_t readIdx1R = (combIdx - delayIntR - 1) & kCombMask;

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
			params.combBufferL[s.combIdx] = add_saturate(outL, feedbackL);
			params.combBufferR[s.combIdx] = add_saturate(outR, feedbackR);
			s.combIdx = (s.combIdx + 1) & kCombMask;

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
		sample.l = clipAndScale(scaledModL) >> 2;
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

		// Increment LFO values (bounded by segment reset)
		lfoL.value += lfoL.delta;
		lfoR.value += lfoR.delta;
		combLfoL.value += combLfoL.delta;
		combLfoR.value += combLfoR.delta;
		tremLfoL.value += tremLfoL.delta;
		tremLfoR.value += tremLfoR.delta;

		// Comb uses direct deltas (slew-limited at buffer rate above)
		processedCombL += combDeltaL;
		processedCombR += combDeltaR;

		// Tremolo: first-order slew limiting (cap max rate of change)
		constexpr q31_t kTremMaxSlew = ONE_Q31 >> 12;
		processedTremL += std::clamp(tremDeltaL, -kTremMaxSlew, kTremMaxSlew);
		processedTremR += std::clamp(tremDeltaR, -kTremMaxSlew, kTremMaxSlew);

		// Advance phases
		lfoPhaseL += phaseInc;
		lfoPhaseR += phaseInc;
		combPhaseL += phaseInc;
		combPhaseR += phaseInc;
		tremPhaseL += phaseInc;
		tremPhaseR += phaseInc;

		// Decrement remaining counters, use precomputed values on segment crossing
		if (--lfoLRemaining == 0) {
			int8_t newSeg = (s.lfoIirL.segment >= 4) ? 0 : (s.lfoIirL.segment + 1);
			s.lfoIirL.segment = newSeg;
			lfoL.value = c.wavetable.segAmpQ[newSeg]; // Reset to segment start
			lfoL.delta = s.stepPerSegment[newSeg];
			lfoLRemaining = s.samplesPerSegment[newSeg];
		}
		if (--lfoRRemaining == 0) {
			int8_t newSeg = (s.lfoIirR.segment >= 4) ? 0 : (s.lfoIirR.segment + 1);
			s.lfoIirR.segment = newSeg;
			lfoR.value = c.wavetable.segAmpQ[newSeg];
			lfoR.delta = s.stepPerSegment[newSeg];
			lfoRRemaining = s.samplesPerSegment[newSeg];
		}
		if (--combLRemaining == 0) {
			int8_t newSeg = (s.combLfoIirL.segment >= 4) ? 0 : (s.combLfoIirL.segment + 1);
			s.combLfoIirL.segment = newSeg;
			combLfoL.value = c.wavetable.segAmpQ[newSeg];
			combLfoL.delta = s.stepPerSegment[newSeg];
			combLRemaining = s.samplesPerSegment[newSeg];
			// Don't reset processedCombL - let it track naturally through slew-limited deltas
			// This prevents discontinuities at segment boundaries
			combDeltaL = s.stepPerSegment[newSeg]; // Raw delta (depth scaling at use)
		}
		if (--combRRemaining == 0) {
			int8_t newSeg = (s.combLfoIirR.segment >= 4) ? 0 : (s.combLfoIirR.segment + 1);
			s.combLfoIirR.segment = newSeg;
			combLfoR.value = c.wavetable.segAmpQ[newSeg];
			combLfoR.delta = s.stepPerSegment[newSeg];
			combRRemaining = s.samplesPerSegment[newSeg];
			combDeltaR = s.stepPerSegment[newSeg]; // Raw delta (depth scaling at use)
		}
		if (--tremLRemaining == 0) {
			int8_t newSeg = (s.tremLfoIirL.segment >= 4) ? 0 : (s.tremLfoIirL.segment + 1);
			s.tremLfoIirL.segment = newSeg;
			tremLfoL.value = c.wavetable.segAmpQ[newSeg];
			tremLfoL.delta = s.stepPerSegment[newSeg];
			tremLRemaining = s.samplesPerSegment[newSeg];
			tremDeltaL = s.stepPerSegment[newSeg]; // Raw delta (depth scaling at use)
		}
		if (--tremRRemaining == 0) {
			int8_t newSeg = (s.tremLfoIirR.segment >= 4) ? 0 : (s.tremLfoIirR.segment + 1);
			s.tremLfoIirR.segment = newSeg;
			tremLfoR.value = c.wavetable.segAmpQ[newSeg];
			tremLfoR.delta = s.stepPerSegment[newSeg];
			tremRRemaining = s.samplesPerSegment[newSeg];
			tremDeltaR = s.stepPerSegment[newSeg]; // Raw delta (depth scaling at use)
		}
	}

	// Write back RAW accumulated LFO values and remaining counts for next buffer
	// (no manual offset, no depth scaling - manual is applied to separate processed variables)
	s.lfoIirL.value = lfoL.value;
	s.lfoIirR.value = lfoR.value;
	s.combLfoIirL.value = combLfoL.value;
	s.combLfoIirR.value = combLfoR.value;
	s.tremLfoIirL.value = tremLfoL.value;
	s.tremLfoIirR.value = tremLfoR.value;
	s.lfoIirL.samplesRemaining = lfoLRemaining;
	s.lfoIirR.samplesRemaining = lfoRRemaining;
	s.combLfoIirL.samplesRemaining = combLRemaining;
	s.combLfoIirR.samplesRemaining = combRRemaining;
	s.tremLfoIirL.samplesRemaining = tremLRemaining;
	s.tremLfoIirR.samplesRemaining = tremRRemaining;

	// Write back processed (slew-limited) values for buffer-to-buffer continuity
	s.smoothedTremLfoL = processedTremL;
	s.smoothedTremLfoR = processedTremR;
	s.smoothedCombLfoL = processedCombL;
	s.smoothedCombLfoR = processedCombR;

#if ENABLE_FX_BENCHMARK
	if (doBench) {
		benchLoop.stop();
		benchTotal.stop();
	}
#endif
}

} // namespace deluge::dsp
