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

#include "instruments/deluge/state_rules.h"

#include "instruments/deluge/composed_state.h"
#include "instruments/deluge/dx7_init_voice.h"

#include <utility>
#include <variant>

namespace deluge::instrument {
namespace {

[[nodiscard]] constexpr size_t index_of(SourceId id) noexcept {
	return std::to_underlying(id);
}

/// A range holding nothing but its top note.
[[nodiscard]] RangeState only_top_note(int16_t top_note) {
	RangeState range;
	range.top_note = top_note;
	return range;
}

/// Slot @p source set to @p type: ranges of the other kind keep only their top notes (one open range where there are
/// none), otherwise the slot takes @p held; a DX7 slot without a patch gains the init voice.
void apply_osc_type(SourceState& source, OscType type, std::span<const RangeState> held, bool held_are_wavetable) {
	source.osc_type = type;
	const bool plays_ranges = type == OscType::SAMPLE || type == OscType::WAVETABLE;
	if (plays_ranges && held_are_wavetable != (type == OscType::WAVETABLE)) {
		for (RangeState& range : source.ranges) {
			range = only_top_note(range.top_note);
		}
		if (source.ranges.empty()) {
			source.ranges.push_back(only_top_note(kOpenTopNote));
		}
	}
	else {
		source.ranges.assign(held.begin(), held.end());
	}
	if (type == OscType::DX7 && !source.dx_patch) {
		source.dx_patch.emplace(DxPatchState{.params = kDx7InitVoice});
	}
}

/// Slot @p source's loop mode set: stretching makes pitch and speed independent, leaving it gives that up.
void apply_loop_mode(SourceState& source, SampleRepeatMode mode) {
	if (mode == SampleRepeatMode::STRETCH) {
		source.pitch_and_speed_independent = true;
	}
	else if (source.repeat_mode == SampleRepeatMode::STRETCH) {
		source.pitch_and_speed_independent = false;
	}
	source.repeat_mode = mode;
}

/// The filters as a switch from @p from to @p to leaves them: into FM both go off; out of it one that is off comes
/// back on (the low-pass as the 24 dB transistor ladder, the high-pass as the ladder).
void switch_filters(effects::FilterState& filter, SynthMode from, SynthMode to) {
	if (to == SynthMode::FM && from != SynthMode::FM) {
		filter.lpf_mode = effects::FilterMode::OFF;
		filter.hpf_mode = effects::FilterMode::OFF;
	}
	else if (to != SynthMode::FM && from == SynthMode::FM) {
		if (filter.lpf_mode == effects::FilterMode::OFF) {
			filter.lpf_mode = effects::FilterMode::TRANSISTOR_24DB;
		}
		if (filter.hpf_mode == effects::FilterMode::OFF) {
			filter.hpf_mode = effects::FilterMode::HPLADDER;
		}
	}
}

/// The two oscillator slots a subtractive or ring-mod sound plays.
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

/// Slot @p s of @p state changed through its whole source, as the file's layout holds it: an FM sound's carrier pitch
/// and kept fields, another's kind state and kept fields.
template <class Change>
void edit_slot(SoundState& state, size_t s, const Change& change) {
	if (auto* fm = std::get_if<synth::FmState>(&state.plays.engine)) {
		SourceState full = state.recall.slots[s];
		full.transpose = fm->carriers[s].tuning.transpose;
		full.cents = fm->carriers[s].tuning.cents;
		change(full);
		fm->carriers[s].tuning = {.transpose = full.transpose, .cents = full.cents};
		full.transpose = 0;
		full.cents = 0;
		state.recall.slots[s] = std::move(full);
		return;
	}
	const bool sync = oscillator_sync(state);
	synth::DualSourceState& dual = dual_of(state.plays);
	uint32_t phase = 0;
	SourceState full = unpack_slot(dual.slots[s], state.recall.slots[s], state.recall.retrigger[s], phase);
	change(full);
	dual.slots[s] = pack_slot(full, phase, state.recall.slots[s], state.recall.retrigger[s]);
	place_sync(state, dual, sync);
}

} // namespace

FlatSoundState with_mode(FlatSoundState state, SynthMode mode) {
	switch_filters(state.effects.filter, state.synth_mode, mode);
	state.synth_mode = mode;
	return state;
}

FlatSoundState with_osc_type(FlatSoundState state, SourceId id, OscType type, std::span<const RangeState> held,
                             bool held_are_wavetable) {
	apply_osc_type(state.sources[index_of(id)], type, held, held_are_wavetable);
	return state;
}

FlatSoundState with_loop_mode(FlatSoundState state, SourceId id, SampleRepeatMode mode) {
	apply_loop_mode(state.sources[index_of(id)], mode);
	return state;
}

FlatSoundState with_song_defaults(FlatSoundState state, const effects::SongDefaults& defaults) {
	state.effects = effects::with_song_defaults(state.effects, defaults);
	return state;
}

FlatSoundState as_sample_sound(FlatSoundState state) {
	state.polyphonic = PolyphonyMode::AUTO;
	state.effects.filter.lpf_mode = effects::FilterMode::TRANSISTOR_24DB;
	state.sources[0].osc_type = OscType::SAMPLE;
	state.sources[1].osc_type = OscType::SAMPLE;
	return state;
}

FlatSoundState as_default_synth(FlatSoundState state) {
	state.effects.filter.lpf_mode = effects::FilterMode::TRANSISTOR_24DB;
	state.sources[0].osc_type = OscType::SAW;
	state.sources[1].transpose = -12;
	state.num_unison = 4;
	state.unison_detune = 10;
	state.transpose = -12;
	return state;
}

FlatSoundState as_blank_synth(FlatSoundState state, bool dx) {
	if (dx) {
		state.sources[0].osc_type = OscType::DX7;
		if (!state.sources[0].dx_patch) {
			state.sources[0].dx_patch.emplace(DxPatchState{.params = kDx7InitVoice});
		}
	}
	return state;
}

SoundState switch_engine(SoundState state, SynthMode mode) {
	const SynthMode from = synth_mode(state);
	switch_filters(state.plays.fx.filter, from, mode);
	if (from == mode) {
		return state;
	}
	if (from != SynthMode::FM && mode != SynthMode::FM) {
		synth::DualSourceState dual = std::move(dual_of(state.plays));
		if (mode == SynthMode::RINGMOD) {
			state.plays.engine = synth::RingModState{.oscillators = std::move(dual)};
		}
		else {
			state.plays.engine = synth::SubtractiveState{.oscillators = std::move(dual)};
		}
		return state;
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
		return state;
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
	if (mode == SynthMode::RINGMOD) {
		state.plays.engine = synth::RingModState{.oscillators = std::move(dual)};
	}
	else {
		state.plays.engine = synth::SubtractiveState{.oscillators = std::move(dual)};
	}
	return state;
}

SoundState switch_source(SoundState state, SourceId id, OscType type, std::span<const RangeState> held,
                         bool held_are_wavetable) {
	edit_slot(state, index_of(id),
	          [&](SourceState& source) { apply_osc_type(source, type, held, held_are_wavetable); });
	return state;
}

SoundState with_loop_mode(SoundState state, SourceId id, SampleRepeatMode mode) {
	edit_slot(state, index_of(id), [&](SourceState& source) { apply_loop_mode(source, mode); });
	return state;
}

SoundState with_song_defaults(SoundState state, const effects::SongDefaults& defaults) {
	state.plays.fx = effects::with_song_defaults(state.plays.fx, defaults);
	return state;
}

SoundState as_sample_sound(SoundState state) {
	state.plays.voicing.polyphony = PolyphonyMode::AUTO;
	state.plays.fx.filter.lpf_mode = effects::FilterMode::TRANSISTOR_24DB;
	for (size_t s = 0; s < 2; s++) {
		edit_slot(state, s, [](SourceState& source) { source.osc_type = OscType::SAMPLE; });
	}
	return state;
}

SoundState as_default_synth(SoundState state) {
	state.plays.fx.filter.lpf_mode = effects::FilterMode::TRANSISTOR_24DB;
	edit_slot(state, 0, [](SourceState& source) { source.osc_type = OscType::SAW; });
	edit_slot(state, 1, [](SourceState& source) { source.transpose = -12; });
	state.plays.voicing.unison.count = 4;
	state.plays.voicing.unison.detune = 10;
	state.plays.voicing.master.transpose = -12;
	return state;
}

SoundState as_blank_synth(SoundState state, bool dx) {
	if (dx) {
		edit_slot(state, 0, [](SourceState& source) {
			source.osc_type = OscType::DX7;
			if (!source.dx_patch) {
				source.dx_patch.emplace(DxPatchState{.params = kDx7InitVoice});
			}
		});
	}
	return state;
}

} // namespace deluge::instrument
