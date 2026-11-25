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

#include "instruments/classic/deluge_synth.h"

#include "dsp/fixed/pitch/fine_tune.h"
#include "instruments/classic/state_access.h"
#include "synth/voice/unison.h"

#include <limits>

namespace deluge::instruments::classic {
namespace {

constexpr size_t kNumSlots = 2;

/// Mark @p part in @p parts when @p differs.
void mark(Parts& parts, Part part, bool differs) noexcept {
	if (differs) {
		parts.set(std::to_underlying(part));
	}
}

/// The slot-indexed parts for slot @p s.
constexpr Part slot_part(Part a, size_t s) noexcept {
	return static_cast<Part>(std::to_underlying(a) + s);
}

} // namespace

synth::SoundConfig DelugeSynth::derive(const State& state) noexcept {
	using dsp::fixed::FineTune;
	synth::SoundConfig config;
	const synth::VoicingState v = instruments::classic::voicing(state);
	synth::Voicing& voicing = config.voicing;
	voicing.priority = v.priority;
	voicing.polyphony = v.polyphony;
	voicing.max_voices = v.max_voices;
	voicing.transpose = v.master.transpose;
	voicing.cents = v.master.cents;
	voicing.tune = FineTune::of_cents(v.master.cents);
	voicing.unison_detune = v.unison.detune;
	voicing.unison_spread = v.unison.spread;
	voicing.unison = synth::derive_unison(v.unison.count, v.unison.detune, v.unison.spread);
	synth::EngineSettings& engine = config.engine;
	engine.mode = synth_mode(state);
	engine.osc_sync = oscillator_sync(state);
	engine.modulator1_to_modulator0 = instruments::classic::modulator1_to_modulator0(state);
	const std::array<synth::Pitch, 2> mods = modulators(state);
	for (size_t m = 0; m < engine.modulators.size(); m++) {
		engine.modulators[m] = {.transpose = mods[m].tuning.transpose,
		                        .cents = mods[m].tuning.cents,
		                        .tune = FineTune::of_cents(mods[m].tuning.cents),
		                        .retrigger_phase = phase_word(mods[m].retrigger)};
	}
	for (size_t l = 0; l < config.lfos.size(); l++) {
		config.lfos[l] = lfo(state, l);
	}
	for (size_t e = 0; e < config.envelopes.size(); e++) {
		config.envelopes[e] = envelope(state, e);
	}
	for (size_t s = 0; s < config.sources.size(); s++) {
		const auto id = static_cast<SourceId>(s);
		const Tuning tuning = source_tuning(state, id);
		const SamplePlayback playback = sample_playback(state, id);
		engine.osc_retrigger_phase[s] = phase_word(source_retrigger(state, id));
		config.sources[s] = {.osc_type = source_type(state, id),
		                     .transpose = tuning.transpose,
		                     .cents = tuning.cents,
		                     .tune = FineTune::of_cents(tuning.cents),
		                     .repeat_mode = playback.repeat_mode,
		                     .time_stretch_amount = playback.time_stretch_amount,
		                     .interpolation_mode = playback.interpolation,
		                     .pitch_and_speed_independent = playback.pitch_and_speed_independent,
		                     .reversed = playback.reversed};
	}
	return config;
}

Parts DelugeSynth::changes(const State& before, const State& after) noexcept {
	Parts parts;
	const synth::VoicingState was_voicing = voicing(before);
	const synth::VoicingState now_voicing = voicing(after);
	mark(parts, Part::VOICING,
	     was_voicing.priority != now_voicing.priority || was_voicing.polyphony != now_voicing.polyphony
	         || was_voicing.max_voices != now_voicing.max_voices);
	mark(parts, Part::TUNING, was_voicing.master != now_voicing.master);
	mark(parts, Part::UNISON_COUNT, was_voicing.unison.count != now_voicing.unison.count);
	mark(parts, Part::UNISON_DETUNE, was_voicing.unison.detune != now_voicing.unison.detune);
	mark(parts, Part::UNISON_SPREAD, was_voicing.unison.spread != now_voicing.unison.spread);
	mark(parts, Part::MODE, synth_mode(before) != synth_mode(after));
	mark(parts, Part::OSC_SYNC, oscillator_sync(before) != oscillator_sync(after));
	bool retrigger = false;
	for (size_t s = 0; s < kNumSlots; s++) {
		const auto id = static_cast<SourceId>(s);
		mark(parts, slot_part(Part::SOURCE_A_TYPE, s), source_type(before, id) != source_type(after, id));
		mark(parts, slot_part(Part::SOURCE_A_TUNING, s), source_tuning(before, id) != source_tuning(after, id));
		mark(parts, slot_part(Part::SOURCE_A_PLAYBACK, s), sample_playback(before, id) != sample_playback(after, id));
		mark(parts, slot_part(Part::SOURCE_A_RANGES, s), !same_ranges(before, after, id));
		const DxPatchState* was_patch = dx_patch(before, id);
		const DxPatchState* now_patch = dx_patch(after, id);
		mark(parts, slot_part(Part::SOURCE_A_DX, s),
		     (was_patch != nullptr) != (now_patch != nullptr) || (was_patch != nullptr && *was_patch != *now_patch));
		retrigger = retrigger || source_retrigger(before, id) != source_retrigger(after, id);
	}
	const std::array<synth::Pitch, 2> was_mods = modulators(before);
	const std::array<synth::Pitch, 2> now_mods = modulators(after);
	bool modulator_tuning = false;
	for (size_t m = 0; m < was_mods.size(); m++) {
		retrigger = retrigger || was_mods[m].retrigger != now_mods[m].retrigger;
		modulator_tuning = modulator_tuning || was_mods[m].tuning != now_mods[m].tuning;
	}
	mark(parts, Part::RETRIGGER, retrigger);
	mark(parts, Part::MODULATOR_TUNING, modulator_tuning);
	mark(parts, Part::MODULATOR_ROUTING,
	     instruments::classic::modulator1_to_modulator0(before)
	         != instruments::classic::modulator1_to_modulator0(after));
	bool lfo_wave = false;
	bool lfo_sync = false;
	for (size_t l = 0; l < LFO_COUNT; l++) {
		const synth::LfoState was = lfo(before, l);
		const synth::LfoState now = lfo(after, l);
		lfo_wave = lfo_wave || was.shape != now.shape;
		lfo_sync = lfo_sync || was.sync_type != now.sync_type || was.sync_level != now.sync_level;
	}
	mark(parts, Part::LFO_WAVE, lfo_wave);
	mark(parts, Part::LFO_SYNC, lfo_sync);
	mark(parts, Part::EFFECTS, sound_effects(before) != sound_effects(after));
	return parts;
}

synth::BlockValues DelugeSynth::begin_block(const synth::HostBlock& host, const SoundBlockFacts& facts) noexcept {
	constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
	const bool low = facts.has_filters
	                 && (facts.lpf_mode == effects::FilterMode::TRANSISTOR_24DB_DRIVE || facts.lpf_modulated
	                     || (facts.lpf_freq < 0x7FFFFFD2) || (facts.lpf_morph > kMin));
	const bool high =
	    facts.has_filters && (facts.hpf_modulated || (facts.hpf_freq != kMin) || (facts.hpf_morph > kMin));
	return {
	    .low_pass = low ? effects::low_pass_slot(facts.lpf_mode) : dsp::fixed::LowPassSlot{},
	    .high_pass = high ? effects::high_pass_slot(facts.hpf_mode) : dsp::fixed::HighPassSlot{},
	    .filter_route = effects::filter_route(facts.filter_route),
	    .cpu_direness = host.cpu_direness,
	    .lfo2_synced_phase_increment = facts.lfo2_synced_phase_increment,
	    .lfo4_synced_phase_increment = facts.lfo4_synced_phase_increment,
	    .has_filters = facts.has_filters,
	    .expression_sources_changed_at_synth_level = facts.expression_sources_changed,
	    .monophonic_expression_values = facts.monophonic_expression_values,
	    .clipping_amount = facts.clipping_amount,
	    .doing_osc_sync = facts.doing_osc_sync,
	    .engine_rendering_in_stereo = host.engine_in_stereo,
	    .dx_patch_changed = facts.dx_patch_changed,
	    .global_source_values = facts.global_source_values,
	    .note_tails_allowed = facts.note_tails_allowed,
	    .source_ever_active = facts.source_ever_active,
	    .noise_volume_contains_something = facts.noise_can_sound,
	    .local_sources_patched_to_anything = facts.local_wired,
	    .bend_ranges = facts.bend_ranges,
	    .portamento = facts.portamento,
	    .source_active_currently = facts.source_active_currently,
	};
}

} // namespace deluge::instruments::classic
