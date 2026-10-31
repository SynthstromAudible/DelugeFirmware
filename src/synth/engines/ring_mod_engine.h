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
#include "synth/engines/dual_source_engine.h"
#include <array>
#include <cstdint>
#include <span>

namespace deluge::synth {

/// @brief The ring-mod engine: each unison part renders both sources' raw waves, source 1 optionally hard-synced to
/// source 0, multiplies them and pans the product into the voice's buffer, beside the noise source.
class RingModEngine : public DualSourceEngine {
public:
	/// One source of one unison part, ready to render raw.
	struct RingSource {
		SourceSlot* part{};         ///< holds the oscillator (VA, wavetable or supersaw) the note built
		uint32_t phase_increment{}; ///< already pitch-adjusted (UQ0.32)
		int32_t pulse_width{};      ///< the source's pulse width, pre-shifted (no-op unless square)
		deluge::dsp::fixed::WaveTableData
		    wave_table;            ///< the loaded table's bands (wavetable component only), else empty
		int32_t wave_index_last{}; ///< wavetable morph index + its per-block increment
		int32_t wave_index_increment{};
		OscType osc_type{};         ///< selects the per-wave amplitude compensation
		uint32_t retrigger_phase{}; ///< the source's retrigger phase, kNoRetrigger when it runs free
	};

	/// Renders @p block's ring-mod and noise, adding into its buffer, and stores the wavetable indexes the next block
	/// starts from. The unison parts render in ascending frequency, so one too high to render ends the rest.
	/// Starts the note; a source that cannot sound fails it, as ring-mod needs both.
	bool note_on(const EngineNote& note) override;
	EngineRenderResult render(const EngineBlock& block) override;

private:
	/// Renders each unison part; stops at the first too high to render.
	static void render_parts(const EngineBlock& block, const std::array<uint32_t, kMaxNumVoicesUnison>& sync_pos,
	                         const std::array<int32_t, kNumSources>& wave_index_increments);
	/// Render one unison part of the ring-mod, *adding* into @p osc_buffer. Source 0 is the hard-sync master (its
	/// phase @p osc_sync_pos, its increment @p src0's); sync is applied to source 1 only when @p do_osc_sync.
	/// @p scratch0, @p scratch1 and @p sync_scratch are the voice's (each ≥ @p num_samples).
	static void render_unison_part(const RingSource& src0, const RingSource& src1, bool do_osc_sync,
	                               uint32_t osc_sync_pos, bool has_filters, int32_t cpu_direness, int32_t filter_gain,
	                               int32_t unison_amplitude_l, int32_t unison_amplitude_r, bool stereo_unison,
	                               int32_t* osc_buffer, int32_t num_samples, int32_t* scratch0, int32_t* scratch1,
	                               std::span<int32_t> sync_scratch);
};

} // namespace deluge::synth
