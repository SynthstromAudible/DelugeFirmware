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

#pragma once

// ============================================================================
// SCATTER TAKEOVER DESIGN
// ============================================================================
//
// When Track B takes over scatter from Track A (via preset change or track switch):
//
// 1. BUFFER: B inherits A's audio buffer content instantly. pWrite controls
//    how fast B's audio overwrites the inherited content.
//
// 2. PARAMS: A's scatter params (zones, macro, pWrite, density) are copied to
//    B's ParamManager. This makes the UI correct - it shows A's inherited
//    values, and B can edit them from there.
//
// 3. CONFIG: A's StutterConfig (mode, phase offsets, etc.) is copied to B's
//    local stutterConfig via checkAndClearInheritConfig().
//
// 4. PERSISTENCE: Changes to B's ParamManager are in RAM only. They persist
//    for the session but are NOT saved to SD card unless B explicitly saves
//    the preset. This is consistent with other live parameter edits.
//
// Implementation in ModControllableAudio::processScatter():
//   if (stutterer.checkAndClearInheritConfig()) {
//       stutterConfig = stutterer.getStutterConfig();  // Copy mode, phase offsets
//       auto cached = stutterer.getCachedScatterParams();
//       // Copy zones, macro, pWrite, density to this source's ParamManager
//   }
//
// ============================================================================

#include "dsp/hash_random.hpp"
#include "dsp/phi_triangle.hpp"
#include "dsp/util.hpp"
#include "dsp/zone_param.hpp"
#include <array>
#include <cstdint>

