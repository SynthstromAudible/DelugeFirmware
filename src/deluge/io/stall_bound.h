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

/// How many caller ticks a request may stay PENDING before its caller gives up on the channel.
///
/// A channel whose driver never runs answers PENDING forever, which is neither a refusal nor an answer. Without a
/// bound, a caller would poll until the end of time. Six hundred ticks is several seconds at the UI's tick rate:
/// far beyond any real operation, far short of forever.
inline constexpr int32_t kChannelStallTicks = 600;

/// Counts the ticks a caller spends waiting on one channel request, and says when to give up.
class StallBound {
public:
	explicit constexpr StallBound(int32_t limit = kChannelStallTicks) noexcept : limit_{limit} {}

	/// Start counting again, for a new request.
	void reset() noexcept { ticks_ = 0; }

	/// Count one tick spent waiting. True once the limit is reached, and on every tick after.
	bool tick() noexcept {
		if (ticks_ < limit_) {
			ticks_++;
		}
		return expired();
	}

	[[nodiscard]] bool expired() const noexcept { return ticks_ >= limit_; }
	[[nodiscard]] int32_t ticks() const noexcept { return ticks_; }
	[[nodiscard]] int32_t limit() const noexcept { return limit_; }

private:
	int32_t limit_;
	int32_t ticks_ = 0;
};

} // namespace deluge::io
