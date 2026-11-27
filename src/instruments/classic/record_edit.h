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

#include "instruments/classic/flat_sound_state.h"
#include "instruments/classic/sound_state.h"
#include "instruments/classic/words.h"
#include "synth/modulation/lfo_state.h"
#include "synth/sources/source_state.h"

#include <array>
#include <cstddef>
#include <cstdint>

/// @file
/// @brief Changes to a sound's record through the parts the file's layout names: an oscillator's whole source, its
/// phase word and the sync; the engine; an LFO; FM's modulators. The rules and the settings rows write through these,
/// so what a write means is what it means to the file.
namespace deluge::instruments::classic {

/// @brief An oscillator slot as the file's layout holds it, for one change: its whole source, its retrigger phase
/// word and the oscillators' sync.
struct SlotEdit {
	SourceState source;
	uint32_t phase{0};
	bool sync{false};
};

/// @brief Slot @p slot of @p state as a SlotEdit: an FM sound's carrier pitch and kept fields, another's kind state and
/// kept fields.
[[nodiscard]] SlotEdit begin_slot_edit(const SoundState& state, size_t slot);

/// @brief Write @p edit back into slot @p slot of @p state, its kind state, carrier and kept fields as the change left
/// them, and the sync where the file writes it.
void end_slot_edit(SoundState& state, size_t slot, const SlotEdit& edit);

/// @brief Slot @p slot of @p state changed through its SlotEdit.
template <class Change>
void edit_slot(SoundState& state, size_t slot, const Change& change) {
	SlotEdit edit = begin_slot_edit(state, slot);
	change(edit);
	end_slot_edit(state, slot, edit);
}

/// @brief @p state's engine set to @p mode and nothing else (the filters stay): what leaves is kept for a switch back,
/// what arrives comes from what was kept.
void set_engine(SoundState& state, SynthMode mode);

/// @brief One of @p state's four LFOs, in file order, to change in place.
[[nodiscard]] synth::LfoState& lfo_of(SoundState& state, size_t index) noexcept;
/// @brief One of the sound's four envelopes' settings, to edit.
[[nodiscard]] synth::EnvelopeState& envelope_of(SoundState& state, size_t index) noexcept;

/// @brief Slot @p slot's kind with its parts: the one it plays, or while FM plays the one it parks.
/// @return The kind, or null when FM plays and the slot parks none (a new synth's square).
[[nodiscard]] const synth::SlotState* slot_kind_of(const SoundState& state, size_t slot) noexcept;

/// @copydoc slot_kind_of(const SoundState&, size_t)
[[nodiscard]] synth::SlotState* slot_kind_of(SoundState& state, size_t slot) noexcept;

/// @brief Slot @p slot's pitch: an FM sound's carrier, another's kind's, a sample's the one its slot keeps (its ranges
/// carry their own tuning).
/// @return The pitch.
[[nodiscard]] const synth::Pitch& source_pitch_of(const SoundState& state, size_t slot) noexcept;

/// @copydoc source_pitch_of(const SoundState&, size_t)
[[nodiscard]] synth::Pitch& source_pitch_of(SoundState& state, size_t slot) noexcept;

/// @brief Slot @p slot's sample playback, to change in place: a sample's own (played, or parked while FM plays), any
/// other kind's the one its slot keeps for a sample.
[[nodiscard]] synth::SamplePlayback& sample_playback_of(SoundState& state, size_t slot) noexcept;

/// @brief Set the oscillators' sync where the file writes it: FM's own, else the dual source's, kept for a switch
/// back while the second oscillator is a sample.
void set_oscillator_sync(SoundState& state, bool sync);

/// @brief FM's two modulators' pitches, played or kept, to change in place.
[[nodiscard]] std::array<synth::Pitch, 2>& modulators_of(SoundState& state) noexcept;

/// @brief Whether FM's second modulator modulates the first, played or kept, to change in place.
[[nodiscard]] bool& modulator_routing_of(SoundState& state) noexcept;

/// @brief The DX7 engine's pitch, played or kept, to change in place.
[[nodiscard]] synth::Pitch& dx7_pitch_of(SoundState& state) noexcept;

} // namespace deluge::instruments::classic