namespace deluge::dsp::scatter {

// Zone helpers from parent namespace (dsp::computeZoneQ31, dsp::ZoneInfo) are
// accessible via C++ parent namespace lookup without explicit qualification

// Precomputed reciprocal for q31 to float conversion (multiplication is ~10x faster than division on ARM)
constexpr float kQ31ToFloat = 1.0f / static_cast<float>(ONE_Q31);

// ============================================================================
// Hash-based random for scatter - uses shared hash utilities
// ============================================================================

// Scatter-specific param seeds for decorrelated random values
namespace HashSeed {
constexpr uint32_t ReverseDecision = 0x12345678u; // Bool: should reverse this slice?
constexpr uint32_t SkipDecision = 0x9ABCDEF0u;    // Bool: should skip to non-adjacent?
constexpr uint32_t SkipTarget = 0x11223344u;      // Int: which slice to skip to
constexpr uint32_t DryMix = 0x2468ACE0u;          // Bool: use dry instead of grain?
constexpr uint32_t BinarySubdiv = 0x13579BDFu;    // Duty: binary subdivision level
constexpr uint32_t TripletSubdiv = 0xFEDCBA98u;   // Duty: triplet subdivision level
constexpr uint32_t SliceOffset = 0xAABBCCDDu;     // Int: offset added to slice index
constexpr uint32_t LengthMult = 0x55667788u;      // Nibble: length multiplier level
constexpr uint32_t DelayRatio = 0xDEADBEEFu;      // 2 bits: power-of-2 delay multiplier
constexpr uint32_t DelayDecision = 0xBAADF00Du;   // Bool: should apply delay this slice?
constexpr uint32_t PitchDecision = 0xCAFEBABEu;   // Bool: should pitch up (2x decimation)?
constexpr uint32_t RepeatSlice = 0xFACEFEEDu;     // Duty: repeat slice probability (inverse of ratchet)
constexpr uint32_t LongGrain = 0xBEEFCAFEu;       // Duty: combine consecutive slices into one grain
} // namespace HashSeed

/**
 * Compute delay time as power-of-2 multiple of base time (32nd note grid)
 * Uses 2 bits from hash: 0=1/4x, 1=1/2x, 2=1x, 3=2x
 * All bit shifts, no multiply or divide (~1 cycle)
 */
[[gnu::always_inline]] inline size_t computeDelayTimeRatio(size_t baseTime, uint32_t hashBits) {
	// 2 bits select power-of-2 multiplier: 1/4, 1/2, 1, 2
	switch (hashBits & 0x3) {
	case 0:
		return baseTime >> 2; // 1/4x (32nd if slice is 8th)
	case 1:
		return baseTime >> 1; // 1/2x (16th if slice is 8th)
	case 2:
		return baseTime; // 1x (same as slice)
	case 3:
		return baseTime << 1; // 2x (quarter if slice is 8th)
	default:
		return baseTime;
	}
}

// Length multiplier discrete levels (8 steps from 0.5 to 1.0)
constexpr float kLengthMultLevels[8] = {0.5f, 0.5625f, 0.625f, 0.6875f, 0.75f, 0.8125f, 0.875f, 1.0f};

// Convenience aliases for scatter
using HashBits = hash::Bits;
using HashContext = hash::Context;

/**
 * Phi triangle bank for structural scatter params (Zone A meta)
 *
 * Bank indices:
 *   [0] sliceOffset - Offset added to slice selection
 *   [1] lengthMult  - Slice length multiplier
 *   [2] skipProb    - Probability of skipping to non-adjacent slice
 *
 * Slower φ^n frequencies for gradual structural evolution.
 * Phase offsets spread for decorrelation.
 */
constexpr std::array<phi::PhiTriConfig, 3> kStructuralBank = {{
    {phi::kPhi100, 0.80f, 0.00f, false}, // [0] sliceOffset
    {phi::kPhi150, 0.60f, 0.33f, false}, // [1] lengthMult
    {phi::kPhi175, 0.70f, 0.67f, false}, // [2] skipProb
}};

/**
 * Phi triangle bank for timbral scatter params (Zone B meta)
 *
 * Bank indices:
 *   [0] reverseProb - Probability of reversing slice
 *   [1] filterFreq  - Bandpass center frequency
 *   [2] delayFeed   - Per-grain delay send amount
 *   [3] envShape    - Envelope shape (percussive to reversed)
 *
 * Mix of slower and faster φ^n for varied timbral movement.
 * Phase offsets spread by 0.25 for decorrelation.
 */
constexpr std::array<phi::PhiTriConfig, 4> kTimbraBank = {{
    {phi::kPhiN050, 0.50f, 0.00f, false}, // [0] reverseProb (slow)
    {phi::kPhi067, 0.70f, 0.25f, false},  // [1] filterFreq
    {phi::kPhi125, 0.60f, 0.50f, false},  // [2] delayFeed
    {phi::kPhi200, 0.80f, 0.75f, false},  // [3] envShape (faster)
}};

/**
 * Map triangle value [0,1] to slice index within a zone
 * @param tri Triangle value in [0,1]
 * @param numSlices Total number of slices in the zone
 * @return Slice index [0, numSlices-1]
 */
[[gnu::always_inline]] inline int32_t sliceIndexFromTriangle(float tri, int32_t numSlices) {
	if (numSlices <= 1) {
		return 0;
	}
	int32_t idx = static_cast<int32_t>(tri * static_cast<float>(numSlices));
	return std::clamp(idx, int32_t{0}, numSlices - 1);
}

/**
 * Map triangle value [0,1] to gate duty cycle
 * Never fully mutes - minimum gate is 12.5%
 * @param tri Triangle value in [0,1]
 * @return Gate ratio [0.125, 1.0]
 */
[[gnu::always_inline]] inline float gateFromTriangle(float tri) {
	return 0.125f + tri * 0.875f;
}

/**
 * Map triangle value [0,1] to reverse decision
 * @param tri Triangle value in [0,1]
 * @param threshold Probability threshold for reversing
 * @return true if slice should be reversed
 */
[[gnu::always_inline]] inline bool reverseFromTriangle(float tri, float threshold) {
	return tri > threshold;
}

/**
 * Map triangle value [0,1] to pitch ratio
 * Center is 1.0 (no pitch change), range depends on depth
 * @param tri Triangle value in [0,1]
 * @param semitoneRange Maximum semitone deviation (e.g., 12 for octave)
 * @return Pitch ratio
 */
[[gnu::always_inline]] inline float pitchFromTriangle(float tri, float semitoneRange) {
	// Map [0,1] to [-semitoneRange, +semitoneRange] semitones
	float semitones = (tri * 2.0f - 1.0f) * semitoneRange;
	// Convert semitones to pitch ratio: 2^(semitones/12)
	// Use fast approximation: exp2f(semitones / 12.0f)
	return std::exp2(semitones / 12.0f);
}

/**
 * Map triangle value [0,1] to tape speed ratio
 * @param tri Triangle value in [0,1]
 * @param minSpeed Minimum speed (e.g., 0.5 for half speed)
 * @param maxSpeed Maximum speed (e.g., 2.0 for double speed)
 * @return Speed ratio
 */
[[gnu::always_inline]] inline float tapeSpeedFromTriangle(float tri, float minSpeed = 0.5f, float maxSpeed = 2.0f) {
	return minSpeed + tri * (maxSpeed - minSpeed);
}

/**
 * Map triangle value [0,1] to filter frequency
 * Uses exponential mapping for perceptually linear sweep
 * @param tri Triangle value in [0,1]
 * @param minFreq Minimum frequency in Hz
 * @param maxFreq Maximum frequency in Hz
 * @return Frequency in Hz
 */
[[gnu::always_inline]] inline float filterFreqFromTriangle(float tri, float minFreq = 200.0f, float maxFreq = 8000.0f) {
	// Exponential mapping for perceptually linear sweep
	float logMin = std::log2(minFreq);
	float logMax = std::log2(maxFreq);
	return std::exp2(logMin + tri * (logMax - logMin));
}

/**
 * Compute grain envelope multiplier with configurable shape
 * Very cheap: ~6 multiplies, few branches, no trig
 *
 * @param positionInSlice Current position within slice [0, sliceLength)
 * @param sliceLength Total length of slice in samples
 * @param gateRatio Gate duty cycle [0,1] - audio plays during this portion
 * @param depth Envelope depth [0,1] - 0=hard cut, 1=full smooth envelope
 * @param envShape Peak position [0,1] - 0=fade-out only, 0.5=symmetric, 1=fade-in only
 * @param envWidth Envelope region [0,1] - 1=full slice, 0.1=edges only (10% each end)
 * @return Amplitude multiplier [0,1]
 */
[[gnu::always_inline]] inline float grainEnvelope(int32_t positionInSlice, int32_t sliceLength, float gateRatio,
                                                  float depth, float envShape = 0.5f, float envWidth = 1.0f) {
	if (sliceLength <= 0) {
		return 1.0f;
	}

	// Anti-click now handled by zero-crossing mute in stutterer.cpp
	// Attack: mute until zero crossing, Release: mute after zero crossing

	// Normalized position within slice [0,1]
	float pos = static_cast<float>(positionInSlice) / static_cast<float>(sliceLength);

	// If past gate threshold, output is silent
	if (pos > gateRatio) {
		return 0.0f;
	}

	// Rescale position to [0,1] within the gated portion
	float gatedPos = pos / gateRatio;

	// Edge-only mode: flat middle with envelope only at edges
	// envWidth=1.0: full envelope, envWidth=0.1: only first/last 10%
	float envelope;
	if (envWidth < 1.0f && envWidth > 0.0f) {
		float edgeSize = envWidth * 0.5f; // Half at each end
		if (gatedPos < edgeSize) {
			// Attack region - remap to [0, envShape]
			float t = gatedPos / edgeSize;
			// Parabolic attack: t^2 for smooth start
			float attack = (envShape > 0.001f) ? t * t : 1.0f;
			envelope = attack;
		}
		else if (gatedPos > (1.0f - edgeSize)) {
			// Decay region - remap to [envShape, 1]
			float t = (gatedPos - (1.0f - edgeSize)) / edgeSize;
			// Parabolic decay: (1-t)^2 for smooth end
			float decay = (envShape < 0.999f) ? (1.0f - t) * (1.0f - t) : 1.0f;
			envelope = decay;
		}
		else {
			// Flat middle region
			envelope = 1.0f;
		}
	}
	else {
		// Full slice envelope with configurable peak position (envShape)
		// envShape=0: peak at start (fade-out only, preserves attack)
		// envShape=0.5: peak at middle (symmetric Hanning-like)
		// envShape=1: peak at end (fade-in only)
		if (envShape < 0.001f) {
			// Fade-out only: (1-pos)^2
			envelope = (1.0f - gatedPos) * (1.0f - gatedPos);
		}
		else if (envShape > 0.999f) {
			// Fade-in only: pos^2
			envelope = gatedPos * gatedPos;
		}
		else {
			// Asymmetric envelope with peak at envShape position
			if (gatedPos < envShape) {
				// Attack phase: parabolic ramp up to peak
				float t = gatedPos / envShape;
				envelope = t * t;
			}
			else {
				// Decay phase: parabolic ramp down from peak
				float t = (gatedPos - envShape) / (1.0f - envShape);
				envelope = (1.0f - t) * (1.0f - t);
			}
		}
	}

	// Depth-controlled envelope
	// depth=0: no envelope (passthrough)
	// depth=1: full envelope shape
	return 1.0f + depth * (envelope - 1.0f);
}

/**
 * Precomputed envelope parameters (Q31 fixed-point) for zero-float per-sample evaluation
 * Compute once per slice boundary, use for all samples in slice
 * All reciprocals stored as Q31: multiply position by reciprocal, result is Q31 [0, ONE_Q31]
 */
struct GrainEnvPrecomputedQ31 {
	int32_t invSliceLength{0};         ///< ONE_Q31 / sliceLength (for pos normalization)
	int32_t invGateRatio{ONE_Q31};     ///< ONE_Q31 / gateRatio (for gated pos)
	int32_t invFadeLen{0};             ///< ONE_Q31 / fadeLen (for anti-click, legacy)
	int32_t invAttackLen{0};           ///< ONE_Q31 / attackFadeLen (asymmetric fade-in)
	int32_t invDecayLen{0};            ///< ONE_Q31 / decayFadeLen (asymmetric fade-out)
	int32_t invEdgeSize{0};            ///< ONE_Q31 / edgeSize (for edge-only mode)
	int32_t invEnvShape{0};            ///< ONE_Q31 / envShape (for attack phase)
	int32_t invOneMinusEnvShape{0};    ///< ONE_Q31 / (1 - envShape) (for decay phase)
	int32_t gatedLength{0};            ///< sliceLength * gateRatio
	int32_t fadeLen{0};                ///< Base anti-click fade length
	int32_t attackFadeLen{0};          ///< Fade-in length (envShape scales this)
	int32_t decayFadeLen{0};           ///< Fade-out length (1-envShape scales this)
	int32_t edgeSizeQ31{0};            ///< envWidth * 0.5 in Q31
	int32_t depthQ31{0};               ///< Envelope depth in Q31
	int32_t oneMinusDepthQ31{ONE_Q31}; ///< (1 - depth) in Q31, precomputed for blend
	int32_t envShapeQ31{ONE_Q31 / 2};  ///< Envelope shape in Q31
	int32_t gateRatioQ31{ONE_Q31};     ///< Gate ratio in Q31 for threshold check
	bool useEdgeMode{false};           ///< Whether envWidth < 1.0
	bool useShortFade{false};          ///< Whether gatedLength <= 880 (2x anti-click)
	bool depthIsMax{false};            ///< depth >= 0.99, skip blending
};

/**
 * Precomputed envelope parameters for fast per-sample evaluation
 * Compute once per slice boundary, use for all samples in slice
 * Eliminates ~9 divisions per sample by converting to multiplications
 */
struct GrainEnvPrecomputed {
	float invSliceLength{0};      ///< 1.0 / sliceLength
	float invGateRatio{1.0f};     ///< 1.0 / gateRatio
	float invFadeLen{0};          ///< 1.0 / fadeLen (for anti-click)
	float invEdgeSize{0};         ///< 1.0 / edgeSize (for edge-only mode)
	float invEnvShape{0};         ///< 1.0 / envShape (for attack phase)
	float invOneMinusEnvShape{0}; ///< 1.0 / (1 - envShape) (for decay phase)
	int32_t gatedLength{0};       ///< sliceLength * gateRatio
	int32_t fadeLen{0};           ///< Anti-click fade length
	float edgeSize{0};            ///< envWidth * 0.5
	float depth{0};               ///< Envelope depth
	float envShape{0.5f};         ///< Envelope shape
	float envWidth{1.0f};         ///< Envelope width
	float gateRatio{1.0f};        ///< Gate ratio for threshold check
	bool useEdgeMode{false};      ///< Whether envWidth < 1.0
	bool useShortFade{false};     ///< Whether gatedLength <= 880 (2x anti-click)
};

/**
 * Prepare precomputed envelope parameters at slice boundary
 * Call once when slice changes, result used for all samples in slice
 */
[[gnu::always_inline]] inline GrainEnvPrecomputed
prepareGrainEnvelope(int32_t sliceLength, float gateRatio, float depth, float envShape = 0.5f, float envWidth = 1.0f) {
	GrainEnvPrecomputed p;
	constexpr int32_t kAntiClickSamples = 440;

	if (sliceLength <= 0) {
		return p; // Will return 1.0 for all samples
	}

	p.invSliceLength = 1.0f / static_cast<float>(sliceLength);
	p.gateRatio = gateRatio;
	p.depth = depth;
	p.envShape = envShape;
	p.envWidth = envWidth;

	// Gate ratio reciprocal (avoid div by zero)
	p.invGateRatio = (gateRatio > 0.001f) ? (1.0f / gateRatio) : 1000.0f;

	// Gated length and fade parameters
	p.gatedLength = static_cast<int32_t>(static_cast<float>(sliceLength) * gateRatio);

	if (p.gatedLength > kAntiClickSamples * 2) {
		p.fadeLen = kAntiClickSamples;
		p.invFadeLen = 1.0f / static_cast<float>(kAntiClickSamples);
		p.useShortFade = false;
	}
	else if (p.gatedLength > 0) {
		p.fadeLen = p.gatedLength / 2;
		p.invFadeLen = (p.fadeLen > 0) ? (1.0f / static_cast<float>(p.fadeLen)) : 0.0f;
		p.useShortFade = true;
	}

	// Edge mode parameters
	p.useEdgeMode = (envWidth < 1.0f && envWidth > 0.0f);
	if (p.useEdgeMode) {
		p.edgeSize = envWidth * 0.5f;
		p.invEdgeSize = (p.edgeSize > 0.001f) ? (1.0f / p.edgeSize) : 1000.0f;
	}

	// Envelope shape reciprocals
	p.invEnvShape = (envShape > 0.001f) ? (1.0f / envShape) : 1000.0f;
	p.invOneMinusEnvShape = (envShape < 0.999f) ? (1.0f / (1.0f - envShape)) : 1000.0f;

	return p;
}

/**
 * Prepare precomputed Q31 envelope parameters at slice boundary
 * All reciprocals in Q31 format for pure integer per-sample math
 *
 * Note: For fast ratchets (<30ms), caller should skip envelope entirely
 * by not setting scatterEnvActive. This function is only called for
 * slices that actually need envelope processing.
 *
 * @param sliceLength Length of slice in samples
 * @param gateRatio Gate duty cycle [0,1]
 * @param depth Envelope depth [0,1]
 * @param envShape Envelope shape [0,1]
 * @param envWidth Envelope width [0,1]
 */
[[gnu::always_inline]] inline GrainEnvPrecomputedQ31 prepareGrainEnvelopeQ31(int32_t sliceLength, float gateRatio,
                                                                             float depth, float envShape = 0.5f,
                                                                             float envWidth = 1.0f) {
	GrainEnvPrecomputedQ31 p;
	constexpr int32_t kAntiClickSamples = 440; // ~10ms fade at 44.1kHz
	constexpr int32_t kMinAntiClickBase = 64;  // Absolute minimum for click-free audio

	if (sliceLength <= 0) {
		return p; // Will return ONE_Q31 for all samples
	}

	// Inverse slice length: ONE_Q31 / sliceLength
	p.invSliceLength = ONE_Q31 / sliceLength;

	// Gate ratio and inverse
	p.gateRatioQ31 = static_cast<int32_t>(gateRatio * static_cast<float>(ONE_Q31));
	p.invGateRatio = (gateRatio > 0.001f) ? static_cast<int32_t>(ONE_Q31 / gateRatio) : ONE_Q31;

	// Depth in Q31
	p.depthQ31 = static_cast<int32_t>(depth * static_cast<float>(ONE_Q31));

	// Envelope shape in Q31
	p.envShapeQ31 = static_cast<int32_t>(envShape * static_cast<float>(ONE_Q31));

	// Gated length and fade parameters
	p.gatedLength = static_cast<int32_t>(static_cast<float>(sliceLength) * gateRatio);

	if (p.gatedLength > kAntiClickSamples * 2) {
		p.fadeLen = kAntiClickSamples;
		p.invFadeLen = ONE_Q31 / kAntiClickSamples;
		p.useShortFade = false;
	}
	else if (p.gatedLength > 0) {
		p.fadeLen = p.gatedLength / 2;
		p.invFadeLen = (p.fadeLen > 0) ? (ONE_Q31 / p.fadeLen) : 0;
		p.useShortFade = true;
	}

	// Asymmetric fade lengths based on envShape
	// envShape=0: instant attack, full decay (percussive)
	// envShape=0.5: symmetric
	// envShape=1: full attack, instant decay (reversed)
	// Scale factor 2x so envShape=0.5 gives full fadeLen to each
	int32_t baseFade = p.fadeLen;
	float attackScale = std::min(envShape * 2.0f, 1.0f);
	float decayScale = std::min((1.0f - envShape) * 2.0f, 1.0f);
	p.attackFadeLen = static_cast<int32_t>(static_cast<float>(baseFade) * attackScale);
	p.decayFadeLen = static_cast<int32_t>(static_cast<float>(baseFade) * decayScale);
	// Ensure minimum anti-click even at extreme shapes
	if (p.attackFadeLen < kMinAntiClickBase && baseFade >= kMinAntiClickBase) {
		p.attackFadeLen = kMinAntiClickBase;
	}
	if (p.decayFadeLen < kMinAntiClickBase && baseFade >= kMinAntiClickBase) {
		p.decayFadeLen = kMinAntiClickBase;
	}
	p.invAttackLen = (p.attackFadeLen > 0) ? (ONE_Q31 / p.attackFadeLen) : 0;
	p.invDecayLen = (p.decayFadeLen > 0) ? (ONE_Q31 / p.decayFadeLen) : 0;

	// Edge mode parameters
	p.useEdgeMode = (envWidth < 1.0f && envWidth > 0.0f);
	if (p.useEdgeMode) {
		float edgeSize = envWidth * 0.5f;
		p.edgeSizeQ31 = static_cast<int32_t>(edgeSize * static_cast<float>(ONE_Q31));
		// invEdgeSize: need Q31 / edgeSize, but edgeSize is [0,0.5], so divide by fraction
		p.invEdgeSize = (edgeSize > 0.001f) ? static_cast<int32_t>(ONE_Q31 / edgeSize) : ONE_Q31;
	}

	// Envelope shape reciprocals
	p.invEnvShape = (envShape > 0.001f) ? static_cast<int32_t>(ONE_Q31 / envShape) : ONE_Q31;
	p.invOneMinusEnvShape = (envShape < 0.999f) ? static_cast<int32_t>(ONE_Q31 / (1.0f - envShape)) : ONE_Q31;

	// Precompute depth blend values for per-sample optimization
	p.oneMinusDepthQ31 = ONE_Q31 - p.depthQ31;
	p.depthIsMax = (depth >= 0.99f);

	return p;
}

/**
 * Fast grain envelope using precomputed reciprocals
 * ~10x faster than grainEnvelope() - uses only multiplications, no divisions
 *
 * @param positionInSlice Current position within slice [0, sliceLength)
 * @param p Precomputed parameters from prepareGrainEnvelope()
 * @return Amplitude multiplier [0,1]
 */
[[gnu::always_inline]] inline float grainEnvelopeFast(int32_t positionInSlice, const GrainEnvPrecomputed& p) {
	// Early out if no valid slice
	if (p.invSliceLength == 0.0f) {
		return 1.0f;
	}

	// Anti-click now handled by zero-crossing mute in stutterer.cpp

	// Normalized position (multiplication instead of division)
	float pos = static_cast<float>(positionInSlice) * p.invSliceLength;

	// Gate threshold check
	if (pos > p.gateRatio) {
		return 0.0f;
	}

	// Gated position (multiplication instead of division)
	float gatedPos = pos * p.invGateRatio;

	// Envelope calculation
	float envelope;
	if (p.useEdgeMode) {
		if (gatedPos < p.edgeSize) {
			float t = gatedPos * p.invEdgeSize;
			envelope = (p.envShape > 0.001f) ? t * t : 1.0f;
		}
		else if (gatedPos > (1.0f - p.edgeSize)) {
			float t = (gatedPos - (1.0f - p.edgeSize)) * p.invEdgeSize;
			envelope = (p.envShape < 0.999f) ? (1.0f - t) * (1.0f - t) : 1.0f;
		}
		else {
			envelope = 1.0f;
		}
	}
	else {
		if (p.envShape < 0.001f) {
			envelope = (1.0f - gatedPos) * (1.0f - gatedPos);
		}
		else if (p.envShape > 0.999f) {
			envelope = gatedPos * gatedPos;
		}
		else {
			if (gatedPos < p.envShape) {
				float t = gatedPos * p.invEnvShape;
				envelope = t * t;
			}
			else {
				float t = (gatedPos - p.envShape) * p.invOneMinusEnvShape;
				envelope = (1.0f - t) * (1.0f - t);
			}
		}
	}

	// Depth-controlled envelope
	return 1.0f + p.depth * (envelope - 1.0f);
}

/**
 * Ultra-fast linear-only Q31 grain envelope - minimal per-sample cost
 * Only computes linear anti-click fades, no parabolic curves or depth blending.
 *
 * @param positionInSlice Current position within slice [0, sliceLength)
 * @param p Precomputed Q31 parameters from prepareGrainEnvelopeQ31()
 * @return Amplitude multiplier in Q31 format [0, ONE_Q31]
 */
[[gnu::always_inline]] inline int32_t grainEnvelopeLinearQ31(int32_t positionInSlice, const GrainEnvPrecomputedQ31& p) {
	// Asymmetric fade: attackFadeLen for fade-in, decayFadeLen for fade-out
	// envShape=0: short attack, long decay (percussive)
	// envShape=1: long attack, short decay (reversed)

	// Fade in region (uses attackFadeLen)
	if (positionInSlice < p.attackFadeLen) {
		return positionInSlice * p.invAttackLen;
	}
	// Fade out region (uses decayFadeLen)
	if (positionInSlice > p.gatedLength - p.decayFadeLen) {
		int32_t remaining = p.gatedLength - positionInSlice;
		return (remaining > 0) ? (remaining * p.invDecayLen) : 0;
	}
	// Flat middle
	return ONE_Q31;
}

/**
 * Pure Q31 fixed-point grain envelope - zero float operations per sample
 * Uses only integer math: comparisons, additions, subtractions, and multiply_32x32_rshift32
 *
 * @param positionInSlice Current position within slice [0, sliceLength)
 * @param p Precomputed Q31 parameters from prepareGrainEnvelopeQ31()
 * @return Amplitude multiplier in Q31 format [0, ONE_Q31]
 */
[[gnu::always_inline]] inline int32_t grainEnvelopeQ31(int32_t positionInSlice, const GrainEnvPrecomputedQ31& p) {
	// Early out if no valid slice
	if (p.invSliceLength == 0) {
		return ONE_Q31;
	}

	// Anti-click now handled by zero-crossing mute in stutterer.cpp

	// Normalized position in Q31: pos = positionInSlice * invSliceLength
	int32_t posQ31 = positionInSlice * p.invSliceLength;

	// Gate threshold check (Q31 comparison)
	if (posQ31 > p.gateRatioQ31) {
		return 0;
	}

	// Gated position in Q31 - need to rescale [0, gateRatio] to [0, 1]
	// gatedPos = pos / gateRatio = pos * invGateRatio, but both are Q31, so multiply_32x32_rshift32
	int32_t gatedPosQ31 = multiply_32x32_rshift32(posQ31, p.invGateRatio) << 1;

	// Envelope calculation in Q31
	// For t^2 in Q31: multiply_32x32_rshift32(t, t) << 1 gives Q31 result
	int32_t envelopeQ31;
	if (p.useEdgeMode) {
		if (gatedPosQ31 < p.edgeSizeQ31) {
			// t = gatedPos / edgeSize
			int32_t tQ31 = multiply_32x32_rshift32(gatedPosQ31, p.invEdgeSize) << 1;
			// envelope = t^2
			envelopeQ31 = (p.envShapeQ31 > (ONE_Q31 / 1000)) ? (multiply_32x32_rshift32(tQ31, tQ31) << 1) : ONE_Q31;
		}
		else if (gatedPosQ31 > (ONE_Q31 - p.edgeSizeQ31)) {
			// t = (gatedPos - (1 - edgeSize)) / edgeSize
			int32_t tQ31 = multiply_32x32_rshift32(gatedPosQ31 - (ONE_Q31 - p.edgeSizeQ31), p.invEdgeSize) << 1;
			// envelope = (1-t)^2
			int32_t oneMinusT = ONE_Q31 - tQ31;
			envelopeQ31 = (p.envShapeQ31 < (ONE_Q31 - ONE_Q31 / 1000))
			                  ? (multiply_32x32_rshift32(oneMinusT, oneMinusT) << 1)
			                  : ONE_Q31;
		}
		else {
			envelopeQ31 = ONE_Q31;
		}
	}
	else {
		if (p.envShapeQ31 < (ONE_Q31 / 1000)) {
			// Fade-out only: (1-gatedPos)^2
			int32_t oneMinusPos = ONE_Q31 - gatedPosQ31;
			envelopeQ31 = multiply_32x32_rshift32(oneMinusPos, oneMinusPos) << 1;
		}
		else if (p.envShapeQ31 > (ONE_Q31 - ONE_Q31 / 1000)) {
			// Fade-in only: gatedPos^2
			envelopeQ31 = multiply_32x32_rshift32(gatedPosQ31, gatedPosQ31) << 1;
		}
		else {
			if (gatedPosQ31 < p.envShapeQ31) {
				// Attack phase: t = gatedPos / envShape, envelope = t^2
				int32_t tQ31 = multiply_32x32_rshift32(gatedPosQ31, p.invEnvShape) << 1;
				envelopeQ31 = multiply_32x32_rshift32(tQ31, tQ31) << 1;
			}
			else {
				// Decay phase: t = (gatedPos - envShape) / (1 - envShape), envelope = (1-t)^2
				int32_t tQ31 = multiply_32x32_rshift32(gatedPosQ31 - p.envShapeQ31, p.invOneMinusEnvShape) << 1;
				int32_t oneMinusT = ONE_Q31 - tQ31;
				envelopeQ31 = multiply_32x32_rshift32(oneMinusT, oneMinusT) << 1;
			}
		}
	}

	// Depth-controlled envelope: result = (1 - depth) + depth * envelope
	return (ONE_Q31 - p.depthQ31) + (multiply_32x32_rshift32(p.depthQ31, envelopeQ31) << 1);
}

/**
 * Scatter control parameters from menu/modulation
 * Zone params are unsigned q31 [0, ONE_Q31] from PatchedParamSet/UnpatchedParamSet
 */
struct ScatterParams {
	q31_t zoneA{0}; ///< Zone A control (meaning depends on mode)
	q31_t zoneB{0}; ///< Zone B control (meaning depends on mode)
	q31_t depth{0}; ///< Effect depth/intensity
	q31_t gate{0};  ///< Gate duty cycle

