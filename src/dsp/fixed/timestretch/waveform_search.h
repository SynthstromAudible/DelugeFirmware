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

#include "dsp/fixed/timestretch/crossfade.h" // totalDifferenceAbs, totalChange

#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <tuple>

namespace deluge::dsp::fixed::timestretch {

/// @file
/// @brief The waveform search the time stretcher's and the pitch shifter's aligners share: where a new head's waveform
/// best matches the old one's, found by sliding the new head's window a frame at a time.

/// @brief A window's three back-to-back moving-average totals of its frames' folded high words.
using Averages = std::array<int32_t, 3>;

/// @brief How a search runs.
struct SearchShape {
	int32_t flips_to_stop;   ///< After how many sign flips, counted across both directions, it stops: 2 for the pitch
	                         ///< shifter, 4 for the stretcher.
	int32_t first_direction; ///< +1 or -1, in the reader's frame order.
	bool turns;              ///< Both ways, a forward first pass turning at once on a worse first step; false: one way.
	bool sub_sample;         ///< Place the match between frames at a sign flip (a shifted rate).
};

/// @brief Where the search settled: frames to move the new head by and the sub-sample phase to add, Q24, in the
/// reader's frame order; or abandoned where a frame could not be read.
struct SearchResult {
	int32_t offset = 0;
	int32_t phase = 0;
	bool abandoned = false;
};

/// @brief The frames a search reads, relative to the new window's first frame (0), in the reader's order.
template <class R>
concept search_reader = requires(const R& r, int32_t frame, int32_t direction, int32_t first_read) {
	/// The frame's channels' high words, summed; nullopt where it cannot be read, which abandons the search.
	{ r.folded(frame) } -> std::same_as<std::optional<int32_t>>;
	/// How many frames the window may slide in @p direction, its leading read starting at @p first_read.
	{ r.reach(direction, first_read) } -> std::same_as<int32_t>;
};

namespace search_detail {
inline constexpr int32_t kOneFrame = 1 << 24;
inline constexpr std::size_t kAverages = std::tuple_size_v<Averages>;

/// @brief One search, pass by pass.
template <search_reader R>
class Run {
public:
	Run(const R& reader, const SearchShape& shape, const Averages& old_totals, const Averages& new_totals,
	    int32_t length, int32_t size)
	    : reader_(&reader), shape_(shape), old_(old_totals), new_(new_totals), length_(length), size_(size),
	      best_difference_(totalDifferenceAbs(old_totals, new_totals)),
	      initial_change_(totalChange(old_totals, new_totals)), direction_(shape.first_direction),
	      directions_searched_(shape.turns ? 0 : 1) {}

	[[nodiscard]] SearchResult result() {
		while (true) {
			const Outcome outcome = pass();
			if (outcome == Outcome::ABANDON) {
				return {.offset = 0, .phase = 0, .abandoned = true};
			}
			if (outcome == Outcome::STOP) {
				break;
			}
			if (outcome == Outcome::TURN) {
				direction_ = -direction_; // turned at the first step: this direction is not yet searched
				continue;
			}
			if (++directions_searched_ >= 2) {
				break;
			}
			direction_ = -direction_;
		}
		return found_;
	}

private:
	enum class Outcome : uint8_t { STOP, NEXT_DIRECTION, TURN, ABANDON };
	using Reads = std::array<int32_t, kAverages + 1>;

	/// @brief One direction's slide from the window's start.
	Outcome pass() {
		int32_t last_change = initial_change_;
		Reads read{};
		read[0] = direction_ == -1 ? -1 : 0;
		for (std::size_t i = 0; i < kAverages; ++i) {
			read[i + 1] = read[i] + length_;
		}
		Averages running = new_;
		const int32_t here = std::min(size_, reader_->reach(direction_, direction_ == 1 ? read[kAverages] : read[0]));
		if (here <= 0) {
			return Outcome::NEXT_DIRECTION;
		}
		const int32_t end = here * direction_;
		int32_t offset = 0;
		do {
			if (!slide(read, running)) {
				return Outcome::ABANDON;
			}
			const int32_t difference = totalDifferenceAbs(old_, running);
			if (offset == 0 && direction_ == 1 && directions_searched_ == 0 && difference > best_difference_) {
				return Outcome::TURN;
			}
			const int32_t next = offset + direction_;
			const bool best_here = difference < best_difference_;
			if (best_here) {
				best_difference_ = difference;
				found_.offset = next;
			}
			const int32_t change = totalChange(old_, running);
			if ((change >= 0) != (last_change >= 0) && flipped(change, last_change, best_here, offset)) {
				return Outcome::STOP;
			}
			offset = next;
			last_change = change;
		} while (offset != end);
		return Outcome::NEXT_DIRECTION;
	}

	/// @brief Slides the window one frame: each frame read moves from one average's total to the next's. False where a
	/// frame cannot be read.
	bool slide(Reads& read, Averages& running) const {
		for (std::size_t i = 0; i < kAverages + 1; ++i) {
			const std::optional<int32_t> value = reader_->folded(read[i]);
			if (!value) {
				return false;
			}
			read[i] += direction_;
			if (i < kAverages) {
				running[i] -= *value * direction_;
			}
			if (i > 0) {
				running[i - 1] += *value * direction_;
			}
		}
		return true;
	}

	/// @brief A sign flip: places the match between frames if it is (or was just) the best; true once the flips are
	/// enough to stop.
	bool flipped(int32_t change, int32_t last_change, bool best_here, int32_t offset) {
		if (shape_.sub_sample && (best_here || offset == found_.offset)) {
			const auto here = static_cast<uint32_t>(std::abs(change));
			const auto last = static_cast<uint32_t>(std::abs(last_change));
			found_.phase =
			    static_cast<int32_t>((static_cast<uint64_t>(last) << 24) / static_cast<uint32_t>(last + here));
			if (direction_ == -1) {
				found_.phase = kOneFrame - found_.phase;
			}
			if (best_here != (direction_ == -1)) {
				--found_.offset;
			}
		}
		return ++sign_flips_ >= shape_.flips_to_stop;
	}

	const R* reader_;
	SearchShape shape_;
	Averages old_;
	Averages new_;
	int32_t length_;
	int32_t size_;
	int32_t best_difference_;
	int32_t initial_change_;
	int32_t direction_;
	int32_t directions_searched_;
	int32_t sign_flips_ = 0;
	SearchResult found_{};
};
} // namespace search_detail

/// @brief Slides a window of @p length-frame averages up to @p size frames each way (as @p shape says) from its start,
/// keeping running totals, and settles on the offset whose totals differ least from @p old_totals (A4, A5 of
/// docs/dev/time_stretcher_contracts.md). The best must be strictly better to move. Where the totals' summed change
/// from the old flips sign, the match may be placed between the two frames by the changes' sizes.
template <search_reader R>
[[nodiscard]] SearchResult search(const R& reader, const SearchShape& shape, const Averages& old_totals,
                                  const Averages& new_totals, int32_t length, int32_t size) {
	return search_detail::Run<R>(reader, shape, old_totals, new_totals, length, size).result();
}

} // namespace deluge::dsp::fixed::timestretch
