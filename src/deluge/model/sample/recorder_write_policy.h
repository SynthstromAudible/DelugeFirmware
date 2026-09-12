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

/// @brief How many consecutive unproductive ticks a recorder operation may spend before it gives up.
///
/// Unproductive means refused, answered UNKNOWN, or answered PENDING; an accepted request starts the count
/// again. A void request needs a STALL bound as well as a refusal bound: a channel whose driver is not
/// running answers PENDING forever, and this project has shipped that wedge three times (the startup
/// fail-safe's boot wedge, `smsysex_pending`, and the settings submit machine). The value matches theirs.
constexpr int32_t kMaxUnproductiveTicks = 600;

/// @brief Whether an operation still in flight has gone past the bound. PURE.
///
/// Past it the operation has failed, but it may not be released: the driver can still use what it was
/// given. The create and finalize machines latch this and fail on the terminal answer.
/// @param collected         The `DELUGE_FILE_WRITE_*` value from this tick's collect.
/// @param unproductiveTicks Consecutive unproductive ticks before this one.
[[nodiscard]] bool stalledPastBound(uint8_t collected, int32_t unproductiveTicks);

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
/// @param unproductiveTicks Consecutive unproductive ticks before this one.
[[nodiscard]] WriteStep nextWriteStep(bool channelUsable, bool issued, uint8_t collected, int32_t unproductiveTicks);

} // namespace deluge::storage
