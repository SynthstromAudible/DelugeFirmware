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

/// @brief What one oscillator slot remembers of the kinds it is not playing, so a switch back loses nothing.
struct SlotMemory {
	/// While FM plays: the slot's kind with its parts, as it plays again when FM goes (its pitch is the carrier's)
	std::optional<synth::SlotState> parked;
	synth::SamplePlayback playback;         ///< how a sample plays, while the slot is another kind
	std::vector<synth::SampleRange> ranges; ///< the slot's ranges, while its kind plays none
	std::optional<synth::DxPatch> patch;    ///< a DX7 voice, while the slot is another kind
	/// While a sample: the pitch the other kinds play (a sample oscillator has none of its own; its ranges carry
	/// theirs)
	synth::Pitch sample_pitch;
	bool operator==(const SlotMemory&) const = default;
};

/// @brief A sound's memory of what its current engine and oscillator kinds cannot hold, so a switch back loses nothing
/// (a sample kept through a sine and back; FM's modulators through subtractive and back). Its file never writes it.
struct Recall {
	std::array<SlotMemory, 2> slots;
	std::array<synth::Pitch, 2> modulators{synth::FmState{}.modulators}; ///< FM's, while another engine plays
	bool modulator1_to_modulator0{false};                                ///< FM's, while another engine plays
	bool sync{false}; ///< the oscillators' sync, while the second is a sample (the file writes it in its pitch)
	bool operator==(const Recall&) const = default;
};

/// @brief The memory of a playing slot whose kind does not hold @p kept (the slot's other fields in the file's layout,
/// the kind's own at their defaults) and @p retrigger (a sample's).
[[nodiscard]] SlotMemory remember(const SourceState& kept, const synth::Retrigger& retrigger);

/// @brief The kept fields and retrigger @p memory holds for a playing slot, in the file's layout.
[[nodiscard]] SourceState recalled(const SlotMemory& memory, synth::Retrigger& retrigger);

/// @brief The memory of a slot parked while FM plays: @p kept is its whole source but for its pitch (the carrier's).
[[nodiscard]] SlotMemory remember_parked(const SourceState& kept);

/// @brief The whole source @p memory parks while FM plays, its pitch left at zero (the carrier's).
[[nodiscard]] SourceState recalled_parked(const SlotMemory& memory);

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
