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

#pragma once

#include "effects/state.h"
#include "instruments/deluge/sound_state.h"
#include "instruments/deluge/words.h"
#include "plugin-api/types/interpolation_mode.h"

#include <cstdint>
#include <span>
#include <vector>

/// @file
/// @brief The Deluge instrument's state as code outside the instrument reads and writes it.
///
/// Callers name what they want of a sound (its mode, a slot's type, tuning, playback, ranges or DX7 patch, its
/// effects), never SoundState's members, so the state's layout belongs to the instrument alone.
namespace deluge::instrument {

/// @brief One of a sound's two oscillator slots.
enum class SourceId : uint8_t {
	A = 0, ///< the first oscillator ("osc1")
	B = 1, ///< the second oscillator ("osc2")
};

/// @brief An oscillator's own tuning. A sample oscillator's is zero: its ranges carry theirs.
struct Tuning {
	int16_t transpose{0}; ///< semitones
	int8_t cents{0};      ///< −50..50
	bool operator==(const Tuning&) const = default;
};

/// @brief How a sample oscillator plays its ranges.
struct SamplePlayback {
	SampleRepeatMode repeat_mode{SampleRepeatMode::CUT};
	int8_t time_stretch_amount{0};
	plugin::InterpolationMode interpolation{plugin::InterpolationMode::SMOOTH};
	bool pitch_and_speed_independent{false};
	bool reversed{false};
	bool operator==(const SamplePlayback&) const = default;
};

/// @brief Which engine renders the sound: subtractive, ring mod or FM.
/// @param state The sound's state.
[[nodiscard]] SynthMode synth_mode(const SoundState& state) noexcept;

/// @brief The type an oscillator slot is set to.
/// @param state The sound's state.
/// @param id The slot.
[[nodiscard]] OscType source_type(const SoundState& state, SourceId id) noexcept;

/// @brief An oscillator slot's own tuning.
/// @param state The sound's state.
/// @param id The slot.
[[nodiscard]] Tuning source_tuning(const SoundState& state, SourceId id) noexcept;

/// @brief How an oscillator slot plays samples: meaningful for a sample slot, defaults elsewhere.
/// @param state The sound's state.
/// @param id The slot.
[[nodiscard]] SamplePlayback sample_playback(const SoundState& state, SourceId id) noexcept;

/// @brief An oscillator slot's key-split ranges, by top note: a sample slot's samples, a wavetable slot's files.
/// @param state The sound's state.
/// @param id The slot.
/// @return The ranges, valid while @p state is unchanged; empty for a slot that plays none.
[[nodiscard]] std::span<const RangeState> source_ranges(const SoundState& state, SourceId id) noexcept;

/// @brief An oscillator slot's DX7 patch.
/// @param state The sound's state.
/// @param id The slot.
/// @return The patch, valid while @p state is unchanged; null when the slot has none.
[[nodiscard]] const DxPatchState* dx_patch(const SoundState& state, SourceId id) noexcept;

/// @brief The settings of the sound's own effects.
/// @param state The sound's state.
[[nodiscard]] const effects::EffectStates& sound_effects(const SoundState& state) noexcept;

/// @brief @p state with one oscillator slot's ranges replaced.
/// @param state The sound's state.
/// @param id The slot.
/// @param ranges Its new ranges, by top note.
[[nodiscard]] SoundState with_ranges(SoundState state, SourceId id, std::vector<RangeState> ranges);

/// @brief @p state with one oscillator slot's DX7 patch replaced.
/// @param state The sound's state.
/// @param id The slot.
/// @param patch Its new patch.
[[nodiscard]] SoundState with_dx_patch(SoundState state, SourceId id, const DxPatchState& patch);

} // namespace deluge::instrument
