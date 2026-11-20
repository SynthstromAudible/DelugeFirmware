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
#include "instruments/deluge/flat_layout.h"

#include "instruments/deluge/composed_state.h"
#include "instruments/deluge/dx7_init_voice.h"
#include "instruments/deluge/record_edit.h"

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
	switch_filters(state.plays.fx.filter, synth_mode(state), mode);
	set_engine(state, mode);
	return state;
}

SoundState switch_source(SoundState state, SourceId id, OscType type, std::span<const RangeState> held,
                         bool held_are_wavetable) {
	edit_slot(state, index_of(id), [&](SlotEdit& edit) { apply_osc_type(edit.full, type, held, held_are_wavetable); });
	return state;
}

SoundState with_loop_mode(SoundState state, SourceId id, SampleRepeatMode mode) {
	edit_slot(state, index_of(id), [&](SlotEdit& edit) { apply_loop_mode(edit.full, mode); });
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
		edit_slot(state, s, [](SlotEdit& edit) { edit.full.osc_type = OscType::SAMPLE; });
	}
	return state;
}

SoundState as_default_synth(SoundState state) {
	state.plays.fx.filter.lpf_mode = effects::FilterMode::TRANSISTOR_24DB;
	edit_slot(state, 0, [](SlotEdit& edit) { edit.full.osc_type = OscType::SAW; });
	edit_slot(state, 1, [](SlotEdit& edit) { edit.full.transpose = -12; });
	state.plays.voicing.unison.count = 4;
	state.plays.voicing.unison.detune = 10;
	state.plays.voicing.master.transpose = -12;
	return state;
}

SoundState as_blank_synth(SoundState state, bool dx) {
	if (dx) {
		edit_slot(state, 0, [](SlotEdit& edit) {
			edit.full.osc_type = OscType::DX7;
			if (!edit.full.dx_patch) {
				edit.full.dx_patch.emplace(DxPatchState{.params = kDx7InitVoice});
			}
		});
	}
	return state;
}

} // namespace deluge::instrument
