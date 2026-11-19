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

#include "instruments/deluge/deluge_synth.h"

#include "dsp-next/fixed/pitch/fine_tune.h"
#include "synth/voice/unison.h"

namespace deluge::instrument {
namespace {

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

synth::SoundConfig DelugeSynth::derive(const State& record) noexcept {
	const FlatSoundState state = record;
	using dsp::fixed::FineTune;
	synth::SoundConfig config;
	synth::Voicing& voicing = config.voicing;
	voicing.priority = state.voice_priority;
	voicing.polyphony = state.polyphonic;
	voicing.max_voices = state.max_voice_count;
	voicing.transpose = state.transpose;
	voicing.cents = state.cents;
	voicing.tune = FineTune::of_cents(state.cents);
	voicing.unison_detune = state.unison_detune;
	voicing.unison_spread = state.unison_stereo_spread;
	voicing.unison = synth::derive_unison(state.num_unison, state.unison_detune, state.unison_stereo_spread);
	synth::EngineSettings& engine = config.engine;
	engine.mode = state.synth_mode;
	engine.osc_sync = state.oscillator_sync;
	engine.osc_retrigger_phase = state.osc_retrigger_phase;
	engine.modulator1_to_modulator0 = state.modulator1_to_modulator0;
	for (size_t m = 0; m < engine.modulators.size(); m++) {
		engine.modulators[m] = {.transpose = state.modulator_transpose[m],
		                        .cents = state.modulator_cents[m],
		                        .tune = FineTune::of_cents(state.modulator_cents[m]),
		                        .retrigger_phase = state.modulator_retrigger_phase[m]};
	}
	for (size_t l = 0; l < config.lfos.size(); l++) {
		config.lfos[l].wave_type = state.lfo_wave_type[l];
		config.lfos[l].sync_type = state.lfo_sync_type[l];
		config.lfos[l].sync_level = state.lfo_sync_level[l];
	}
	for (size_t s = 0; s < config.sources.size(); s++) {
		const SourceState& source = state.sources[s];
		config.sources[s] = {.osc_type = source.osc_type,
		                     .transpose = source.transpose,
		                     .cents = source.cents,
		                     .tune = FineTune::of_cents(source.cents),
		                     .repeat_mode = source.repeat_mode,
		                     .time_stretch_amount = source.time_stretch_amount,
		                     .interpolation_mode = source.interpolation_mode,
		                     .pitch_and_speed_independent = source.pitch_and_speed_independent,
		                     .reversed = source.reversed};
	}
	return config;
}

Parts DelugeSynth::changes(const State& before_record, const State& after_record) noexcept {
	const FlatSoundState before = before_record;
	const FlatSoundState after = after_record;
	Parts parts;
	mark(parts, Part::VOICING,
	     before.voice_priority != after.voice_priority || before.polyphonic != after.polyphonic
	         || before.max_voice_count != after.max_voice_count);
	mark(parts, Part::TUNING, before.transpose != after.transpose || before.cents != after.cents);
	mark(parts, Part::UNISON_COUNT, before.num_unison != after.num_unison);
	mark(parts, Part::UNISON_DETUNE, before.unison_detune != after.unison_detune);
	mark(parts, Part::UNISON_SPREAD, before.unison_stereo_spread != after.unison_stereo_spread);
	mark(parts, Part::MODE, before.synth_mode != after.synth_mode);
	mark(parts, Part::OSC_SYNC, before.oscillator_sync != after.oscillator_sync);
	for (size_t s = 0; s < before.sources.size(); s++) {
		const SourceState& was = before.sources[s];
		const SourceState& now = after.sources[s];
		mark(parts, slot_part(Part::SOURCE_A_TYPE, s), was.osc_type != now.osc_type);
		mark(parts, slot_part(Part::SOURCE_A_TUNING, s), was.transpose != now.transpose || was.cents != now.cents);
		mark(parts, slot_part(Part::SOURCE_A_PLAYBACK, s),
		     was.repeat_mode != now.repeat_mode || was.reversed != now.reversed
		         || was.time_stretch_amount != now.time_stretch_amount
		         || was.interpolation_mode != now.interpolation_mode
		         || was.pitch_and_speed_independent != now.pitch_and_speed_independent);
		mark(parts, slot_part(Part::SOURCE_A_RANGES, s), was.ranges != now.ranges);
		mark(parts, slot_part(Part::SOURCE_A_DX, s), was.dx_patch != now.dx_patch);
	}
	mark(parts, Part::RETRIGGER,
	     before.osc_retrigger_phase != after.osc_retrigger_phase
	         || before.modulator_retrigger_phase != after.modulator_retrigger_phase);
	mark(parts, Part::MODULATOR_TUNING,
	     before.modulator_transpose != after.modulator_transpose || before.modulator_cents != after.modulator_cents);
	mark(parts, Part::MODULATOR_ROUTING, before.modulator1_to_modulator0 != after.modulator1_to_modulator0);
	mark(parts, Part::LFO_WAVE, before.lfo_wave_type != after.lfo_wave_type);
	mark(parts, Part::LFO_SYNC,
	     before.lfo_sync_type != after.lfo_sync_type || before.lfo_sync_level != after.lfo_sync_level);
	mark(parts, Part::EFFECTS, before.effects != after.effects);
	mark(parts, Part::HOST,
	     before.side_chain_send_level != after.side_chain_send_level
	         || before.output_midi_channel != after.output_midi_channel
	         || before.output_midi_note_for_drum != after.output_midi_note_for_drum);
	return parts;
}

} // namespace deluge::instrument
