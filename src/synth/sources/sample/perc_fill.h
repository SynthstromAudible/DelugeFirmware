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

#include <array>
#include <cstdint>
#include <optional>

namespace deluge::synth {

/// @file
/// @brief What one fill of a sample's perc store tells the time stretcher reading it: where the filled zone ends, which
/// perc-store chunks it touched (so the stretcher keeps them), and what becomes of its lookahead reservation. The fill
/// reports this, and the stretcher acts on it, so the fill never sees the stretcher.

/// @brief A chunk of a sample's perc store, by direction and index.
struct PercChunk {
	bool reversed;
	int32_t index;
	constexpr bool operator==(const PercChunk&) const = default;
};

/// @brief One perc fill's outcome.
struct PercFill {
	/// The filled zone's end, in frames; plus or minus INT32_MAX / 2 once the waveform's end is filled.
	int32_t filled_end = 0;
	/// The chunks the fill touched, in the order it first touched them. A fill reads at most 32 render blocks of frames
	/// and a chunk covers 4,194,304, so it touches at most two.
	std::array<PercChunk, 2> touched{};
	uint8_t touched_count = 0;
	/// What becomes of the stretcher's lookahead reservation of source audio.
	enum class Lookahead : uint8_t { UNCHANGED, LET_GO, MOVE_TO } lookahead = Lookahead::UNCHANGED;
	/// For MOVE_TO: the source byte the reservation moves to.
	int32_t lookahead_byte = 0;

	/// Notes @p chunk as touched, once.
	/// @return False where it is a third chunk, which cannot be kept (a fill should never touch one).
	[[nodiscard]] constexpr bool touch(PercChunk chunk) {
		for (uint8_t i = 0; i < touched_count; i++) {
			if (touched[i] == chunk) {
				return true;
			}
		}
		if (touched_count == touched.size()) {
			return false;
		}
		touched[touched_count++] = chunk;
		return true;
	}
};

/// @brief A fill to make: from a frame toward another, at most so many frames.
struct FillRange {
	int32_t from;
	int32_t to;
	int32_t max_frames;
};

/// @brief Where the stretcher's last perc fill started and ended, and its direction, so a block whose read the last
/// fill already reaches skips its fill (P4 of docs/dev/time_stretcher_contracts.md). The inputs are the voice's raw
/// words: the increment and ratio as Q24 words, the direction as +1 or -1.
struct PercFillMemo {
	int32_t start = 0;
	int32_t end = 0;
	int8_t direction = 0;

	/// @brief The fill a block wants, or none: only while stretching (the ratio not 1.0), and not when the last fill
	/// went this way, started at or before @p position_frame and reached the block's read's end.
	[[nodiscard]] constexpr std::optional<FillRange> wanted(int32_t position_frame, uint32_t increment, uint32_t ratio,
	                                                        int8_t play_direction, int32_t block_frames,
	                                                        bool writing_cache) const {
		if (ratio == (1u << 24)) {
			return std::nullopt;
		}
		const int32_t end_frame = position_frame + (static_cast<int32_t>(increment) >> 10) * play_direction;
		if (direction == play_direction && (position_frame - start) * play_direction >= 0
		    && (end - end_frame) * play_direction >= 0) {
			return std::nullopt;
		}
		return FillRange{.from = position_frame,
		                 .to = end_frame + block_frames * 4 * play_direction,
		                 .max_frames = block_frames * (writing_cache ? 32 : 6)};
	}
	/// @brief A fill from @p from went @p fill_direction and reported @p fill.
	constexpr void filled(int32_t from, const PercFill& fill, int8_t fill_direction) {
		start = from;
		end = fill.filled_end;
		direction = fill_direction;
	}
};

} // namespace deluge::synth
