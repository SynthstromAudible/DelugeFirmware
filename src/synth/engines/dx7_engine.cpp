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

#include "synth/engines/dx7_engine.h"
#include "dsp/core/divide.h"
#include "dsp/fixed/vca/pan.h"
#include "synth/engines/pitch.h"
#include "synth/sources/source.h"
#include <algorithm>
#include <optional>

namespace deluge::synth {

Dx7Engine::~Dx7Engine() {
	for (Part& part : parts_) {
		part.dx7.unassign(false);
	}
}

void Dx7Engine::bind(int32_t u, DxVoicePort* port) {
	parts_[u].dx7.bind_port(port);
}

void Dx7Engine::calculate_phase_increments(const EngineNote& note) {
	const Dx7Setup& dx7 = note.setup->dx7;
	const SoundConfig& config = *note.config;
	const int32_t count = config.voicing.unison.count;
	const std::optional<uint32_t> base =
	    dx7.ever_active ? wave_phase_increment(note.note_code + config.voicing.transpose + dx7.transpose)
	                    : std::nullopt;
	if (!base.has_value()) { // Not sounding, or too high to render (above 22.05 kHz)
		for (int32_t u = 0; u < count; u++) {
			parts_[u].active = false;
		}
		return;
	}
	// Cents, then the master tune's; the tuners work on signed increments, whose bits are the same
	auto increment = static_cast<uint32_t>(dx7.fine_tuner.apply(static_cast<int32_t>(*base)));
	increment = static_cast<uint32_t>(config.voicing.tune.apply(static_cast<int32_t>(increment)));
	if (count == 1) {
		parts_[0].phase_increment = dsp::UQ<32>::from_raw(increment);
		return;
	}
	for (int32_t u = 0; u < count; u++) {
		const std::optional<uint32_t> part = detuned_increment(increment, config.voicing.unison.detunes[u]);
		if (part.has_value()) {
			parts_[u].phase_increment = dsp::UQ<32>::from_raw(*part);
		}
		else {
			parts_[u].active = false;
		}
	}
}

// The voice's phase draws came first (randomize_osc_phases); here, per part, the DX7's note-on, whose port draws its
// detunes and phases.
bool Dx7Engine::note_on(const EngineNote& note) {
	const Dx7Setup& dx7 = note.setup->dx7;
	const int32_t count = note.config->voicing.unison.count;
	for (int32_t u = 0; u < count; u++) {
		parts_[u].active = dx7.ever_active;
		for (SourceSlot& slot : note.parts[u].slots) {
			slot.active = false; // DX7 mode plays no slot
		}
	}
	calculate_phase_increments(note);
	if (!dx7.ever_active) [[unlikely]] {
		return true;
	}
	const SourceNote source_note{
	    .samples_late = note.samples_late,
	    .note_code = note.note_code,
	    .velocity = note.velocity,
	    .priority_rating = note.priority_rating,
	    .dx_patch = dx7.patch,
	    .osc_type = OscType::DX7,
	};
	for (int32_t u = 0; u < count; u++) {
		if (parts_[u].active && !parts_[u].dx7.note_on(source_note)) [[unlikely]] {
			return false;
		}
	}
	return true;
}

EngineRenderResult Dx7Engine::render(const EngineBlock& block) {
	using dsp::Q;
	const BlockValues& cfg = *block.values;
	const Dx7Controls& c = block.dx7();
	const int32_t num_samples = block.num_samples;
	SourceLevels& levels = *block.levels;

	// The level at Q27: times the filters' gain rounded, or a sixteenth unfiltered. The Q27 word is the DX7's level as
	// the oscillators' VCAs take it, read as the Q31 level the DX7's render steps (the subtractive engine's crossing)
	const auto level =
	    dsp::reinterpret_bits<Q<31>>(cfg.has_filters ? mul_r(c.volume, c.filter_gain) : as_floor<Q<27>>(c.volume));
	// A new filter gain, or a slow attack on the first block, jumps to the new level rather than ramping to it
	const bool hold = c.first_render ? (c.env0_attack > 245632) : (levels.filter_gain_last_time != c.filter_gain);
	if (hold) {
		levels.amplitudes_last_time[0] = level;
	}
	const Q<31> start = levels.amplitudes_last_time[0];
	// Raw: the increment is an integer division of the levels' difference
	const auto increment =
	    Q<31>::from_raw(dsp::divide_by_small(static_cast<int32_t>(level.raw() - start.raw()), num_samples));
	levels.filter_gain_last_time = c.filter_gain;

	// The buffer starts cleared, both halves for stereo pairs
	const bool stereo = c.in_stereo;
	std::fill_n(block.osc_buffer, (stereo || c.stereo_unison ? 2 : 1) * num_samples, Q<31>{});

	const int32_t count = block.config->voicing.unison.count;
	bool part_ended = false;
	if (cfg.dx7_active_currently) {
		for (int32_t u = 0; u < count; u++) {
			Part& part = parts_[u];
			if (!part.active) {
				continue;
			}
			// The voice's pitch adjust, then the DX7's
			dsp::UQ<32> phase_increment = part.phase_increment;
			if (!adjust_pitch(phase_increment, c.overall_pitch_adjust)
			    || !adjust_pitch(phase_increment, c.pitch_adjust)) {
				continue; // Too high to render
			}
			const bool stereo_unison = block.config->voicing.unison_spread != 0u && count > 1 && stereo;
			const dsp::fixed::PanAmplitudes pans =
			    dsp::fixed::pan_amplitudes(stereo_unison ? block.config->voicing.unison.pans[u] : Q<31>{});
			const SourceBlock source_block{
			    .osc_buffer = block.osc_buffer,
			    .num_samples = num_samples,
			    .stereo_buffer = stereo,
			    .stereo_unison = stereo_unison,
			    .amplitude_l = pans.left,
			    .amplitude_r = pans.right,
			    .source_amplitude = start,
			    .amplitude_increment = increment,
			    .phase_increment = phase_increment,
			    .sync = std::nullopt,
			    .controls = {},
			    .stereo_scratch = nullptr,
			    .stereo_scratch_clear_samples = 0,
			    .sync_scratch = nullptr,
			    .noise = nullptr,
			    .wavetable = {},
			    .dx7 = {.amp_mod_param = c.amp_mod, .patch_changed = cfg.dx7_patch_changed, .note_code = c.note_code},
			    .sample = {},
			    .input = {},
			};
			if (part.dx7.render(source_block) == SourceRenderResult::UNASSIGN) {
				part_ended = true;
				part.active = false;
				part.dx7.unassign(false);
			}
		}
	}
	levels.amplitudes_last_time[0] = level;

	// A part that ended: perhaps every part has
	bool all_ended = part_ended;
	for (int32_t u = 0; u < count && all_ended; u++) {
		all_ended = !parts_[u].active;
	}
	return {.did_stereo_buffer = stereo, .all_parts_ended = all_ended};
}

void Dx7Engine::release(std::span<UnisonPart> /*parts*/, const ReleaseInfo& info, int32_t /*priority_rating*/) {
	for (int32_t u = 0; u < info.num_unison; u++) {
		(void)parts_[u].dx7.release({});
	}
}

} // namespace deluge::synth