	/// Get zone info for Zone A using standard zone helpers
	[[gnu::always_inline]] ZoneInfo getZoneAInfo(int32_t numZones = 8) const { return computeZoneQ31(zoneA, numZones); }

	/// Get zone info for Zone B using standard zone helpers
	[[gnu::always_inline]] ZoneInfo getZoneBInfo(int32_t numZones = 8) const { return computeZoneQ31(zoneB, numZones); }

	/// Get depth as normalized [0,1]
	[[gnu::always_inline]] float depthNormalized() const { return static_cast<float>(depth) * kQ31ToFloat; }

	/// Get gate as normalized [0,1]
	[[gnu::always_inline]] float gateNormalized() const { return static_cast<float>(gate) * kQ31ToFloat; }
};

/**
 * State for scatter DSP processing
 */
struct ScatterState {
	double phiPhase{0.0};       ///< Phase accumulator for phi triangle evolution
	int32_t currentSlice{0};    ///< Current slice index in playback sequence
	int32_t targetSlice{0};     ///< Target slice (remapped) for scatter playback
	int32_t positionInSlice{0}; ///< Current sample position within current slice
	int32_t sliceLength{0};     ///< Length of each slice in samples
	int32_t numSlices{8};       ///< Number of slices to divide buffer into
	int32_t bufferLength{0};    ///< Total buffer length for dynamic slice updates
	float tapeSpeed{1.0f};      ///< Current tape speed for Tape mode
	float pitchRatio{1.0f};     ///< Current pitch ratio for Pitch mode
	float filterFreq{4000.0f};  ///< Current filter frequency for Filter mode
	int32_t repeatCount{0};     ///< Current repeat count for Repeat mode
	bool sliceReversed{false};  ///< Whether current slice is playing reversed
	float gatePosition{0.0f};   ///< Current position within gate cycle
	float gateRatio{1.0f};      ///< Current gate duty cycle
	bool initialized{false};    ///< Whether scatter state has been initialized for current buffer

