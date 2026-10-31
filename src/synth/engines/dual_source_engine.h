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

#include "definitions_cxx.hpp" // kNumSources, kMaxNumVoicesUnison
#include "dsp-next/fixed/noise/cong.h"
#include "synth/engines/source_levels.h"
#include "synth/sources/source.h"
#include "synth/voice/unison_part.h"
#include "synth/voice/voice_config.h"
#include "synth/voice/voice_inputs.h"
#include <array>
#include <cstdint>
#include <span>

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

/// @brief One block of a dual-source engine: the voice's buffer, the unison parts and levels it lends, and scratch.
struct DualSourceBlock {
	int32_t* osc_buffer{}; ///< The voice's buffer, stereo pairs when the unison is spread
	int32_t num_samples{};
	std::span<UnisonPart> parts;
	SourceLevels* levels{};
	const VoiceConfig* cfg{};
	const VoiceBlock* voice_block{};
	std::array<int32_t*, 4> spare_buffers{}; ///< The render's work buffers; [0] is @ref osc_buffer
	std::span<int32_t> sync_scratch;         ///< A synced oscillator's work buffer
	dsp::fixed::Cong* noise{};               ///< The synth's noise stream
	DualSourceControls controls;
};

/// @brief What an engine's block left for the voice.
struct EngineRenderResult {
	bool did_stereo_buffer{}; ///< The engine rendered stereo pairs into the voice's buffer
	bool all_parts_ended{};   ///< Every unison part's sources ended (the voice unassigns, or fast-releases)
};

/// @brief The dual-source family: two source slots per unison part, the A→B osc sync, and the noise source.
/// Subtractive and ring-mod derive from it and keep their own render loops.
class DualSourceEngine {
protected:
	/// The noise source into @c block.osc_buffer across @p channels, or a cleared buffer when it is silent.
	/// @return whether noise was written.
	static bool render_noise(const DualSourceBlock& block, int32_t channels);
	/// Source 0's phase in each unison part at block start: the osc-sync master's.
	[[nodiscard]] static std::array<uint32_t, kMaxNumVoicesUnison> capture_sync(const DualSourceBlock& block);
	/// Each source's wavetable index slope for this block.
	[[nodiscard]] static std::array<int32_t, kNumSources> wave_index_increments(const DualSourceBlock& block);
	/// Stores the levels the next block starts from: @p amplitudes and this block's wavetable indexes.
	static void store_levels(const DualSourceBlock& block, const std::array<int32_t, kNumSources>* amplitudes);

	/// Renders source @p s in each unison part, adding into @c block.osc_buffer. @p increments_out, unless empty,
	/// takes each part's increment (0 when too high to render) for the parts osc sync follows; with @p advance_only
	/// the source only advances its phase. @return whether a unison part became inactive (its sample ended).
	[[nodiscard]] static bool render_source(const DualSourceBlock& block, int32_t s, bool stereo_buffer,
	                                        int32_t source_amplitude, int32_t amplitude_increment, bool do_osc_sync,
	                                        const uint32_t* sync_pos, const uint32_t* sync_increments,
	                                        std::span<uint32_t> increments_out, bool advance_only,
	                                        int32_t wave_index_increment);
	/// Whether every unison part's sources have ended, with no noise to keep the voice sounding.
	[[nodiscard]] static bool all_parts_inactive(const DualSourceBlock& block);

private:
	/// One unison part of one source through the @ref Source contract.
	static SourceRenderResult render_part(const DualSourceBlock& block, int32_t u, int32_t s, bool stereo_buffer,
	                                      int32_t source_amplitude, int32_t amplitude_increment,
	                                      uint32_t phase_increment, bool do_osc_sync, const uint32_t* sync_pos,
	                                      const uint32_t* sync_increments, int32_t wave_index_increment,
	                                      bool stereo_unison, int32_t amplitude_l, int32_t amplitude_r);
};

} // namespace deluge::synth
