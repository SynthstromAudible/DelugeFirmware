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

#include "io/file.hpp"
#include "libdeluge/types.h"

#include <cstdint>
#include <optional>

/// @file
/// @brief The one path operation a SysEx request may have in flight.
///
/// ## Why the handlers need this at all
///
/// Every `smsysex` path handler ends by sending a reply that CARRIES the operation's status. So
/// deferring the operation defers the reply, and the handler has to survive across ticks — which it
/// does by being re-entered and re-parsing its request, then finding this record.
///
/// Re-parsing rather than storing a half-consumed `JsonDeserializer` is deliberate: the queued bytes
/// are unchanged and in memory, the parse costs microseconds, and a parser saved across ticks would
/// be the fragile half of this.
///
/// ## One at a time
///
/// `smsysex` already processes one queued request at a time, so one pending operation is all there
/// can be. This holds it, and its own emptiness is what tells a re-entered handler whether it is
/// starting or resuming.
namespace deluge::storage {

/// Which path operation is in flight.
enum class PathOp : uint8_t {
	None,
	Unlink,
	Rename,
	MkdirAll,
	SetTime,
	ReadAt,
	WriteAt,
	Stat,
	Copy,
	/// Create the file empty, truncating an existing one -- the whole-file write with zero bytes.
	/// This is what `DELUGE_FILE_WRITE_CREATE` meant, kept as its own operation because
	/// @ref PathOp::WriteAt deliberately does NOT truncate.
	CreateEmpty,
};

/// @brief Whether an operation has been started and not yet cleared.
///
/// A re-entered handler asks this first: false means "parse my arguments and start", true means
/// "collect".
[[nodiscard]] bool pathOpPending();

/// @brief Start `op`, issuing it on the channel, or running it inline where blocking is legal.
///
/// @param op    Which operation.
/// @param a     The path, or the rename SOURCE.
/// @param b     The rename destination. Ignored by every other operation; may be nullptr.
/// @param stamp Used only by @ref PathOp::SetTime.
/// @return true when the operation was started. **false when it was refused this tick** — the
///         handler returns "not done" and the next tick's re-entry tries again.
bool pathOpStart(PathOp op, const char* a, const char* b, DelugeTimestamp stamp);

/// @brief Start an offset-addressed operation.
///
/// @param op     @ref PathOp::ReadAt, @ref PathOp::WriteAt or @ref PathOp::Stat.
/// @param path   The file.
/// @param offset Byte offset. Ignored by `Stat`.
/// @param bytes  The payload for a write; nullptr otherwise. **Copied**, so the caller's decode
///               buffer is free the instant this returns.
/// @param len    Byte count for a write, or how many bytes a read wants.
/// @return As @ref pathOpStart.
bool pathOpStartBlock(PathOp op, const char* path, uint32_t offset, const uint8_t* bytes, uint32_t len);

/// @brief Collect an offset-addressed operation, copying a read's bytes out.
///
/// @param outBuf Written **only** when the outcome is `OK` and the operation was a read.
/// @param outLen Written **only** then, with the byte count. **A short count is end-of-file**, not a
///               failure -- the transfer detects its own end that way.
/// @return `nullopt` while running; the outcome once terminal.
[[nodiscard]] std::optional<deluge::io::Status> pathOpCollectBlock(uint8_t* outBuf, uint32_t* outLen);

/// @brief The scalar a `Stat` or `WriteAt` produced: a size, or a byte count.
///
/// Valid only immediately after @ref pathOpCollectBlock returned a terminal outcome, and before
/// @ref pathOpClear.
[[nodiscard]] uint32_t pathOpResultValue();

/// @brief Collect the operation in flight.
/// @return `nullopt` while it is still running; its outcome once terminal. The caller then sends its
///         reply and calls @ref pathOpClear.
[[nodiscard]] std::optional<deluge::io::Status> pathOpCollect();

/// @brief Forget the operation, so the next one may start.
///
/// Also resets @ref pathOpStage, so a multi-step handler starts its next request from step 0.
void pathOpClear();

/// @brief Where a handler that needs SEVERAL operations has got to.
///
/// Only `createDirectory` uses it, to tell "I have created the directory" from "I have also stamped
/// it". The meaning of the number belongs entirely to the handler; this unit only keeps it across
/// the ticks the handler is re-entered on.
[[nodiscard]] uint8_t pathOpStage();

/// @brief Set the stage. See @ref pathOpStage.
void pathOpSetStage(uint8_t stage);

} // namespace deluge::storage
