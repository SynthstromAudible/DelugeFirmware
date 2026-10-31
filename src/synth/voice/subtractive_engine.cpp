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

#include "synth/voice/subtractive_engine.h"
#include "definitions_cxx.hpp"
#include "synth/voice/voice.h"
#include <array>
#include <cstring>
#include <span>

namespace deluge::synth {

void SubtractiveEngine::render(Voice& voice, const ControlBlock& control_block, const deluge::synth::VoiceBlock& block,
                               int32_t* osc_buffer, int32_t num_samples, bool doing_osc_sync, uint32_t* osc_sync_pos,
                               int32_t overall_pitch_adjust, const int32_t* source_amplitudes_now,
                               const int32_t* source_amplitude_increments, const int32_t* source_wave_index_increments,
                               bool& anything_in_osc_buffer, bool& did_stereo_temp_buffer, bool& unassign_voice_after,
                               const VoiceConfig& cfg) {

	bool unison_part_became_inactive = false;

	std::array<uint32_t, kMaxNumVoicesUnison> osc_sync_phase_increment; // left as is where no part writes it
	uint32_t sources_to_render_in_stereo = 0;

	// First, render any mono sources, and note whether there are any stereo ones
	for (int32_t s = 0; s < kNumSources; s++) {

		std::span<uint32_t> get_phase_increments;
		bool get_out_after_getting_phase_increments = false;

		// If we're doing osc sync and this is osc A...
		if ((s == 0) && doing_osc_sync) {
			get_phase_increments = osc_sync_phase_increment;
		}

		// This this source isn't active...
		if (!cfg.source_active_currently[s]) {

			// If we're doing osc sync...
			if (!get_phase_increments.empty()) {
				get_out_after_getting_phase_increments = true;

				// Otherwise, skip it
			}
			else {
				continue;
			}
		}

		if (!block.source_in_stereo[s]) {
			unison_part_became_inactive |= voice.render_basic_source(
			    control_block, block, s, osc_buffer, num_samples, false, source_amplitudes_now[s], overall_pitch_adjust,
			    (s == 1) && doing_osc_sync, osc_sync_pos, osc_sync_phase_increment.data(),
			    source_amplitude_increments[s], get_phase_increments, get_out_after_getting_phase_increments,
			    source_wave_index_increments[s], cfg);
			anything_in_osc_buffer = true;
		}
		else {
			sources_to_render_in_stereo |= (1 << s);
		}
	}

	// If any sources need rendering in stereo
	if (sources_to_render_in_stereo != 0u) {

		// If we've already got something mono in the buffer, copy that to the right-channel buffer
		if (anything_in_osc_buffer) {
			for (int32_t i = num_samples - 1; i >= 0; i--) {
				osc_buffer[(i << 1) + 1] = osc_buffer[i];
				osc_buffer[(i << 1)] = osc_buffer[i];
			}
		}

		// Otherwise, make it blank
		else {
			memset(&osc_buffer[num_samples], 0, num_samples * sizeof(int32_t));
		}

		// Render each source that's stereo. Osc sync must still be applied here: with stereo unison spread, both
		// sources get deferred into this path (see VoiceBlock::source_in_stereo), so we have to capture osc A's phase
		// increments and sync osc B to them just like the mono path above does.
		for (int32_t s = 0; s < kNumSources; s++) {
			if ((sources_to_render_in_stereo & (1 << s)) != 0u) {

				std::span<uint32_t> get_phase_increments;
				if ((s == 0) && doing_osc_sync) {
					get_phase_increments = osc_sync_phase_increment;
				}
				bool get_out_after_getting_phase_increments =
				    !get_phase_increments.empty() && !cfg.source_active_currently[s];

				unison_part_became_inactive |= voice.render_basic_source(
				    control_block, block, s, osc_buffer, num_samples, true, source_amplitudes_now[s],
				    overall_pitch_adjust, (s == 1) && doing_osc_sync, osc_sync_pos, osc_sync_phase_increment.data(),
				    source_amplitude_increments[s], get_phase_increments, get_out_after_getting_phase_increments,
				    source_wave_index_increments[s], cfg);
			}
		}

		did_stereo_temp_buffer = true;
	}

	// If any unison part became inactive (for either source), and no noise-source, then it might be time to
	// unassign the voice...
	if (unison_part_became_inactive && voice.are_all_unison_parts_inactive(cfg)) {

		// If no filters, we can just unassign
		if (!voice.filter_set.is_on()) {
			unassign_voice_after = true;
		}

		// Otherwise, must do a fast-release to avoid a click
		else {
			if (voice.envelopes[0].stage() < Envelope::Stage::FAST_RELEASE) {
				voice.envelopes[0].unconditional_release(Envelope::Stage::FAST_RELEASE);
			}
		}
	}
}

} // namespace deluge::synth
