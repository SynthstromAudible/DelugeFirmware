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

#include "definitions_cxx.hpp" // OscType
#include "dsp-next/fixed/wavetable/wave_table_data.h"
#include <cstdint>
#include <span>

namespace deluge::synth {
class Voice;
} // namespace deluge::synth

class VoiceUnisonPartSource;
struct VoiceConfig;
namespace deluge::synth {
struct VoiceBlock;
} // namespace deluge::synth

/// @brief The ring-mod synth engine: the first **cross-source swap engine** (per the L1 reframe,
/// `docs/dev/synth_l1_engine_architecture.md`). It owns the full per-voice ring-mod render: the unison loop, the
/// per-source pitch math, and the ring-mod *relationship* between the two sources (render each source raw with
/// source 0 the optional hard-sync master, then multiply and pan into the voice buffer).
///
/// The two sources are still **owned by Voice** (their persistent VA / wavetable *components* live on the
/// VoiceUnisonPartSources); the engine borrows them via @ref Source and drives them through the shared
/// `deluge::voice::renderVaSource` / `renderWavetableSource` seam. The per-voice pre-roll (filter gain, osc-sync
/// master phases, wave-index increments) is computed by Voice and passed in, mirroring @ref SubtractiveEngine.
/// Component ownership + per-unison instancing move in a later slice; for now the engine holds no state of its own.
class RingModEngine {
public:
	/// One ring-moddable source for this unison part (RINGMOD only ever uses the VA / wavetable components).
	struct Source {
		VoiceUnisonPartSource* part; ///< holds the persistent vaSource / wavetableSource component
		uint32_t phase_increment;    ///< already pitch-adjusted (UQ0.32)
		int32_t pulse_width;         ///< LOCAL_OSC_*_PHASE_WIDTH, pre-shifted (no-op unless square)
		deluge::dsp::fixed::WaveTableData
		    wave_table;          ///< the loaded table's bands (wavetable component only), else empty
		int32_t wave_index_last; ///< wavetable morph index + its per-block increment
		int32_t wave_index_increment;
		OscType osc_type;         ///< selects the per-wave amplitude compensation
		uint32_t retrigger_phase; ///< oscRetriggerPhase[s]
	};

	/// Render all of @p voice's ring-mod sources, *adding* into @p oscBuffer. Owns the unison loop (it iterates the
	/// voice's unison parts internally, in ascending frequency) + the per-source pitch adjust; on a too-high
	/// frequency it abandons all remaining unison parts (valid because they ascend). The per-voice pre-roll
	/// (@p filterGain, @p oscSyncPos, @p sourceWaveIndexIncrements, @p stereoUnison, @p overallPitchAdjust) is
	/// computed by Voice and passed in; @p didStereoTempBuffer is set true when @p stereoUnison. @p scratch0 /
	/// @p scratch1 are the caller's two spare buffers (≥ @p numSamples).
	void render(deluge::synth::Voice& voice, int32_t* osc_buffer, int32_t num_samples, bool do_osc_sync,
	            uint32_t* osc_sync_pos, int32_t overall_pitch_adjust, const int32_t* source_wave_index_increments,
	            bool stereo_unison, int32_t filter_gain, bool& did_stereo_temp_buffer, int32_t* scratch0,
	            int32_t* scratch1, std::span<int32_t> sync_scratch, const VoiceConfig& cfg,
	            const deluge::synth::VoiceBlock& block);

private:
	/// Render one unison part of the ring-mod, *adding* into @p oscBuffer. Source 0 is the hard-sync master (its
	/// phase = @p oscSyncPos, increment = @p src0.phaseIncrement); sync is applied to source 1 only when
	/// @p doOscSync. @p scratch0 / @p scratch1 are the caller's two spare buffers (≥ @p numSamples).
	void render_unison_part(const Source& src0, const Source& src1, bool do_osc_sync, uint32_t osc_sync_pos,
	                        bool has_filters, int32_t cpu_direness, int32_t filter_gain, int32_t unison_amplitude_l,
	                        int32_t unison_amplitude_r, bool stereo_unison, int32_t* osc_buffer, int32_t num_samples,
	                        int32_t* scratch0, int32_t* scratch1, std::span<int32_t> sync_scratch);
};
