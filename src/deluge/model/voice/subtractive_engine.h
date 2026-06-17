/*
 * Copyright © 2014-2023 Synthstrom Audible Limited, 2026 Synthstrom Audible Limited
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
 */
#pragma once

#include <cstdint>

class Voice;
class Sound;
class ParamManagerForTimeline;
class ModelStackWithSoundFlags;

/// @brief The subtractive synth engine: the cross-source *driver* for `SynthMode::SUBTRACTIVE` (per the L1
/// reframe, `docs/dev/synth_l1_engine_architecture.md`). It owns the per-source render loop, the osc-sync
/// orchestration (source 0 master → source 1 slave), the mono→stereo buffer dance, and the
/// unison-inactive→unassign decision for one voice.
///
/// **Transitional shape (slice 1):** the per-source render itself still lives in `Voice::renderBasicSource`
/// (which dispatches the 5 source-component types) — the engine **delegates** to it via the passed-in `Voice&`,
/// so the engine is a `friend` of Voice. Per-source dispatch unification + moving component ownership into the
/// engine are later slices; for now the engine holds no state of its own.
class SubtractiveEngine {
public:
	/// Render all subtractive sources for one voice, accumulating into @p oscBuffer (the per-source renders add
	/// in place). The per-source amplitude/wave-index arrays and the osc-sync master phases are computed by Voice
	/// and passed in; @p anythingInOscBuffer / @p didStereoTempBuffer / @p unassignVoiceAfter are updated exactly
	/// as the inline driver did.
	void render(Voice& voice, Sound& sound, ParamManagerForTimeline* paramManager, ModelStackWithSoundFlags& modelStack,
	            int32_t* oscBuffer, int32_t numSamples, bool doingOscSync, uint32_t* oscSyncPos,
	            int32_t overallPitchAdjust, const int32_t* sourceAmplitudesNow,
	            const int32_t* sourceAmplitudeIncrements, const int32_t* sourceWaveIndexIncrements,
	            bool& anythingInOscBuffer, bool& didStereoTempBuffer, bool& unassignVoiceAfter);
};
