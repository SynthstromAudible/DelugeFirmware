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
#include "synth/engines/subtractive_engine.h"
#include "definitions_cxx.hpp"
#include "dsp-next/core/q31_ops.h" // multiply_32x32_rshift32_rounded
#include <array>
#include <cstring>
#include <span>

namespace deluge::synth {

EngineRenderResult SubtractiveEngine::render(const DualSourceBlock& block) {
	const VoiceConfig& cfg = *block.cfg;
	const DualSourceControls& c = block.controls;
	const int32_t num_samples = block.num_samples;
	int32_t* const osc_buffer = block.osc_buffer;

	// Each source's amplitude, with the filters' gain folded in when there are filters; the voice's overall amplitude
	// is applied after the filters.
	std::array<int32_t, kNumSources> source_amplitudes{};
	for (int32_t s = 0; s < kNumSources; s++) {
		source_amplitudes[s] =
		    cfg.has_filters ? multiply_32x32_rshift32_rounded(c.volume[s], c.filter_gain) : c.volume[s] >> 4;
	}

	Pass pass = begin(block, source_amplitudes);

	bool anything_in_osc_buffer = render_noise(block, c.stereo_unison ? 2 : 1);

	EngineRenderResult result{};
	bool unison_part_became_inactive = false;

	// First the mono sources, noting the stereo ones. Source 0 renders, at least its phase, when it is the osc-sync
	// master.
	uint32_t sources_to_render_in_stereo = 0;
	for (int32_t s = 0; s < kNumSources; s++) {
		const bool renders = cfg.source_active_currently[s] || (s == 0 && cfg.doing_osc_sync);
		if (!renders) {
			continue;
		}
		if (block.voice_block->source_in_stereo[s]) {
			sources_to_render_in_stereo |= (1 << s);
			continue;
		}
		unison_part_became_inactive |= render_one(block, pass, s, false);
		anything_in_osc_buffer = true;
	}

	// Then the stereo ones, over a stereo copy of the mono ones
	if (sources_to_render_in_stereo != 0u) {
		if (anything_in_osc_buffer) {
			for (int32_t i = num_samples - 1; i >= 0; i--) {
				osc_buffer[(i << 1) + 1] = osc_buffer[i];
				osc_buffer[(i << 1)] = osc_buffer[i];
			}
		}
		else {
			memset(&osc_buffer[num_samples], 0, num_samples * sizeof(int32_t));
		}

		// Osc sync still applies: with stereo unison spread, both sources come here (see VoiceBlock::source_in_stereo)
		for (int32_t s = 0; s < kNumSources; s++) {
			if ((sources_to_render_in_stereo & (1 << s)) != 0u) {
				unison_part_became_inactive |= render_one(block, pass, s, true);
			}
		}
		result.did_stereo_buffer = true;
	}

	// A unison part that ended (for either source), with no noise source: perhaps every part has ended
	result.all_parts_ended = unison_part_became_inactive && all_parts_inactive(block);

	store_levels(block, &source_amplitudes);
	return result;
}

SubtractiveEngine::Pass SubtractiveEngine::begin(const DualSourceBlock& block,
                                                 const std::array<int32_t, kNumSources>& source_amplitudes) {
	const DualSourceControls& c = block.controls;
	SourceLevels& levels = *block.levels;

	// A new filter gain, or a slow attack on the first block, jumps to the new levels rather than ramping to them
	const bool hold_levels =
	    c.first_render ? (c.env0_attack > 245632) : (levels.filter_gain_last_time != c.filter_gain);
	if (hold_levels) {
		levels.amplitudes_last_time = source_amplitudes;
	}
	Pass pass{.amplitudes = levels.amplitudes_last_time};
	for (int32_t s = 0; s < kNumSources; s++) {
		pass.amplitude_increments[s] =
		    static_cast<int32_t>(source_amplitudes[s] - levels.amplitudes_last_time[s]) / block.num_samples;
	}
	levels.filter_gain_last_time = c.filter_gain;

	pass.wave_index_increments = wave_index_increments(block);
	if (block.cfg->doing_osc_sync) {
		pass.sync_pos = capture_sync(block);
	}
	return pass;
}

bool SubtractiveEngine::render_one(const DualSourceBlock& block, Pass& pass, int32_t s, bool stereo) {
	const bool doing_osc_sync = block.cfg->doing_osc_sync;
	std::span<uint32_t> master_increments;
	if (s == 0 && doing_osc_sync) {
		master_increments = pass.sync_increments;
	}
	// A silent master still advances its phase, which the slave syncs to
	const bool advance_only = !master_increments.empty() && !block.cfg->source_active_currently[s];
	return render_source(block, s, stereo, pass.amplitudes[s], pass.amplitude_increments[s], (s == 1) && doing_osc_sync,
	                     pass.sync_pos.data(), pass.sync_increments.data(), master_increments, advance_only,
	                     pass.wave_index_increments[s]);
}

} // namespace deluge::synth