	/// Reset state for new stutter session
	void reset() {
		phiPhase = 0.0;
		currentSlice = 0;
		targetSlice = 0;
		positionInSlice = 0;
		sliceLength = 0;
		numSlices = 8;
		bufferLength = 0;
		tapeSpeed = 1.0f;
		pitchRatio = 1.0f;
		filterFreq = 4000.0f;
		repeatCount = 0;
		sliceReversed = false;
		gatePosition = 0.0f;
		gateRatio = 1.0f;
		initialized = false;
	}

	/// Initialize slice parameters based on buffer size
	void initSlices(int32_t bufferSize, int32_t sliceCount) {
		numSlices = std::clamp(sliceCount, int32_t{2}, int32_t{16});
		sliceLength = bufferSize / numSlices;
		positionInSlice = 0;
		currentSlice = 0;
		targetSlice = 0;
		bufferLength = bufferSize;
		initialized = true;
	}

	/// Update slice count dynamically during playback
	/// Preserves relative position within buffer when slice count changes
	/// @param newSliceCount Desired number of slices
	/// @return true if slice count changed
	bool updateSliceCount(int32_t newSliceCount) {
		newSliceCount = std::clamp(newSliceCount, int32_t{2}, int32_t{16});
		if (newSliceCount == numSlices || bufferLength <= 0) {
			return false;
		}

		// Calculate current absolute position in buffer
		int32_t absPos = targetSlice * sliceLength + positionInSlice;

		// Update slice parameters
		numSlices = newSliceCount;
		sliceLength = bufferLength / numSlices;

		// Recalculate slice and position from absolute position
		if (sliceLength > 0) {
			targetSlice = (absPos / sliceLength) % numSlices;
			positionInSlice = absPos % sliceLength;
			currentSlice = targetSlice;
		}
		return true;
	}

