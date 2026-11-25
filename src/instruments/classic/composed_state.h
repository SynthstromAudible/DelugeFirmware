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
#include "instruments/classic/words.h"
#include "synth/engines/engine_state.h"
#include "synth/modulation/envelope_state.h"
#include "synth/modulation/lfo_state.h"
#include "synth/voice/voicing_state.h"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace deluge::instruments::classic {

/// @brief What a Deluge synth sound plays, composed of its components' states: its voicing, the engine it renders
/// with (whose oscillators hold only what their kind plays), its LFOs and its effects.
struct ComposedState {
	synth::VoicingState voicing;
	synth::EngineState engine{synth::SubtractiveState{}};
	std::array<synth::LfoState, 2> instrument_lfos;            ///< LFO1, LFO3: one per sound
	std::array<synth::LfoState, 2> voice_lfos;                 ///< LFO2, LFO4: one per voice
	std::array<synth::EnvelopeState, kNumEnvelopes> envelopes; ///< each envelope's mode, curve and follow input
	effects::EffectStates fx;
	bool operator==(const ComposedState&) const = default;
};

/// @brief What one oscillator slot remembers of the kinds it is not playing, so a switch back loses nothing.
struct SlotMemory {
	/// While FM plays: the slot's kind with its parts, as it plays again when FM goes (its pitch is the carrier's)
	std::optional<synth::SlotState> parked;
	synth::SamplePlayback playback;         ///< how a sample plays, while the slot is another kind
	std::vector<synth::SourceRange> ranges; ///< the slot's ranges, while its kind plays none
	std::optional<synth::DxPatch> patch;    ///< a DX7 voice, while the slot is another kind
	/// While a sample: the pitch the other kinds play (a sample oscillator has none of its own; its ranges carry
	/// theirs)
	synth::Pitch sample_pitch;
	bool operator==(const SlotMemory&) const = default;
};

/// @brief A sound's memory of what its current engine and oscillator kinds cannot hold, so a switch back loses nothing
/// (a sample kept through a sine and back; FM's modulators through subtractive and back). Its file never writes it.
struct Recall {
	std::array<SlotMemory, kSourceCount> slots;
	std::array<synth::Pitch, 2> modulators{synth::FmState{}.modulators}; ///< FM's, while another engine plays
	bool modulator1_to_modulator0{false};                                ///< FM's, while another engine plays
	bool sync{false}; ///< the oscillators' sync, while the second is a sample (the file writes it in its pitch)
	bool operator==(const Recall&) const = default;
};

/// @brief The two oscillator slots a subtractive or ring-mod sound plays; null for FM, whose carriers have only a
/// pitch.
[[nodiscard]] const synth::DualSourceState* dual_of(const ComposedState& plays) noexcept;

/// @copydoc dual_of(const ComposedState&)
[[nodiscard]] synth::DualSourceState* dual_of(ComposedState& plays) noexcept;

/// @brief The oscillator type a slot's kind is.
[[nodiscard]] OscType osc_type_of(const synth::SlotState& kind) noexcept;

/// @brief The retrigger a file's 32-bit phase word means; kFreeRunningPhase means none.
[[nodiscard]] synth::Retrigger retrigger_of_word(uint32_t phase);

/// @brief The file's 32-bit phase word for @p retrigger: the phase a note starts at, or kFreeRunningPhase where the
/// oscillator runs free.
[[nodiscard]] uint32_t phase_word(const synth::Retrigger& retrigger) noexcept;

} // namespace deluge::instruments::classic
