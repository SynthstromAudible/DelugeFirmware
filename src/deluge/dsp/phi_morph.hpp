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

#pragma once

#include "dsp/phi_triangle.hpp"
#include "io/debug/fx_benchmark.h"
#include "util/fixedpoint.h"
#include <array>
#include <cstdint>

namespace deluge::dsp {

// ============================================================================
// Constants
// ============================================================================

inline constexpr int32_t kPhiMorphMaxSegments = 8;
inline constexpr int32_t kPhiMorphMovableWaypoints = 7; // 9 total - 2 fixed endpoints

inline constexpr float kPhiMorphPhaseMin = 0.04f;
inline constexpr float kPhiMorphPhaseMax = 0.96f;

// Phase scale: controls how many segments fit before the return-to-zero tail.
// Larger = fewer segments fit (simpler shapes). With 0.20:
//   all-small deltas (0.3): 7 waypoints fit → 8 segments, narrow complex pulse
//   all-large deltas (1.0): ~4 waypoints fit → 5 segments, wider simpler shape
inline constexpr float kPhiMorphPhaseScale = 0.20f;

// Reference amplitude level (~0.5 × Q31, matches triangle oscillator convention)
inline constexpr float kPhiMorphRefAmplitude = 1073741823.0f;

// ============================================================================
// Phi Triangle Bank Configurations (per zone)
// ============================================================================

// 8 phase delta triangles (bipolar → abs → accumulate → normalize)
// Wide ratio spread (φ^-0.5 to φ^3.5) for fast decorrelation at high gamma.
inline constexpr std::array<phi::PhiTriConfig, kPhiMorphMaxSegments> kPhiMorphPhaseBank = {{
    {phi::kPhiN050, 0.7f, 0.000f, true},
    {phi::kPhi125, 0.5f, 0.111f, true},
    {phi::kPhi275, 0.4f, 0.222f, true},
    {phi::kPhi050, 0.6f, 0.333f, true},
    {phi::kPhi200, 0.5f, 0.444f, true},
    {phi::kPhi350, 0.3f, 0.555f, true},
    {phi::kPhi075, 0.6f, 0.666f, true},
    {phi::kPhi300, 0.4f, 0.777f, true},
}};

// 7 amplitude triangles for movable waypoints (bipolar -1 to +1)
// Mix of sub-golden and super-golden periods with varied amplitudes.
inline constexpr std::array<phi::PhiTriConfig, kPhiMorphMovableWaypoints> kPhiMorphAmplitudeBank = {{
    {phi::kPhi325, 0.3f, 0.100f, true},
    {phi::kPhiN050, 0.8f, 0.250f, true},
    {phi::kPhi175, 0.5f, 0.400f, true},
    {phi::kPhiN025, 0.9f, 0.550f, true},
    {phi::kPhi250, 0.4f, 0.700f, true},
    {phi::kPhi067, 0.7f, 0.850f, true},
    {phi::kPhi375, 0.3f, 0.050f, true},
}};

// 8 curvature triangles per segment (bipolar: positive = concave, negative = convex)
// Wider ratio spread and varied amplitudes for more distinct curvature per zone.
inline constexpr std::array<phi::PhiTriConfig, kPhiMorphMaxSegments> kPhiMorphCurvatureBank = {{
    {phi::kPhi275, 0.3f, 0.050f, true},
    {phi::kPhiN050, 0.6f, 0.175f, true},
    {phi::kPhi150, 0.4f, 0.300f, true},
    {phi::kPhi350, 0.3f, 0.425f, true},
    {phi::kPhi025, 0.5f, 0.550f, true},
    {phi::kPhi225, 0.4f, 0.675f, true},
    {phi::kPhiN025, 0.6f, 0.800f, true},
    {phi::kPhi300, 0.3f, 0.925f, true},
}};

// 1 gain triangle (unipolar) - controls amplitude scaling per zone.
// Values > 1.0 cause flat-topped clipping for square-like waveforms.
inline constexpr std::array<phi::PhiTriConfig, 1> kPhiMorphGainBank = {{
    {phi::kPhi150, 0.5f, 0.500f, false},
}};

// 1 endpoint triangle (unipolar) - non-zero start/end amplitude for pulse-like waveforms.
// 30% duty cycle: only produces non-zero endpoints for ~30% of zone positions.
inline constexpr std::array<phi::PhiTriConfig, 1> kPhiMorphEndpointBank = {{
    {phi::kPhi225, 1.0f, 0.300f, false},
}};

// 1 morph amplitude overshoot triangle (unipolar) — controls how much amplitude
// pushes beyond linear interpolation during crossfade midpoint.
// Derived from zone B only (morph target defines transition character).
inline constexpr std::array<phi::PhiTriConfig, 1> kPhiMorphAmpOvershootBank = {{
    {phi::kPhi175, 0.6f, 0.200f, false},
}};

// 1 morph curvature boost triangle (unipolar) — controls how much curvature
// is amplified during crossfade midpoint for richer harmonics.
// Derived from zone B only.
inline constexpr std::array<phi::PhiTriConfig, 1> kPhiMorphCurvBoostBank = {{
    {phi::kPhi300, 0.4f, 0.700f, false},
}};

// 1 morph phase distortion triangle (bipolar) — warps waypoint phases during crossfade.
// Quadratic warp: compresses one half of the cycle, stretches the other.
// Only active at crossfade midpoint (scaled by morphExcite). Derived from zone B.
inline constexpr std::array<phi::PhiTriConfig, 1> kPhiMorphPhaseDistortBank = {{
    {phi::kPhi125, 0.7f, 0.450f, true},
}};

// --- Waveform shaping banks (applied at wavetable construction) ---

// 1 sine blend triangle (unipolar) — lerps waypoint amplitudes toward sin(2π × phase).
// Strengthens fundamental by smoothing toward the purest possible tone.
inline constexpr std::array<phi::PhiTriConfig, 1> kPhiMorphSineBlendBank = {{
    {phi::kPhi067, 0.5f, 0.600f, false},
}};

// 1 odd symmetry triangle (unipolar) — enforces f(x) = -f(1-x) by pairing waypoints.
// Eliminates even harmonics for cleaner, more organ/flute-like tones.
inline constexpr std::array<phi::PhiTriConfig, 1> kPhiMorphOddSymBank = {{
    {phi::kPhi375, 0.4f, 0.350f, false},
}};

// 1 amplitude windowing triangle (unipolar) — raised cosine taper toward cycle extremes.
// Concentrates energy in the waveform center, strengthening the fundamental.
inline constexpr std::array<phi::PhiTriConfig, 1> kPhiMorphWindowBank = {{
    {phi::kPhi025, 0.3f, 0.800f, false},
}};

// 1 slope match triangle (unipolar) — blends curvatures toward C1-continuous values.
// Computed from Catmull-Rom tangents at waypoint boundaries for smoother transitions.
inline constexpr std::array<phi::PhiTriConfig, 1> kPhiMorphSlopeMatchBank = {{
    {phi::kPhiN025, 0.6f, 0.150f, false},
}};

// --- Per-sample render modifiers ---

// 1 phase jitter triangle (unipolar) — adds noise to phase for analog drift character.
// Noise amplitude scales with triangle value, max ~5% of phase cycle.
inline constexpr std::array<phi::PhiTriConfig, 1> kPhiMorphPhaseJitterBank = {{
    {phi::kPhi250, 0.1f, 0.075f, false},
}};

// 1 amplitude-dependent noise triangle (unipolar) — adds grit scaled by signal level.
// Louder parts get more noise, zero crossings stay clean. Analog VCO character.
inline constexpr std::array<phi::PhiTriConfig, 1> kPhiMorphAmpNoiseBank = {{
    {phi::kPhi050, 0.1f, 0.525f, false},
}};

// --- Waveform asymmetry (applied at wavetable construction) ---

// 1 asymmetric gain triangle (bipolar) — breaks waveform symmetry for even harmonics.
// Negative values: reduce gain on positive-amplitude waypoints only.
// Positive values: symmetric gain boost on all waypoints.
inline constexpr std::array<phi::PhiTriConfig, 1> kPhiMorphAsymGainBank = {{
    {phi::kPhi350, 0.5f, 0.875f, true},
}};

// ============================================================================
// Types
// ============================================================================

struct PhiMorphSegment {
	q31_t startAmp;
	uint32_t startPhase;
	uint32_t endPhase;
	float invWidth;   // Precomputed 1.0f / (endPhase - startPhase)
	float ampDeltaF;  // Precomputed float(endAmp - startAmp)
	float curvatureF; // Precomputed float curvature (0.0 = no curvature, branch-free)
};

struct PhiMorphWavetable {
	PhiMorphSegment segments[kPhiMorphMaxSegments];
	int32_t numSegments;

