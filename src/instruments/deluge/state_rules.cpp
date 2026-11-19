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

#include "instruments/deluge/dx7_init_voice.h"

#include <utility>

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

} // namespace

SoundState with_mode(SoundState state, SynthMode mode) {
	effects::FilterState& filter = state.effects.filter;
	if (mode == SynthMode::FM && state.synth_mode != SynthMode::FM) {
		filter.lpf_mode = effects::FilterMode::OFF;
		filter.hpf_mode = effects::FilterMode::OFF;
	}
	else if (mode != SynthMode::FM && state.synth_mode == SynthMode::FM) {
		if (filter.lpf_mode == effects::FilterMode::OFF) {
			filter.lpf_mode = effects::FilterMode::TRANSISTOR_24DB;
		}
		if (filter.hpf_mode == effects::FilterMode::OFF) {
			filter.hpf_mode = effects::FilterMode::HPLADDER;
		}
	}
	state.synth_mode = mode;
	return state;
}

SoundState with_osc_type(SoundState state, SourceId id, OscType type, std::span<const RangeState> held,
                         bool held_are_wavetable) {
	SourceState& source = state.sources[index_of(id)];
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
	return state;
}

SoundState with_loop_mode(SoundState state, SourceId id, SampleRepeatMode mode) {
	SourceState& source = state.sources[index_of(id)];
	if (mode == SampleRepeatMode::STRETCH) {
		source.pitch_and_speed_independent = true;
	}
	else if (source.repeat_mode == SampleRepeatMode::STRETCH) {
		source.pitch_and_speed_independent = false;
	}
	source.repeat_mode = mode;
	return state;
}

SoundState with_song_defaults(SoundState state, const effects::SongDefaults& defaults) {
	state.effects = effects::with_song_defaults(state.effects, defaults);
	return state;
}

SoundState as_sample_sound(SoundState state) {
	state.polyphonic = PolyphonyMode::AUTO;
	state.effects.filter.lpf_mode = effects::FilterMode::TRANSISTOR_24DB;
	state.sources[0].osc_type = OscType::SAMPLE;
	state.sources[1].osc_type = OscType::SAMPLE;
	return state;
}

SoundState as_default_synth(SoundState state) {
	state.effects.filter.lpf_mode = effects::FilterMode::TRANSISTOR_24DB;
	state.sources[0].osc_type = OscType::SAW;
	state.sources[1].transpose = -12;
	state.num_unison = 4;
	state.unison_detune = 10;
	state.transpose = -12;
	return state;
}

SoundState as_blank_synth(SoundState state, bool dx) {
	if (dx) {
		state.sources[0].osc_type = OscType::DX7;
		if (!state.sources[0].dx_patch) {
			state.sources[0].dx_patch.emplace(DxPatchState{.params = kDx7InitVoice});
		}
	}
	return state;
}

} // namespace deluge::instrument
