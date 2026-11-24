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
#include "synth/sources/sample/stretch_context.h"

#include <algorithm>
#include <cstdint>

namespace deluge::synth {

/// @file
/// @brief When the time stretcher hops: the output frames to its next hop, and how often a due hop has waited for the
/// audio routine's budget (S1 to S3 of docs/dev/time_stretcher_contracts.md).

/// @brief The frames a fudge's new head starts into the pre-margin (S2): the output frames to the loop, as source
/// frames at @p increment, rounded. Quirk: the crossfade then lasts that many output frames.
[[nodiscard]] constexpr int32_t fudge_into(int32_t frames_to_loop, dsp::fixed::sample_read::Increment increment) {
	// The increment's word: S2 multiplies it into a 64-bit frame count and rounds the Q24 product, as the legacy did
	if (increment.raw() == (1u << 24)) {
		return frames_to_loop;
	}
	return static_cast<int32_t>(
	    ((static_cast<uint64_t>(static_cast<uint32_t>(frames_to_loop)) * increment.raw()) + (1u << 23)) >> 24);
}

/// @brief The output frames to the next hop, and how many times a due hop was passed over.
class HopClock {
public:
	/// @brief What a segment's check found: no hop due, a hop to take now, or a due hop the budget makes wait.
	enum class Turn : uint8_t { NOT_DUE, HOP, WAIT };
	/// @brief A check's turn and the segment's frames. For HOP the caller hops, which replaces the clock, and the
	/// segment runs to the new clock's hop (frames()); its frames here are 0.
	struct Segment {
		Turn turn;
		int32_t frames;
	};

	/// @brief A normal start's first hop (S1): 200 output frames, moved by -32 to 31 by the draw's top byte.
	[[nodiscard]] static constexpr HopClock first_hop(uint32_t draw) {
		return after(200 + (static_cast<int8_t>(draw >> 24) >> 2));
	}
	/// @brief A fudge's clock (S2): its hop never comes.
	[[nodiscard]] static constexpr HopClock never() { return after(INT32_MAX); }
	/// @brief A failed hop's clock (S4): try again in 500 frames.
	[[nodiscard]] static constexpr HopClock retry() { return after(500); }
	/// @brief A hop's clock: the next hop @p hop_frames output frames on.
	[[nodiscard]] static constexpr HopClock after(int32_t hop_frames) { return at(hop_frames, 0); }
	/// @brief A clock @p to_hop frames from its hop that has passed a due hop over @p passed_over times.
	[[nodiscard]] static constexpr HopClock at(int32_t to_hop, uint8_t passed_over) {
		HopClock clock;
		clock.to_hop_ = to_hop;
		clock.passed_over_ = passed_over;
		return clock;
	}

	/// @brief The check before a segment, made only while the newer head reads (S3). A due hop the budget allows is
	/// taken (HOP: the budget spends one and the clock's count clears); a due one it does not allow waits (WAIT: the
	/// count rises). Allowed, the segment runs to the hop or the block's @p rest, whichever is first; not allowed,
	/// the rest of the block, so the clock runs on past 0.
	[[nodiscard]] constexpr Segment segment(HopBudget& budget, int32_t rest) {
		const bool allowed = budget.allows(passed_over_);
		if (to_hop_ <= 0) {
			if (allowed) {
				budget.spend();
				passed_over_ = 0;
				return {.turn = Turn::HOP, .frames = 0};
			}
			passed_over_++;
			return {.turn = Turn::WAIT, .frames = rest};
		}
		return {.turn = Turn::NOT_DUE, .frames = allowed ? frames(rest) : rest};
	}
	/// @brief The frames to the hop, or @p rest if that is sooner.
	[[nodiscard]] constexpr int32_t frames(int32_t rest) const { return std::min(rest, to_hop_); }
	/// @brief After a segment of @p frames output frames.
	constexpr void advance(int32_t frames) { to_hop_ -= frames; }
	[[nodiscard]] constexpr int32_t to_hop() const { return to_hop_; }
	[[nodiscard]] constexpr uint8_t passed_over() const { return passed_over_; }

private:
	int32_t to_hop_ = 0;
	uint8_t passed_over_ = 0; ///< Wraps, as the legacy's byte does.
};

} // namespace deluge::synth
