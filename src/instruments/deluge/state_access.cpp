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

namespace deluge::instrument {
namespace {

/// The index of slot @p id in SoundState::sources.
[[nodiscard]] constexpr size_t index_of(SourceId id) noexcept {
	return std::to_underlying(id);
}

} // namespace

SynthMode synth_mode(const SoundState& state) noexcept {
	return state.synth_mode;
}

OscType source_type(const SoundState& state, SourceId id) noexcept {
	return state.sources[index_of(id)].osc_type;
}

Tuning source_tuning(const SoundState& state, SourceId id) noexcept {
	const SourceState& source = state.sources[index_of(id)];
	return {.transpose = source.transpose, .cents = source.cents};
}

SamplePlayback sample_playback(const SoundState& state, SourceId id) noexcept {
	const SourceState& source = state.sources[index_of(id)];
	return {.repeat_mode = source.repeat_mode,
	        .time_stretch_amount = source.time_stretch_amount,
	        .interpolation = source.interpolation_mode,
	        .pitch_and_speed_independent = source.pitch_and_speed_independent,
	        .reversed = source.reversed};
}

std::span<const RangeState> source_ranges(const SoundState& state, SourceId id) noexcept {
	return state.sources[index_of(id)].ranges;
}

const DxPatchState* dx_patch(const SoundState& state, SourceId id) noexcept {
	const std::optional<DxPatchState>& patch = state.sources[index_of(id)].dx_patch;
	return patch ? &*patch : nullptr;
}

const effects::EffectStates& sound_effects(const SoundState& state) noexcept {
	return state.effects;
}

SoundState with_ranges(SoundState state, SourceId id, std::vector<RangeState> ranges) {
	state.sources[index_of(id)].ranges = std::move(ranges);
	return state;
}

SoundState with_dx_patch(SoundState state, SourceId id, const DxPatchState& patch) {
	state.sources[index_of(id)].dx_patch = patch;
	return state;
}

} // namespace deluge::instrument