	// Raw float waypoint data (for crossfade interpolation before segment building)
	float phases[kPhiMorphMovableWaypoints];
	float amplitudes[kPhiMorphMovableWaypoints]; // Already gain-scaled, may exceed [-1,1]
	float curvatures[kPhiMorphMaxSegments];
	float gain;              // Amplitude gain multiplier [0.5..2.0]
	float endpointAmp;       // Non-zero start/end amplitude for pulse-like waveforms [0..~0.8]
	float morphAmpOvershoot; // Amplitude overshoot during morph [0..~1.5]
	float morphCurvBoost;    // Curvature boost multiplier during morph [0..~6.0]
	float morphPhaseDistort; // Phase distortion during morph [-0.15..+0.15]
	float phaseJitter;       // Phase noise amount [0..1], scaled to ~5% of cycle in render
	float ampNoise;          // Amplitude-dependent noise amount [0..0.5]
};

struct PhiMorphCache {
	PhiMorphWavetable bankA{};
	PhiMorphWavetable bankB{};

	// Crossfaded effective table — shared across unison voices within a buffer
	PhiMorphWavetable effective{};
	q31_t prevCrossfade{INT32_MIN};

	// IIR-smoothed crossfade position to prevent clicks from abrupt table changes.
	// Caller advances this once per buffer; renderPhiMorph uses it for the rebuild check.
	q31_t smoothedCrossfade{INT32_MIN};

