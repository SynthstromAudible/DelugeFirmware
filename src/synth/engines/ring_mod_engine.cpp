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

#include "synth/engines/ring_mod_engine.h"
#include "dsp-next/core/types.h" // deluge::dsp::fixed::Buffer
#include "dsp-next/fixed/vca/pan.h"
#include "synth/engines/block_controls.h" // make_block_controls
#include "synth/engines/pitch.h"
#include "synth/sources/source_slot.h"
#include "util/fixedpoint.h"
#include <algorithm>
#include <type_traits>
#include <utility>

using namespace deluge;

namespace deluge::synth {

bool RingModEngine::note_on(const EngineNote& note) {
	if (!mark_active(note, /*refuse_silent=*/true)) {
		return false;
	}
	calculate_phase_increments(note);
	return start_sources(note);
}

EngineRenderResult RingModEngine::render(const EngineBlock& block) {
	const VoiceConfig& cfg = *block.cfg;
	const DualSourceControls& c = block.dual;
	const bool stereo_unison = c.stereo_unison;

	const std::array<int32_t, kNumSources> wave_index_increments = DualSourceEngine::wave_index_increments(block);
	std::array<uint32_t, kMaxNumVoicesUnison> sync_pos{};
	if (cfg.doing_osc_sync) {
		sync_pos = capture_sync(block);
	}
	render_noise(block, stereo_unison ? 2 : 1);

	render_parts(block, sync_pos, wave_index_increments);

	// The source amplitudes are not ring-mod's: it stores only the wavetable indexes
	store_levels(block, nullptr);
	// osc_buffer is always a stereo buffer when the unison is spread
	return EngineRenderResult{.did_stereo_buffer = stereo_unison};
}

void RingModEngine::render_parts(const EngineBlock& block, const std::array<uint32_t, kMaxNumVoicesUnison>& sync_pos,
                                 const std::array<int32_t, kNumSources>& wave_index_increments) {
	const VoiceConfig& cfg = *block.cfg;
	const DualSourceControls& c = block.dual;
	const bool stereo_unison = c.stereo_unison;

	for (int32_t u = 0; std::cmp_less(u, cfg.num_unison); u++) {

		const deluge::dsp::fixed::PanAmplitudes unison_pan_amplitudes =
		    deluge::dsp::fixed::pan_amplitudes(stereo_unison ? cfg.unison_pan[u] : 0);
		int32_t unison_amplitude_l = unison_pan_amplitudes.left;
		int32_t unison_amplitude_r = unison_pan_amplitudes.right;

		// The two sources' phase increments. One too high abandons every remaining unison part: the parts ascend in
		// frequency, so the rest are too high as well.
		std::array<uint32_t, kNumSources> phase_increments{};
		for (int32_t s = 0; s < kNumSources; s++) {
			phase_increments[s] = block.parts[u].slots[s].phase_increment_stored_value;
		}

		// If overall pitch adjusted...
		if (c.overall_pitch_adjust != kMaxSampleValue) {
			for (int32_t s = 0; s < kNumSources; s++) {
				if (!adjust_pitch(phase_increments[s], c.overall_pitch_adjust)) {
					return;
				}
			}
		}

		// If individual source pitch adjusted...
		for (int32_t s = 0; s < kNumSources; s++) {
			if (!adjust_pitch(phase_increments[s], c.pitch_adjust[s])) {
				return;
			}
		}

		std::array<RingSource, kNumSources> ring_mod_sources{};
		for (int32_t s = 0; s < kNumSources; s++) {
			// The pulse width only matters to a square wave, but is cheap to work out for any
			const int32_t pulse_width = lshift_and_saturate<1>(c.phase_width[s]);

			ring_mod_sources[s] = RingSource{
			    .part = &block.parts[u].slots[s],
			    .phase_increment = phase_increments[s],
			    .pulse_width = pulse_width,
			    .wave_table = block.voice_block->wave_tables[s],
			    .wave_index_last = static_cast<int32_t>(block.levels->wave_indexes_last_time[s]),
			    .wave_index_increment = wave_index_increments[s],
			    .osc_type = cfg.osc_type[s],
			    .retrigger_phase = cfg.osc_retrigger_phase[s],
			};
		}

		render_unison_part(ring_mod_sources[0], ring_mod_sources[1], cfg.doing_osc_sync, sync_pos[u], cfg.has_filters,
		                   cfg.cpu_direness, c.filter_gain, unison_amplitude_l, unison_amplitude_r, stereo_unison,
		                   block.osc_buffer, block.num_samples, block.spare_buffers[2], block.spare_buffers[3],
		                   block.sync_scratch);
	}
}

void RingModEngine::render_unison_part(const RingSource& src0, const RingSource& src1, bool do_osc_sync,
                                       uint32_t osc_sync_pos, bool has_filters, int32_t cpu_direness,
                                       int32_t filter_gain, int32_t unison_amplitude_l, int32_t unison_amplitude_r,
                                       bool stereo_unison, int32_t* osc_buffer, int32_t num_samples, int32_t* scratch0,
                                       int32_t* scratch1, std::span<int32_t> sync_scratch) {

	int32_t amplitude_for_ring_mod = 1 << 27;

	if (has_filters) {
		amplitude_for_ring_mod = multiply_32x32_rshift32_rounded(amplitude_for_ring_mod, filter_gain) << 4;
	}

	const std::array<const RingSource*, kNumSources> sources{&src0, &src1};
	const std::array<int32_t*, kNumSources> scratch{scratch0, scratch1};

	for (int32_t s = 0; s < 2; s++) {
		// Source 0 can never be the synced (slave) oscillator — it is the master.
		bool synced = (s == 0) ? false : do_osc_sync;
		const RingSource& source = *sources[s];

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
		std::visit(
		    [&]<class Held>(Held& held) {
			    if constexpr (std::is_same_v<Held, VirtualAnalogSource>) {
				    held.render_into(controls, source.phase_increment, out, sync_block);
			    }
			    else if constexpr (std::is_same_v<Held, SupersawSource>) {
				    held.render_into(controls, source.phase_increment, source.wave_index_last,
				                     source.wave_index_increment, out, sync_block);
			    }
			    else if constexpr (std::is_same_v<Held, WavetableSource>) {
				    held.render_into(controls, source.wave_table, source.wave_index_last, source.wave_index_increment,
				                     source.phase_increment, out, sync_block);
			    }
			    else {
				    // No oscillator held (a mode change under a live voice): a silent wave
				    std::ranges::fill(out, 0);
			    }
		    },
		    source.part->oscillator());

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
