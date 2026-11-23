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

#include "dsp/core/fixed_point.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace deluge::dsp::fixed::sample_read {

/// @file
/// @brief How the DSP components read a sample's frames without seeing the sample: runs of frames, each readable in
/// one direction up to a chunk or waveform boundary, from a source that answers whether a frame is resident.

/// @brief A play direction through a sample.
enum class Direction : int8_t { FORWARD = 1, REVERSE = -1 };

/// @brief Source frames per output frame, 2^24 = 1.0.
using Increment = UQ<24>;

/// @brief Readable frames from one frame on, in one direction, before a chunk or waveform boundary.
struct FrameRun {
	const std::byte* first; ///< The unmasked 32-bit read origin of the first frame's first sample.
	int32_t frames;         ///< Readable frames, the first included.
	int32_t stride;         ///< Bytes from one frame to the next in the run's direction: negative reversed.
	int8_t channels;
	int8_t byte_depth;
};

/// @brief A sample's frames as runs: nullopt where the frame is not resident.
template <class S>
concept frame_source = requires(const S& s, int32_t frame, Direction d) {
	{ s.run(frame, d) } -> std::same_as<std::optional<FrameRun>>;
	{ s.channels() } -> std::same_as<int32_t>;
	{ s.length_frames() } -> std::same_as<int32_t>;
};

/// @brief The unmasked 32-bit word ending at the top byte of frame @p k's @p channel sample in @p run: the sample in
/// its top bytes, the bytes before it below.
[[nodiscard]] inline int32_t frame_word(const FrameRun& run, int32_t k, int32_t channel) {
	int32_t word = 0;
	std::memcpy(&word, run.first + static_cast<std::ptrdiff_t>(k) * run.stride + channel * run.byte_depth, sizeof word);
	return word;
}

} // namespace deluge::dsp::fixed::sample_read
