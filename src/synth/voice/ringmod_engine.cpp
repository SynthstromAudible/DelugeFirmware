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

#include "synth/voice/ringmod_engine.h"
#include "dsp-next/core/types.h" // deluge::dsp::fixed::Buffer
#include "dsp-next/fixed/vca/pan.h"
#include "modulation/params/param.h"             // LOCAL_OSC_A_PITCH_ADJUST / LOCAL_OSC_A_PHASE_WIDTH
#include "synth/voice/source_component_render.h" // voice::makeBlockControls
#include "synth/voice/unison_part_source.h"
#include "synth/voice/voice.h" // Voice (member access — pre-roll + per-source state)
#include "util/fixedpoint.h"

using namespace deluge;

namespace params = deluge::modulation::params;

namespace deluge::synth {

void RingModEngine::render(Voice& voice, int32_t* osc_buffer, int32_t num_samples, bool do_osc_sync,
                           uint32_t* osc_sync_pos, int32_t overall_pitch_adjust,
                           const int32_t* source_wave_index_increments, bool stereo_unison, int32_t filter_gain,
                           bool& did_stereo_temp_buffer, int32_t* scratch0, int32_t* scratch1,
                           std::span<int32_t> sync_scratch, const VoiceConfig& cfg,
                           const deluge::synth::VoiceBlock& block) {

	if (stereo_unison) {
		// oscBuffer is always a stereo temp buffer
		did_stereo_temp_buffer = true;
	}

	// For each unison part
	for (int32_t u = 0; u < cfg.num_unison; u++) {

		const deluge::dsp::fixed::PanAmplitudes unison_pan_amplitudes =
		    deluge::dsp::fixed::pan_amplitudes(stereo_unison ? cfg.unison_pan[u] : 0);
		int32_t unison_amplitude_l = unison_pan_amplitudes.left;
		int32_t unison_amplitude_r = unison_pan_amplitudes.right;

		// Work out the phase increments of the two sources. If these are too high, we abandon all remaining unison
		// parts. Yes this will affect all unison parts, which seems like it's not what we want, but since we're
		// traversing the unison parts in ascending frequency, it's fine!

		uint32_t phase_increments[kNumSources];
		for (int32_t s = 0; s < kNumSources; s++) {
			phase_increments[s] = voice.unison_parts[u].sources[s].phase_increment_stored_value;
		}

		// If overall pitch adjusted...
		if (overall_pitch_adjust != kMaxSampleValue) {
			for (int32_t s = 0; s < kNumSources; s++) {
				if (!Voice::adjust_pitch(phase_increments[s], overall_pitch_adjust)) {
					return; // freq too high — all remaining (higher) unison parts are too high too
				}
			}
		}

		// If individual source pitch adjusted...
		for (int32_t s = 0; s < kNumSources; s++) {
			if (!Voice::adjust_pitch(phase_increments[s],
			                         voice.params.param_final_values[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
				return; // freq too high — abandon the rest of the unison spread
			}
		}

		// Build the per-source render context; renderUnisonPart owns the cross-source ring-mod relationship
		// (render both sources raw with source 0 the hard-sync master, then multiply + pan).
		RingModEngine::Source ring_mod_sources[kNumSources];
		for (int32_t s = 0; s < kNumSources; s++) {
			// Work out pulse width, from parameter. This has no effect if we're not actually using square
			// waves, but just do it anyway, it's a simple calculation
			int32_t pulse_width = static_cast<uint32_t>(
			    lshift_and_saturate<1>(voice.params.param_final_values[params::LOCAL_OSC_A_PHASE_WIDTH + s]));

			OscType osc_type = cfg.osc_type[s];

			ring_mod_sources[s] = RingModEngine::Source{
			    .part = &voice.unison_parts[u].sources[s],
			    .phase_increment = phase_increments[s],
			    .pulse_width = pulse_width,
			    .wave_table = block.wave_tables[s],
			    .wave_index_last = static_cast<int32_t>(voice.source_wave_indexes_last_time[s]),
			    .wave_index_increment = source_wave_index_increments[s],
			    .osc_type = osc_type,
			    .retrigger_phase = cfg.osc_retrigger_phase[s],
			};
		}

		render_unison_part(ring_mod_sources[0], ring_mod_sources[1], do_osc_sync, osc_sync_pos[u], cfg.has_filters,
		                   cfg.cpu_direness, filter_gain, unison_amplitude_l, unison_amplitude_r, stereo_unison,
		                   osc_buffer, num_samples, scratch0, scratch1, sync_scratch);
	}
}

void RingModEngine::render_unison_part(const Source& src0, const Source& src1, bool do_osc_sync, uint32_t osc_sync_pos,
                                       bool has_filters, int32_t cpu_direness, int32_t filter_gain,
                                       int32_t unison_amplitude_l, int32_t unison_amplitude_r, bool stereo_unison,
                                       int32_t* osc_buffer, int32_t num_samples, int32_t* scratch0, int32_t* scratch1,
                                       std::span<int32_t> sync_scratch) {

	int32_t amplitude_for_ring_mod = 1 << 27;

	if (has_filters) {
		amplitude_for_ring_mod = multiply_32x32_rshift32_rounded(amplitude_for_ring_mod, filter_gain) << 4;
	}

	const Source* sources[2] = {&src0, &src1};
	int32_t* scratch[2] = {scratch0, scratch1};

	for (int32_t s = 0; s < 2; s++) {
		// Source 0 can never be the synced (slave) oscillator — it is the master.
		bool synced = (s == 0) ? false : do_osc_sync;
		const Source& source = *sources[s];

		// Both ring-moddable wave families render through their persistent per-source engine (VA or wavetable) via
		// its render_into. Ring-mod renders the raw wave: no amplitude applied, amplitude 0. The hard-sync master is
		// source 0 (phase osc_sync_pos, increment src0.phase_increment).
		// The master's phase and increment are the resetter's
		// NOLINTBEGIN(readability-suspicious-call-argument)
		const synth::BlockControls controls =
		    make_block_controls(0, 0, source.pulse_width, /*apply_amplitude=*/false, synced, osc_sync_pos,
		                        src0.phase_increment, source.retrigger_phase, cpu_direness);
		// NOLINTEND(readability-suspicious-call-argument)
		const dsp::fixed::Buffer out{scratch[s], static_cast<size_t>(num_samples)};
		const dsp::fixed::Buffer sync_block = sync_scratch.first(static_cast<size_t>(num_samples));
		if (source.part->va_source.has_value()) {
			source.part->va_source->render_into(controls, source.phase_increment, out, sync_block);
		}
		else if (source.part->supersaw_source.has_value()) {
			source.part->supersaw_source->render_into(controls, source.phase_increment, source.wave_index_last,
			                                          source.wave_index_increment, out, sync_block);
		}
		else {
			source.part->wavetable_source->render_into(controls, source.wave_table, source.wave_index_last,
			                                           source.wave_index_increment, source.phase_increment, out,
			                                           sync_block);
		}

		// Sine and triangle waves come out bigger in fixed-amplitude rendering (for arbitrary reasons), so we
		// need to compensate
		if (source.osc_type == OscType::SAW || source.osc_type == OscType::ANALOG_SAW_2) {
			amplitude_for_ring_mod <<= 1;
		}
		else if (source.osc_type == OscType::WAVETABLE || source.osc_type == OscType::SUPERSAW) {
			amplitude_for_ring_mod <<= 2;
		}
	}

	int32_t* __restrict__ output = osc_buffer;
	int32_t* __restrict__ input0 = scratch0;
	int32_t* __restrict__ input1 = scratch1;

	if (stereo_unison) {
		int32_t const* const osc_buffer_end = osc_buffer + 2 * num_samples;
		do {
			int32_t out =
			    multiply_32x32_rshift32_rounded(multiply_32x32_rshift32(*input0, *input1), amplitude_for_ring_mod);
			*output++ += multiply_32x32_rshift32(out, unison_amplitude_l) << 2;
			*output++ += multiply_32x32_rshift32(out, unison_amplitude_r) << 2;
			input0++;
			input1++;
		} while (output != osc_buffer_end);
	}
	else {
		int32_t const* const osc_buffer_end = osc_buffer + num_samples;
		do {
			*output +=
			    multiply_32x32_rshift32_rounded(multiply_32x32_rshift32(*input0, *input1), amplitude_for_ring_mod);
			input0++;
			input1++;
		} while (++output != osc_buffer_end);
	}
}

} // namespace deluge::synth
