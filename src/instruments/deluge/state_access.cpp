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

#include "instruments/deluge/state_access.h"
#include "instruments/deluge/flat_layout.h"

#include <utility>
#include <variant>

namespace deluge::instrument {
namespace {

/// What slot @p id plays, or null when the engine is FM.
[[nodiscard]] const synth::SlotState* slot_of(const SoundState& state, SourceId id) noexcept {
	const synth::DualSourceState* slots = dual_of(state.plays);
	return slots != nullptr ? &slots->slots[slot_index(id)] : nullptr;
}

/// What slot @p id remembers of the kinds it is not playing.
[[nodiscard]] const SlotMemory& memory_of(const SoundState& state, SourceId id) noexcept {
	return state.recall.slots[slot_index(id)];
}

/// Slot @p id's kind with its parts: the one it plays, or while FM plays the one it parks.
[[nodiscard]] const synth::SlotState& kind_of(const SoundState& state, SourceId id) noexcept {
	static const synth::SlotState new_synth_kind = synth::VaState{}; // a slot parks its kind whenever FM plays
	const synth::SlotState* slot = slot_of(state, id);
	if (slot != nullptr) {
		return *slot;
	}
	const std::optional<synth::SlotState>& parked = memory_of(state, id).parked;
	return parked ? *parked : new_synth_kind;
}

/// @p state with @p change made to its flat layout and composed again: a slot's ranges and DX7 patch are written where
/// the file's layout puts them, whatever the slot's kind.
template <class Change>
[[nodiscard]] SoundState changed_flat(const SoundState& state, const Change& change) {
	FlatSoundState flat = to_flat(state);
	change(flat);
	return to_record(flat);
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

namespace {

/// Slot @p id's pitch as its kind or an FM carrier plays it; a sample's is the one its memory keeps for other kinds.
[[nodiscard]] const synth::Pitch& pitch_of(const SoundState& state, SourceId id) noexcept {
	if (const auto* fm = std::get_if<synth::FmState>(&state.plays.engine)) {
		return fm->carriers[slot_index(id)];
	}
	const synth::SlotState& kind = kind_of(state, id);
	if (const auto* va = std::get_if<synth::VaState>(&kind)) {
		return va->pitch;
	}
	if (const auto* wavetable = std::get_if<synth::WavetableState>(&kind)) {
		return wavetable->pitch;
	}
	if (const auto* dx = std::get_if<synth::Dx7State>(&kind)) {
		return dx->pitch;
	}
	if (const auto* input = std::get_if<synth::InputState>(&kind)) {
		return input->pitch;
	}
	return memory_of(state, id).sample_pitch; // its ranges carry their own tuning
}

} // namespace

Tuning source_tuning(const SoundState& state, SourceId id) noexcept {
	return pitch_of(state, id).tuning;
}

SamplePlayback sample_playback(const SoundState& state, SourceId id) noexcept {
	const auto* sample = std::get_if<synth::SampleState>(&kind_of(state, id));
	return sample != nullptr ? sample->playback : memory_of(state, id).playback;
}

std::vector<RangeState> source_ranges(const SoundState& state, SourceId id) {
	const synth::SlotState& kind = kind_of(state, id);
	std::vector<RangeState> ranges;
	if (const auto* wavetable = std::get_if<synth::WavetableState>(&kind)) {
		ranges.reserve(wavetable->ranges.size());
		for (const synth::WavetableRange& r : wavetable->ranges) {
			ranges.push_back({.top_note = r.top_note, .file_path = r.file_path});
		}
		return ranges;
	}
	const auto* sample = std::get_if<synth::SampleState>(&kind);
	const std::vector<synth::SampleRange>& held = sample != nullptr ? sample->ranges : memory_of(state, id).ranges;
	ranges.reserve(held.size());
	for (const synth::SampleRange& r : held) {
		ranges.push_back(range_state_of(r));
	}
	return ranges;
}

bool same_ranges(const SoundState& a, const SoundState& b, SourceId id) {
	const auto* wa = std::get_if<synth::WavetableState>(&kind_of(a, id));
	const auto* wb = std::get_if<synth::WavetableState>(&kind_of(b, id));
	if (wa != nullptr && wb != nullptr) {
		return wa->ranges == wb->ranges;
	}
	if (wa == nullptr && wb == nullptr) {
		const auto* sa = std::get_if<synth::SampleState>(&kind_of(a, id));
		const auto* sb = std::get_if<synth::SampleState>(&kind_of(b, id));
		return (sa != nullptr ? sa->ranges : memory_of(a, id).ranges)
		       == (sb != nullptr ? sb->ranges : memory_of(b, id).ranges);
	}
	return source_ranges(a, id) == source_ranges(b, id); // a wavetable's and a sample's, compared as the file has them
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
	return pitch_of(state, id).retrigger;
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

SoundState with_ranges(SoundState state, SourceId id, std::vector<RangeState> ranges) {
	return changed_flat(state, [&](FlatSoundState& flat) { flat.sources[slot_index(id)].ranges = std::move(ranges); });
}

SoundState with_dx_patch(SoundState state, SourceId id, std::optional<DxPatchState> patch) {
	return changed_flat(state, [&](FlatSoundState& flat) { flat.sources[slot_index(id)].dx_patch = patch; });
}

} // namespace deluge::instrument
