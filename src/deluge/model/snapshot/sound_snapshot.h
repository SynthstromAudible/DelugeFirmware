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

#include "definitions_cxx.hpp" // SynthMode, VoicePriority, kNumSources, kNumModulators, LFO_COUNT
#include "synth/lfo.h"         // LFOConfig

#include <array>
#include <cstdint>

namespace deluge::snapshot {

/// The engine's derived read-view of a Sound's HOT render fields (project-model migration M4b). The `deluge::project`
/// DOM model (`DocSound`) is the authoritative source for save AND load; this snapshot is PROJECTED from it
/// (`Sound::projectSnapshot()`) at the bind / doneReadingFromFile hooks and after each edit, and the per-render /
/// per-voice audio path reads it — the north-star's "engine plays derived Snapshots" made real for Sound. The model
/// never references this type (net dependency: engine→model). It gathers what were scattered native cache fields on
/// `Sound` into one cohesive read-view; the fields grow slice-by-slice as M4b flips them. Unbound sounds (preview /
/// GlobalEffectable, with no DocSound) write the snapshot directly and never save.
struct SoundSnapshot {
	int16_t transpose{0}; ///< master semitone transpose (DocSound::transpose); read per-voice-start (Voice::render)
	SynthMode synthMode{SynthMode::SUBTRACTIVE}; ///< DocSound::synth_mode; selects the subtractive/ring-mod/FM render
	bool oscillatorSync{false};                  ///< DocSound::oscillator_sync
	VoicePriority voicePriority{VoicePriority::MEDIUM}; ///< DocSound::voice_priority; ranks voices for stealing
	bool modulator1ToModulator0{false};                 ///< DocSound::modulator1_to_modulator0 (FM routing)
	std::array<int16_t, kNumModulators> modulatorTranspose{0, -12}; ///< DocSound::modulator_transpose, semitones
	/// DocSound::osc_retrigger_phase; a phase as a fraction of 2^32, 0xFFFFFFFF = free-running (no retrigger)
	std::array<uint32_t, kNumSources> oscRetriggerPhase{0xFFFFFFFF, 0xFFFFFFFF};
	/// DocSound::modulator_retrigger_phase; same encoding as oscRetriggerPhase
	std::array<uint32_t, kNumModulators> modulatorRetriggerPhase{};
	/// DocSound::lfo_wave_type / lfo_sync_type / lfo_sync_level per LFO. The LFO phase state stays on the engine.
	std::array<LFOConfig, LFO_COUNT> lfoConfig{};
};

} // namespace deluge::snapshot
