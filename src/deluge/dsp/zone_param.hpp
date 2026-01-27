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

#include "definitions_cxx.hpp"
#include <algorithm>
#include <cstdint>

namespace deluge::dsp {

// ============================================================================
// Zone-based Parameter Storage Design
// ============================================================================
//
// Zone parameters have TWO storage locations that work together:
//
// 1. PATCHED PARAM PRESET (PatchedParamSet[LOCAL_* or GLOBAL_*])
//    - Stored in: ParamManager's PatchedParamSet
//    - Serialized as: Explicit handlers in sound.cpp (e.g., "patchedSineShaperHarmonic")
//    - Purpose: Base value for DSP, set by menu via ZoneBasedPatchedParam
//    - Used by: combinePresetAndCables(preset, cables) - preset IS the base
//    - Features: Mod matrix routing (LFO, envelope, etc.), automation, gold knob recording
//
// 2. UNPATCHED PARAM (UnpatchedParamSet[UNPATCHED_*])
//    - Stored in: ParamManager's UnpatchedParamSet
//    - Serialized as: Standard unpatched param serialization
//    - Purpose: Additional modulation on top of preset (for clips without mod matrix)
//    - Used by: combinePresetAndCables(preset, unpatchedMod) - as "cables" parameter
//    - Features: CC learning, simple modulation (no mod matrix routing)
//
// DSP Combination via combinePresetAndCables():
// - Voice path: preset (patched param) + cables (mod matrix output)
// - Clip path:  preset (patched param) + cables (unpatched param)
//
// Note: ZoneBasedParam.value field is vestigial - serialized for backwards
// compatibility but not used by DSP. Menu writes to patched param.
// ============================================================================

// ============================================================================
// Zone computation helpers (work on any q31_t value)
// ============================================================================

/// Result of zone computation
struct ZoneInfo {
	int32_t index;   ///< Zone index (0 to numZones-1)
	float position;  ///< Position within zone (0.0 to 1.0)
	q31_t zoneStart; ///< Start of current zone in q31
	q31_t zoneWidth; ///< Width of one zone in q31
};

/// Compute zone width for given number of zones
[[nodiscard]] constexpr q31_t computeZoneWidth(int32_t numZones) {
	return ONE_Q31 / numZones;
}

/// Compute zone info from a q31 value
[[nodiscard]] inline ZoneInfo computeZoneQ31(q31_t value, int32_t numZones) {
	q31_t zoneWidth = computeZoneWidth(numZones);
	int32_t index = std::clamp(static_cast<int32_t>(value / zoneWidth), static_cast<int32_t>(0),
	                           static_cast<int32_t>(numZones - 1));
	q31_t zoneStart = index * zoneWidth;
	float position = static_cast<float>(value - zoneStart) / static_cast<float>(zoneWidth);
	return {index, position, zoneStart, zoneWidth};
}

/// Check if value is in or past a specific zone (e.g., "is this in zone 5+?")
[[nodiscard]] constexpr bool isInZoneOrLater(q31_t value, int32_t zoneIndex, int32_t numZones) {
	return value >= (computeZoneWidth(numZones) * zoneIndex);
}

/// Get the start position of a zone in q31
[[nodiscard]] constexpr q31_t getZoneStart(int32_t zoneIndex, int32_t numZones) {
	return computeZoneWidth(numZones) * zoneIndex;
}

/// Convert zone position (0.0-1.0) to display value (0-127)
[[nodiscard]] constexpr int32_t zonePositionToDisplay(float position) {
	return static_cast<int32_t>(position * 127.0f);
}

// ============================================================================

/**
 * Zone-based parameter with configurable behavior
 *
 * Encapsulates a q31 field value with zone semantics. Knows how to combine
 * preset + cables according to its configuration (zone count, clipping).
 *
 * Used by SineTableShaperParams and other zone-based effect parameters.
 * DSP calls combinePresetAndCables() to merge patched param + modulation.
 *
 * @tparam NUM_ZONES Number of zones (e.g., 8)
 * @tparam CLIP_TO_ZONE If true, cable modulation clips to zone boundaries
 */
template <int32_t NUM_ZONES = 8, bool CLIP_TO_ZONE = false>
struct ZoneBasedParam {
	q31_t value{0};

	static constexpr int32_t kNumZones = NUM_ZONES;
	static constexpr bool kClipToZone = CLIP_TO_ZONE;
	static constexpr q31_t kZoneWidth = computeZoneWidth(NUM_ZONES);

	/// Get full zone info (index, position, start, width)
	[[nodiscard]] ZoneInfo getZoneInfo() const { return computeZoneQ31(value, NUM_ZONES); }

	/// Get zone index (0 to NUM_ZONES-1)
	[[nodiscard]] int32_t getZoneIndex() const { return getZoneInfo().index; }

	/// Get position within current zone (0.0 to 1.0)
	[[nodiscard]] float getPosInZone() const { return getZoneInfo().position; }

	/// Get global position across all zones (0.0 to 1.0)
	[[nodiscard]] float getGlobalPos() const { return static_cast<float>(value) / static_cast<float>(ONE_Q31); }

	/// Check if value is in or past a specific zone
	[[nodiscard]] bool isInZoneOrLater(int32_t zoneIndex) const {
		return dsp::isInZoneOrLater(value, zoneIndex, NUM_ZONES);
	}

	/// Combine preset (base position) with modulation cables
	/// Used by patcher/DSP when preset comes from patched param, not field
	/// Cables are scaled so full modulation = 1 zone, preset is not scaled
	/// If CLIP_TO_ZONE, cables are clipped to zone boundaries (prevents LFO glitches)
	/// @param preset Base position from patched param preset (gold knob/automation)
	/// @param cables Mod matrix cable combination (from patcher, rshift32 scaled)
	/// @return Combined value: preset + scaled cables, with appropriate clipping
	[[nodiscard]] q31_t combinePresetAndCables(q31_t preset, q31_t cables) const {
		// Patcher outputs cables with rshift32 scaling (full mod ≈ 2^30, not 2^31)
		// Divide by (kNumZones/2) so full modulation = 1 zone width
		q31_t scaledCables = cables / (kNumZones / 2);
		// Use 64-bit to avoid overflow when preset + scaledCables exceeds INT32_MAX
		int64_t combined = static_cast<int64_t>(preset) + static_cast<int64_t>(scaledCables);
		if constexpr (CLIP_TO_ZONE) {
			// Clip cables to zone boundaries of the preset position
			int32_t baseZone = std::clamp(static_cast<int32_t>(preset / kZoneWidth), static_cast<int32_t>(0),
			                              static_cast<int32_t>(NUM_ZONES - 1));
			int64_t zoneLower = static_cast<int64_t>(baseZone) * kZoneWidth;
			int64_t zoneUpper = (baseZone == NUM_ZONES - 1) ? ONE_Q31 : (baseZone + 1) * kZoneWidth - 1;
			return static_cast<q31_t>(std::clamp(combined, zoneLower, zoneUpper));
		}
		else {
			return static_cast<q31_t>(std::clamp(combined, static_cast<int64_t>(0), static_cast<int64_t>(ONE_Q31)));
		}
	}

	/// Implicit conversion to q31_t for direct field access
	operator q31_t() const { return value; }
	ZoneBasedParam& operator=(q31_t v) {
		value = v;
		return *this;
	}
};

} // namespace deluge::dsp
