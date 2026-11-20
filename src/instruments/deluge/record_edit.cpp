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

#include "instruments/deluge/record_edit.h"

#include "instruments/deluge/flat_layout.h"
#include "instruments/deluge/state_access.h"

#include <utility>
#include <variant>

namespace deluge::instrument {
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
	if (mode == SynthMode::RINGMOD) {
		plays.engine = synth::RingModState{.oscillators = std::move(dual)};
	}
	else {
		plays.engine = synth::SubtractiveState{.oscillators = std::move(dual)};
	}
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

} // namespace

SlotEdit begin_slot_edit(const SoundState& state, size_t slot) {
	if (const auto* fm = std::get_if<synth::FmState>(&state.plays.engine)) {
		SlotEdit edit{.source = unpark_slot(state.recall.slots[slot]),
		              .phase = phase_word(fm->carriers[slot].retrigger),
		              .sync = fm->sync};
		edit.source.transpose = fm->carriers[slot].tuning.transpose;
		edit.source.cents = fm->carriers[slot].tuning.cents;
		return edit;
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
	if (mode == SynthMode::FM) {
		enter_fm(state);
	}
	else if (from == SynthMode::FM) {
		leave_fm(state, mode);
	}
	else {
		play_dual(state.plays, mode, std::move(*dual_of(state.plays)));
	}
}

synth::Pitch& source_pitch_of(SoundState& state, size_t slot) noexcept {
	if (auto* fm = std::get_if<synth::FmState>(&state.plays.engine)) {
		return fm->carriers[slot];
	}
	synth::SlotState& kind = dual_of(state.plays)->slots[slot];
	if (auto* va = std::get_if<synth::VaState>(&kind)) {
		return va->pitch;
	}
	if (auto* wavetable = std::get_if<synth::WavetableState>(&kind)) {
		return wavetable->pitch;
	}
	if (auto* dx = std::get_if<synth::Dx7State>(&kind)) {
		return dx->pitch;
	}
	if (auto* input = std::get_if<synth::InputState>(&kind)) {
		return input->pitch;
	}
	return state.recall.slots[slot].sample_pitch;
}

synth::SamplePlayback& sample_playback_of(SoundState& state, size_t slot) noexcept {
	SlotMemory& memory = state.recall.slots[slot];
	synth::SlotState* kind = nullptr; // what the slot plays, or parks while FM plays
	if (synth::DualSourceState* dual = dual_of(state.plays)) {
		kind = &dual->slots[slot];
	}
	else if (memory.parked) {
		kind = &*memory.parked;
	}
	auto* sample = kind != nullptr ? std::get_if<synth::SampleState>(kind) : nullptr;
	return sample != nullptr ? sample->playback : memory.playback;
}

void set_oscillator_sync(SoundState& state, bool sync) {
	if (auto* fm = std::get_if<synth::FmState>(&state.plays.engine)) {
		fm->sync = sync;
		return;
	}
	place_sync(state, *dual_of(state.plays), sync);
}

synth::LfoState& lfo_of(SoundState& state, size_t index) noexcept {
	// LFO1 and LFO3 run per sound, LFO2 and LFO4 per voice
	auto& pair = (index % 2 == 0) ? state.plays.instrument_lfos : state.plays.voice_lfos;
	return pair[index / 2];
}

std::array<synth::Pitch, 2>& modulators_of(SoundState& state) noexcept {
	auto* fm = std::get_if<synth::FmState>(&state.plays.engine);
	return fm != nullptr ? fm->modulators : state.recall.modulators;
}

bool& modulator_routing_of(SoundState& state) noexcept {
	auto* fm = std::get_if<synth::FmState>(&state.plays.engine);
	return fm != nullptr ? fm->modulator1_to_modulator0 : state.recall.modulator1_to_modulator0;
}

} // namespace deluge::instrument
