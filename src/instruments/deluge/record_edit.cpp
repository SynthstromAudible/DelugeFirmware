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

#include "instruments/deluge/composed_state.h"
#include "instruments/deluge/state_access.h"

#include <utility>
#include <variant>

namespace deluge::instrument {
namespace {

/// The two oscillator slots a subtractive or ring-mod sound plays.
[[nodiscard]] const synth::DualSourceState& dual_of(const ComposedState& plays) {
	if (const auto* ring = std::get_if<synth::RingModState>(&plays.engine)) {
		return ring->oscillators;
	}
	return std::get_if<synth::SubtractiveState>(&plays.engine)->oscillators;
}

[[nodiscard]] synth::DualSourceState& dual_of(ComposedState& plays) {
	if (auto* ring = std::get_if<synth::RingModState>(&plays.engine)) {
		return ring->oscillators;
	}
	return std::get_if<synth::SubtractiveState>(&plays.engine)->oscillators;
}

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

} // namespace

SlotEdit begin_slot_edit(const SoundState& state, size_t s) {
	if (const auto* fm = std::get_if<synth::FmState>(&state.plays.engine)) {
		SlotEdit edit{
		    .full = state.recall.slots[s], .phase = phase_word(fm->carriers[s].retrigger), .sync = state.recall.sync};
		edit.full.transpose = fm->carriers[s].tuning.transpose;
		edit.full.cents = fm->carriers[s].tuning.cents;
		return edit;
	}
	const synth::DualSourceState& dual = dual_of(state.plays);
	SlotEdit edit{.full = {}, .phase = 0, .sync = oscillator_sync(state)};
	edit.full = unpack_slot(dual.slots[s], state.recall.slots[s], state.recall.retrigger[s], edit.phase);
	return edit;
}

void end_slot_edit(SoundState& state, size_t s, const SlotEdit& edit) {
	if (auto* fm = std::get_if<synth::FmState>(&state.plays.engine)) {
		fm->carriers[s] = {.tuning = {.transpose = edit.full.transpose, .cents = edit.full.cents},
		                   .retrigger = retrigger_of_word(edit.phase)};
		SourceState kept = edit.full;
		kept.transpose = 0;
		kept.cents = 0;
		state.recall.slots[s] = std::move(kept);
		state.recall.sync = edit.sync;
		return;
	}
	synth::DualSourceState& dual = dual_of(state.plays);
	dual.slots[s] = pack_slot(edit.full, edit.phase, state.recall.slots[s], state.recall.retrigger[s]);
	place_sync(state, dual, edit.sync);
}

void set_engine(SoundState& state, SynthMode mode) {
	const SynthMode from = synth_mode(state);
	if (from == mode) {
		return;
	}
	if (from != SynthMode::FM && mode != SynthMode::FM) {
		play_dual(state.plays, mode, std::move(dual_of(state.plays)));
		return;
	}
	Recall& recall = state.recall;
	if (mode == SynthMode::FM) {
		// The slots' pitches become the carriers'; the rest of each slot, the sync and nothing of FM's are kept
		const bool sync = oscillator_sync(state);
		synth::DualSourceState& dual = dual_of(state.plays);
		synth::FmState fm{.carriers = {},
		                  .modulators = recall.modulators,
		                  .modulator1_to_modulator0 = recall.modulator1_to_modulator0};
		for (size_t s = 0; s < 2; s++) {
			uint32_t phase = 0;
			SourceState full = unpack_slot(dual.slots[s], recall.slots[s], recall.retrigger[s], phase);
			fm.carriers[s] = {.tuning = {.transpose = full.transpose, .cents = full.cents},
			                  .retrigger = retrigger_of_word(phase)};
			full.transpose = 0;
			full.cents = 0;
			recall.slots[s] = std::move(full);
			recall.retrigger[s].reset();
		}
		recall.modulators = synth::FmState{}.modulators;
		recall.modulator1_to_modulator0 = false;
		recall.sync = sync;
		state.plays.engine = fm;
		return;
	}
	// Out of FM: the carriers' pitches go back to the slots, which take their kept kinds; FM's modulators are kept
	const synth::FmState fm = *std::get_if<synth::FmState>(&state.plays.engine);
	const bool sync = recall.sync;
	synth::DualSourceState dual{.slots = {}, .sync = false};
	for (size_t s = 0; s < 2; s++) {
		SourceState full = recall.slots[s];
		full.transpose = fm.carriers[s].tuning.transpose;
		full.cents = fm.carriers[s].tuning.cents;
		dual.slots[s] = pack_slot(full, phase_word(fm.carriers[s].retrigger), recall.slots[s], recall.retrigger[s]);
	}
	recall.modulators = fm.modulators;
	recall.modulator1_to_modulator0 = fm.modulator1_to_modulator0;
	place_sync(state, dual, sync);
	play_dual(state.plays, mode, std::move(dual));
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
