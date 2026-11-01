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

#include "definitions_cxx.hpp" // kNumSources, kNumModulators
#include <array>
#include <cstdint>

namespace deluge::synth {

/// @brief What the voice derives each block for a dual-source engine from its params; the engine reads no param plane.
struct DualSourceControls {
	std::array<int32_t, kNumSources> volume{};       ///< Each source's volume param
	std::array<int32_t, kNumSources> pitch_adjust{}; ///< Each source's pitch adjust (2^24 is unity)
	std::array<int32_t, kNumSources> phase_width{};  ///< Each source's pulse width param (a DX7's amplitude modulation)
	std::array<int32_t, kNumSources> wave_index{};   ///< Each source's wavetable index param
	/// Per source: a source that can change during the note modulates a sample's pitch, so it is not cached.
	std::array<bool, kNumSources> pitch_mod_disallows_cache{};
	int32_t noise_volume{};         ///< The noise volume param
	int32_t env0_attack{};          ///< The amplitude envelope's attack param (a slow attack holds the levels still)
	int32_t filter_gain{};          ///< The gain the filters' configuration asks of what goes in
	int32_t overall_pitch_adjust{}; ///< The voice's pitch adjust, bend and portamento (2^24 is unity)
	uint32_t porta_envelope_pos{};  ///< The voice's portamento position; kNoPorta when not gliding
	int32_t priority_rating{};      ///< The voice's cull rating
	int32_t note_code{};            ///< The note after arpeggiation
	bool first_render{};            ///< The voice's first block since its note started
	bool stereo_unison{};           ///< The unison parts are spread across the stereo field
};

/// @brief What the voice derives each block for FM from its params; the engine reads no param plane.
struct FmControls {
	std::array<int32_t, kNumSources> carrier_volume{};       ///< Each carrier's (source's) volume param
	std::array<int32_t, kNumSources> carrier_pitch_adjust{}; ///< Each carrier's pitch adjust (2^24 is unity)
	std::array<int32_t, kNumSources> carrier_feedback{};     ///< Each carrier's feedback param
	std::array<int32_t, kNumSources> wave_index{};           ///< Each source's wavetable index (kept for the levels)
	std::array<int32_t, kNumModulators> modulator_volume{};  ///< Each modulator's volume param
	std::array<int32_t, kNumModulators> modulator_pitch_adjust{}; ///< Each modulator's pitch adjust (2^24 is unity)
	std::array<int32_t, kNumModulators> modulator_feedback{};     ///< Each modulator's feedback param
	int32_t overall_osc_amplitude{}; ///< The voice's amplitude, compensated for unison, worked into the carriers'
	int32_t env0_attack{};           ///< The amplitude envelope's attack param (a slow attack holds the levels still)
	int32_t filter_gain{};           ///< The gain the filters' configuration asks of what goes in
	int32_t overall_pitch_adjust{};  ///< The voice's pitch adjust, bend and portamento (2^24 is unity)
	bool first_render{};             ///< The voice's first block since its note started
	bool stereo_unison{};            ///< The unison parts are spread across the stereo field
};

} // namespace deluge::synth
