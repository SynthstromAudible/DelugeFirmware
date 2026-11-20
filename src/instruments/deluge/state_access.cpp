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

#include <utility>
#include <variant>

namespace deluge::instrument {
namespace {

/// The index of slot @p id in a state's slot arrays.
[[nodiscard]] constexpr size_t index_of(SourceId id) noexcept {
	return std::to_underlying(id);
}

/// The oscillator slots the engine plays, or null for FM (whose carriers have only a pitch).
[[nodiscard]] const synth::DualSourceState* slots_of(const ComposedState& plays) noexcept {
	if (const auto* sub = std::get_if<synth::SubtractiveState>(&plays.engine)) {
		return &sub->oscillators;
	}
	if (const auto* ring = std::get_if<synth::RingModState>(&plays.engine)) {
		return &ring->oscillators;
	}
	return nullptr;
}

/// What slot @p id plays, or null when the engine is FM.
[[nodiscard]] const synth::SlotState* slot_of(const SoundState& state, SourceId id) noexcept {
	const synth::DualSourceState* slots = slots_of(state.plays);
	return slots != nullptr ? &slots->slots[index_of(id)] : nullptr;
}

/// What slot @p id remembers of the kinds it is not playing.
[[nodiscard]] const SlotMemory& memory_of(const SoundState& state, SourceId id) noexcept {
	return state.recall.slots[index_of(id)];
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

/// A sample range in the file's layout.
[[nodiscard]] RangeState range_of(const synth::SampleRange& r) {
	return {.top_note = r.top_note,
	        .file_path = r.file_path,
	        .start_pos = r.zone.start,
	        .end_pos = r.zone.end,
	        .loop_start_pos = r.zone.loop_start,
	        .loop_end_pos = r.zone.loop_end,
	        .transpose = r.tuning.transpose,
	        .cents = r.tuning.cents};
}

/// @p state with @p change made to its flat form: what a write means is what it meant to the flat state.
template <class Change>
[[nodiscard]] SoundState changed_flat(const SoundState& state, const Change& change) {
	FlatSoundState flat(state);
	change(flat);
	return SoundState(flat);
}

} // namespace

SynthMode synth_mode(const SoundState& state) noexcept {
	switch (state.plays.engine.index()) {
	case 1:
		return SynthMode::RINGMOD;
	case 2:
		return SynthMode::FM;
	default:
		return SynthMode::SUBTRACTIVE;
	}
}

OscType source_type(const SoundState& state, SourceId id) noexcept {
	const synth::SlotState& kind = kind_of(state, id);
	if (const auto* va = std::get_if<synth::VaState>(&kind)) {
		return static_cast<OscType>(std::to_underlying(va->shape));
	}
	if (std::holds_alternative<synth::WavetableState>(kind)) {
		return OscType::WAVETABLE;
	}
	if (std::holds_alternative<synth::SampleState>(kind)) {
		return OscType::SAMPLE;
	}
	if (std::holds_alternative<synth::Dx7State>(kind)) {
		return OscType::DX7;
	}
	switch (std::get_if<synth::InputState>(&kind)->channel) {
	case synth::InputChannel::RIGHT:
		return OscType::INPUT_R;
	case synth::InputChannel::STEREO:
		return OscType::INPUT_STEREO;
	case synth::InputChannel::LEFT:
		break;
	}
	return OscType::INPUT_L;
}

namespace {

/// Slot @p id's pitch as its kind or an FM carrier plays it; a sample's is the one its memory keeps for other kinds.
[[nodiscard]] const synth::Pitch& pitch_of(const SoundState& state, SourceId id) noexcept {
	if (const auto* fm = std::get_if<synth::FmState>(&state.plays.engine)) {
		return fm->carriers[index_of(id)];
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
	const synth::Tuning& tuning = pitch_of(state, id).tuning;
	return {.transpose = tuning.transpose, .cents = tuning.cents};
}

SamplePlayback sample_playback(const SoundState& state, SourceId id) noexcept {
	const auto* sample = std::get_if<synth::SampleState>(&kind_of(state, id));
	const synth::SamplePlayback& p = sample != nullptr ? sample->playback : memory_of(state, id).playback;
	return {.repeat_mode = p.repeat_mode,
	        .time_stretch_amount = p.time_stretch_amount,
	        .interpolation = p.interpolation,
	        .pitch_and_speed_independent = p.pitch_and_speed_independent,
	        .reversed = p.reversed};
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
		ranges.push_back(range_of(r));
	}
	return ranges;
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
	const synth::DualSourceState* slots = slots_of(state.plays);
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

HostFields host_fields(const SoundState& state) noexcept {
	return {.side_chain_send_level = state.plays.side_chain_send_level,
	        .output_midi_channel = state.plays.output_midi_channel,
	        .output_midi_note_for_drum = state.plays.output_midi_note_for_drum};
}

SoundState with_host_fields(SoundState state, const HostFields& fields) {
	state.plays.side_chain_send_level = fields.side_chain_send_level;
	state.plays.output_midi_channel = fields.output_midi_channel;
	state.plays.output_midi_note_for_drum = fields.output_midi_note_for_drum;
	return state;
}

SoundState with_ranges(SoundState state, SourceId id, std::vector<RangeState> ranges) {
	return changed_flat(state, [&](FlatSoundState& flat) { flat.sources[index_of(id)].ranges = std::move(ranges); });
}

SoundState with_dx_patch(SoundState state, SourceId id, std::optional<DxPatchState> patch) {
	return changed_flat(state, [&](FlatSoundState& flat) { flat.sources[index_of(id)].dx_patch = patch; });
}

} // namespace deluge::instrument
