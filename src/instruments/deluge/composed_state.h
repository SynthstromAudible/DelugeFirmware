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
#include "instruments/deluge/flat_sound_state.h"
#include "synth/engines/engine_state.h"
#include "synth/modulation/lfo_state.h"
#include "synth/voice/voicing_state.h"

#include <array>
#include <cstdint>
#include <utility>

namespace deluge::instrument {

/// @brief What a Deluge synth sound plays, composed of its components' states: its voicing, the engine it renders
/// with (whose oscillators hold only what their kind plays), its LFOs and its effects.
struct ComposedState {
	synth::VoicingState voicing;
	synth::EngineState engine{synth::SubtractiveState{}};
	std::array<synth::LfoState, 2> instrument_lfos; ///< LFO1, LFO3: one per sound
	std::array<synth::LfoState, 2> voice_lfos;      ///< LFO2, LFO4: one per voice
	effects::EffectStates fx;
	// The owner's send and the kit row's MIDI output, until they move to host records
	int32_t side_chain_send_level{0};
	uint8_t output_midi_channel{255};
	uint8_t output_midi_note_for_drum{255};
	bool operator==(const ComposedState&) const = default;
};

/// @brief What a sound keeps of what its file does not write, for a switch back: each oscillator's settings its current
/// kind does not hold (the fields its state holds are left at their defaults here), a sample oscillator's retrigger
/// phase, FM's modulators while another engine plays, and the oscillators' sync while FM plays or the second is a
/// sample.
struct Recall {
	std::array<SourceState, 2> slots;
	std::array<synth::Retrigger, 2> retrigger{}; ///< a sample oscillator's; free-running where its kind holds its own
	std::array<synth::Pitch, 2> modulators{synth::FmState{}.modulators};
	bool modulator1_to_modulator0{false};
	bool sync{false};
	bool operator==(const Recall&) const = default;
};

/// @brief The retrigger a file's 32-bit phase word means: none for 0xFFFFFFFF (free-running).
[[nodiscard]] synth::Retrigger retrigger_of_word(uint32_t phase);

/// @brief The kind state slot source @p full plays, its phase word @p phase; what the kind does not hold goes to
/// @p kept and @p kept_retrigger (the fields the kind holds are left at their defaults there).
[[nodiscard]] synth::SlotState pack_slot(const SourceState& full, uint32_t phase, SourceState& kept,
                                         synth::Retrigger& kept_retrigger);

/// @brief The slot source a kind state and what it keeps are, its phase word into @p phase.
[[nodiscard]] SourceState unpack_slot(const synth::SlotState& slot, const SourceState& kept,
                                      const synth::Retrigger& kept_retrigger, uint32_t& phase);

/// @brief The file's 32-bit phase word for @p retrigger: the phase a note starts at, 0xFFFFFFFF for free-running.
[[nodiscard]] uint32_t phase_word(const synth::Retrigger& retrigger) noexcept;

/// @brief The flat record @p flat as the state it plays and what it keeps for a switch back.
[[nodiscard]] std::pair<ComposedState, Recall> compose(const FlatSoundState& flat);

/// @brief The flat record a composed state and its recall are: `flatten(compose(f)) == f` for every record a file
/// decodes to or a rule makes.
[[nodiscard]] FlatSoundState flatten(const ComposedState& state, const Recall& recall);

} // namespace deluge::instrument
