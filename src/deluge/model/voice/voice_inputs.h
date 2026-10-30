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

#include "definitions_cxx.hpp" // kNumSources
#include "dsp-next/fixed/wavetable/wave_table_data.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::synth {

/// @brief The voice-state clock the cull order reads: each envelope stage a voice enters takes the next tick, so of two
/// voices in one stage the one that entered it first is culled first.
struct VoiceClock {
	uint32_t& next; ///< The next tick to hand out, shared by every voice.

	/// @return The next tick, advancing the clock.
	[[nodiscard]] uint32_t tick() const { return next++; }
};

/// @brief What one voice of a sound renders from, beyond its sound's config: what differs between the sound's voices,
/// or changes from one voice's render to the next.
struct VoiceBlock {
	/// Each WAVETABLE source's band data (empty for any other source, or a source with no table loaded).
	std::array<dsp::fixed::WaveTableData, kNumSources> wave_tables{};
	/// Per source: a play-once sample may release itself before its end. False when no sample is playing, or while its
	/// loop end is still obeyed (until the note-off).
	std::array<bool, kNumSources> sample_can_auto_release{};
	/// Per source: it renders in stereo (the engine renders in stereo, and the source spreads its unison or plays a
	/// stereo sample or input).
	std::array<bool, kNumSources> source_in_stereo{};
	size_t num_active_voices{}; ///< The sound's voice count, which the cull order ranks by.
	VoiceClock clock;
};

} // namespace deluge::synth
