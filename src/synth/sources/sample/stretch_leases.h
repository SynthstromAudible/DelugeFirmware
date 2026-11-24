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

#include "dsp/fixed/sample_read/frame_source.h"
#include "dsp/fixed/timestretch/hop_tuning.h"
#include "synth/sources/sample/perc_fill.h"
#include "synth/sources/sample/sample_stretch_port.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace deluge::synth {

/// @file
/// @brief The residency the time stretcher holds through its port (section 5 of docs/dev/time_stretcher_design.md):
/// three reservations and the two perc chunks it used last, and where the hop-back reservation starts.

/// @brief The stretcher's lease handles. Every handle is the port's to fill and empty; the stretcher gives each back
/// at `end`.
struct StretchLeases {
	Reservation hop_back;                         ///< From the earliest frame the next hop could start.
	Reservation hop_retry;                        ///< A hop's start chunk, not resident, until the retry starts.
	Reservation lookahead;                        ///< Where the perc fill stopped reading.
	std::array<std::optional<PercChunk>, 2> perc; ///< The two perc chunks used last, the older first.
};

/// @brief The hop-back reservation's first frame (section 5's reach): the widest width a hop can pick (its jitter can
/// nearly double the minimum), repitched; half a 45 Hz period and a frame back from the position, more when slowed
/// down; clamped to the waveform. The ratio is read unsigned (decision 3).
[[nodiscard]] constexpr int32_t hop_back_frame(int64_t position_frames, dsp::fixed::timestretch::Ratio ratio,
                                               dsp::fixed::sample_read::Increment increment,
                                               const dsp::fixed::timestretch::HopShape& shape, int32_t sample_rate,
                                               dsp::fixed::sample_read::Direction direction, int32_t length_frames) {
	// The increment's and the ratio's Q24 words, multiplied in 64 bits as the legacy's reach does
	int32_t widest = std::max(shape.max_width, shape.min_width << 1);
	widest = static_cast<int32_t>((static_cast<uint64_t>(static_cast<uint32_t>(widest)) * increment.raw()) >> 24);
	int64_t back = ((sample_rate / 45) >> 1) + 1;
	if (ratio.raw() < (1u << 24)) {
		back += (static_cast<int64_t>(widest) * ((1u << 24) - ratio.raw())) >> 25;
	}
	const int64_t at = position_frames - back * static_cast<int64_t>(direction);
	return static_cast<int32_t>(std::clamp<int64_t>(at, 0, length_frames - 1));
}

} // namespace deluge::synth
