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
inline constexpr std::array<phi::PhiTriConfig, kPhiMorphMaxSegments> kPhiMorphPhaseBank = {{
    {phi::kPhi075, 0.6f, 0.000f, true},
    {phi::kPhi100, 0.5f, 0.111f, true},
    {phi::kPhi125, 0.5f, 0.222f, true},
    {phi::kPhi150, 0.6f, 0.333f, true},
    {phi::kPhi175, 0.5f, 0.444f, true},
    {phi::kPhi200, 0.5f, 0.555f, true},
    {phi::kPhi225, 0.6f, 0.666f, true},
    {phi::kPhi250, 0.5f, 0.777f, true},
}};

// 7 amplitude triangles for movable waypoints (bipolar -1 to +1)
inline constexpr std::array<phi::PhiTriConfig, kPhiMorphMovableWaypoints> kPhiMorphAmplitudeBank = {{
    {phi::kPhi050, 0.7f, 0.100f, true},
    {phi::kPhi067, 0.6f, 0.250f, true},
    {phi::kPhiN050, 0.6f, 0.400f, true},
    {phi::kPhi025, 0.7f, 0.550f, true},
    {phi::kPhi075, 0.5f, 0.700f, true},
    {phi::kPhi100, 0.6f, 0.850f, true},
    {phi::kPhiN025, 0.7f, 0.050f, true},
}};

// 8 curvature triangles per segment (bipolar: positive = concave, negative = convex)
inline constexpr std::array<phi::PhiTriConfig, kPhiMorphMaxSegments> kPhiMorphCurvatureBank = {{
    {phi::kPhi125, 0.4f, 0.050f, true},
    {phi::kPhi175, 0.4f, 0.175f, true},
    {phi::kPhi075, 0.4f, 0.300f, true},
    {phi::kPhi225, 0.4f, 0.425f, true},
    {phi::kPhi100, 0.4f, 0.550f, true},
    {phi::kPhi150, 0.4f, 0.675f, true},
    {phi::kPhiN050, 0.4f, 0.800f, true},
    {phi::kPhi200, 0.4f, 0.925f, true},
}};

// 1 gain triangle (unipolar) - controls amplitude scaling per zone.
// Values > 1.0 cause flat-topped clipping for square-like waveforms.
inline constexpr std::array<phi::PhiTriConfig, 1> kPhiMorphGainBank = {{
    {phi::kPhi150, 0.5f, 0.500f, false},
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
	float gain; // Amplitude gain multiplier [0.5..2.0]
};

struct PhiMorphCache {
	PhiMorphWavetable bankA{};
	PhiMorphWavetable bankB{};

	// Crossfaded effective table — shared across unison voices within a buffer
	PhiMorphWavetable effective{};
	q31_t prevCrossfade{INT32_MIN};

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
                    int32_t amplitudeIncrement, bool applyAmplitude, q31_t crossfade);

} // namespace deluge::dsp
