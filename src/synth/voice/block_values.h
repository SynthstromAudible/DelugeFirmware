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

#include "definitions_cxx.hpp" // OscType, SampleRepeatMode, InterpolationMode, kNumExpressionDimensions, kFirstLocalSource
#include "dsp/fixed/filter/filter_modes.h" // LowPassSlot, HighPassSlot, FilterRoute
#include "util/misc.h"                     // util::to_underlying
#include <array>
#include <bitset>
#include <cstdint>

namespace deluge::synth {

/// @brief What changes block by block for a sound's voices: the host's facts, what the transport and the sound's
/// params make of this block, its expression, and the slot settings the runtime holds (an FM sound's oscillators play
/// sines whatever its record holds, and a preview has no record). Each voice's render reads it beside the sound's
/// config.
struct BlockValues {
	deluge::dsp::fixed::LowPassSlot low_pass{}; ///< What the low-pass slot runs: OFF when the sound needs no low-pass
	deluge::dsp::fixed::HighPassSlot
	    high_pass{}; ///< What the high-pass slot runs: OFF when the sound needs no high-pass
	deluge::dsp::fixed::FilterRoute filter_route{}; ///< How the two filters join
	/// How far behind the engine's render is running, 0 when on time: the render takes cheaper paths as it grows.
	int32_t cpu_direness{};

	/// Each local LFO's phase increment when synced to the transport (0, unused, when it runs free).
	dsp::UQ<32> lfo2_synced_phase_increment{};
	dsp::UQ<32> lfo4_synced_phase_increment{};

	bool has_filters{}; ///< The sound's filters can run at all

	/// The MPE dimensions whose sound-level value changed this block
	std::bitset<kNumExpressionDimensions> expression_sources_changed_at_synth_level;
	std::array<int32_t, kNumExpressionDimensions> monophonic_expression_values{}; ///< The sound's own MPE values

	uint8_t clipping_amount{}; ///< The output stage's saturation amount, 0 for none

	bool doing_osc_sync{};             ///< Source 1 is hard-synced to source 0
	bool engine_rendering_in_stereo{}; ///< The engine renders in stereo (headphones), so voices may pan

	bool dx7_active_currently{}; ///< DX7 mode: the DX7 sounds this block (its level is not off)
	bool dx7_patch_changed{};    ///< DX7 mode: the sound's DX7 patch changed since the last block

	/// The sound's global patch-source values, copied into the voice's own before its patching. The sound's patching
	/// runs first, so they are settled for the block.
	std::array<int32_t, util::to_underlying(kFirstLocalSource)> global_source_values{};

	/// Note-offs release the voice. When false a note-off is only remembered, and taken once this is true again.
	bool note_tails_allowed{};

	/// Each source can sound at all at the sound's settings.
	std::array<bool, kNumSources> source_ever_active{};

	/// The noise source can sound, which keeps a voice whose oscillators have all ended alive.
	bool noise_volume_contains_something{};

	/// Bit per local patch source that is patched to anything: the local envelopes and LFOs render only then.
	uint32_t local_sources_patched_to_anything{};

	/// The MPE bend ranges, in semitones: the main one, then the per-finger one.
	std::array<uint8_t, 2> bend_ranges{};
	/// The portamento param as set (the render turns it into a glide rate).
	int32_t portamento{};

	/// Each source sounds at the sound's current levels: the subtractive engine renders only those.
	std::array<bool, kNumSources> source_active_currently{};
};

} // namespace deluge::synth
