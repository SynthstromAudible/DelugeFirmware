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

#include "instruments/classic/state_access.h"

#include "instruments/classic/record_edit.h"

#include <utility>
#include <variant>

namespace deluge::instruments::classic {
namespace {

/// What slot @p id remembers of the kinds it is not playing.
[[nodiscard]] const SlotMemory& memory_of(const SoundState& state, SourceId id) noexcept {
	return state.recall.slots[slot_index(id)];
}

/// @copydoc memory_of(const SoundState&, SourceId)
[[nodiscard]] SlotMemory& memory_of(SoundState& state, SourceId id) noexcept {
	return state.recall.slots[slot_index(id)];
}

/// Slot @p id's kind with its parts: the one it plays, or while FM plays the one it parks (a new synth's square when
/// it parks none).
[[nodiscard]] const synth::SlotState& kind_of(const SoundState& state, SourceId id) noexcept {
	static const synth::SlotState new_synth_kind = synth::VaState{}; // a slot parks its kind whenever FM plays
	const synth::SlotState* kind = slot_kind_of(state, slot_index(id));
	return kind != nullptr ? *kind : new_synth_kind;
}

} // namespace

SynthMode synth_mode(const SoundState& state) noexcept {
	if (std::holds_alternative<synth::FmState>(state.plays.engine)) {
		return SynthMode::FM;
	}
	if (std::holds_alternative<synth::RingModState>(state.plays.engine)) {
		return SynthMode::RINGMOD;
	}
	return SynthMode::SUBTRACTIVE;
}

OscType source_type(const SoundState& state, SourceId id) noexcept {
	return osc_type_of(kind_of(state, id));
}

Tuning source_tuning(const SoundState& state, SourceId id) noexcept {
	return source_pitch_of(state, slot_index(id)).tuning;
}

SamplePlayback sample_playback(const SoundState& state, SourceId id) noexcept {
	const auto* sample = std::get_if<synth::SampleState>(&kind_of(state, id));
	return sample != nullptr ? sample->playback : memory_of(state, id).playback;
}

const std::vector<synth::SourceRange>& source_ranges(const SoundState& state, SourceId id) noexcept {
	const synth::SlotState& kind = kind_of(state, id);
	if (const auto* wavetable = std::get_if<synth::WavetableState>(&kind)) {
		return wavetable->ranges;
	}
	const auto* sample = std::get_if<synth::SampleState>(&kind);
	return sample != nullptr ? sample->ranges : memory_of(state, id).ranges;
}

bool same_ranges(const SoundState& a, const SoundState& b, SourceId id) noexcept {
	return source_ranges(a, id) == source_ranges(b, id);
}

const DxPatchState* dx_patch(const SoundState& state, SourceId id) noexcept {
	const auto* dx = std::get_if<synth::Dx7State>(&kind_of(state, id));
	const std::optional<DxPatchState>& patch = dx != nullptr ? dx->patch : memory_of(state, id).patch;
	return patch ? &*patch : nullptr;
}

const effects::EffectStates& sound_effects(const SoundState& state) noexcept {
	return state.plays.fx;
}

effects::EffectStates& sound_effects(SoundState& state) noexcept {
	return state.plays.fx;
}

synth::UnisonState unison(const SoundState& state) noexcept {
	return state.plays.voicing.unison;
}

synth::VoicingState voicing(const SoundState& state) noexcept {
	return state.plays.voicing;
}

bool oscillator_sync(const SoundState& state) noexcept {
	if (const auto* fm = std::get_if<synth::FmState>(&state.plays.engine)) {
		return fm->sync;
	}
	const synth::DualSourceState* slots = dual_of(state.plays);
	if (std::holds_alternative<synth::SampleState>(slots->slots[1])) {
		return state.recall.sync; // the file writes it in the second oscillator's pitch, which a sample lacks
	}
	return slots->sync;
}

synth::Retrigger source_retrigger(const SoundState& state, SourceId id) noexcept {
	return source_pitch_of(state, slot_index(id)).retrigger;
}

std::array<synth::Pitch, 2> modulators(const SoundState& state) noexcept {
	const auto* fm = std::get_if<synth::FmState>(&state.plays.engine);
	return fm != nullptr ? fm->modulators : state.recall.modulators;
}

bool modulator1_to_modulator0(const SoundState& state) noexcept {
	const auto* fm = std::get_if<synth::FmState>(&state.plays.engine);
	return fm != nullptr ? fm->modulator1_to_modulator0 : state.recall.modulator1_to_modulator0;
}

synth::LfoState lfo(const SoundState& state, size_t index) noexcept {
	// LFO1 and LFO3 run per sound, LFO2 and LFO4 per voice
	const auto& pair = (index % 2 == 0) ? state.plays.instrument_lfos : state.plays.voice_lfos;
	return pair[index / 2];
}

synth::EnvelopeState envelope(const SoundState& state, size_t index) noexcept {
	return state.plays.envelopes[index];
}

SoundState with_ranges(SoundState state, SourceId id, std::vector<synth::SourceRange> ranges) {
	synth::SlotState* kind = slot_kind_of(state, slot_index(id));
	if (auto* wavetable = kind != nullptr ? std::get_if<synth::WavetableState>(kind) : nullptr) {
		// A wavetable's range is its note and its file
		for (synth::SourceRange& r : ranges) {
			r.zone = {};
			r.tuning = {};
		}
		wavetable->ranges = std::move(ranges);
		return state;
	}
	// A sample's ranges are its own; any other kind's are the ones the slot keeps for a sample
	auto* sample = kind != nullptr ? std::get_if<synth::SampleState>(kind) : nullptr;
	(sample != nullptr ? sample->ranges : memory_of(state, id).ranges) = std::move(ranges);
	return state;
}

SoundState with_dx_patch(SoundState state, SourceId id, std::optional<DxPatchState> patch) {
	synth::SlotState* kind = slot_kind_of(state, slot_index(id));
	// A DX7's patch is its own; any other kind's is the one the slot keeps for a DX7
	auto* dx = kind != nullptr ? std::get_if<synth::Dx7State>(kind) : nullptr;
	(dx != nullptr ? dx->patch : memory_of(state, id).patch) = patch;
	return state;
}

} // namespace deluge::instruments::classic
