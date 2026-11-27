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
#include "instruments/classic/sound_state.h"
#include "instruments/classic/words.h"
#include "plugin/types/interpolation_mode.h"
#include "synth/modulation/lfo_state.h"
#include "synth/sources/source_state.h"
#include "synth/voice/voicing_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// @file
/// @brief The Deluge instrument's state as code outside the instrument reads and writes it.
///
/// Callers name what they want of a sound (its mode, a slot's type, tuning, playback, ranges or DX7 patch, its
/// effects) rather than reach into SoundState's members (`plays`, `recall`), whose layout belongs to the instrument.
namespace deluge::instruments::classic {

/// @brief One of a sound's two oscillator slots.
enum class SourceId : uint8_t {
	A = 0, ///< the first oscillator ("osc1")
	B = 1, ///< the second oscillator ("osc2")
};

/// @brief An oscillator's own tuning. A sample oscillator plays its ranges' and keeps this one for the other kinds.
using synth::Tuning;

/// @brief How a sample oscillator plays its ranges.
using synth::SamplePlayback;

/// @brief Slot @p id's index in a sound's two slots.
[[nodiscard]] constexpr size_t slot_index(SourceId id) noexcept {
	return static_cast<size_t>(id);
}

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

/// @brief An oscillator slot's key-split ranges, by top note: a sample slot's samples, a wavetable slot's files, and
/// for any other kind the ones the slot keeps for a switch back to samples.
/// @param state The sound's state.
/// @param id The slot.
/// @return The ranges as the state holds them, valid until the state is next written.
[[nodiscard]] const std::vector<synth::SourceRange>& source_ranges(const SoundState& state, SourceId id) noexcept;

/// @brief Whether an oscillator slot's ranges are the same in @p a and @p b, without copying them.
/// @param a One state.
/// @param b The other.
/// @param id The slot.
[[nodiscard]] bool same_ranges(const SoundState& a, const SoundState& b, SourceId id) noexcept;

/// @brief An oscillator slot's DX7 patch.
/// @param state The sound's state.
/// @param id The slot.
/// @return The patch, valid while @p state is unchanged; null when the slot has none.
[[nodiscard]] const DxPatchState* dx_patch(const SoundState& state, SourceId id) noexcept;

/// @brief The DX7 engine's state: the one playing, or the one the sound keeps while another engine plays.
/// @param state The sound's state.
[[nodiscard]] const synth::Dx7State& dx7_state(const SoundState& state) noexcept;

/// @brief The DX7 engine's patch (dx7_state()'s).
/// @param state The sound's state.
/// @return The patch, valid while @p state is unchanged; null when the DX7 has none (its runtime makes the init voice).
[[nodiscard]] const DxPatchState* dx7_patch(const SoundState& state) noexcept;

/// @brief The DX7 engine's pitch (dx7_state()'s).
/// @param state The sound's state.
[[nodiscard]] synth::Pitch dx7_pitch(const SoundState& state) noexcept;

/// @brief The settings of the sound's own effects.
/// @param state The sound's state.
[[nodiscard]] const effects::EffectStates& sound_effects(const SoundState& state) noexcept;

/// @brief The settings of the sound's own effects, to change in place: what the effects binding plays.
/// @param state The sound's state.
[[nodiscard]] effects::EffectStates& sound_effects(SoundState& state) noexcept;

/// @brief How many unison parts each voice plays, how far apart and how wide.
/// @param state The sound's state.
[[nodiscard]] synth::UnisonState unison(const SoundState& state) noexcept;

/// @brief How the sound's voices are allocated and tuned as a whole, its unison included.
/// @param state The sound's state.
[[nodiscard]] synth::VoicingState voicing(const SoundState& state) noexcept;

/// @brief Whether the second oscillator is hard-synced to the first (kept while FM plays).
/// @param state The sound's state.
[[nodiscard]] bool oscillator_sync(const SoundState& state) noexcept;

/// @brief Where an oscillator slot's phase starts a note: none for one that runs free. An FM sound's is its carrier's.
/// @param state The sound's state.
/// @param id The slot.
[[nodiscard]] synth::Retrigger source_retrigger(const SoundState& state, SourceId id) noexcept;

/// @brief FM's two modulators' pitches (kept while another engine plays).
/// @param state The sound's state.
[[nodiscard]] std::array<synth::Pitch, 2> modulators(const SoundState& state) noexcept;

/// @brief Whether FM's second modulator modulates the first rather than the carriers.
/// @param state The sound's state.
[[nodiscard]] bool modulator1_to_modulator0(const SoundState& state) noexcept;

/// @brief One of the sound's four LFOs, in file order: LFO1 and LFO3 one per sound, LFO2 and LFO4 one per voice.
/// @param state The sound's state.
/// @param index 0..3.
[[nodiscard]] synth::LfoState lfo(const SoundState& state, size_t index) noexcept;

/// @brief One of the sound's four envelopes' mode, curve and follow input.
/// @param state The sound's state.
/// @param index 0..3.
[[nodiscard]] synth::EnvelopeState envelope(const SoundState& state, size_t index) noexcept;

/// @brief @p state with one oscillator slot's ranges replaced.
/// @param state The sound's state.
/// @param id The slot.
/// @param ranges Its new ranges, by top note.
[[nodiscard]] SoundState with_ranges(SoundState state, SourceId id, std::vector<synth::SourceRange> ranges);

/// @brief @p state with one oscillator slot's DX7 patch replaced.
/// @param state The sound's state.
/// @param id The slot.
/// @param patch Its new patch; none drops the one it has.
[[nodiscard]] SoundState with_dx_patch(SoundState state, SourceId id, std::optional<DxPatchState> patch);

/// @brief @p state with the DX7 engine's patch replaced (dx7_state()'s).
/// @param state The sound's state.
/// @param patch Its new patch; none drops the one it has.
[[nodiscard]] SoundState with_dx7_patch(SoundState state, std::optional<DxPatchState> patch);

} // namespace deluge::instruments::classic
