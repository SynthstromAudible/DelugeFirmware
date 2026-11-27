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

#include "instruments/classic/composed_state.h"
#include "instruments/classic/flat_layout.h"

#include <algorithm>
#include <utility>

namespace deluge::instruments::classic {

// The flat record and the synth config carry the phase as the file's 32-bit word; the composed state holds it typed,
// so the crossing is here and in phase_word.
synth::Retrigger retrigger_of_word(uint32_t phase) {
	return phase == kFreeRunningPhase ? synth::Retrigger{} : synth::Retrigger{dsp::UQ<32>::from_raw(phase)};
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

// What slot @p src plays, its phase @p phase; @p kept takes what the kind does not hold, the fields it does left at
// their defaults.
synth::SlotState pack_kind(const SourceState& src, uint32_t phase, SourceState& kept,
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
			sample.ranges.push_back(source_range_of(r));
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
	case OscType::DX7:
		// No slot plays a DX7: it is an engine of its own, and the codec refuses one on an oscillator
		keep_no_tuning();
		return synth::VaState{.pitch = pitch, .shape = synth::VaShape::SQUARE};
	default:
		keep_no_tuning();
		return synth::InputState{.pitch = pitch, .channel = channel_of(src.osc_type)};
	}
}

// @p slot as a flat source, over @p kept (what its kind does not hold); its retrigger phase word into @p phase.
SourceState unpack_kind(const synth::SlotState& slot, const SourceState& kept, const synth::Retrigger& kept_retrigger,
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
			    for (const synth::SourceRange& r : state.ranges) {
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
			    for (const synth::SourceRange& r : state.ranges) {
				    src.ranges.push_back(range_state_of(r));
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

synth::LfoState lfo_from_flat(const FlatSoundState& f, size_t i) {
	return {.shape = f.lfo_wave_type[i], .sync_type = f.lfo_sync_type[i], .sync_level = f.lfo_sync_level[i]};
}

void put_lfo(FlatSoundState& f, size_t i, const synth::LfoState& lfo) {
	f.lfo_wave_type[i] = lfo.shape;
	f.lfo_sync_type[i] = lfo.sync_type;
	f.lfo_sync_level[i] = lfo.sync_level;
}

} // namespace

const synth::DualSourceState* dual_of(const ComposedState& plays) noexcept {
	return synth::oscillators_of(plays.engine);
}

synth::DualSourceState* dual_of(ComposedState& plays) noexcept {
	return synth::oscillators_of(plays.engine);
}

OscType osc_type_of(const synth::SlotState& kind) noexcept {
	if (const auto* va = std::get_if<synth::VaState>(&kind)) {
		return type_of(va->shape);
	}
	if (std::holds_alternative<synth::WavetableState>(kind)) {
		return OscType::WAVETABLE;
	}
	if (std::holds_alternative<synth::SampleState>(kind)) {
		return OscType::SAMPLE;
	}
	return type_of(std::get_if<synth::InputState>(&kind)->channel);
}

uint32_t phase_word(const synth::Retrigger& retrigger) noexcept {
	return retrigger ? retrigger->raw() : kFreeRunningPhase;
}

namespace {

// The memory of a playing slot whose kind does not hold @p kept (its other fields in the file's layout) or
// @p retrigger (a sample's).
SlotMemory remember(const SourceState& kept, const synth::Retrigger& retrigger) {
	SlotMemory memory{
	    .parked = std::nullopt,
	    .playback = {.repeat_mode = kept.repeat_mode,
	                 .time_stretch_amount = kept.time_stretch_amount,
	                 .interpolation = kept.interpolation_mode,
	                 .pitch_and_speed_independent = kept.pitch_and_speed_independent,
	                 .reversed = kept.reversed},
	    .ranges = {},
	    .sample_pitch = {.tuning = {.transpose = kept.transpose, .cents = kept.cents}, .retrigger = retrigger}};
	memory.ranges.reserve(kept.ranges.size());
	for (const RangeState& r : kept.ranges) {
		memory.ranges.push_back(source_range_of(r));
	}
	return memory;
}

// The kept fields and retrigger @p memory holds for a playing slot, in the file's layout.
SourceState recall(const SlotMemory& memory, synth::Retrigger& retrigger) {
	SourceState kept{.osc_type = SourceState{}.osc_type,
	                 .transpose = memory.sample_pitch.tuning.transpose,
	                 .cents = memory.sample_pitch.tuning.cents,
	                 .repeat_mode = memory.playback.repeat_mode,
	                 .time_stretch_amount = memory.playback.time_stretch_amount,
	                 .interpolation_mode = memory.playback.interpolation,
	                 .pitch_and_speed_independent = memory.playback.pitch_and_speed_independent,
	                 .reversed = memory.playback.reversed,
	                 .ranges = {}};
	kept.ranges.reserve(memory.ranges.size());
	for (const synth::SourceRange& r : memory.ranges) {
		kept.ranges.push_back(range_state_of(r));
	}
	retrigger = memory.sample_pitch.retrigger;
	return kept;
}

} // namespace

PackedSlot pack_slot(const SourceState& source, uint32_t phase) {
	SourceState kept;
	synth::Retrigger retrigger;
	synth::SlotState kind = pack_kind(source, phase, kept, retrigger);
	return {.kind = std::move(kind), .memory = remember(kept, retrigger)};
}

UnpackedSlot unpack_slot(const synth::SlotState& kind, const SlotMemory& memory) {
	synth::Retrigger retrigger;
	const SourceState kept = recall(memory, retrigger);
	UnpackedSlot unpacked;
	unpacked.source = unpack_kind(kind, kept, retrigger, unpacked.phase);
	return unpacked;
}

SlotMemory park_slot(SourceState source) {
	source.transpose = 0; // the carrier's
	source.cents = 0;
	PackedSlot packed = pack_slot(source, kFreeRunningPhase);
	packed.memory.parked = std::move(packed.kind);
	return std::move(packed.memory);
}

SourceState unpark_slot(const SlotMemory& memory) {
	// A slot parks its kind whenever FM plays; one without is a new synth's square
	return unpack_slot(memory.parked.value_or(synth::VaState{}), memory).source;
}

SlotMemory park_whole_slot(const SourceState& source, uint32_t phase) {
	PackedSlot packed = pack_slot(source, phase);
	packed.memory.parked = std::move(packed.kind);
	return std::move(packed.memory);
}

UnpackedSlot unpark_whole_slot(const SlotMemory& memory) {
	return unpack_slot(memory.parked.value_or(synth::VaState{}), memory);
}

namespace {

synth::Dx7State dx7_of(const Dx7FileState& file) {
	return {.pitch = pitch_of(file.transpose, file.cents, file.retrigger_phase), .patch = file.patch};
}

Dx7FileState dx7_file_of(const synth::Dx7State& dx7) {
	return {.transpose = dx7.pitch.tuning.transpose,
	        .cents = dx7.pitch.tuning.cents,
	        .retrigger_phase = phase_word(dx7.pitch.retrigger),
	        .patch = dx7.patch};
}

} // namespace

std::pair<ComposedState, Recall> compose(const FlatSoundState& f) {
	ComposedState state{
	    .voicing = {.polyphony = f.polyphonic,
	                .max_voices = f.max_voice_count,
	                .priority = f.voice_priority,
	                .master = {.transpose = f.transpose, .cents = f.cents},
	                .unison = {.count = f.num_unison, .detune = f.unison_detune, .spread = f.unison_stereo_spread}},
	    .engine = synth::SubtractiveState{},
	    .instrument_lfos = {lfo_from_flat(f, 0), lfo_from_flat(f, 2)},
	    .voice_lfos = {lfo_from_flat(f, 1), lfo_from_flat(f, 3)},
	    .envelopes = f.envelopes,
	    .fx = f.effects,
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
			recall.slots[s] = park_slot(f.sources[s]);
		}
		fm.sync = f.oscillator_sync;
		state.engine = fm;
		recall.dx7 = dx7_of(f.dx7);
	}
	else if (f.synth_mode == SynthMode::DX7) {
		for (size_t s = 0; s < 2; s++) {
			recall.slots[s] = park_whole_slot(f.sources[s], f.osc_retrigger_phase[s]);
		}
		recall.sync = f.oscillator_sync;
		recall.modulators = modulators;
		recall.modulator1_to_modulator0 = f.modulator1_to_modulator0;
		state.engine = dx7_of(f.dx7);
	}
	else {
		synth::DualSourceState dual{.slots = {}, .sync = f.oscillator_sync};
		for (size_t s = 0; s < 2; s++) {
			auto [kind, memory] = pack_slot(f.sources[s], f.osc_retrigger_phase[s]);
			dual.slots[s] = std::move(kind);
			recall.slots[s] = std::move(memory);
		}
		if (std::holds_alternative<synth::SampleState>(dual.slots[1])) {
			// The file writes the sync in the second oscillator's pitch, which a sample oscillator has none of
			recall.sync = dual.sync;
			dual.sync = false;
		}
		recall.modulators = modulators;
		recall.modulator1_to_modulator0 = f.modulator1_to_modulator0;
		recall.dx7 = dx7_of(f.dx7);
		state.engine = synth::dual_state_for(f.synth_mode, dual);
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
	f.envelopes = state.envelopes;
	f.effects = state.fx;
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
			f.sources[s] = unpark_slot(recall.slots[s]);
			f.sources[s].transpose = fm->carriers[s].tuning.transpose;
			f.sources[s].cents = fm->carriers[s].tuning.cents;
			f.osc_retrigger_phase[s] = phase_word(fm->carriers[s].retrigger);
		}
		f.oscillator_sync = fm->sync;
		f.dx7 = dx7_file_of(recall.dx7);
		return f;
	}
	put_modulators(recall.modulators, recall.modulator1_to_modulator0);
	if (const auto* dx7 = std::get_if<synth::Dx7State>(&state.engine)) {
		f.synth_mode = SynthMode::DX7;
		for (size_t s = 0; s < 2; s++) {
			auto [source, phase] = unpark_whole_slot(recall.slots[s]);
			f.sources[s] = std::move(source);
			f.osc_retrigger_phase[s] = phase;
		}
		f.oscillator_sync = recall.sync;
		f.dx7 = dx7_file_of(*dx7);
		return f;
	}
	const synth::DualSourceState& dual = *dual_of(state);
	f.synth_mode = synth::mode_of(state.engine);
	f.dx7 = dx7_file_of(recall.dx7);
	for (size_t s = 0; s < 2; s++) {
		auto [source, phase] = unpack_slot(dual.slots[s], recall.slots[s]);
		f.sources[s] = std::move(source);
		f.osc_retrigger_phase[s] = phase;
	}
	f.oscillator_sync = std::holds_alternative<synth::SampleState>(dual.slots[1]) ? recall.sync : dual.sync;
	return f;
}

SoundState to_record(const FlatSoundState& flat) {
	auto [plays, recall] = compose(flat);
	return SoundState{.plays = std::move(plays), .recall = std::move(recall)};
}

FlatSoundState to_flat(const SoundState& state) {
	return flatten(state.plays, state.recall);
}

} // namespace deluge::instruments::classic