	uint16_t prevZoneA{0xFFFF};
	uint16_t prevZoneB{0xFFFF};
	float prevPhaseOffsetA{-1.0f};
	float prevPhaseOffsetB{-1.0f};

	[[nodiscard]] bool needsUpdate(uint16_t zoneA, uint16_t zoneB, float phaseOffsetA, float phaseOffsetB) const {
		return zoneA != prevZoneA || zoneB != prevZoneB || phaseOffsetA != prevPhaseOffsetA
		       || phaseOffsetB != prevPhaseOffsetB;
	}
};

// ============================================================================
// Function declarations
// ============================================================================

PhiMorphWavetable buildPhiMorphWavetable(uint16_t zone, float phaseOffset = 0.0f);

void buildSegmentsFromWaypoints(const float* phases, const float* amplitudes, const float* curvatures,
                                PhiMorphWavetable& out);

q31_t evaluateWaveformDirect(const PhiMorphWavetable& table, uint32_t phase);

/// Render PHI_MORPH oscillator for one buffer.
/// Waveform is evaluated per sample from crossfaded wavetable segments.
void renderPhiMorph(PhiMorphCache& cache, int32_t* bufferStart, int32_t* bufferEnd, int32_t numSamples,
                    uint32_t phaseIncrement, uint32_t* startPhase, uint32_t retriggerPhase, int32_t amplitude,
                    int32_t amplitudeIncrement, bool applyAmplitude, q31_t crossfade, uint32_t pulseWidth);

} // namespace deluge::dsp
