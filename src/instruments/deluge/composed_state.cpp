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

#include "instruments/deluge/composed_state.h"
#include "instruments/deluge/flat_layout.h"

#include <algorithm>
#include <utility>

namespace deluge::instrument {

namespace {

constexpr uint32_t kFreeRunning = 0xFFFFFFFF; ///< the flat record's retrigger phase for an oscillator that runs free

} // namespace

// The flat record and the synth config carry the phase as the file's 32-bit word; the composed state holds it typed,
// so the crossing is here and in phase_word.
synth::Retrigger retrigger_of_word(uint32_t phase) {
	return phase == kFreeRunning ? synth::Retrigger{} : synth::Retrigger{dsp::UQ<32>::from_raw(phase)};
}

namespace {

synth::Pitch pitch_of(int16_t transpose, int8_t cents, uint32_t phase) {
	return {.tuning = {.transpose = transpose, .cents = cents}, .retrigger = retrigger_of_word(phase)};
}

bool is_va(OscType type) {
	return type <= OscType::SUPERSAW;
}

synth::VaShape shape_of(OscType type) {
	return static_cast<synth::VaShape>(std::to_underlying(type));
}

OscType type_of(synth::VaShape shape) {
	return static_cast<OscType>(std::to_underlying(shape));
}

synth::InputChannel channel_of(OscType type) {
	switch (type) {
	case OscType::INPUT_R:
		return synth::InputChannel::RIGHT;
	case OscType::INPUT_STEREO:
		return synth::InputChannel::STEREO;
	default:
		return synth::InputChannel::LEFT;
	}
}

OscType type_of(synth::InputChannel channel) {
	switch (channel) {
	case synth::InputChannel::RIGHT:
		return OscType::INPUT_R;
	case synth::InputChannel::STEREO:
		return OscType::INPUT_STEREO;
	case synth::InputChannel::LEFT:
		break;
	}
	return OscType::INPUT_L;
}

} // namespace

/// What slot @p src plays, its phase @p phase; @p kept takes what the kind does not hold, the fields it does left at
/// their defaults.
synth::SlotState pack_slot(const SourceState& src, uint32_t phase, SourceState& kept,
                           synth::Retrigger& kept_retrigger) {
	kept = src;
	kept_retrigger.reset();
	const synth::Pitch pitch = pitch_of(src.transpose, src.cents, phase);
	const auto keep_no_tuning = [&] {
		kept.transpose = 0;
		kept.cents = 0;
	};
	kept.osc_type = SourceState{}.osc_type;
	if (is_va(src.osc_type)) {
		keep_no_tuning();
		return synth::VaState{.pitch = pitch, .shape = shape_of(src.osc_type)};
	}
	switch (src.osc_type) {
	case OscType::WAVETABLE: {
		keep_no_tuning();
		synth::WavetableState wavetable{.pitch = pitch, .ranges = {}};
		for (const RangeState& r : src.ranges) {
			wavetable.ranges.push_back({.top_note = r.top_note, .file_path = r.file_path});
		}
		kept.ranges.clear();
		return wavetable;
	}
	case OscType::SAMPLE: {
		synth::SampleState sample{.playback = {.repeat_mode = src.repeat_mode,
		                                       .time_stretch_amount = src.time_stretch_amount,
		                                       .interpolation = src.interpolation_mode,
		                                       .pitch_and_speed_independent = src.pitch_and_speed_independent,
		                                       .reversed = src.reversed},
		                          .ranges = {}};
		kept_retrigger = pitch.retrigger;
		for (const RangeState& r : src.ranges) {
			sample.ranges.push_back({.top_note = r.top_note,
			                         .file_path = r.file_path,
			                         .zone = {.start = r.start_pos,
			                                  .end = r.end_pos,
			                                  .loop_start = r.loop_start_pos,
			                                  .loop_end = r.loop_end_pos},
			                         .tuning = {.transpose = r.transpose, .cents = r.cents}});
		}
		kept.ranges.clear();
		const SourceState neutral;
		kept.repeat_mode = neutral.repeat_mode;
		kept.time_stretch_amount = neutral.time_stretch_amount;
		kept.interpolation_mode = neutral.interpolation_mode;
		kept.pitch_and_speed_independent = neutral.pitch_and_speed_independent;
		kept.reversed = neutral.reversed;
		return sample;
	}
	case OscType::DX7: {
		keep_no_tuning();
		synth::Dx7State dx{.pitch = pitch, .patch = {}};
		if (src.dx_patch) {
			dx.patch = *src.dx_patch;
		}
		kept.dx_patch.reset();
		return dx;
	}
	default:
		keep_no_tuning();
		return synth::InputState{.pitch = pitch, .channel = channel_of(src.osc_type)};
	}
}

/// The flat source slot @p slot is, over @p kept (what its kind does not hold); its phase into @p phase.
SourceState unpack_slot(const synth::SlotState& slot, const SourceState& kept, const synth::Retrigger& kept_retrigger,
                        uint32_t& phase) {
	SourceState src = kept;
	const auto take_pitch = [&](const synth::Pitch& pitch) {
		src.transpose = pitch.tuning.transpose;
		src.cents = pitch.tuning.cents;
		phase = phase_word(pitch.retrigger);
	};
	std::visit(
	    [&](const auto& state) {
		    using T = std::decay_t<decltype(state)>;
		    if constexpr (std::is_same_v<T, synth::VaState>) {
			    src.osc_type = type_of(state.shape);
			    take_pitch(state.pitch);
		    }
		    else if constexpr (std::is_same_v<T, synth::WavetableState>) {
			    src.osc_type = OscType::WAVETABLE;
			    take_pitch(state.pitch);
			    src.ranges.clear();
			    for (const synth::WavetableRange& r : state.ranges) {
				    src.ranges.push_back({.top_note = r.top_note, .file_path = r.file_path});
			    }
		    }
		    else if constexpr (std::is_same_v<T, synth::SampleState>) {
			    src.osc_type = OscType::SAMPLE;
			    phase = phase_word(kept_retrigger);
			    src.repeat_mode = state.playback.repeat_mode;
			    src.time_stretch_amount = state.playback.time_stretch_amount;
			    src.interpolation_mode = state.playback.interpolation;
			    src.pitch_and_speed_independent = state.playback.pitch_and_speed_independent;
			    src.reversed = state.playback.reversed;
			    src.ranges.clear();
			    for (const synth::SampleRange& r : state.ranges) {
				    src.ranges.push_back({.top_note = r.top_note,
				                          .file_path = r.file_path,
				                          .start_pos = r.zone.start,
				                          .end_pos = r.zone.end,
				                          .loop_start_pos = r.zone.loop_start,
				                          .loop_end_pos = r.zone.loop_end,
				                          .transpose = r.tuning.transpose,
				                          .cents = r.tuning.cents});
			    }
		    }
		    else if constexpr (std::is_same_v<T, synth::Dx7State>) {
			    src.osc_type = OscType::DX7;
			    take_pitch(state.pitch);
			    src.dx_patch.reset();
			    if (state.patch) {
				    src.dx_patch = *state.patch;
			    }
		    }
		    else {
			    src.osc_type = type_of(state.channel);
			    take_pitch(state.pitch);
		    }
	    },
	    slot);
	return src;
}

namespace {
synth::LfoState lfo_of(const FlatSoundState& f, size_t i) {
	return {.shape = f.lfo_wave_type[i], .sync_type = f.lfo_sync_type[i], .sync_level = f.lfo_sync_level[i]};
}

void put_lfo(FlatSoundState& f, size_t i, const synth::LfoState& lfo) {
	f.lfo_wave_type[i] = lfo.shape;
	f.lfo_sync_type[i] = lfo.sync_type;
	f.lfo_sync_level[i] = lfo.sync_level;
}

} // namespace

uint32_t phase_word(const synth::Retrigger& retrigger) noexcept {
	return retrigger ? retrigger->raw() : kFreeRunning;
}

SlotMemory remember(const SourceState& kept, const synth::Retrigger& retrigger) {
	SlotMemory memory{
	    .parked = std::nullopt,
	    .playback = {.repeat_mode = kept.repeat_mode,
	                 .time_stretch_amount = kept.time_stretch_amount,
	                 .interpolation = kept.interpolation_mode,
	                 .pitch_and_speed_independent = kept.pitch_and_speed_independent,
	                 .reversed = kept.reversed},
	    .ranges = {},
	    .patch = kept.dx_patch,
	    .sample_pitch = {.tuning = {.transpose = kept.transpose, .cents = kept.cents}, .retrigger = retrigger}};
	memory.ranges.reserve(kept.ranges.size());
	for (const RangeState& r : kept.ranges) {
		memory.ranges.push_back({.top_note = r.top_note,
		                         .file_path = r.file_path,
		                         .zone = {.start = r.start_pos,
		                                  .end = r.end_pos,
		                                  .loop_start = r.loop_start_pos,
		                                  .loop_end = r.loop_end_pos},
		                         .tuning = {.transpose = r.transpose, .cents = r.cents}});
	}
	return memory;
}

SourceState recalled(const SlotMemory& memory, synth::Retrigger& retrigger) {
	SourceState kept{.osc_type = SourceState{}.osc_type,
	                 .transpose = memory.sample_pitch.tuning.transpose,
	                 .cents = memory.sample_pitch.tuning.cents,
	                 .repeat_mode = memory.playback.repeat_mode,
	                 .time_stretch_amount = memory.playback.time_stretch_amount,
	                 .interpolation_mode = memory.playback.interpolation,
	                 .pitch_and_speed_independent = memory.playback.pitch_and_speed_independent,
	                 .reversed = memory.playback.reversed,
	                 .dx_patch = memory.patch,
	                 .ranges = {}};
	kept.ranges.reserve(memory.ranges.size());
	for (const synth::SampleRange& r : memory.ranges) {
		kept.ranges.push_back({.top_note = r.top_note,
		                       .file_path = r.file_path,
		                       .start_pos = r.zone.start,
		                       .end_pos = r.zone.end,
		                       .loop_start_pos = r.zone.loop_start,
		                       .loop_end_pos = r.zone.loop_end,
		                       .transpose = r.tuning.transpose,
		                       .cents = r.tuning.cents});
	}
	retrigger = memory.sample_pitch.retrigger;
	return kept;
}

SlotMemory remember_parked(const SourceState& kept) {
	SourceState inner;
	synth::Retrigger inner_retrigger;
	synth::SlotState parked = pack_slot(kept, phase_word(synth::Retrigger{}), inner, inner_retrigger);
	SlotMemory memory = remember(inner, inner_retrigger);
	memory.parked = std::move(parked);
	return memory;
}

SourceState recalled_parked(const SlotMemory& memory) {
	synth::Retrigger inner_retrigger;
	const SourceState inner = recalled(memory, inner_retrigger);
	uint32_t phase = 0;
	// A slot parks its kind whenever FM plays; one without is a new synth's square
	return unpack_slot(memory.parked.value_or(synth::VaState{}), inner, inner_retrigger, phase);
}

std::pair<ComposedState, Recall> compose(const FlatSoundState& f) {
	ComposedState state{
	    .voicing = {.polyphony = f.polyphonic,
	                .max_voices = f.max_voice_count,
	                .priority = f.voice_priority,
	                .master = {.transpose = f.transpose, .cents = f.cents},
	                .unison = {.count = f.num_unison, .detune = f.unison_detune, .spread = f.unison_stereo_spread}},
	    .engine = synth::SubtractiveState{},
	    .instrument_lfos = {lfo_of(f, 0), lfo_of(f, 2)},
	    .voice_lfos = {lfo_of(f, 1), lfo_of(f, 3)},
	    .fx = f.effects,
	    .side_chain_send_level = f.side_chain_send_level,
	    .output_midi_channel = f.output_midi_channel,
	    .output_midi_note_for_drum = f.output_midi_note_for_drum,
	};
	Recall recall;
	std::array<synth::Pitch, 2> modulators{};
	for (size_t m = 0; m < 2; m++) {
		modulators[m] = pitch_of(f.modulator_transpose[m], f.modulator_cents[m], f.modulator_retrigger_phase[m]);
	}
	if (f.synth_mode == SynthMode::FM) {
		synth::FmState fm{
		    .carriers = {}, .modulators = modulators, .modulator1_to_modulator0 = f.modulator1_to_modulator0};
		for (size_t s = 0; s < 2; s++) {
			fm.carriers[s] = pitch_of(f.sources[s].transpose, f.sources[s].cents, f.osc_retrigger_phase[s]);
			SourceState kept = f.sources[s];
			kept.transpose = 0; // the carrier's
			kept.cents = 0;
			recall.slots[s] = remember_parked(kept);
		}
		fm.sync = f.oscillator_sync;
		state.engine = fm;
	}
	else {
		synth::DualSourceState dual{.slots = {}, .sync = f.oscillator_sync};
		for (size_t s = 0; s < 2; s++) {
			SourceState kept;
			synth::Retrigger retrigger;
			dual.slots[s] = pack_slot(f.sources[s], f.osc_retrigger_phase[s], kept, retrigger);
			recall.slots[s] = remember(kept, retrigger);
		}
		if (std::holds_alternative<synth::SampleState>(dual.slots[1])) {
			// The file writes the sync in the second oscillator's pitch, which a sample oscillator has none of
			recall.sync = dual.sync;
			dual.sync = false;
		}
		recall.modulators = modulators;
		recall.modulator1_to_modulator0 = f.modulator1_to_modulator0;
		if (f.synth_mode == SynthMode::RINGMOD) {
			state.engine = synth::RingModState{.oscillators = dual};
		}
		else {
			state.engine = synth::SubtractiveState{.oscillators = dual};
		}
	}
	return {std::move(state), std::move(recall)};
}

FlatSoundState flatten(const ComposedState& state, const Recall& recall) {
	FlatSoundState f;
	f.polyphonic = state.voicing.polyphony;
	f.max_voice_count = state.voicing.max_voices;
	f.voice_priority = state.voicing.priority;
	f.transpose = state.voicing.master.transpose;
	f.cents = state.voicing.master.cents;
	f.num_unison = state.voicing.unison.count;
	f.unison_detune = state.voicing.unison.detune;
	f.unison_stereo_spread = state.voicing.unison.spread;
	put_lfo(f, 0, state.instrument_lfos[0]);
	put_lfo(f, 2, state.instrument_lfos[1]);
	put_lfo(f, 1, state.voice_lfos[0]);
	put_lfo(f, 3, state.voice_lfos[1]);
	f.effects = state.fx;
	f.side_chain_send_level = state.side_chain_send_level;
	f.output_midi_channel = state.output_midi_channel;
	f.output_midi_note_for_drum = state.output_midi_note_for_drum;
	const auto put_modulators = [&](const std::array<synth::Pitch, 2>& modulators, bool m1_to_m0) {
		for (size_t m = 0; m < 2; m++) {
			f.modulator_transpose[m] = modulators[m].tuning.transpose;
			f.modulator_cents[m] = modulators[m].tuning.cents;
			f.modulator_retrigger_phase[m] = phase_word(modulators[m].retrigger);
		}
		f.modulator1_to_modulator0 = m1_to_m0;
	};
	if (const auto* fm = std::get_if<synth::FmState>(&state.engine)) {
		f.synth_mode = SynthMode::FM;
		put_modulators(fm->modulators, fm->modulator1_to_modulator0);
		for (size_t s = 0; s < 2; s++) {
			f.sources[s] = recalled_parked(recall.slots[s]);
			f.sources[s].transpose = fm->carriers[s].tuning.transpose;
			f.sources[s].cents = fm->carriers[s].tuning.cents;
			f.osc_retrigger_phase[s] = phase_word(fm->carriers[s].retrigger);
		}
		f.oscillator_sync = fm->sync;
		return f;
	}
	const synth::DualSourceState& dual = std::holds_alternative<synth::RingModState>(state.engine)
	                                         ? std::get<synth::RingModState>(state.engine).oscillators
	                                         : std::get<synth::SubtractiveState>(state.engine).oscillators;
	f.synth_mode =
	    std::holds_alternative<synth::RingModState>(state.engine) ? SynthMode::RINGMOD : SynthMode::SUBTRACTIVE;
	put_modulators(recall.modulators, recall.modulator1_to_modulator0);
	for (size_t s = 0; s < 2; s++) {
		synth::Retrigger retrigger;
		const SourceState kept = recalled(recall.slots[s], retrigger);
		f.sources[s] = unpack_slot(dual.slots[s], kept, retrigger, f.osc_retrigger_phase[s]);
	}
	f.oscillator_sync = std::holds_alternative<synth::SampleState>(dual.slots[1]) ? recall.sync : dual.sync;
	return f;
}

SoundState::SoundState(const FlatSoundState& flat) {
	auto [composed, kept] = compose(flat);
	plays = std::move(composed);
	recall = std::move(kept);
}

SoundState::operator FlatSoundState() const {
	return flatten(plays, recall);
}

} // namespace deluge::instrument
