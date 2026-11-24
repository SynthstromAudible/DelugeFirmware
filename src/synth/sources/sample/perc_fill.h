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

} // namespace deluge::synth
