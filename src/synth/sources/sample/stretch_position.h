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

#include <cstdint>

namespace deluge::synth {

/// @file
/// @brief Where the time stretcher is in its source and when it leaves it: the position and its advance (Q1, Q2),
/// where a stretch starts (Q3, S6), the loop end ahead of a block's part (Q4) and when stretching may end (E1) of
/// docs/dev/time_stretcher_contracts.md.

using dsp::fixed::sample_read::Direction;
using dsp::fixed::sample_read::Increment;
using dsp::fixed::timestretch::Ratio;

/// @brief The source frames one output frame moves: the ratio times the increment, Q24 (Q1).
[[nodiscard]] constexpr uint64_t combined_increment(Ratio ratio, Increment increment) {
	// The two Q24 words multiply in 64 bits, as the legacy's position arithmetic does
	return (static_cast<uint64_t>(ratio.raw()) * increment.raw()) >> 24;
}

/// @brief The position in the source: Q24 frames from the data's start, signed, so it may lie before the data.
class SourcePosition {
public:
	constexpr explicit SourcePosition(int64_t q24) : q24_(q24) {}

	/// @brief A stretch's start at @p play_byte (Q3): its frame, rounding toward minus infinity.
	[[nodiscard]] static constexpr SourcePosition from_play_byte(int32_t play_byte, int32_t data_start,
	                                                             int32_t bytes_per_frame) {
		const int32_t bytes = play_byte - data_start;
		const int32_t frame =
		    bytes >= 0 ? bytes / bytes_per_frame : -((-bytes + bytes_per_frame - 1) / bytes_per_frame);
		return SourcePosition(static_cast<int64_t>(frame) * (1 << 24));
	}
	/// @brief A synced hop's position (S6): the start marker's frame plus @p frames_in played since, in the direction.
	/// The start marker's byte is read unsigned against the data's start, and the frame count wraps as 64-bit unsigned.
	[[nodiscard]] static constexpr SourcePosition
	synced(int32_t start_byte, int32_t data_start, int32_t bytes_per_frame, int64_t frames_in, Direction direction) {
		const uint64_t start = static_cast<uint32_t>(start_byte - data_start) / static_cast<uint8_t>(bytes_per_frame);
		const uint64_t frames =
		    start + (static_cast<uint64_t>(frames_in) * static_cast<uint64_t>(static_cast<int64_t>(direction)));
		return SourcePosition(static_cast<int64_t>(frames << 24));
	}

	/// @brief After @p frames output frames at @p combined (Q1).
	constexpr void advance(uint64_t combined, int32_t frames, Direction direction) {
		q24_ += static_cast<int64_t>(combined) * frames * static_cast<int64_t>(direction);
	}
	/// @brief The whole frame (Q2): rounded down forwards, up reversed.
	[[nodiscard]] constexpr int32_t frame(Direction direction) const {
		return static_cast<int32_t>(direction == Direction::FORWARD ? (q24_ >> 24) : ((q24_ + ((1 << 24) - 1)) >> 24));
	}
	[[nodiscard]] constexpr int64_t raw() const { return q24_; }

private:
	int64_t q24_;
};

/// @brief The loop facts Q4 reads, all bytes from the file's start.
struct LoopBytes {
	int32_t data_start;
	int32_t bytes_per_frame;
	int32_t loop_end_byte; ///< The guide's end or loop-end byte.
	int32_t restart_byte;  ///< The guide's start byte after a loop.
	bool looping;
};

/// @brief One turn of Q4's check: read the part as it is, read it shortened to the loop end, restart the position
/// before the loop end (the caller turns again, at most kMaxLoopTurns times in a block), or end the note.
struct LoopStep {
	enum class Kind : uint8_t { READ, SHORTEN, RESTART, END } kind;
	int32_t frames;         ///< READ and SHORTEN: the part's frames.
	SourcePosition restart; ///< RESTART: the position overshot past the restart frame.
};

inline constexpr int32_t kMaxLoopTurns = 1024;

/// @brief Q4 over a block's part of @p frames output frames from @p position.
[[nodiscard]] constexpr LoopStep loop_step(SourcePosition position, uint64_t combined, int32_t frames,
                                           Direction direction, const LoopBytes& bytes) {
	const auto dir = static_cast<int32_t>(direction);
	int64_t after =
	    position.raw() + static_cast<int64_t>(combined * static_cast<uint64_t>(static_cast<int64_t>(frames))) * dir;
	if (direction == Direction::REVERSE) {
		after += (1 << 24) - 1;
	}
	// The byte arithmetic is the legacy's 32-bit word arithmetic
	const auto byte_after = static_cast<int32_t>(static_cast<uint32_t>(static_cast<int32_t>(after >> 24))
	                                                 * static_cast<uint32_t>(bytes.bytes_per_frame)
	                                             + static_cast<uint32_t>(bytes.data_start));
	if ((byte_after - bytes.loop_end_byte) * dir <= 0) {
		return {.kind = LoopStep::Kind::READ, .frames = frames, .restart = position};
	}
	const int32_t relative = bytes.loop_end_byte - bytes.data_start;
	const int32_t end_frame =
	    relative >= 0
	        ? static_cast<int32_t>(static_cast<uint32_t>(relative) / static_cast<uint8_t>(bytes.bytes_per_frame))
	        : -1;
	const int64_t left = (static_cast<int64_t>(end_frame) << 24) - position.raw();
	const int64_t left_ahead = left * dir;
	if (left_ahead < static_cast<int64_t>(combined)) {
		if (!bytes.looping) {
			return {.kind = LoopStep::Kind::END, .frames = 0, .restart = position};
		}
		const auto restart_frame = static_cast<int32_t>(static_cast<uint32_t>(bytes.restart_byte - bytes.data_start)
		                                                / static_cast<uint8_t>(bytes.bytes_per_frame));
		return {.kind = LoopStep::Kind::RESTART,
		        .frames = frames,
		        .restart = SourcePosition((static_cast<int64_t>(restart_frame) << 24) - left)};
	}
	const auto to_end = static_cast<int32_t>(static_cast<uint64_t>(left_ahead) / combined);
	return {.kind = LoopStep::Kind::SHORTEN, .frames = to_end < frames ? to_end : frames, .restart = position};
}

/// @brief What E1 reads at a block's start.
struct ExitFacts {
	bool begun_this_block; ///< Every begin skips the check.
	bool fudge;
	Ratio ratio;
	bool cursors_meet; ///< The newer head's cursor equals the older's, reading or stopped.
	bool synced;
	int32_t sync_lag; ///< Frames the voice lags its sync.
};

/// @brief Whether stretching may end now (E1).
[[nodiscard]] constexpr bool may_end(const ExitFacts& f) {
	// The ratio's word compared with 1.0's
	return !f.begun_this_block && !f.fudge && f.ratio.raw() == (1u << 24) && f.cursors_meet
	       && (!f.synced || f.sync_lag == 0);
}

} // namespace deluge::synth
