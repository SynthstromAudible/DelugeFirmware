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
/// @name Directory creation and timestamps
/// @{

/// @brief Ask for `path` and every missing directory above it to be created.
///
/// Whole-path, not one segment: the walk lives in Rust, so a caller needs no loop and no phase
/// machine of its own to do it over this channel.
/// @return `Pending` when accepted; `Unknown` when REFUSED because the channel is busy — **nothing
///         was queued and nothing was created**; `Failed` when the path cannot be keyed at all.
ByteIo requestMkdirAll(const char* path);

/// @brief Collect the outcome of the directory creation for `path`, if it has finished.
/// @param path      NUL-terminated path, matched exactly against the one requested.
/// @param outStatus Written **only** on `Failed`, with the first failure that was not "already
///                  exists". May be passed uninitialised.
/// @note An already-existing path reports `Ready`, NOT a failure. Callers use this to mean "make sure
///       this is there", and the commonest outcome is that it already is.
ByteIo collectMkdirAll(const char* path, DelugeStatus* outStatus);

/// @brief Ask for `path`'s modification time to be set.
/// @param path      NUL-terminated path.
/// @param timestamp By value — `DelugeTimestamp` is a fixed-width POD, so it needs no lifetime.
/// @return As @ref requestMkdirAll.
ByteIo requestSetTime(const char* path, DelugeTimestamp timestamp);

/// @brief Collect the outcome of the timestamp write for `path`, if it has finished.
/// @param path      NUL-terminated path, matched exactly against the one requested.
/// @param outStatus Written **only** on `Failed`. May be passed uninitialised.
ByteIo collectSetTime(const char* path, DelugeStatus* outStatus);

/// @}
/// @name Offset-addressed transfers
/// @{

/// @brief Ask for up to `DELUGE_FILE_BYTES_BLOCK` bytes from `path` at `offset`.
///
/// Stateless: nothing is opened, held or returned. The consumer these serve already carries its
/// offset, so the path and offset are the whole request.
/// @return `Pending` when accepted; `Unknown` when REFUSED because the channel is busy; `Failed`
///         when the path cannot be keyed or `len` exceeds the block bound — neither of which a retry
///         would fix.
ByteIo requestReadAt(const char* path, uint32_t offset, uint32_t len);

/// @brief Collect the bytes read for `path`, if they are ready.
/// @param outLen Written **only** on `Ready`. **A short count is not an error** — it means
///               end-of-file, which is how a transfer detects its own end.
ByteIo collectReadAt(const char* path, char* buf, uint32_t cap, uint32_t* outLen, DelugeStatus* outStatus);

/// @brief Ask for `len` bytes to be written into `path` at `offset`.
///
/// @warning Does NOT truncate, and must not — see `libdeluge/file_bytes.h`, which carries the full
///          reasoning. A caller wanting an empty file asks for one with @ref requestWrite and zero
///          bytes; there is no write-create mode to pass here.
ByteIo requestWriteAt(const char* path, uint32_t offset, const char* bytes, uint32_t len);

/// @brief Collect the outcome of the offset-addressed write to `path`, if it has finished.
/// @param outWritten Written **only** on `Ready`. A short write is reported honestly rather than as
///                   success; the caller decides what it means.
ByteIo collectWriteAt(const char* path, uint32_t* outWritten, DelugeStatus* outStatus);

/// @brief Ask for `path`'s size.
/// @return `Pending` when accepted; `Failed` when the path cannot be keyed at all.
ByteIo requestStat(const char* path);

/// @brief Collect `path`'s size, if it is known.
/// @param outSize   Written **only** on `Ready`.
/// @param outStatus Written **only** on `Failed`, with `DELUGE_ERR_NOT_FOUND` for a path that is not
///                  there.
ByteIo collectStat(const char* path, uint32_t* outSize, DelugeStatus* outStatus);

/// @brief Ask for `from` to be copied to `to`, whole.
///
/// The entire copy, not a block of it: the loop lives on the Rust side, so a caller needs neither a
/// loop nor two open files.
ByteIo requestCopy(const char* from, const char* to);

/// @brief Collect the outcome of the copy to `to`. Keyed on the DESTINATION, like a rename.
ByteIo collectCopy(const char* to, DelugeStatus* outStatus);

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

