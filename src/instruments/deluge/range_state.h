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

#include "synth/sources/source_state.h"

#include <cstdint>
#include <string>

namespace deluge::instrument {

/// @brief The top note of a source's last range, when a file names none: it covers every note above the range before
/// it.
inline constexpr int16_t kOpenTopNote = 32767;

/// @brief A retrigger phase word that means free-running: the oscillator starts each note where it is.
inline constexpr uint32_t kFreeRunningPhase = 0xFFFFFFFF;

/// @brief The end a range's zone has before a file sets it: a file loaded into it plays to its own end.
inline constexpr uint64_t kUnsetEndPos = 9999999;

/// @brief One sample or wavetable key-split range of a source, ordered by `top_note` (the highest MIDI note it covers).
/// A wavetable range uses only `top_note` and `file_path`.
struct RangeState {
	int16_t top_note{0};
	std::string file_path;      ///< the sample's path on the card
	uint64_t start_pos{0};      ///< in samples
	uint64_t end_pos{0};        ///< in samples
	uint32_t loop_start_pos{0}; ///< in samples; 0 = none
	uint32_t loop_end_pos{0};   ///< in samples; 0 = none
	int16_t transpose{0};       ///< semitones
	int8_t cents{0};
	bool operator==(const RangeState&) const = default;
};

/// @brief The DX7 patch a DX7 source carries: the 156 voice bytes and two extensions.
using DxPatchState = synth::DxPatch;

/// @brief A sample range from the file's layout.
[[nodiscard]] inline synth::SampleRange sample_range_of(const RangeState& r) {
	return {
	    .top_note = r.top_note,
	    .file_path = r.file_path,
	    .zone = {.start = r.start_pos, .end = r.end_pos, .loop_start = r.loop_start_pos, .loop_end = r.loop_end_pos},
	    .tuning = {.transpose = r.transpose, .cents = r.cents}};
}

/// @brief A sample range in the file's layout.
[[nodiscard]] inline RangeState range_state_of(const synth::SampleRange& r) {
	return {.top_note = r.top_note,
	        .file_path = r.file_path,
	        .start_pos = r.zone.start,
	        .end_pos = r.zone.end,
	        .loop_start_pos = r.zone.loop_start,
	        .loop_end_pos = r.zone.loop_end,
	        .transpose = r.tuning.transpose,
	        .cents = r.tuning.cents};
}

} // namespace deluge::instrument
