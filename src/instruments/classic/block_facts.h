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

#include "effects/words.h"
#include "synth/voice/block_values.h"

/// @file
/// @brief What a Deluge sound reads of itself and its params for its voices' block, gathered by the host.

namespace deluge::instruments::classic {

/// @brief A sound's own facts for one block of its voices: its filters' settings and smoothed values, what its params
/// wire and hold, its synced LFO rates, its expression, and what its sources can do now. DelugeSynth::begin_block turns
/// them into the block's values; each field's type is the block value it becomes, or feeds.
struct SoundBlockFacts {
	using Values = synth::BlockValues;

	bool has_filters{};                                     ///< Either filter is set to run
	effects::FilterMode lpf_mode{effects::FilterMode::OFF}; ///< The low-pass's mode as set
	effects::FilterMode hpf_mode{effects::FilterMode::OFF}; ///< The high-pass's mode as set
	effects::FilterRoute filter_route{};                    ///< How the two filters join
	int32_t lpf_morph{};                                    ///< The smoothed low-pass morph
	int32_t lpf_freq{};                                     ///< The smoothed low-pass frequency
	int32_t hpf_morph{};                                    ///< The smoothed high-pass morph
	int32_t hpf_freq{};                                     ///< The smoothed high-pass frequency
	bool lpf_modulated{};                                   ///< A cable reaches the low-pass frequency
	bool hpf_modulated{};                                   ///< A cable reaches the high-pass frequency
	bool noise_can_sound{};                                 ///< The noise volume can rise above silence
	uint32_t local_wired{};                                 ///< A bit per local patch source wired to anything
	decltype(Values::bend_ranges) bend_ranges{};            ///< The MPE bend ranges, in semitones
	int32_t portamento{};                                   ///< The portamento param as set
	/// Each local LFO's phase increment when synced to the transport (0, unused, when it runs free).
	decltype(Values::lfo2_synced_phase_increment) lfo2_synced_phase_increment{};
	decltype(Values::lfo4_synced_phase_increment) lfo4_synced_phase_increment{};
	/// The MPE dimensions whose sound-level value changed this block
	decltype(Values::expression_sources_changed_at_synth_level) expression_sources_changed;
	decltype(Values::monophonic_expression_values) monophonic_expression_values{}; ///< The sound's own MPE values
	uint8_t clipping_amount{};                                                     ///< The output stage's saturation
	bool doing_osc_sync{};       ///< Source 1 is hard-synced to source 0
	bool dx7_active_currently{}; ///< DX7 mode: the DX7 sounds at its current level
	bool dx7_patch_changed{};    ///< DX7 mode: the sound's DX7 patch changed since the last block
	decltype(Values::global_source_values) global_source_values{};       ///< The sound's global patch-source values
	bool note_tails_allowed{};                                           ///< Note-offs release the voice
	decltype(Values::source_ever_active) source_ever_active{};           ///< Each source can sound at all
	decltype(Values::source_active_currently) source_active_currently{}; ///< Each source sounds at its current levels
};

} // namespace deluge::instruments::classic