	/// Advance position within slice, returns true if slice boundary crossed
	bool advancePosition() {
		positionInSlice++;
		if (positionInSlice >= sliceLength) {
			positionInSlice = 0;
			currentSlice = (currentSlice + 1) % numSlices;
			return true; // Slice boundary crossed
		}
		return false;
	}

	/// Get buffer offset for current scatter position (targetSlice + positionInSlice)
	[[gnu::always_inline]] int32_t getBufferOffset() const { return targetSlice * sliceLength + positionInSlice; }

	/// Advance phi phase for quasi-periodic evolution
	void advancePhase(float rate = 0.001f) { phiPhase += rate; }
};

/**
 * Computed grain parameters from zone knobs
 * Discrete decisions computed via hash, continuous params still float
 */
struct GrainParams {
	// Structural (from Zone A) - DISCRETE
	int32_t sliceOffset{0}; ///< Offset to add to slice selection [0, numSlices)
	float lengthMult{1.0f}; ///< Slice length multiplier from kLengthMultLevels
	bool shouldSkip{false}; ///< Should skip to non-adjacent slice?
	int32_t skipTarget{0};  ///< Target slice index when skipping [0, numSlices)
	bool useDry{false};     ///< Use dry signal instead of grain?

	// Timbral (from Zone B) - DISCRETE DECISIONS
	bool shouldReverse{false}; ///< Should reverse this slice?
	bool shouldPitchUp{false}; ///< Should pitch up (2x via decimation) this slice?
	bool shouldDelay{false};   ///< Should apply delay this slice?
	float filterFreq{0.5f};    ///< Bandpass center [0,1] maps to freq range
	uint8_t delaySendBits{0};  ///< 2 bits: 0=off, 1=25%, 2=50%, 3=100% (shift = 3-bits)
	uint8_t delayRatioBits{0}; ///< 2 bits for power-of-2 delay mult (use with computeDelayTimeRatio)
	float envShape{0.5f};      ///< Envelope shape (0=percussive, 0.5=hanning, 1=reverse)
	float envDepth{0};         ///< Envelope depth [0,1]: 0=hard cut, 1=full envelope
	float panAmount{0};        ///< Crossfeed pan amount [0,1] before direction applied
	float stereoWidth{0};      ///< Stereo spread [-1,1]: 0=mono, +1=A left/B right, -1=A right/B left

	// Combined
	float gateRatio{1.0f};   ///< Gate duty cycle [0.125, 1.0]
	int32_t subdivisions{1}; ///< Ratchet subdivisions (1,2,3,4,6,8,12) - play slice start N times
	int32_t repeatSlices{1}; ///< Hold grain for N slices (1=normal, 2/4/8=repeat) - inverse of ratchet
	int32_t grainLength{1};  ///< Combine N consecutive slices into one grain (1=normal, 2/4=long grain)
};

/**
 * Phase offsets from secret encoder menus (push+twist on zone knobs)
 * These shift the effective zone position and scale phi evolution
 */
struct ScatterPhaseOffsets {
	float zoneA{0};       ///< Zone A structural phase offset
	float zoneB{0};       ///< Zone B timbral phase offset
	float macroConfig{0}; ///< Macro config phase offset
	float gamma{0};       ///< Gamma multiplier for phi evolution (100x scale)

