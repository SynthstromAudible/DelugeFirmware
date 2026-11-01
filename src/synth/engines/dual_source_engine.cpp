/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "synth/engines/dual_source_engine.h"
#include "dsp-next/core/q31_ops.h" // multiply_32x32_rshift32, lshift_and_saturate
#include "dsp-next/fixed/vca/pan.h"
#include "synth/engines/block_controls.h"
#include "synth/engines/pitch.h"
#include "synth/sources/interpolation_buffer_size.h"
#include "util/fixedpoint.h"
#include <algorithm>
#include <cstring>
#include <optional>
#include <utility>

namespace deluge::synth {

bool DualSourceEngine::render_noise(const EngineBlock& block, int32_t channels) {
	int32_t* const osc_buffer = block.osc_buffer;
	const DualSourceControls& c = block.dual;
	// Rendered once for every unison part rather than for each: more unison parts turn the oscillators down
	if (c.noise_volume != 0) [[unlikely]] {
		// >>1 rather than >>2: noise was too loud with a filter on in V2.0.x, and is kept as loud since
		int32_t n = c.noise_volume >> 1;
		if (block.cfg->has_filters) {
			n = multiply_32x32_rshift32(n, c.filter_gain) << 4;
		}

		// The same limit as the oscillators'
		const int32_t noise_amplitude = std::min(n, static_cast<int32_t>(268435455)) >> 2;

		int32_t const* const noise_end = osc_buffer + (block.num_samples * channels);
		int32_t* __restrict__ this_sample = osc_buffer;
		do {
			*this_sample = multiply_32x32_rshift32(static_cast<int32_t>(block.noise->next()), noise_amplitude);
			this_sample++;
		} while (this_sample != noise_end);
		return true;
	}
	memset(osc_buffer, 0, channels * block.num_samples * sizeof(int32_t));
	return false;
}

std::array<uint32_t, kMaxNumVoicesUnison> DualSourceEngine::capture_sync(const EngineBlock& block) {
	std::array<uint32_t, kMaxNumVoicesUnison> phases{};
	for (int32_t u = 0; u < block.cfg->num_unison; u++) {
		phases[u] = block.parts[u].slots[0].current_osc_phase();
	}
	return phases;
}

std::array<int32_t, kNumSources> DualSourceEngine::wave_index_increments(const EngineBlock& block) {
	std::array<int32_t, kNumSources> increments{};
	for (int32_t s = 0; s < kNumSources; s++) {
		increments[s] = static_cast<int32_t>(block.dual.wave_index[s] - block.levels->wave_indexes_last_time[s])
		                / block.num_samples;
	}
	return increments;
}

void DualSourceEngine::store_levels(const EngineBlock& block, const std::array<int32_t, kNumSources>* amplitudes) {
	for (int32_t s = 0; s < kNumSources; s++) {
		if (amplitudes != nullptr) {
			block.levels->amplitudes_last_time[s] = (*amplitudes)[s];
		}
		block.levels->wave_indexes_last_time[s] = block.dual.wave_index[s];
	}
}

void DualSourceEngine::release(std::span<UnisonPart> parts, const ReleaseInfo& info, int32_t priority_rating) {
	for (int32_t s = 0; s < kNumSources; s++) {
		if (info.osc_types[s] == OscType::SAMPLE) {
			for (int32_t u = 0; u < info.num_unison; u++) {
				SourceSlot& slot = parts[u].slots[s];
				if (slot.active && !slot.sample().release({.priority_rating = priority_rating})) {
					slot.unassign(false);
				}
			}
		}
		else if (info.osc_types[s] == OscType::DX7) {
			for (int32_t u = 0; u < info.num_unison; u++) {
				parts[u].slots[s].dx7().release({});
			}
		}
	}
}

bool DualSourceEngine::all_parts_inactive(const EngineBlock& block) {
	const VoiceConfig& cfg = *block.cfg;
	if (cfg.noise_volume_contains_something) {
		return false; // noise keeps the voice sounding
	}
	for (int32_t s = 0; s < kNumSources; s++) {
		if (!cfg.source_ever_active[s]) {
			continue;
		}
		for (int32_t u = 0; u < cfg.num_unison; u++) {
			if (block.parts[u].slots[s].active) {
				return false;
			}
		}
	}
	return true;
}

// Each unison part renders and applies its own amplitude, though it is the same for every part: each part then adds
// straight onto the voice's buffer, which is cleared once. Applying the amplitude to the parts' sum would need a
// buffer per source to clear, sum into and copy out of, and an osc-synced wave already applies its amplitude only as
// it adds onto the buffer, once its render has finished rewriting its own.
bool DualSourceEngine::render_source(const EngineBlock& block, int32_t s, bool stereo_buffer, int32_t source_amplitude,
                                     int32_t amplitude_increment, bool do_osc_sync, const uint32_t* sync_pos,
                                     const uint32_t* sync_increments, std::span<uint32_t> increments_out,
                                     bool advance_only, int32_t wave_index_increment) {
	const VoiceConfig& cfg = *block.cfg;
	bool unison_part_became_inactive = false;

	for (int32_t u = 0; u < cfg.num_unison; u++) {
		SourceSlot& slot = block.parts[u].slots[s];

		// Samples may become inactive
		if (!slot.active) {
			continue;
		}

		// The overall pitch adjust, then this source's
		uint32_t phase_increment = slot.phase_increment_stored_value;
		if (!adjust_pitch(phase_increment, block.dual.overall_pitch_adjust)
		    || !adjust_pitch(phase_increment, block.dual.pitch_adjust[s])) {
			// Too high to render
			if (!increments_out.empty()) {
				increments_out[u] = 0;
			}
			continue;
		}

		if (!increments_out.empty()) {
			increments_out[u] = phase_increment;

			if (advance_only) {
				// A skipped osc-sync master still advances its phase
				slot.advance_osc_phase(phase_increment * block.num_samples);
				continue;
			}
		}

		const bool stereo_unison = (cfg.unison_stereo_spread != 0u) && cfg.num_unison > 1 && stereo_buffer;
		const dsp::fixed::PanAmplitudes unison_pan_amplitudes =
		    dsp::fixed::pan_amplitudes(stereo_unison ? cfg.unison_pan[u] : 0);

		const SourceRenderResult result =
		    render_part(block, u, s, stereo_buffer, source_amplitude, amplitude_increment, phase_increment, do_osc_sync,
		                sync_pos, sync_increments, wave_index_increment, stereo_unison, unison_pan_amplitudes.left,
		                unison_pan_amplitudes.right);
		if (result == SourceRenderResult::UNASSIGN) {
			unison_part_became_inactive = true;
			slot.unassign(false);
		}
	}
	return unison_part_became_inactive;
}

SourceRenderResult DualSourceEngine::render_part(const EngineBlock& block, int32_t u, int32_t s, bool stereo_buffer,
                                                 int32_t source_amplitude, int32_t amplitude_increment,
                                                 uint32_t phase_increment, bool do_osc_sync, const uint32_t* sync_pos,
                                                 const uint32_t* sync_increments, int32_t wave_index_increment,
                                                 bool stereo_unison, int32_t amplitude_l, int32_t amplitude_r) {
	const VoiceConfig& cfg = *block.cfg;
	const DualSourceControls& c = block.dual;
	SourceSlot& slot = block.parts[u].slots[s];

	const uint32_t osc_retrigger_phase = cfg.osc_retrigger_phase[s]; // needed even without osc sync
	const uint32_t sync_pos_this_unison = do_osc_sync ? sync_pos[u] : 0;
	const uint32_t sync_increment_this_unison = do_osc_sync ? sync_increments[u] : 0;
	const auto pulse_width = static_cast<uint32_t>(lshift_and_saturate<1>(c.phase_width[s]));

	std::optional<SyncMaster> sync;
	if (do_osc_sync) {
		sync = SyncMaster{.phase = sync_pos_this_unison, .increment = sync_increment_this_unison};
	}
	const SourceBlock source_block{
	    .osc_buffer = block.osc_buffer,
	    .num_samples = block.num_samples,
	    .stereo_buffer = stereo_buffer,
	    .stereo_unison = stereo_unison,
	    .amplitude_l = amplitude_l,
	    .amplitude_r = amplitude_r,
	    .source_amplitude = source_amplitude,
	    .amplitude_increment = amplitude_increment,
	    .phase_increment = phase_increment,
	    .sync = sync,
	    .controls = make_block_controls(source_amplitude, amplitude_increment, pulse_width, /*apply_amplitude=*/true,
	                                    do_osc_sync, sync_pos_this_unison, sync_increment_this_unison,
	                                    osc_retrigger_phase, cfg.cpu_direness),
	    .stereo_scratch = block.spare_buffers[2],
	    .stereo_scratch_clear_samples = SSI_TX_BUFFER_NUM_SAMPLES,
	    .sync_scratch = block.sync_scratch.data(),
	    .noise = block.noise,
	    .wavetable =
	        {
	            .data = block.voice_block->wave_tables[s],
	            .index_last = static_cast<int32_t>(block.levels->wave_indexes_last_time[s]),
	            .index_increment = wave_index_increment,
	        },
	    .dx7 =
	        {
	            .amp_mod_param = c.phase_width[s],
	            .patch_changed = cfg.dx_patch_changed[s],
	            .note_code = c.note_code,
	        },
	    .sample =
	        {
	            .porta_envelope_pos = c.porta_envelope_pos,
	            .priority_rating = c.priority_rating,
	            .phase_increment_stored_value = slot.phase_increment_stored_value,
	            .pitch_mod_disallows_cache = c.pitch_mod_disallows_cache[s],
	        },
	    .input =
	        {
	            .interpolation_buffer_size = interpolation_buffer_size(
	                cfg.interpolation_mode[s], static_cast<int32_t>(phase_increment), cfg.cpu_direness),
	        },
	};

	// A port-backed source by the sound's current type, else the oscillator the note built. A slot holding none (a
	// mode change under a live voice) renders nothing this block.
	Source* component = slot.source_for(cfg.osc_type[s]);
	if (component == nullptr) [[unlikely]] {
		return SourceRenderResult::SKIP;
	}
	return component->render(source_block);
}

} // namespace deluge::synth
