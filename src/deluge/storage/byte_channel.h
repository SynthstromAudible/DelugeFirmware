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
/// @brief The C++ seam onto `libdeluge/file_bytes.h`.
///
/// One shared place rather than the channel open-coded in every consumer. They ask the same handful
/// of questions of the same channel, and a copy of a request/collect state machine per consumer is a
/// copy per consumer of somewhere to get the collect contract subtly wrong.
///
/// Named for what it carries rather than for its first caller. The settings family (runtime feature
/// settings, the MIDI device table, MIDI follow's defaults, performance view) was that first caller;
/// the startup song fail-safe is the second, and it needs the same five operations.
///
/// **Nothing here parses.** Every line of XML handling, policy and migration stays in the consumers;
/// this moves the source and the sink and nothing else.
namespace deluge::storage::bytes {

/// @brief Where a settings file operation has got to.
///
/// The C ABI's four statuses, in the type the C++ side switches on. Deliberately a scoped enum with
/// no `default:` at its use sites, so `-Werror=switch` catches an unhandled value -- though note that
/// guard protects the FIRMWARE build only: `tests/CMakeLists.txt` is a separate `project(DelugeTests)`
/// which does not inherit `DELUGE_COMMON_COMPILE_OPTIONS`.
enum class ByteIo : uint8_t {
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
/// to keep the INTERACTION tier off the card.
///
/// It adds an `Owner::started()` term that the browser's does not need, and that term is what keeps
/// the three boot reads in `deluge.cpp` working. They measure `on_owner=0 started=0`, so `on_owner()`
/// alone would route them through a channel whose task is not yet running and whose answer nothing is
/// yet ticking to collect -- and the device would boot with defaults for every setting.
///
/// The selector is checked LAST because with the channel absent the weak collect answers UNKNOWN
/// forever, so a caller that consulted it would defer every tick and never read anything.
[[nodiscard]] bool usingByteChannel();

/// @name Whole-file reads
/// @{

/// @brief Ask for the whole of `path` to be read.
///
/// A no-op for a request already in flight, so polling callers cannot starve their own read.
/// @param path NUL-terminated path. Refused (and reported through the next collect as `Unknown`) if
///        it is longer than `DELUGE_FILE_BYTES_MAX_PATH`.
/// @return `Pending` when the request was accepted or was already in flight; `Failed` when `path`
///         cannot be keyed at all, which no amount of re-requesting will fix.
ByteIo requestRead(const char* path);

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
ByteIo collectRead(const char* path, char* buf, uint32_t cap, uint32_t* outLen, DelugeStatus* outStatus);

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
ByteIo requestWrite(const char* path, const char* bytes, uint32_t len);

/// @brief Collect the outcome of the write to `path`, if it has finished.
/// @param path      NUL-terminated path, matched exactly against the one requested.
/// @param outStatus Written **only** on `Failed`. May be passed uninitialised.
ByteIo collectWrite(const char* path, DelugeStatus* outStatus);

/// @}
/// @name Renames
/// @{

/// @brief Ask for `from` to be renamed to `to`. For the legacy-path migration into `SETTINGS/`.
/// @return As @ref requestWrite: `Pending` accepted, `Unknown` refused-retry, `Failed` never.
ByteIo requestRename(const char* from, const char* to);

/// @brief Collect the outcome of the rename to `to`. Keyed on the DESTINATION, because that is the
///        name the caller goes on to use.
/// @param to        NUL-terminated destination, matched exactly against the one requested.
/// @param outStatus Written **only** on `Failed`. A missing source reports `DELUGE_ERR_NOT_FOUND`,
///                  which for the migration callers is the ordinary case -- there is no legacy file
///                  -- and not an error to show anyone.
ByteIo collectRename(const char* to, DelugeStatus* outStatus);

/// @}
/// @name Deletions and existence checks
/// @{

/// @brief Ask for `path` to be deleted.
/// @return `Pending` when accepted; `Unknown` when REFUSED because the channel is busy — **nothing
///         was queued and nothing was deleted**, so the caller must keep its intent and ask again;
///         `Failed` when the path cannot be keyed at all.
ByteIo requestUnlink(const char* path);

/// @brief Collect the outcome of the deletion of `path`, if it has finished.
/// @param path      NUL-terminated path, matched exactly against the one requested.
/// @param outStatus Written **only** on `Failed`. May be passed uninitialised.
/// @note Deleting a path that was already gone reports `Failed` with `DELUGE_ERR_NOT_FOUND`. For a
///       cleanup that is success by another name — the file is not there, which is what was wanted.
ByteIo collectUnlink(const char* path, DelugeStatus* outStatus);

/// @brief Ask whether `path` exists.
///
/// A distinct operation rather than a read, because a read can only answer for files below
/// `DELUGE_FILE_BYTES_MAX_BYTES`: a larger file that IS there reports `Failed` with
/// `DELUGE_ERR_NO_MEMORY`, which a caller must not read as absence.
/// @return `Pending` when accepted; `Failed` when the path cannot be keyed at all.
ByteIo requestExists(const char* path);

/// @brief Collect the existence answer for `path`, if there is one.
///
/// @param path       NUL-terminated path, matched exactly against the one requested.
/// @param outPresent Written **only** on `Ready`: true if the path is there, false if it is known
///                   not to be. May be passed uninitialised.
/// @param outStatus  Written **only** on `Failed`. May be passed uninitialised.
/// @note THREE-STATE. `Ready` with `*outPresent == false` is "known absent"; `Failed` is "could not
///       establish", which is NOT absence. Feed both into `startup_action()` rather than comparing
///       here — that verb exists to keep the two apart.
ByteIo collectExists(const char* path, bool* outPresent, DelugeStatus* outStatus);

/// @}
/// @name The consumers' seam
/// @{
///
/// The four consumers do not drive request/collect themselves. They submit one operation and return;
/// @ref serviceRoutine drives it to completion and hands a read's bytes back through @p parse. Four
/// hand-written copies of a collect loop would be four places for the contract to be got subtly
/// wrong, and each of the four would need its own tick to be driven from.

/// @brief What a consumer does with the bytes a read produced.
/// @param ctx   The consumer's own pointer, passed through unexamined.
/// @param bytes The file's contents. Valid ONLY for the duration of the call.
/// @param len   Byte count.
using ParseFn = void (*)(void* ctx, char* bytes, uint32_t len);

/// @brief What a consumer does when a read found the file genuinely absent.
///
/// Called ONLY for `DELUGE_ERR_NOT_FOUND`, never for an unreadable card and never for a file too
/// large for the channel. That distinction is the whole point of it: two of the four consumers write
/// their defaults when the file is missing, and writing defaults because the card hiccuped would
/// replace the user's real settings. It is the same line `bootstrap_action()` draws between
/// `WriteDefaults` and `UseInMemoryOnly`, and this rung does not move it.
using AbsentFn = void (*)(void* ctx);

/// @brief A consumer's existing synchronous body, to run where blocking is legal.
using OwnerFn = void (*)(void* ctx);

/// @brief Take a read through the channel, if the channel is the right place for it.
///
/// @param path       The file to read.
/// @param legacyPath The pre-1.3 location at the card root, migrated into `path` on a first read that
///                   finds nothing there. May be nullptr for a consumer with no legacy location.
/// @param parse      Called once, from @ref serviceRoutine, with the bytes. NOT called if the file is
///                   absent or unreadable.
/// @param onAbsent   Called instead of @p parse when the file is genuinely absent -- after the legacy
///                   migration has been tried and also come up empty. May be nullptr for a consumer
///                   that does nothing on absence. NOT called for an unreadable card.
/// @param ctx        Passed through to @p parse and @p onAbsent.
/// @return true when the channel took it and one of the two callbacks will be called later. **false when the caller
///         must do the read itself**, either because the channel is not in use here or because it is
///         already busy with another operation. A false is never a lost read: the caller falls
///         through to @ref runOnStorageOwner, which does the same work where blocking is legal.
bool submitRead(const char* path, const char* legacyPath, ParseFn parse, AbsentFn onAbsent, void* ctx);

/// @brief Take a write through the channel, if the channel is the right place for it.
///
/// @param path  The file to write. Its parent directory is created if absent.
/// @param bytes The serialised contents. Copied out before this returns, so the caller's serialiser
///              may be destroyed immediately afterwards.
/// @param len   Byte count.
/// @return true when the channel took it. **false when the caller must do the write itself.**
///
/// @note A false here is a REFUSAL, not a queue. That is the no-coalescing rule: two rapid toggles
///       are two distinct settings changes, and superseding the first would lose one the user watched
///       themselves make. The caller falls through to @ref runOnStorageOwner rather than retrying, so
///       the second intent still reaches the card -- just synchronously, on a tier where blocking is
///       legal.
bool submitWrite(const char* path, const char* bytes, uint32_t len);

/// @brief Run @p fn where blocking on the card is legal: on the storage owner, or inline if already
///        there.
///
/// The fallback arm for every consumer, and the whole of the fix on a build with no channel. It is
/// what takes settings I/O off the interaction tier even when nothing has been relocated into Rust.
void runOnStorageOwner(OwnerFn fn, void* ctx);

/// @brief How many settings operations the CHANNEL has carried to completion.
///
/// The check that the async arm is executed at all. Every consumer falls back to
/// @ref runOnStorageOwner when the channel refuses, and that fallback is correct and silent -- so a
/// channel that never worked would leave every gate passing and every settings file intact, which is
/// exactly how a previous rung's livelock survived review. This number is the difference between the
/// channel being used and the channel merely being compiled.
///
/// A read that found nothing counts too: the channel carried the question and answered it.
[[nodiscard]] int32_t channelServedOps();

/// @brief Drive whatever @ref submitRead or @ref submitWrite handed over.
///
/// Registered as a repeating task in `deluge.cpp` beside `loadSongUI.loadRoutine()`, and called by
/// the settings-channel gate's own tick. Cheap and reentrant when there is nothing pending.
void serviceRoutine();

/// @}
/// @name The tier gate
/// @{

/// @brief Which consumer's tier count to read or bump.
///
/// Two counters rather than one, because two gates read them independently: the settings gate's
/// `SC-TIER` must stay 0 while the startup gate is driving `SF-TIER` non-zero, and a shared counter
/// would make each gate fail for the other's reasons.
enum class IoTier : uint8_t {
	Settings, ///< runtime feature settings, the MIDI device table, MIDI follow, performance view
	Startup,  ///< the startup song fail-safe
};

/// @brief How many of `which`'s file operations have begun on the INTERACTION tier.
///
/// Counted rather than only frozen so a gate can report the number BEFORE a conversion as well as
/// after: that before/after is each rung's evidence that anything changed. Mirrors
/// `SaveUI::interactionTierSaves()`, which is what made rung 6's 3 -> 0 legible.
[[nodiscard]] int32_t interactionTierIo(IoTier which);

/// @brief Record one entry. Call as the FIRST statement of each consumer's io path.
///
/// Boot is the one legal exception: `deluge.cpp` reads all three settings files before any
/// interaction tier exists, measured `on_owner=0 started=0`. `Owner::started()` is the same
/// monotonic latch the `TIR1`-`TIR6` reader and save checks use.
void noteIoTier(IoTier which);

/// @}

} // namespace deluge::storage::bytes