	// Precomputed threshold scales (from staticTriangles, depend only on macroConfig)
	float reverseScale{0}; ///< Bipolar [-1,1] scale for reverse probability
	float pitchScale{0};   ///< Bipolar [-1,1] scale for pitch probability
	float delayScale{0};   ///< Bipolar [-1,1] scale for delay probability

	// Multi-bar pattern state
	int32_t barIndex{0}; ///< Bar counter (0-3) for multi-bar evolution
};

/**
 * Compute grain parameters from zone knobs via phi triangles
 *
 * When phaseOffset == 0 (standard mode):
 *   Zone A (Structural): Controls grain selection, length, skip patterns
 *     Zones 0-4: Individual behaviors with position controlling intensity
 *     Zones 5-7: Meta - all structural params via phi evolution (uses kStructuralBank)
 *   Zone B (Timbral): Controls per-grain effects
 *     Zones 0-3: Individual effects (reverse, filter, delay, envelope)
 *     Zones 4-7: Meta - all timbral params via phi evolution (uses kTimbraBank)
 *
 * When phaseOffset != 0 (full evolution mode, like sine shaper):
 *   Ignores discrete zones entirely - applies phi triangles to ALL params
 *   Position (knob) controls intensity, phaseOffset controls pattern selection
 *   Different phi frequencies per parameter for non-monotonic evolution
 *
 * @param zoneAParam Zone A raw q31 param value [0, ONE_Q31]
 * @param zoneBParam Zone B raw q31 param value [0, ONE_Q31]
 * @param macroConfigParam Macro config raw q31 param value [0, ONE_Q31]
 * @param macroParam Macro raw q31 param value [0, ONE_Q31]
 * @param sliceIndex Current slice index (converted to phi-based phase internally)
 * @param offsets Phase offsets from secret encoder menus (optional)
 */
inline GrainParams computeGrainParams(q31_t zoneAParam, q31_t zoneBParam, q31_t macroConfigParam, q31_t macroParam,
                                      int32_t sliceIndex, const ScatterPhaseOffsets* offsets = nullptr) {
	// Use provided offsets or default (all zeros)
	static constexpr ScatterPhaseOffsets kDefaultOffsets{};
	const ScatterPhaseOffsets& ofs = offsets ? *offsets : kDefaultOffsets;

	// === Early cache check: when stride==0 && effectiveSlice==0, params are identical ===
	// Check stride condition first (cheap: just compare zoneBParam to threshold)
	constexpr float kStrideDeadzone = 0.3f;
	float zoneBNormEarly = static_cast<float>(zoneBParam) * kQ31ToFloat;
	zoneBNormEarly = std::clamp(zoneBNormEarly, 0.0f, 1.0f);
	bool strideIsZero = (zoneBNormEarly <= kStrideDeadzone);

	// Check effectiveSlice condition (sliceWeight <= 0.1 means effectiveSlice=0)
	float sliceWeight = triangleSimpleUnipolar(phi::wrapPhase(static_cast<float>(sliceIndex) * phi::kPhiN050), 0.5f);
	bool effectiveSliceIsZero = (sliceWeight <= 0.1f);

	// Static cache for "dead zone" case where params are identical across slices
	static GrainParams cachedGrain{};
	static q31_t cachedZoneA{0}, cachedZoneB{0}, cachedMacroConfig{0}, cachedMacro{0};
	static float cachedGamma{-1.0f}; // Invalid initial value to force first computation
	static int32_t cachedBarIndex{-1};

	// When both deadzones active, result only depends on params + gamma + barIndex, NOT sliceIndex
	if (strideIsZero && effectiveSliceIsZero) {
		// Check if cache is valid (params unchanged)
		if (zoneAParam == cachedZoneA && zoneBParam == cachedZoneB && macroConfigParam == cachedMacroConfig
		    && macroParam == cachedMacro && ofs.gamma == cachedGamma && ofs.barIndex == cachedBarIndex) {
			return cachedGrain; // ~0 cycles: skip all computation
		}
	}

	GrainParams p;

	constexpr int32_t kNumZones = 8;
	constexpr double kResolution = 1024.0; // Matches UI kScatterResolution for non-overlapping phase patterns

	// Compute effective phase offsets (individual offset + resolution * gammaPhase)
	// This matches sine shaper: phaseOffset + 1024.0 * gammaPhase
	// Resolution (1024) ensures gamma sweeps through distinct non-repeating patterns
	double phRawA = static_cast<double>(ofs.zoneA) + kResolution * static_cast<double>(ofs.gamma);
	double phRawB = static_cast<double>(ofs.zoneB) + kResolution * static_cast<double>(ofs.gamma);

	// Bar counter contribution: individual bits weighted by Zone B-derived triangles
	// Bit 0 toggles every bar, Bit 1 toggles every 2 bars
	// Weights use decorrelated phi frequencies for smooth, musical evolution
	// Result offsets Zone A for multi-bar pattern variation
	if (ofs.barIndex != 0) {
		float barBit0 = (ofs.barIndex & 1) ? 1.0f : 0.0f;
		float barBit1 = (ofs.barIndex & 2) ? 1.0f : 0.0f;
		float weight0 = triangleSimpleUnipolar(zoneBNormEarly * phi::kPhi050, 0.5f); // fast bit (every bar)
		float weight1 = triangleSimpleUnipolar(zoneBNormEarly * phi::kPhi125, 0.5f); // slow bit (every 2 bars)
		float barOffset = barBit0 * weight0 + barBit1 * weight1;
		phRawA += static_cast<double>(barOffset) * kResolution * 0.25; // Scale to subtle pattern shift
	}

	// Zone B controls stride through Zone A's hash pattern
	// 30% deadzone: below 0.3, stride=0 (all slices get same hash = no variation)
	// Above 0.3: stride ramps 0→8x (higher = faster evolution through patterns)
	constexpr float kMaxStride = 8.0f;
	float stride = 0.0f;
	if (!strideIsZero) {
		// Remap [0.3, 1.0] → [0, 1] then scale to [0, maxStride]
		stride = ((zoneBNormEarly - kStrideDeadzone) / (1.0f - kStrideDeadzone)) * kMaxStride;
	}
	int32_t stridedSlice = static_cast<int32_t>(static_cast<float>(sliceIndex) * stride);

	// Single hash context for all hash-based operations (amortize mix() cost)
	// Incorporate phRawA into seed so gamma/phaseOffset changes Zone A hash patterns
	// stridedSlice (controlled by Zone B) determines how fast we evolve through patterns
	// Add 0x12345678 to prevent mix(0)=0 degenerate case when sliceIndex=0 and phRawA=0
	uint32_t hashSeed = static_cast<uint32_t>(stridedSlice) ^ static_cast<uint32_t>(phRawA * 65536.0f) ^ 0x12345678u;
	HashContext hashCtx{hashSeed};

	// Apply macroConfig offset (in normalized units, 0.1 per click) + gamma
	// Gamma adds slow evolution to macroConfig pattern selection
	float macroConfigOffset = ofs.macroConfig * 0.1f + static_cast<float>(ofs.gamma);
	float macroConfigNorm = static_cast<float>(macroConfigParam) * kQ31ToFloat;
	macroConfigNorm = std::clamp(macroConfigNorm + macroConfigOffset, 0.0f, 1.0f);
	float macroNorm = static_cast<float>(macroParam) * kQ31ToFloat;

	// Phi triangle deadzone: when triangle output is low, sliceIndex contribution is zeroed
	// This creates sparse activation - many consecutive slices get identical params → cache hits
	int32_t effectiveSlice = effectiveSliceIsZero ? 0 : sliceIndex;
	float slicePhase = phi::wrapPhase(static_cast<float>(effectiveSlice) * phi::kPhi);

	// === Zone A: Structural (all hash-based discrete decisions) ===
	float zoneANorm = static_cast<float>(zoneAParam) * kQ31ToFloat;
	zoneANorm = std::clamp(zoneANorm, 0.0f, 1.0f);

	// Compute effective Zone A position that cycles with phase offset
	// This is used for threshold calculations so probability patterns evolve with gamma/phaseOffset
	float effectiveZoneANorm = zoneANorm;
	if (phRawA != 0.0) {
		float phaseContrib = phi::wrapPhase(static_cast<float>(phRawA) * phi::kPhi075);
		effectiveZoneANorm = phi::wrapPhase(zoneANorm + phaseContrib);
	}
	// Convert to 8-bit for integer threshold calculations (equivalent to zoneAParam >> 24)
	uint8_t effectiveZoneA8 = static_cast<uint8_t>(effectiveZoneANorm * 127.0f);

	// Slice offset: hash-based [0-15], caller scales by numSlices/16
	// Higher effectiveZoneA = more offset variation (cycles with gamma/phaseOffset)
	uint8_t maxOffset = static_cast<uint8_t>(effectiveZoneANorm * 15.0f);
	p.sliceOffset = (maxOffset > 0) ? hashCtx.evalInt(HashSeed::SliceOffset, maxOffset + 1) : 0;

	// Length multiplier: hash selects from 8 discrete levels, effectiveZoneA biases toward shorter
	// Uses effectiveZoneANorm so length behavior cycles with gamma/phaseOffset
	uint8_t lengthBits = (hash::derive(hashCtx.baseHash, HashSeed::LengthMult) >> 4) & 0x7;
	uint8_t minLengthIdx = static_cast<uint8_t>((1.0f - effectiveZoneANorm) * 7.0f);
	uint8_t lengthIdx = minLengthIdx + ((lengthBits * (8 - minLengthIdx)) >> 3);
	if (lengthIdx > 7) {
		lengthIdx = 7;
	}
	p.lengthMult = kLengthMultLevels[lengthIdx];

	// Skip decision: hash bool with effectiveZoneA-scaled probability
	// Cycles with gamma/phaseOffset: 0=never skip, 1=80% skip chance
	float skipProb = effectiveZoneANorm * 0.8f;
	p.shouldSkip = hashCtx.evalBool(HashSeed::SkipDecision, skipProb);
	p.skipTarget = hashCtx.evalInt(HashSeed::SkipTarget, 16); // [0-15], caller scales

	// Dry decision: hash bool, sparse (mostly grain, occasional dry)
	// effectiveZoneA modulates probability: cycles with gamma/phaseOffset
	float dryProb = effectiveZoneANorm * 0.3f; // Max 30% dry at full effectiveZoneA
	p.useDry = hashCtx.evalBool(HashSeed::DryMix, dryProb);

	// === Zone B: Timbral (continuous params keep triangles, reverse is hash bool) ===
	// zoneBNorm already computed as zoneBNormEarly for stride calculation
	float zoneBNorm = zoneBNormEarly;

	// Zone A (with phRawA in hash seed) determines WHICH grains get effects
	// Zone B determines PROBABILITY but non-monotonically via phi triangle
	// phRawB modulates the triangle phase for evolving probability patterns
	float probPhase = phi::wrapPhase(zoneBNorm * phi::kPhi + static_cast<float>(phRawB) * phi::kPhi125);

	// Reverse decision: probability from phi triangle on zoneB position
	// Triangle gives 0→peak→0 pattern as zoneB sweeps, phRawB shifts the pattern
	float reverseProb = triangleSimpleUnipolar(probPhase, 0.5f);

	// Pitch-up decision: separate triangle phase for decorrelated probability
	float pitchPhase = phi::wrapPhase(zoneBNorm * phi::kPhi150 + static_cast<float>(phRawB) * phi::kPhi067);
	float pitchProb = triangleSimpleUnipolar(pitchPhase, 0.3f);

	// Delay decision: separate triangle phase for decorrelated probability
	float delayPhase = phi::wrapPhase(zoneBNorm * phi::kPhi075 + static_cast<float>(phRawB) * phi::kPhi150);
	float delayProb = triangleSimpleUnipolar(delayPhase, 0.4f);

	// Macro config scales thresholds via precomputed bipolar triangles (can increase or decrease probability)
	// Scales are precomputed in staticTriangles, passed via offsets
	if (macroNorm > 0.01f) {
		// Use precomputed bipolar scales, apply macro intensity
		reverseProb = std::clamp(reverseProb + macroNorm * ofs.reverseScale * 0.5f, 0.0f, 1.0f);
		pitchProb = std::clamp(pitchProb + macroNorm * ofs.pitchScale * 0.3f, 0.0f, 1.0f);
		delayProb = std::clamp(delayProb + macroNorm * ofs.delayScale * 0.4f, 0.0f, 1.0f);
	}

	p.shouldReverse = hashCtx.evalBool(HashSeed::ReverseDecision, reverseProb);
	p.shouldPitchUp = hashCtx.evalBool(HashSeed::PitchDecision, pitchProb);
	p.shouldDelay = hashCtx.evalBool(HashSeed::DelayDecision, delayProb);

	// Delay ratio: hash-based n/d for rhythmic delay times (changes per-slice)
	uint32_t delayHash = hash::derive(hashCtx.baseHash, HashSeed::DelayRatio);
	p.delayRatioBits = static_cast<uint8_t>(delayHash & 0xF);

	// Continuous timbral params (keep triangle-based for smooth audio evolution)
	if (phRawB != 0.0) {
		// Full range phi-triangle evolution
		float pos = zoneBNorm;

		// Per-effect frequency modulation
		float fmF = 1.0f + pos * (0.25f + 0.25f * phi::wrapPhase(phRawB * phi::kPhi067));
		float fmD = 1.0f + pos * (0.25f + 0.25f * phi::wrapPhase(phRawB * phi::kPhi125));
		float fmE = 1.0f + pos * (0.25f + 0.25f * phi::wrapPhase(phRawB * phi::kPhi200));

		float ph067 = phi::wrapPhase(phRawB * phi::kPhi067);
		float ph125 = phi::wrapPhase(phRawB * phi::kPhi125);
		float ph200 = phi::wrapPhase(phRawB * phi::kPhi200);

		p.filterFreq = triangleSimpleUnipolar(pos * phi::kPhi067 * fmF + ph067 + slicePhase + 0.250f, 0.7f);
		// Delay send: triangle [0,0.6] → 2 bits [0-3] (0=off, 1=25%, 2=50%, 3=100%)
		float delayRaw = triangleSimpleUnipolar(pos * phi::kPhi125 * fmD + ph125 + slicePhase + 0.500f, 0.6f);
		p.delaySendBits = static_cast<uint8_t>(delayRaw * 5.0f); // [0,0.6]*5 = [0,3]
		p.envShape = triangleSimpleUnipolar(pos * phi::kPhi200 * fmE + ph200 + slicePhase + 0.750f, 0.8f);

		float phDepth = phi::wrapPhase(phRawB * phi::kPhi050);
		float fmDepth = 1.0f + pos * (0.25f + 0.25f * phi::wrapPhase(phRawB * phi::kPhi050));
		p.envDepth = triangleSimpleUnipolar(pos * phi::kPhi050 * fmDepth + phDepth + slicePhase, 0.6f);

		float phPan = phi::wrapPhase(phRawB * phi::kPhi125);
		p.panAmount = triangleSimpleUnipolar(pos * phi::kPhi125 + phPan + slicePhase, 0.25f);

		// Stereo width: bipolar phi triangle for voice A/B stereo spread
		// 30% duty cycle total: 15% positive (A→L,B→R), 15% negative (A→R,B→L), 70% mono
		float phStereo = phi::wrapPhase(phRawB * phi::kPhi175);
		float stereoPhase = pos * phi::kPhi175 + phStereo + slicePhase;
		p.stereoWidth = triangleSimpleUnipolar(stereoPhase, 0.15f) - triangleSimpleUnipolar(stereoPhase + 0.5f, 0.15f);

		// Gate phi triangle with 50% deadzone
		float phGate = phi::wrapPhase(phRawB * phi::kPhi150);
		float gateRaw = triangleSimpleUnipolar(pos * phi::kPhi150 + phGate + slicePhase, 0.5f);
		p.gateRatio = 0.125f + (1.0f - gateRaw) * 0.875f;
	}
	else {
		// Standard discrete zone behavior
		ZoneInfo zoneBInfo = computeZoneQ31(zoneBParam, kNumZones);
		phi::PhiTriContext ctx{slicePhase, 1.0f, 1.0f, ofs.gamma};
		constexpr int32_t kZoneBDiscreteZones = 4;

		if (zoneBInfo.index < kZoneBDiscreteZones) {
			switch (zoneBInfo.index) {
			case 0: // Flip: boost reverse probability further
				reverseProb = zoneBInfo.position;
				p.shouldReverse = hashCtx.evalBool(HashSeed::ReverseDecision, reverseProb);
				break;
			case 1: // Filter
				p.filterFreq = zoneBInfo.position;
				break;
			case 2:                                                                // Echo
				p.delaySendBits = static_cast<uint8_t>(zoneBInfo.position * 3.0f); // [0,1] → [0,3]
				break;
			case 3: // Shape
			default:
				p.envShape = zoneBInfo.position;
				break;
			}
		}
		else {
			// Zones 4-7: Meta
			auto timbral = ctx.evalBank(kTimbraBank, zoneBInfo.position);
			// Reverse still hash-based but with triangle-modulated probability
			reverseProb = timbral[0];
			p.shouldReverse = hashCtx.evalBool(HashSeed::ReverseDecision, reverseProb);
			p.filterFreq = timbral[1];
			p.delaySendBits = static_cast<uint8_t>(timbral[2] * 3.0f); // [0,1] → [0,3]
			p.envShape = timbral[3];
		}

		p.envDepth = triangleSimpleUnipolar(zoneBInfo.position * phi::kPhi050, 0.6f);
		p.panAmount = triangleSimpleUnipolar(zoneBInfo.position * phi::kPhi125, 0.25f);
		// Stereo width: bipolar with 30% duty (15% each direction)
		float stereoPhase = zoneBInfo.position * phi::kPhi175;
		p.stereoWidth = triangleSimpleUnipolar(stereoPhase, 0.15f) - triangleSimpleUnipolar(stereoPhase + 0.5f, 0.15f);
		// Gate: macroConfig triangle selects sensitivity, macro controls intensity
		// At macro=0, gateRatio stays at 1.0 (no gate)
		if (zoneBInfo.position > 0.02f) {
			float gateInfluence = triangleSimpleUnipolar(macroConfigNorm * phi::kPhi100, 0.6f);
			p.gateRatio = 1.0f - macroNorm * gateInfluence * 0.75f;
		}
		// else: gateRatio stays at default 1.0 (no gate)
	}

	// === Repeat vs Ratchet (mutually exclusive) + Long Grain (orthogonal) ===
	// Repeat: hold same grain params for N slices (performance optimization)
	// Ratchet: subdivide slice into rapid repetitions of grain start
	// Long grain: combine N consecutive slices into one continuous grain (can combine with either)
	// Repeat/longGrain probability falls as effectiveZoneANorm rises, ratchet probability rises

	// === Structural modifiers: ALL require Zone A > 0 to activate ===
	// At Zone A = 0 (default), slices play in order with no repeat/ratchet/longGrain
	// This ensures "clean" default behavior - just straight playback
	if (effectiveZoneA8 < 2) {
		// Zone A essentially at zero - disable all structural modifiers
		p.grainLength = 1;
		p.repeatSlices = 1;
		p.subdivisions = 1;
	}
	else {
		// Long grain: evaluated independently (orthogonal to repeat/ratchet)
		// Threshold decreases as effectiveZoneANorm increases: ~102 at low, ~26 at high
		uint8_t longThresh = 102 - static_cast<uint8_t>((effectiveZoneA8 * 76) >> 7);
		uint8_t longMag = hashCtx.evalDutyU8(HashSeed::LongGrain, longThresh);
		// Magnitude [0-15] → grain length: 0-5=2, 6-11=4, 12-15=8 (full bar), 16=inactive
		// Note: caller must cap grainLength to not exceed bar/buffer boundary
		p.grainLength = (longMag >= 16) ? 1 : (longMag >= 12) ? 8 : (longMag >= 6) ? 4 : 2;

		// Repeat threshold scales with Zone A: 0 at low, ~128 at high
		// Higher Zone A = more repeat probability
		uint8_t repeatThresh = static_cast<uint8_t>((effectiveZoneA8 * 128) >> 7);
		uint8_t repeatMag = hashCtx.evalDutyU8(HashSeed::RepeatSlice, repeatThresh);
		// Magnitude [0-15] → repeat slices: 0-4=2, 5-10=4, 11-15=8, 16=inactive
		p.repeatSlices = (repeatMag >= 16) ? 1 : (repeatMag >= 11) ? 8 : (repeatMag >= 5) ? 4 : 2;

		// Repeat and ratchet are mutually exclusive
		if (p.repeatSlices > 1) {
			p.subdivisions = 1;
		}
		else {
			// === Subdivisions (Ratchet) - scales with Zone A ===
			// Binary: threshold scales 0→102 as Zone A increases
			uint8_t binaryThresh = static_cast<uint8_t>((effectiveZoneA8 * 102) >> 7);
			uint8_t binaryMag = hashCtx.evalDutyU8(HashSeed::BinarySubdiv, binaryThresh);
			// Magnitude [0-15] → subdivisions: 0-4=2, 5-10=4, 11-15=8, 16=inactive
			int32_t binarySub = (binaryMag >= 16) ? 1 : (binaryMag >= 11) ? 8 : (binaryMag >= 5) ? 4 : 2;

			// Triplet: threshold scales 0→51 as Zone A increases
			uint8_t tripletThresh = static_cast<uint8_t>((effectiveZoneA8 * 51) >> 7);
			uint8_t tripletMag = hashCtx.evalDutyU8(HashSeed::TripletSubdiv, tripletThresh);
			// Magnitude [0-15] → subdivisions: 0-7=3, 8-15=6, 16=inactive
			int32_t tripletSub = (tripletMag >= 16) ? 1 : (tripletMag >= 8) ? 6 : 3;

			// Combine base subdivisions (multiply when both active, cap at 12)
			int32_t baseSub;
			if (tripletSub > 1 && binarySub > 1) {
				baseSub = std::min<int32_t>(binarySub * tripletSub, 12);
			}
			else {
				baseSub = (tripletSub > 1) ? tripletSub : binarySub;
			}

			// macro + macroConfig influence on final subdivision intensity
			// subdivInfluence from triangle gates macro's effect
			float subdivInfluence = triangleSimpleUnipolar(macroConfigNorm * phi::kPhi225, 0.5f);
			// No base floor - macro gates ratchet entirely (original behavior)
			float subdivMix = macroNorm * subdivInfluence;

			// Scale from 1 to baseSub*2 (double), capped at 12
			int32_t targetSub = std::min<int32_t>(baseSub * 2, 12);
			p.subdivisions = 1 + static_cast<int32_t>(static_cast<float>(targetSub - 1) * subdivMix);
		}
	}

	// Update cache if in cacheable condition (both deadzones active)
	if (strideIsZero && effectiveSliceIsZero) {
		cachedGrain = p;
		cachedZoneA = zoneAParam;
		cachedZoneB = zoneBParam;
		cachedMacroConfig = macroConfigParam;
		cachedMacro = macroParam;
		cachedGamma = ofs.gamma;
		cachedBarIndex = ofs.barIndex;
	}

	return p;
}

} // namespace deluge::dsp::scatter