/// @brief How an operation the channel ACCEPTED ended. Reported once, through @ref DoneFn.
///
/// Settings files never needed this -- a consumer that hears nothing keeps its in-memory defaults --
/// but a consumer that must not act until it knows, like the favourites bank, cannot tell "still going"
/// from "gave up" without it.
enum class SettingsOutcome : uint8_t {
	Parsed,      ///< a read delivered its bytes to `parse`
	Absent,      ///< a read found the file genuinely absent (`DELUGE_ERR_NOT_FOUND`); `onAbsent` ran first
	Unreadable,  ///< a read ended any other way: an unreadable card, a file too large, a stalled channel
	Written,     ///< a write's collect reported success
	WriteFailed, ///< a write ended any other way: the card refused it, or the channel stalled
};

/// @brief Told, exactly once, how an accepted operation ended.
/// @param ctx     The pointer passed at submission, unexamined.
/// @param outcome How it ended. Fired AFTER `parse` / `onAbsent` where those apply.
using DoneFn = void (*)(void* ctx, SettingsOutcome outcome);

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
/// @param ctx        Passed through to @p parse, @p onAbsent and @p onDone.
/// @param onDone     Called exactly once when the operation ends, however it ends -- including a stalled
///                   channel. May be nullptr.
/// @return true when the channel took it -- in flight or queued behind what is -- and @p onDone (if any) will
///         be called later. **false when the caller must do the read itself**, because the channel is not the
///         place for it here (switch off, on the owner, before boot) or @p path does not fit
///         `DELUGE_FILE_BYTES_MAX_PATH`. A busy channel is never a false: it queues.
///
/// @note The path is COPIED; the caller's string may be a temporary.
bool submitRead(const char* path, const char* legacyPath, ParseFn parse, AbsentFn onAbsent, void* ctx,
                DoneFn onDone = nullptr);

/// @brief Take a whole-path directory creation through the channel, driven to completion by
///        @ref serviceRoutine.
///
/// For a caller that wants the directories made but has nowhere to wait for the answer — the
/// browser's folder pre-creates reach here from `opened()`, which returns once. Requesting directly
/// and never collecting would leave the answer in the slot and the outcome unlearned; this hands the
/// collect to the pump instead.
///
/// @param path   The directory path to create, every segment. COPIED.
/// @param onDone Called exactly once when the creation ends. May be nullptr.
/// @param ctx    Passed through to @p onDone.
/// @return true when the channel took it, in flight or queued. **false when the caller must create the
///         directories itself**, because the channel is not the place for it here or @p path does not fit.
bool submitMkdirAll(const char* path, DoneFn onDone = nullptr, void* ctx = nullptr);

/// @brief Take a write through the channel, if the channel is the right place for it.
///
/// @param path   The file to write. Its parent directory is created if absent. COPIED.
/// @param bytes  The serialised contents. Copied out before this returns, so the caller's serialiser
///               may be destroyed immediately afterwards.
/// @param len    Byte count.
/// @param onDone Called exactly once when the write ends: `Written` or `WriteFailed`. May be nullptr.
/// @param ctx    Passed through to @p onDone.
/// @return true when the channel took it, in flight or queued. **false when the caller must do the write
///         itself** -- the channel is not the place for it here, or @p len / @p path exceed its bounds. A busy
///         channel is never a false.
///
/// @note No change is ever lost. A write to a path that already has a write WAITING, with the same @p onDone
///       and @p ctx, replaces that write's bytes in place: every write here is a whole file serialised from
///       current state, so the newer one carries every change the older did. A write already in flight is
///       never replaced, and with different callbacks the new write is appended, so each accepted submission's
///       @p onDone still fires exactly once.
bool submitWrite(const char* path, const char* bytes, uint32_t len, DoneFn onDone = nullptr, void* ctx = nullptr);

/// @brief Take a file removal through the channel, driven to completion by @ref serviceRoutine.
///
/// @param path   The file to remove. COPIED.
/// @param onDone Called exactly once when the removal ends: `Written` when the file is gone -- including when it was
///               never there, which is what a removal wants -- or `WriteFailed`. May be nullptr.
/// @param ctx    Passed through to @p onDone.
/// @return true when the channel took it, in flight or queued. **false when the caller must remove the file itself**,
///         because the channel is not the place for it here or @p path does not fit. A busy channel is never a false.
bool submitUnlink(const char* path, DoneFn onDone = nullptr, void* ctx = nullptr);

/// @brief Run @p fn where blocking on the card is legal: on the storage owner, or inline if already
///        there.
///
/// The arm for a consumer whose operation does not belong on the channel here -- the switch is off, the caller is
/// on the owner, it is boot, or the operation exceeds the channel's bounds. Never a busy fallback: a busy channel
/// queues. Counted in @ref ownerFallbacks when it really dispatches.
void runOnStorageOwner(OwnerFn fn, void* ctx);

