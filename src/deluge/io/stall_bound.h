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

#include "runtime/app_loop.h"

#include <cstdint>

namespace deluge::io {

/// How long a caller may wait on a channel request with no progress before it gives up on it, in microseconds.
///
/// A channel whose driver never runs answers PENDING forever, or refuses forever, which is neither an answer nor a
/// failure. Without a bound, a caller would poll until the end of time. 3.6 s is far beyond any real operation and far
/// short of forever.
inline constexpr uint64_t kChannelStallUs = 3'600'000;

/// Measures how long a caller has waited on one channel request with nothing to show for it, and says when to give
/// up.
///
/// An unproductive tick is one caller tick that produced no answer: the request was refused, is still PENDING, or was
/// reported UNKNOWN and must be asked again. Each call site ticks on whichever of those it can meet. The span runs
/// from the first unproductive tick to the latest one, on the BSP's monotonic clock.
///
/// Time, not a tick count, because a caller that runs only when something wakes it would never tick against a dead
/// channel, and a count of its runs would never reach any bound. Such a caller asks to run at @ref deadlineUs, so its
/// tick there finds the span over.
///
/// Against a channel that reports its progress, tick with @ref tick(uint32_t): a moved mark restarts the span, so a
/// caller held off by someone else's long operation waits for as long as that operation keeps moving, and a channel
/// that has stopped moving still fails it once @ref limitUs has passed.
class StallBound {
public:
	using Clock = uint64_t (*)();

	/// @param limitUs How long the span may run before the bound expires, in microseconds.
	/// @param clock   The clock the span is measured on, in microseconds.
	explicit constexpr StallBound(uint64_t limitUs = kChannelStallUs, Clock clock = &deluge::runtime::nowUs) noexcept
	    : limitUs_{limitUs}, clock_{clock} {}

	/// Start a new span, for a new request.
	void reset() noexcept {
		sinceUs_ = deluge::runtime::kNever;
		lastUs_ = 0;
		seenProgress_ = false;
	}

	/// Note one unproductive tick. The first after @ref reset starts the span.
	/// @return True once @ref limitUs has passed since the span started, and on every tick after.
	bool tick() noexcept {
		const uint64_t now = clock_();
		if (sinceUs_ == deluge::runtime::kNever) {
			sinceUs_ = now;
		}
		lastUs_ = (now > sinceUs_) ? now : sinceUs_;
		return expired();
	}

	/// Note one unproductive tick on a channel whose progress mark reads @p progress.
	///
	/// A mark that differs from the one the previous tick saw means the channel finished some work in between, so the
	/// span starts again at this tick. Marks are compared for equality only, so a wrapped counter is still progress.
	/// The first tick after @ref reset has nothing to compare against and simply ticks.
	/// @param progress The channel's progress mark, read this tick (e.g. `deluge_file_bytes_progress()`).
	/// @return True once @ref limitUs has passed with the same mark.
	bool tick(uint32_t progress) noexcept {
		if (seenProgress_ && progress != progress_) {
			sinceUs_ = deluge::runtime::kNever;
		}
		progress_ = progress;
		seenProgress_ = true;
		return tick();
	}

	/// Whether the latest tick found the span at or past @ref limitUs. Time passing without a tick changes nothing.
	[[nodiscard]] bool expired() const noexcept {
		return sinceUs_ != deluge::runtime::kNever && elapsedUs() >= limitUs_;
	}
	/// The span from its start to the latest tick, in microseconds. Zero before the first tick.
	[[nodiscard]] uint64_t elapsedUs() const noexcept {
		return (sinceUs_ == deluge::runtime::kNever) ? 0 : lastUs_ - sinceUs_;
	}
	[[nodiscard]] uint64_t limitUs() const noexcept { return limitUs_; }
	/// When this bound will expire if nothing progresses: a job waiting on its channel asks for a run at this time,
	/// so it gives up even if the channel never wakes it. kNever before the first tick.
	[[nodiscard]] uint64_t deadlineUs() const noexcept {
		if (sinceUs_ == deluge::runtime::kNever) {
			return deluge::runtime::kNever;
		}
		return (limitUs_ > deluge::runtime::kNever - sinceUs_) ? deluge::runtime::kNever : sinceUs_ + limitUs_;
	}

private:
	uint64_t limitUs_;
	Clock clock_;
	/// When the span started, or kNever when none has.
	uint64_t sinceUs_ = deluge::runtime::kNever;
	/// The latest tick's time.
	uint64_t lastUs_ = 0;
	/// The progress mark the previous @ref tick(uint32_t) saw, valid once `seenProgress_` is set.
	uint32_t progress_ = 0;
	bool seenProgress_ = false;
};

} // namespace deluge::io
