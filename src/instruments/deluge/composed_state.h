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

/// @brief What a sound keeps of what it does not play, for a switch back: each oscillator's settings its current kind
/// does not hold (the fields its state holds are left at their defaults here), FM's modulators while another engine
/// plays, and the oscillators' sync while FM plays.
struct Recall {
	std::array<SourceState, 2> slots;
	std::array<synth::Pitch, 2> modulators{synth::FmState{}.modulators};
	bool modulator1_to_modulator0{false};
	bool sync{false};
	bool operator==(const Recall&) const = default;
};

/// @brief The flat record @p flat as the state it plays and what it keeps for a switch back.
[[nodiscard]] std::pair<ComposedState, Recall> compose(const SoundState& flat);

/// @brief The flat record a composed state and its recall are: `flatten(compose(f)) == f` for every record a file
/// decodes to or a rule makes.
[[nodiscard]] SoundState flatten(const ComposedState& state, const Recall& recall);

} // namespace deluge::instrument
