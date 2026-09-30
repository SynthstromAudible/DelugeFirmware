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

#include <cstdint>

namespace deluge::io {

/// How many caller ticks a caller may spend waiting on a channel before it gives up on it.
///
/// A channel whose driver never runs answers PENDING forever, or refuses forever, which is neither an answer nor a
/// failure. Without a bound, a caller would poll until the end of time. Six hundred caller ticks is several seconds at
/// the UI's tick rate: far beyond any real operation, far short of forever.
inline constexpr int32_t kChannelStallTicks = 600;

/// Counts the caller ticks a caller spends waiting on one channel request, and says when to give up.
///
/// A tick is one caller tick that produced no answer: the request was refused, is still PENDING, or was reported
/// UNKNOWN and must be asked again. Each call site counts whichever of those it can meet.
///
/// Against a channel that reports its progress, count with @ref tick(uint32_t): then only the ticks on which the
/// channel finished nothing count, so a caller held off by someone else's long operation waits for as long as that
/// operation keeps moving, and a channel that has stopped moving still fails it after @ref limit ticks.
class StallBound {
public:
	explicit constexpr StallBound(int32_t limit = kChannelStallTicks) noexcept : limit_{limit} {}

	/// Start counting again, for a new request.
	void reset() noexcept {
		ticks_ = 0;
		seenProgress_ = false;
	}

	/// Count one caller tick spent waiting. True once the limit is reached, and on every tick after.
	bool tick() noexcept {
		if (ticks_ < limit_) {
			ticks_++;
		}
		return expired();
	}

	/// Count one caller tick spent waiting on a channel whose progress mark reads @p progress.
	///
	/// A mark that differs from the one the previous tick saw means the channel finished some work in between, so
	/// the count starts over before this tick is counted. Marks are compared for equality only, so a wrapped counter
	/// is still progress. The first tick after @ref reset has nothing to compare against and simply counts.
	/// @param progress The channel's progress mark, read this tick (e.g. `deluge_file_bytes_progress()`).
	/// @return True once @ref limit consecutive ticks have seen the same mark.
	bool tick(uint32_t progress) noexcept {
		if (seenProgress_ && progress != progress_) {
			ticks_ = 0;
		}
		progress_ = progress;
		seenProgress_ = true;
		return tick();
	}

	[[nodiscard]] bool expired() const noexcept { return ticks_ >= limit_; }
	[[nodiscard]] int32_t ticks() const noexcept { return ticks_; }
	[[nodiscard]] int32_t limit() const noexcept { return limit_; }

private:
	int32_t limit_;
	int32_t ticks_ = 0;
	/// The progress mark the previous @ref tick(uint32_t) saw, valid once `seenProgress_` is set.
	uint32_t progress_ = 0;
	bool seenProgress_ = false;
};

} // namespace deluge::io
