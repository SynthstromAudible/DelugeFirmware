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

#include "instruments/classic/record_edit.h"

#include "instruments/classic/dx7_init_voice.h"
#include "instruments/classic/flat_layout.h"
#include "instruments/classic/state_access.h"

#include <utility>
#include <variant>

namespace deluge::instruments::classic {
namespace {

/// The sync where the file writes it: in the second oscillator's pitch, so kept for a switch back while that is a
/// sample.
void place_sync(SoundState& state, synth::DualSourceState& dual, bool sync) {
	const bool kept = std::holds_alternative<synth::SampleState>(dual.slots[1]);
	dual.sync = kept ? false : sync;
	state.recall.sync = kept ? sync : false;
}

/// @p dual played as @p mode's engine.
void play_dual(ComposedState& plays, SynthMode mode, synth::DualSourceState dual) {
	plays.engine = synth::dual_state_for(mode, std::move(dual));
}

/// Into FM: each slot's pitch becomes its carrier's and the rest of it parks; the sync goes with FM, and FM's
/// modulators come back from the recall.
void enter_fm(SoundState& state) {
	Recall& recall = state.recall;
	const bool sync = oscillator_sync(state);
	const synth::DualSourceState& dual = *dual_of(state.plays);
	synth::FmState fm{.carriers = {},
	                  .sync = sync,
	                  .modulators = recall.modulators,
	                  .modulator1_to_modulator0 = recall.modulator1_to_modulator0};
	for (size_t slot = 0; slot < 2; slot++) {
		const UnpackedSlot unpacked = unpack_slot(dual.slots[slot], recall.slots[slot]);
		fm.carriers[slot] = {.tuning = {.transpose = unpacked.source.transpose, .cents = unpacked.source.cents},
		                     .retrigger = retrigger_of_word(unpacked.phase)};
		recall.slots[slot] = park_slot(unpacked.source);
	}
	recall.modulators = synth::FmState{}.modulators;
	recall.modulator1_to_modulator0 = false;
	recall.sync = false;
	state.plays.engine = fm;
}

/// Out of FM into @p mode: each carrier's pitch goes back to its slot, which plays its parked kind again; FM's
/// modulators go to the recall.
void leave_fm(SoundState& state, SynthMode mode) {
	Recall& recall = state.recall;
	const synth::FmState fm = *std::get_if<synth::FmState>(&state.plays.engine);
	synth::DualSourceState dual{.slots = {}, .sync = false};
	for (size_t slot = 0; slot < 2; slot++) {
		SourceState source = unpark_slot(recall.slots[slot]);
		source.transpose = fm.carriers[slot].tuning.transpose;
		source.cents = fm.carriers[slot].tuning.cents;
		auto [kind, memory] = pack_slot(source, phase_word(fm.carriers[slot].retrigger));
		dual.slots[slot] = std::move(kind);
		recall.slots[slot] = std::move(memory);
	}
	recall.modulators = fm.modulators;
	recall.modulator1_to_modulator0 = fm.modulator1_to_modulator0;
	place_sync(state, dual, fm.sync);
	play_dual(state.plays, mode, std::move(dual));
}

/// Into the DX7 from a dual-source engine: each slot parks whole, its pitch with it, and the sync with them; the DX7
/// comes back from the recall.
void enter_dx7(SoundState& state) {
	Recall& recall = state.recall;
	const bool sync = oscillator_sync(state);
	const synth::DualSourceState& dual = *dual_of(state.plays);
	for (size_t slot = 0; slot < 2; slot++) {
		const UnpackedSlot unpacked = unpack_slot(dual.slots[slot], recall.slots[slot]);
		recall.slots[slot] = park_whole_slot(unpacked.source, unpacked.phase);
	}
	recall.sync = sync;
	synth::Dx7State dx7 = std::exchange(recall.dx7, synth::Dx7State{});
	// A DX7 with no patch of its own plays the init voice, as one read without a patch does
	if (!dx7.patch) {
		dx7.patch.emplace(DxPatchState{.params = kDx7InitVoice});
	}
	state.plays.engine = dx7;
}

/// Out of the DX7 into dual-source engine @p mode: each slot plays its parked kind again; the DX7 goes to the recall.
void leave_dx7(SoundState& state, SynthMode mode) {
	Recall& recall = state.recall;
	synth::DualSourceState dual{.slots = {}, .sync = false};
	for (size_t slot = 0; slot < 2; slot++) {
		const UnpackedSlot unpacked = unpark_whole_slot(recall.slots[slot]);
		auto [kind, memory] = pack_slot(unpacked.source, unpacked.phase);
		dual.slots[slot] = std::move(kind);
		recall.slots[slot] = std::move(memory);
	}
	const bool sync = std::exchange(recall.sync, false);
	recall.dx7 = *std::get_if<synth::Dx7State>(&state.plays.engine);
	place_sync(state, dual, sync);
	play_dual(state.plays, mode, std::move(dual));
}

/// slot_kind_of for a const or a changeable state.
template <class State>
auto slot_kind_in(State& state, size_t slot) noexcept -> decltype(&*state.recall.slots[slot].parked) {
	if (auto* dual = dual_of(state.plays)) {
		return &dual->slots[slot];
	}
	auto& parked = state.recall.slots[slot].parked;
	return parked ? &*parked : nullptr;
}

/// source_pitch_of for a const or a changeable state.
template <class State>
auto& source_pitch_in(State& state, size_t slot) noexcept {
	if (auto* fm = std::get_if<synth::FmState>(&state.plays.engine)) {
		return fm->carriers[slot];
	}
	// A dual-source engine's slot, or one the DX7 parks whole (it parks every slot)
	auto& kind = *slot_kind_in(state, slot);
	if (auto* va = std::get_if<synth::VaState>(&kind)) {
		return va->pitch;
	}
	if (auto* wavetable = std::get_if<synth::WavetableState>(&kind)) {
		return wavetable->pitch;
	}
	if (auto* input = std::get_if<synth::InputState>(&kind)) {
		return input->pitch;
	}
	return state.recall.slots[slot].sample_pitch;
}

} // namespace

const synth::SlotState* slot_kind_of(const SoundState& state, size_t slot) noexcept {
	return slot_kind_in(state, slot);
}

synth::SlotState* slot_kind_of(SoundState& state, size_t slot) noexcept {
	return slot_kind_in(state, slot);
}

const synth::Pitch& source_pitch_of(const SoundState& state, size_t slot) noexcept {
	return source_pitch_in(state, slot);
}

synth::Pitch& source_pitch_of(SoundState& state, size_t slot) noexcept {
	return source_pitch_in(state, slot);
}

SlotEdit begin_slot_edit(const SoundState& state, size_t slot) {
	if (const auto* fm = std::get_if<synth::FmState>(&state.plays.engine)) {
		SlotEdit edit{.source = unpark_slot(state.recall.slots[slot]),
		              .phase = phase_word(fm->carriers[slot].retrigger),
		              .sync = fm->sync};
		edit.source.transpose = fm->carriers[slot].tuning.transpose;
		edit.source.cents = fm->carriers[slot].tuning.cents;
		return edit;
	}
	if (std::holds_alternative<synth::Dx7State>(state.plays.engine)) {
		const UnpackedSlot parked = unpark_whole_slot(state.recall.slots[slot]);
		return {.source = parked.source, .phase = parked.phase, .sync = state.recall.sync};
	}
	const UnpackedSlot unpacked = unpack_slot(dual_of(state.plays)->slots[slot], state.recall.slots[slot]);
	return {.source = unpacked.source, .phase = unpacked.phase, .sync = oscillator_sync(state)};
}

void end_slot_edit(SoundState& state, size_t slot, const SlotEdit& edit) {
	if (auto* fm = std::get_if<synth::FmState>(&state.plays.engine)) {
		fm->carriers[slot] = {.tuning = {.transpose = edit.source.transpose, .cents = edit.source.cents},
		                      .retrigger = retrigger_of_word(edit.phase)};
		fm->sync = edit.sync;
		state.recall.slots[slot] = park_slot(edit.source);
		return;
	}
	if (std::holds_alternative<synth::Dx7State>(state.plays.engine)) {
		state.recall.slots[slot] = park_whole_slot(edit.source, edit.phase);
		state.recall.sync = edit.sync;
		return;
	}
	synth::DualSourceState& dual = *dual_of(state.plays);
	auto [kind, memory] = pack_slot(edit.source, edit.phase);
	dual.slots[slot] = std::move(kind);
	state.recall.slots[slot] = std::move(memory);
	place_sync(state, dual, edit.sync);
}

void set_engine(SoundState& state, SynthMode mode) {
	const SynthMode from = synth_mode(state);
	if (from == mode) {
		return;
	}
	// FM and the DX7 each enter from a dual-source engine and leave to one: between the two, through subtractive
	const bool dual_to = mode != SynthMode::FM && mode != SynthMode::DX7;
	if (from == SynthMode::FM) {
		leave_fm(state, dual_to ? mode : SynthMode::SUBTRACTIVE);
	}
	else if (from == SynthMode::DX7) {
		leave_dx7(state, dual_to ? mode : SynthMode::SUBTRACTIVE);
	}
	else if (dual_to) {
		play_dual(state.plays, mode, std::move(*dual_of(state.plays)));
	}
	if (mode == SynthMode::FM) {
		enter_fm(state);
	}
	else if (mode == SynthMode::DX7) {
		enter_dx7(state);
	}
}

synth::SamplePlayback& sample_playback_of(SoundState& state, size_t slot) noexcept {
	synth::SlotState* kind = slot_kind_of(state, slot);
	auto* sample = kind != nullptr ? std::get_if<synth::SampleState>(kind) : nullptr;
	return sample != nullptr ? sample->playback : state.recall.slots[slot].playback;
}

void set_oscillator_sync(SoundState& state, bool sync) {
	if (auto* fm = std::get_if<synth::FmState>(&state.plays.engine)) {
		fm->sync = sync;
		return;
	}
	if (std::holds_alternative<synth::Dx7State>(state.plays.engine)) {
		state.recall.sync = sync;
		return;
	}
	place_sync(state, *dual_of(state.plays), sync);
}

synth::LfoState& lfo_of(SoundState& state, size_t index) noexcept {
	// LFO1 and LFO3 run per sound, LFO2 and LFO4 per voice
	auto& pair = (index % 2 == 0) ? state.plays.instrument_lfos : state.plays.voice_lfos;
	return pair[index / 2];
}

synth::EnvelopeState& envelope_of(SoundState& state, size_t index) noexcept {
	return state.plays.envelopes[index];
}

std::array<synth::Pitch, 2>& modulators_of(SoundState& state) noexcept {
	auto* fm = std::get_if<synth::FmState>(&state.plays.engine);
	return fm != nullptr ? fm->modulators : state.recall.modulators;
}

bool& modulator_routing_of(SoundState& state) noexcept {
	auto* fm = std::get_if<synth::FmState>(&state.plays.engine);
	return fm != nullptr ? fm->modulator1_to_modulator0 : state.recall.modulator1_to_modulator0;
}

synth::Pitch& dx7_pitch_of(SoundState& state) noexcept {
	auto* dx7 = std::get_if<synth::Dx7State>(&state.plays.engine);
	return (dx7 != nullptr ? *dx7 : state.recall.dx7).pitch;
}

} // namespace deluge::instruments::classic
