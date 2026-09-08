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

#include "libdeluge/types.h"

#include <cstdint>

/// @file
/// @brief The settings family's one seam onto `libdeluge/file_bytes.h`.
///
/// One shared place rather than the channel open-coded in each of the four consumers (runtime
/// feature settings, the MIDI device table, MIDI follow's defaults, performance view). They ask the
/// same three questions of the same channel, and four copies of a request/collect state machine is
/// four places for the collect contract to be got subtly wrong.
///
/// **Nothing here parses.** Every line of XML handling, policy and migration stays in the consumers;
/// this moves the source and the sink and nothing else.
namespace deluge::storage::settings {

/// @brief Where a settings file operation has got to.
///
/// The C ABI's four statuses, in the type the C++ side switches on. Deliberately a scoped enum with
/// no `default:` at its use sites, so `-Werror=switch` catches an unhandled value -- though note that
/// guard protects the FIRMWARE build only: `tests/CMakeLists.txt` is a separate `project(DelugeTests)`
/// which does not inherit `DELUGE_COMMON_COMPILE_OPTIONS`.
enum class SettingsIo : uint8_t {
	/// Requested, or running right now. Ask again on a later tick; do NOT re-request.
	Pending,
	/// The operation completed and its answer has been consumed. Terminal.
	Ready,
	/// Nothing is known about this operation. Request it (again). Also what a caller sees before its
	/// first request.
	Unknown,
	/// The operation did not happen, and `outStatus` says why. TERMINAL -- do NOT re-request, or a
	/// file that cannot be read is re-requested every tick forever.
	Failed,
};

/// @brief Whether this call should go through the channel rather than doing the I/O itself.
///
/// @return true when the caller must request/collect; false when it must take its synchronous arm.
///
/// Mirrors `Browser::usingDirectoryChannel()`, and inherits its reasoning: on the owner the
/// synchronous path is correct and cheaper, because blocking there is legal and the channel exists
/// to keep the INTERACTION tier off the card. That `on_owner()` term is also what keeps the three
/// boot reads in `deluge.cpp` working unchanged -- measured `on_owner=0 started=0`, so they run
/// before an owner exists and the selector below is what stands them down.
///
/// The selector is checked LAST because with the channel absent the weak collect answers UNKNOWN
/// forever, so a caller that consulted it would defer every tick and never read anything.
[[nodiscard]] bool usingSettingsChannel();

/// @name Whole-file reads
/// @{

/// @brief Ask for the whole of `path` to be read.
///
/// A no-op for a request already in flight, so polling callers cannot starve their own read.
/// @param path NUL-terminated path. Refused (and reported through the next collect as `Unknown`) if
///        it is longer than `DELUGE_FILE_BYTES_MAX_PATH`.
/// @return `Pending` when the request was accepted or was already in flight; `Failed` when `path`
///         cannot be keyed at all, which no amount of re-requesting will fix.
SettingsIo requestRead(const char* path);

/// @brief Collect the bytes read for `path`, if they are ready.
/// @param path      NUL-terminated path, matched exactly against the one requested.
/// @param buf       Written **only** on `Ready`, with `*outLen` bytes. No NUL is appended.
/// @param cap       Capacity of `buf`. A file longer than this reports `Failed` with
///                  `DELUGE_ERR_NO_MEMORY` rather than a truncated read, because a settings file cut
///                  short parses as one whose remaining settings were absent.
/// @param outLen    Written **only** on `Ready`. May be passed uninitialised.
/// @param outStatus Written **only** on `Failed`. May be passed uninitialised. Carries the
///                  distinction the consumers act on: `DELUGE_ERR_NOT_FOUND` means "no such file,
///                  write the defaults", anything else means "do not write anything", because
///                  replacing a user's real settings with defaults because the card hiccuped is the
///                  failure mode `existence_policy.h`'s `Presence` exists to prevent.
SettingsIo collectRead(const char* path, char* buf, uint32_t cap, uint32_t* outLen, DelugeStatus* outStatus);

/// @}
/// @name Whole-file writes
/// @{

/// @brief Ask for `len` bytes to be written over `path`, creating its parent directory if absent.
///
/// @param path  NUL-terminated destination.
/// @param bytes Copied out before this returns, so the caller's serialiser may be destroyed
///              immediately afterwards.
/// @param len   Byte count. A `len` above `DELUGE_FILE_BYTES_MAX_BYTES` reports `Failed`, never a
///              clipped write.
/// @return `Pending` when the request was accepted. `Unknown` when it was REFUSED because the
///         channel is busy -- **the write did not happen and nothing is queued**, so the caller must
///         keep its intent and ask again. `Failed` when it can never be accepted (unkeyable path, or
///         too many bytes).
///
/// @warning `Unknown` here does NOT mean the same thing it means on a read. A read that is dropped
///          is a question worth re-asking; a write that is dropped is a settings change the user
///          watched themselves make and which silently did not happen. That is why this returns a
///          status at all rather than being fire-and-forget.
SettingsIo requestWrite(const char* path, const char* bytes, uint32_t len);

/// @brief Collect the outcome of the write to `path`, if it has finished.
/// @param path      NUL-terminated path, matched exactly against the one requested.
/// @param outStatus Written **only** on `Failed`. May be passed uninitialised.
SettingsIo collectWrite(const char* path, DelugeStatus* outStatus);

/// @}
/// @name Renames
/// @{

/// @brief Ask for `from` to be renamed to `to`. For the legacy-path migration into `SETTINGS/`.
/// @return As @ref requestWrite: `Pending` accepted, `Unknown` refused-retry, `Failed` never.
SettingsIo requestRename(const char* from, const char* to);

/// @brief Collect the outcome of the rename to `to`. Keyed on the DESTINATION, because that is the
///        name the caller goes on to use.
/// @param to        NUL-terminated destination, matched exactly against the one requested.
/// @param outStatus Written **only** on `Failed`. A missing source reports `DELUGE_ERR_NOT_FOUND`,
///                  which for the migration callers is the ordinary case -- there is no legacy file
///                  -- and not an error to show anyone.
SettingsIo collectRename(const char* to, DelugeStatus* outStatus);

/// @}
/// @name The tier gate
/// @{

/// @brief How many settings file operations have begun on the INTERACTION tier.
///
/// Counted rather than only frozen so the gate can report the number BEFORE the conversion as well
/// as after: that before/after is this rung's evidence that anything changed. Mirrors
/// `SaveUI::interactionTierSaves()`, which is what made rung 6's 3 -> 0 legible.
[[nodiscard]] int32_t interactionTierSettingsIo();

/// @brief Record one entry. Call as the FIRST statement of each consumer's io path.
///
/// Boot is the one legal exception: `deluge.cpp` reads all three settings files before any
/// interaction tier exists, measured `on_owner=0 started=0`. `Owner::started()` is the same
/// monotonic latch the `TIR1`-`TIR5` reader and save checks use.
void noteSettingsIoTier();

/// @}

} // namespace deluge::storage::settings
