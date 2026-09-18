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

namespace deluge::storage {

/// @brief What `SampleRecorder`'s cluster write should do on this tick.
enum class WriteStep : uint8_t {
	/// Write synchronously, here and now. Only where the channel cannot be used: on the storage owner, or
	/// before the owner has started, where blocking is legal.
	Inline,
	/// Ask the channel for the owed write: never asked yet, refused last time, or answered UNKNOWN.
	Issue,
	/// The write is in flight. Return without touching the buffer; collect again next tick.
	Wait,
	/// The write is still in flight past the bound. The take has failed, but the driver still holds a
	/// pointer into the buffer, so keep collecting and release it only on a terminal answer.
	StalledWait,
	/// The write landed. Release the buffer and let the caller advance.
	Complete,
	/// The write did not happen, and nothing is outstanding. Release the buffer; the take fails.
	Failed,
};

/// @brief One operation's unproductive run, measured on a clock rather than in the caller's ticks.
///
/// A tick count means different amounts of waiting to different callers: the offline stem export ticks its
/// recorder ~37 times per run-loop yield, the realtime path about once, so 600 ticks is ~16 yields for one
/// and ~600 for the other. No single tick value can be correct for both, which is why an export that needed
/// ~23 yields gave up at ~16 and wrote a zero-byte file while reporting success. Elapsed time means the same
/// thing to every caller.
///
/// PURE: the caller supplies @p nowUs; this never reads a clock.
struct UnproductiveSpan {
	/// No unproductive run is in progress.
	static constexpr uint64_t kNone = UINT64_MAX;

	/// When the current unproductive run began, or @ref kNone.
	uint64_t sinceUs = kNone;
	/// Microseconds from the start of the run to its latest unproductive answer. Zero for the first, which
	/// is what the tick counters this replaces reported as "ticks before this one".
	int32_t elapsedUs = 0;

	/// @brief Note one unproductive answer: refused, UNKNOWN, or PENDING.
	void note(uint64_t nowUs) {
		if (sinceUs == kNone) {
			sinceUs = nowUs;
			elapsedUs = 0;
			return;
		}
		elapsedUs = static_cast<int32_t>(nowUs - sinceUs);
	}

	/// @brief A terminal answer arrived, or the phase moved on: the next run starts fresh.
	void reset() {
		sinceUs = kNone;
		elapsedUs = 0;
	}
};

/// @brief How long a recorder operation may go unproductive before it gives up, in microseconds.
///
/// Unproductive means refused, answered UNKNOWN, or answered PENDING; an accepted request starts the run
/// again. A void request needs a STALL bound as well as a refusal bound: a channel whose driver is not
/// running answers PENDING forever, and this project has shipped that wedge three times (the startup
/// fail-safe's boot wedge, `smsysex_pending`, and the settings submit machine).
///
/// MEASURED, both ends, before it was pinned -- which the tick constant it replaces never was:
///   - the offline stem export's longest single unproductive run is 6 ms, 50x below this;
///   - every stall gate leg holds its operation for 3000 ms, 10x above this.
/// Changing it requires re-measuring both.
constexpr int32_t kMaxUnproductiveUs = 300'000;

/// @brief Whether an operation still in flight has gone past the bound. PURE.
///
/// Past it the operation has failed, but it may not be released: the driver can still use what it was
/// given. The create and finalize machines latch this and fail on the terminal answer.
/// @param collected      The `DELUGE_FILE_WRITE_*` value from this tick's collect.
/// @param unproductiveUs Microseconds this operation has been unproductive.
[[nodiscard]] bool stalledPastBound(uint8_t collected, int32_t unproductiveUs);

/// @brief Decide this tick's step. PURE -- no I/O, no globals, no clock.
///
/// Pure so every ruling above is testable without fault injection, which is what made the startup
/// phase machine's conflation rulings provable rather than argued.
///
/// @param channelUsable     Whether this write may use the write channel: the caller is off the storage
///                          owner and the owner has started.
/// @param issued            Whether the channel accepted the owed write and it has not been collected.
/// @param collected         The `DELUGE_FILE_WRITE_*` value from the collect, or
///                          `DELUGE_FILE_WRITE_UNKNOWN` when nothing was collected this tick.
/// @param unproductiveUs   Microseconds this write has been unproductive.
[[nodiscard]] WriteStep nextWriteStep(bool channelUsable, bool issued, uint8_t collected, int32_t unproductiveUs);

} // namespace deluge::storage
