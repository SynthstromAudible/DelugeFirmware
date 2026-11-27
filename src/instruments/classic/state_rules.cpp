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

#include "instruments/classic/state_rules.h"

#include "instruments/classic/composed_state.h"
#include "instruments/classic/dx7_init_voice.h"
#include "instruments/classic/record_edit.h"
#include "instruments/classic/state_access.h"

#include <utility>
#include <variant>

namespace deluge::instruments::classic {
namespace {

/// A range holding nothing but its top note.
[[nodiscard]] RangeState only_top_note(int16_t top_note) {
	RangeState range;
	range.top_note = top_note;
	return range;
}

/// Slot @p source set to @p type: ranges of the other kind keep only their top notes (one open range where there are
/// none), otherwise the slot takes @p held.
void apply_osc_type(SourceState& source, OscType type, std::span<const synth::SourceRange> held,
                    bool held_are_wavetable) {
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
		source.ranges.clear();
		for (const synth::SourceRange& range : held) {
			source.ranges.push_back(range_state_of(range));
		}
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

SoundState switch_engine(SoundState state, SynthMode mode) {
	switch_filters(state.plays.fx.filter, synth_mode(state), mode);
	set_engine(state, mode);
	return state;
}

SoundState switch_source(SoundState state, SourceId id, OscType type, std::span<const synth::SourceRange> held,
                         bool held_are_wavetable) {
	edit_slot(state, slot_index(id),
	          [&](SlotEdit& edit) { apply_osc_type(edit.source, type, held, held_are_wavetable); });
	return state;
}

SoundState with_loop_mode(SoundState state, SourceId id, SampleRepeatMode mode) {
	edit_slot(state, slot_index(id), [&](SlotEdit& edit) { apply_loop_mode(edit.source, mode); });
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
		edit_slot(state, s, [](SlotEdit& edit) { edit.source.osc_type = OscType::SAMPLE; });
	}
	return state;
}

SoundState as_default_synth(SoundState state) {
	state.plays.fx.filter.lpf_mode = effects::FilterMode::TRANSISTOR_24DB;
	edit_slot(state, 0, [](SlotEdit& edit) { edit.source.osc_type = OscType::SAW; });
	edit_slot(state, 1, [](SlotEdit& edit) { edit.source.transpose = -12; });
	state.plays.voicing.unison.count = 4;
	state.plays.voicing.unison.detune = 10;
	state.plays.voicing.master.transpose = -12;
	return state;
}

SoundState as_blank_synth(SoundState state, bool dx) {
	if (dx) {
		set_engine(state, SynthMode::DX7);
		if (dx7_patch(state) == nullptr) {
			state = with_dx7_patch(std::move(state), DxPatchState{.params = kDx7InitVoice});
		}
	}
	return state;
}

} // namespace deluge::instruments::classic
