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

struct ControlBlock;

namespace deluge::synth {

class Voice;
struct VoiceBlock;
struct VoiceConfig;

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
	void render(deluge::synth::Voice& voice, const ControlBlock& control_block, const deluge::synth::VoiceBlock& block,
	            int32_t* osc_buffer, int32_t num_samples, bool doing_osc_sync, uint32_t* osc_sync_pos,
	            int32_t overall_pitch_adjust, const int32_t* source_amplitudes_now,
	            const int32_t* source_amplitude_increments, const int32_t* source_wave_index_increments,
	            bool& anything_in_osc_buffer, bool& did_stereo_temp_buffer, bool& unassign_voice_after,
	            const VoiceConfig& cfg);
};

} // namespace deluge::synth
