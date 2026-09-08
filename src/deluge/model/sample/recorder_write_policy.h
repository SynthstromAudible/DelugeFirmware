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

/// @brief What `SampleRecorder::writeCluster` should do on this tick.
enum class WriteStep : uint8_t {
	/// Do the write synchronously, here and now. The channel-off arm, and the arm a stalled or
	/// refusing channel falls back to. Always correct, never wrong -- only slower.
	Inline,
	/// Ask the channel for this write. Nothing is in flight for this handle.
	Issue,
	/// A write is genuinely in flight. Return without touching the buffer; ask again next tick.
	Wait,
	/// The write landed. Recycle the buffer and let the caller advance.
	Complete,
	/// The write did not happen. The caller reports `Error::SD_CARD`.
	Failed,
};

/// @brief How many ticks a PENDING write may go unanswered before the channel is abandoned.
///
/// A void request needs a STALL bound as well as a refusal bound: a channel whose driver is not
/// running answers PENDING forever, and this project has shipped that wedge three times (the
/// startup fail-safe's boot wedge, `smsysex_pending`, and the settings submit machine). The value
/// matches `smsysex_pending`'s. Losing the async arm is acceptable; losing the audio is not.
constexpr int32_t kMaxPendingTicks = 600;

/// @brief Decide this tick's step. PURE -- no I/O, no globals, no clock.
///
/// Pure so every ruling above is testable without fault injection, which is what made the startup
/// phase machine's conflation rulings provable rather than argued.
///
/// @param channelActive `deluge_file_write_async_active()` AND the caller is off the storage owner.
/// @param haveInFlight  Whether this recorder issued a request it has not yet collected.
/// @param collected     The `DELUGE_FILE_WRITE_*` value from the collect, or
///                      `DELUGE_FILE_WRITE_UNKNOWN` when nothing was collected this tick.
/// @param pendingTicks  Consecutive ticks this request has read PENDING.
[[nodiscard]] WriteStep nextWriteStep(bool channelActive, bool haveInFlight, uint8_t collected, int32_t pendingTicks);

} // namespace deluge::storage