/// @brief Run @p fn inline where blocking on the card is legal, and nowhere else.
///
/// Legal means on the storage owner, or before the owner has started: boot, and every C++-only build, whose worker is
/// inline and never reports started. Anywhere else the byte channel is the only place card I/O may start from, so an
/// operation that reaches here there is one the channel could not carry (a document over its bound). It is not run,
/// not dispatched, and it is counted in @ref illegalInline.
/// @param fn  The consumer's synchronous body.
/// @param ctx Passed through to @p fn.
/// @return true when @p fn ran; false when it was refused.
[[nodiscard]] bool runInlineWhereLegal(OwnerFn fn, void* ctx);

/// @brief How many operations @ref runInlineWhereLegal refused because the caller was off the owner after boot.
[[nodiscard]] int32_t illegalInline();

/// @brief How many settings operations the CHANNEL has carried to completion.
///
/// The check that the async arm is executed at all. Every consumer falls back to
/// @ref runOnStorageOwner when the channel refuses, and that fallback is correct and silent -- so a
/// channel that never worked would leave every gate passing and every file intact, which is exactly
/// how a previous rung's livelock survived review. This number is the difference between the channel
/// being used and the channel merely being compiled.
///
/// Counted at the COLLECT wrappers, not in the submit machine, because not every consumer uses that
/// machine: the startup fail-safe drives request/collect itself, and a counter that only saw the
/// machine reported zero for it while it was demonstrably using the channel.
///
/// A read that found nothing counts too, as does a failure: the channel carried the question and
/// answered it, which is what this measures.
[[nodiscard]] int32_t channelServedOps();

/// @brief Whether the seam has nothing in flight and nothing waiting.
///
/// What a caller that must know its operations have landed waits on -- never on a file appearing, which is a
/// proxy for an operation finishing and not the thing itself.
[[nodiscard]] bool idle();

/// @brief How many operations @ref runOnStorageOwner has dispatched to the storage owner after boot.
///
/// Counted only for a real dispatch: not the inline boot path (`!Owner::started()`) and not a caller already on
/// the owner. With the channel in use this should never move -- a nonzero count there means a consumer took its
/// owner arm when the channel was the right place for the work.
[[nodiscard]] int32_t ownerFallbacks();

/// @brief How many of those dispatches the storage owner's queue DROPPED because it was full.
///
/// The owner's queue holds four operations and refuses a fifth; @ref runOnStorageOwner cannot block or run the work
/// inline there, so the operation does not happen. Anything nonzero here is a lost operation.
[[nodiscard]] int32_t ownerDispatchesDropped();

/// @brief Drive whatever @ref submitRead, @ref submitWrite and @ref submitMkdirAll handed over, one operation
///        at a time, oldest first.
///
/// Registered as a repeating task in `deluge.cpp`, and called by the settings-channel gate's own tick. Cheap and
/// reentrant when there is nothing pending.
void serviceRoutine();

/// @}
/// @name The tier gate
/// @{

/// @brief Which consumer's tier count to read or bump.
///
/// One counter per consumer family, because the gates read them independently: the settings gate's
/// `SC-TIER` must stay 0 while the startup gate is driving `SF-TIER` non-zero, and a shared counter
/// would make each gate fail for the other's reasons.
enum class IoTier : uint8_t {
	Settings,   ///< runtime feature settings, the MIDI device table, MIDI follow, performance view
	Startup,    ///< the startup song fail-safe
	Browser,    ///< the browser's folder creation: the context-menu gesture and the UI-open pre-creates
	Favourites, ///< the favourites bank: every browser visit, favourite pad, bank pad and browser close
};

/// @brief How many of `which`'s file operations have begun on the INTERACTION tier.
///
/// Counted rather than only frozen so a gate can report the number BEFORE a conversion as well as
/// after: that before/after is each rung's evidence that anything changed.
[[nodiscard]] int32_t interactionTierIo(IoTier which);

/// @brief Record one entry. Call as the FIRST statement of each consumer's io path.
///
/// Boot is the one legal exception: `deluge.cpp` reads all three settings files before any
/// interaction tier exists, measured `on_owner=0 started=0`. `Owner::started()` is the same
/// monotonic latch the TIR reader checks use.
void noteIoTier(IoTier which);

/// @}

} // namespace deluge::storage::bytes
