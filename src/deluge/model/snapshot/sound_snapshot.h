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

#include "definitions_cxx.hpp"    // SynthMode, VoicePriority, kNumSources, kNumModulators, LFO_COUNT
#include "synth/modulation/lfo.h" // LFOConfig
#include "util/phase_increment_fine_tuner.h"

#include <array>
#include <cstdint>

namespace deluge::snapshot {

/// @brief The engine's derived read-view of a Sound's hot render fields.
///
/// The `deluge::project` DOM model (`SoundState`) is the authoritative source for save and load; this snapshot is
/// projected from it (`Sound::projectSnapshot()`) at the bind / doneReadingFromFile hooks and after each edit, and
/// the per-render / per-voice audio path reads it.
///
/// @note The model never references this type (dependency: engine to model). Unbound sounds (preview /
///       GlobalEffectable, with no SoundState) write the snapshot directly and never save.
struct SoundSnapshot {
	int16_t transpose{0}; ///< master semitone transpose (SoundState::transpose); read per-voice-start (Voice::render)
	int8_t cents{0};      ///< master fine tune (SoundState::cents), cents
	PhaseIncrementFineTuner fineTuner; ///< derived from cents: the master transpose's fine detune
	uint8_t numUnison{1};              ///< SoundState::num_unison
	int8_t unisonDetune{8};            ///< SoundState::unison_detune, cents across the spread
	uint8_t unisonStereoSpread{0};     ///< SoundState::unison_stereo_spread
	/// Derived from numUnison and unisonDetune: each unison part's detune. Written only when numUnison != 1; the
	/// render reads parts below numUnison.
	std::array<PhaseIncrementFineTuner, kMaxNumVoicesUnison> unisonDetuners{};
	/// Derived from numUnison and unisonStereoSpread: each unison part's pan offset, under the same rule.
	std::array<int32_t, kMaxNumVoicesUnison> unisonPan{};
	SynthMode synthMode{SynthMode::SUBTRACTIVE}; ///< SoundState::synth_mode; selects the subtractive/ring-mod/FM render
	bool oscillatorSync{false};                  ///< SoundState::oscillator_sync
	VoicePriority voicePriority{VoicePriority::MEDIUM}; ///< SoundState::voice_priority; ranks voices for stealing
	bool modulator1ToModulator0{false};                 ///< SoundState::modulator1_to_modulator0 (FM routing)
	std::array<int16_t, kNumModulators> modulatorTranspose{0, -12}; ///< SoundState::modulator_transpose, semitones
	std::array<int8_t, kNumModulators> modulatorCents{};            ///< SoundState::modulator_cents
	/// derived from modulatorCents: each FM modulator's fine detune
	std::array<PhaseIncrementFineTuner, kNumModulators> modulatorTransposers{};
	/// SoundState::osc_retrigger_phase; a phase as a fraction of 2^32, 0xFFFFFFFF = free-running (no retrigger)
	std::array<uint32_t, kNumSources> oscRetriggerPhase{0xFFFFFFFF, 0xFFFFFFFF};
	/// SoundState::modulator_retrigger_phase; same encoding as oscRetriggerPhase
	std::array<uint32_t, kNumModulators> modulatorRetriggerPhase{};
	/// SoundState::lfo_wave_type / lfo_sync_type / lfo_sync_level per LFO. The LFO phase state stays on the engine.
	std::array<LFOConfig, LFO_COUNT> lfoConfig{};

	bool operator==(const SoundSnapshot&) const = default;
};

} // namespace deluge::snapshot
