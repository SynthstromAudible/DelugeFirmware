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

#include "storage/byte_channel.h"

#include "definitions_cxx.hpp"
#include "hid/display/display.h"
#include "io/file.hpp"
#include "libdeluge/file_bytes.h"
#include "storage/owner.h"

#include <cstddef>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace deluge::storage::bytes {

namespace {

/// How many of each consumer's file operations have begun on the interaction tier. Indexed by
/// @ref IoTier. See @ref interactionTierIo.
int32_t g_interactionTierIo[5] = {0, 0, 0, 0, 0};

/// Whether a violation by this consumer is a hard freeze yet. Indexed by @ref IoTier.
///
/// **A tier's freeze arrives WITH its conversion, never before.** The gate that has to measure the
/// pre-conversion number cannot measure it if the first violation aborts the run — which is exactly
/// what happened the moment the startup fail-safe's sites were instrumented against an already-armed
/// freeze. Counting is what proves the sites are reachable; freezing is what keeps them from coming
/// back, and it is only truthful once there is a converted path for them to come back FROM.
constexpr bool kFreezeOnViolation[5] = {
    true, // Settings: converted, so any violation is now a regression.
    true, // Startup: converted, likewise.
    true, // Browser: converted, likewise.
    true, // Favourites: converted, likewise.
    true, // Recorder: converted, likewise.
};

/// Translate the C ABI's `uint32_t`-shaped status byte into the type the callers switch on.
///
/// No `default:`, deliberately: `-Werror=switch` is what catches a fifth status added to the header
/// and not handled here. The trailing return is for a value that is not one of the four at all,
/// which can only come from a corrupted ABI -- reported as `Unknown` rather than asserted, because
/// "ask again" is the one answer that is safe under every interpretation.
ByteIo fromAbi(uint8_t status) {
	switch (status) {
	case DELUGE_FILE_BYTES_PENDING:
		return ByteIo::Pending;
	case DELUGE_FILE_BYTES_READY:
		return ByteIo::Ready;
	case DELUGE_FILE_BYTES_UNKNOWN:
		return ByteIo::Unknown;
	case DELUGE_FILE_BYTES_FAILED:
		return ByteIo::Failed;
	}
	return ByteIo::Unknown;
}

/// Whether `path` is short enough for the channel to key on at all.
///
/// Checked HERE, with a real answer, rather than passed on. The request functions silently drop an
/// over-long path and the collect then answers UNKNOWN forever -- which for a read would be an
/// infinite re-request loop with no error ever shown, and for a write would be a settings change
/// that never lands and never reports. Settings paths are compile-time literals today, so this
/// cannot fire; it is here because the bound is the channel's, not the caller's.
bool keyable(const char* path) {
	return path != nullptr && strlen(path) < DELUGE_FILE_BYTES_MAX_PATH;
}

/// See @ref channelServedOps.
int32_t g_channelServed = 0;

/// See @ref illegalInline.
int32_t g_illegalInline = 0;

/// Count one channel operation that reached a terminal status, and pass that status through.
///
/// Counted HERE, at the collect wrappers, rather than in the submit machine below -- because not
/// every consumer uses that machine. The startup fail-safe drives request/collect itself, and a
/// counter that only saw the machine reported zero for it while it was demonstrably using the
/// channel. What this number proves is that the channel RAN, so it has to be incremented wherever an
/// answer actually comes back.
///
/// Terminal only: PENDING and UNKNOWN are "ask again", and counting those would report a number that
/// grows with polling rather than with work.
ByteIo counted(ByteIo status) {
	if (status == ByteIo::Ready || status == ByteIo::Failed) {
		g_channelServed++;
	}
	return status;
}

} // namespace

bool usingByteChannel() {
	// `started()` as well as `on_owner()`, and the extra term is load-bearing. `deluge.cpp` reads all
	// three settings files during boot, measured `on_owner=0 started=0` -- so `on_owner()` alone would
	// route those reads through the channel, whose task is not yet running and whose answer nothing is
	// yet ticking to collect. Boot would come up with defaults for every setting. This is the same
	// monotonic latch `Owner::started()`'s own doc names for exactly this class of caller.
	return !deluge::storage::Owner::on_owner() && deluge::storage::Owner::started() && deluge_file_bytes_async_active();
}

ByteIo requestRead(const char* path) {
	if (!keyable(path)) {
		return ByteIo::Failed;
	}
	deluge_file_bytes_request_read(path);
	return ByteIo::Pending;
}

ByteIo collectRead(const char* path, char* buf, uint32_t cap, uint32_t* outLen, DelugeStatus* outStatus) {
	if (!keyable(path) || buf == nullptr || outLen == nullptr || outStatus == nullptr) {
		return ByteIo::Failed;
	}
	// The cast is the identity on the wire: the channel carries bytes, and the deserialiser wants
	// them as `char`. Nothing here interprets them.
	return counted(
	    fromAbi(deluge_file_bytes_collect_read(path, reinterpret_cast<uint8_t*>(buf), cap, outLen, outStatus)));
}

ByteIo requestWrite(const char* path, const char* bytes, uint32_t len) {
	if (!keyable(path) || bytes == nullptr || len > DELUGE_FILE_BYTES_MAX_BYTES) {
		// Terminal, not a retry. A payload too large for the channel will be too large on the next
		// tick too, and a caller that retried it would spin forever writing nothing.
		return ByteIo::Failed;
	}
	if (!deluge_file_bytes_request_write(path, reinterpret_cast<const uint8_t*>(bytes), len)) {
		// REFUSED because the channel is busy: nothing was queued and nothing was written. `Unknown`
		// is the documented "ask again", and the caller must still be holding its intent.
		return ByteIo::Unknown;
	}
	return ByteIo::Pending;
}

ByteIo collectWrite(const char* path, DelugeStatus* outStatus) {
	if (!keyable(path) || outStatus == nullptr) {
		return ByteIo::Failed;
	}
	return counted(fromAbi(deluge_file_bytes_collect_write(path, outStatus)));
}

ByteIo requestRename(const char* from, const char* to) {
	if (!keyable(from) || !keyable(to)) {
		return ByteIo::Failed;
	}
	if (!deluge_file_bytes_request_rename(from, to)) {
		return ByteIo::Unknown;
	}
	return ByteIo::Pending;
}

ByteIo collectRename(const char* to, DelugeStatus* outStatus) {
	if (!keyable(to) || outStatus == nullptr) {
		return ByteIo::Failed;
	}
	return counted(fromAbi(deluge_file_bytes_collect_rename(to, outStatus)));
}

namespace {

/// Where a submitted operation has got to.
///
/// A read is up to four channel operations, not one: the file, then -- only when it is genuinely
/// absent -- the legacy-path migration, then the file again. That sequence IS the migration the
/// consumers do inline today (`runtime_feature_settings.cpp:199`), moved here unchanged. This rung
/// adds no existence policy; it relocates the one already there.
enum class Step : uint8_t {
	Idle,
	/// Ask for the file.
	ReadRequest,
	/// Wait for the file's bytes, or for the failure that sends us to the migration.
	ReadCollect,
	/// Ask for the legacy file to be moved into place.
	RenameRequest,
	/// Wait for the move.
	RenameCollect,
	/// Ask for the file again, now that the move has landed. Entered at most once, which is what
	/// stops a file that stays absent from looping between these two forever.
	ReadRetryRequest,
	/// Wait for the retried read.
	ReadRetryCollect,
	WriteRequest,
	WriteCollect,
	/// Ask for a whole path's directories.
	MkdirAllRequest,
	/// Wait for it. A creation nobody waited for would leave its answer in the slot and its failure
	/// unnoticed -- see `submitMkdirAll`.
	MkdirAllCollect,
	/// Ask for a file to be removed.
	UnlinkRequest,
	/// Wait for the removal. A file that was never there is success, not failure -- see `submitUnlink`.
	UnlinkCollect,
};

/// The one operation in flight, if any.
///
/// One because the channel underneath is single-flight. Everything submitted behind it waits in
/// @ref g_waiting, and is promoted here by @ref serviceRoutine when this slot is idle.
struct PendingOp {
	Step step = Step::Idle;
	/// The operation's path, COPIED. The four settings files were compile-time literals, but a
	/// favourites bank's path is built at run time, and the channel keys on the characters.
	char path[DELUGE_FILE_BYTES_MAX_PATH] = {};
	const char* legacyPath = nullptr;
	ParseFn parse = nullptr;
	AbsentFn onAbsent = nullptr;
	DoneFn onDone = nullptr;
	void* ctx = nullptr;
	uint32_t writeLen = 0;
};

PendingOp g_pending;

/// Copy a submission's path into the pending slot. Callers check @ref keyable first.
void holdPath(const char* path) {
	std::memcpy(g_pending.path, path, std::strlen(path) + 1);
}

/// How many ticks the pending operation has spent waiting for an answer. See @ref kMaxPendingTicks.
int32_t g_pendingTicks = 0;

/// How many ticks one operation may wait before this machine gives up on the channel.
///
/// A channel whose driver is not running answers PENDING forever -- the request sits in its slot with
/// nothing to take it -- which is neither a refusal nor an answer, so nothing else here sees it.
/// Without this bound a submitted operation is polled until the end of time and never happens,
/// silently: the caller was told the channel took it, so it does not fall back either.
///
/// Found by neutering, twice. The startup fail-safe hit it first and wedged boot; this machine hit it
/// again through the browser's folder creation, which simply never occurred. Six hundred ticks is
/// several seconds at this routine's rate -- far beyond any real operation, far short of forever.
constexpr int32_t kMaxPendingTicks = 600;

/// The bytes in flight: a write's payload on the way out, a read's contents on the way in.
///
/// One buffer for both directions because only one operation is ever pending. Sized to the channel's
/// own bound, so anything the channel will carry fits here and nothing has to be refused twice for
/// the same reason.
char g_buffer[DELUGE_FILE_BYTES_MAX_BYTES];

/// One submission waiting behind the operation in flight.
///
/// Owns everything the operation needs, so its submitter can return at once: the path is copied, and a write's
/// bytes are copied onto the heap -- the on-chip @ref g_buffer holds only the operation in flight.
struct WaitingOp {
	Step first = Step::Idle; ///< `ReadRequest`, `WriteRequest`, `MkdirAllRequest` or `UnlinkRequest`
	std::string path;
	const char* legacyPath = nullptr;
	ParseFn parse = nullptr;
	AbsentFn onAbsent = nullptr;
	DoneFn onDone = nullptr;
	void* ctx = nullptr;
	std::vector<char> bytes;
};

/// Submissions behind the operation in flight, oldest first. Only a write can replace an entry instead of
/// appending -- see @ref submitWrite.
std::deque<WaitingOp> g_waiting;

/// Move the oldest waiting submission into the in-flight slot, when that slot is idle.
void promoteHead() {
	if (g_pending.step != Step::Idle || g_waiting.empty()) {
		return;
	}
	WaitingOp next = std::move(g_waiting.front());
	g_waiting.pop_front();
	g_pending = PendingOp{};
	g_pendingTicks = 0;
	holdPath(next.path.c_str());
	g_pending.legacyPath = next.legacyPath;
	g_pending.parse = next.parse;
	g_pending.onAbsent = next.onAbsent;
	g_pending.onDone = next.onDone;
	g_pending.ctx = next.ctx;
	if (next.first == Step::WriteRequest) {
		std::memcpy(g_buffer, next.bytes.data(), next.bytes.size());
		g_pending.writeLen = static_cast<uint32_t>(next.bytes.size());
	}
	g_pending.step = next.first;
}

/// Give up on the pending operation and go idle.
///
/// A read that ends here leaves the consumer's in-memory defaults standing, which is what
/// `bootstrap_action()`'s `UseInMemoryOnly` already does for an unreadable card: replacing a user's
/// real settings with defaults because the card hiccuped is the failure that policy exists to
/// prevent, and it is no more acceptable here.
///
/// @see finishAbsent for the one failure that is NOT this.
void finish() {
	g_pending = PendingOp{};
	g_pendingTicks = 0;
}

/// End the pending operation and tell its submitter how it ended.
void finishWith(SettingsOutcome outcome) {
	DoneFn onDone = g_pending.onDone;
	void* ctx = g_pending.ctx;
	finish();
	if (onDone != nullptr) {
		onDone(ctx, outcome);
	}
}

/// Give up on the channel for the operation in flight, tell its submitter it failed, and go idle.
///
/// Nothing is redone elsewhere: after boot, off the owner, the channel is the only legal place for card I/O.
void abandonChannel() {
	switch (g_pending.step) {
	case Step::MkdirAllRequest:
	case Step::MkdirAllCollect:
		// The folder is not created; its listing reports it missing.
		finishWith(SettingsOutcome::WriteFailed);
		return;
	case Step::WriteRequest:
	case Step::WriteCollect:
	case Step::UnlinkRequest:
	case Step::UnlinkCollect:
		finishWith(SettingsOutcome::WriteFailed);
		return;
	case Step::ReadRequest:
	case Step::ReadCollect:
	case Step::RenameRequest:
	case Step::RenameCollect:
	case Step::ReadRetryRequest:
	case Step::ReadRetryCollect:
		// A read's bytes have nowhere to go once abandoned; its settings consumers keep their in-memory
		// defaults, and a consumer that asked to be told is told it could not read.
		finishWith(SettingsOutcome::Unreadable);
		return;
	case Step::Idle:
		finish();
		return;
	}
}

/// End a read whose file is genuinely absent, telling the consumer so.
///
/// Split from @ref finish because only `DELUGE_ERR_NOT_FOUND` reaches here. Two of the four
/// consumers write their defaults on absence, and routing an unreadable card down this path would
/// have them overwrite settings that are still on the card.
void finishAbsent() {
	AbsentFn onAbsent = g_pending.onAbsent;
	DoneFn onDone = g_pending.onDone;
	void* ctx = g_pending.ctx;
	finish();
	if (onAbsent != nullptr) {
		onAbsent(ctx);
	}
	if (onDone != nullptr) {
		onDone(ctx, SettingsOutcome::Absent);
	}
}

/// Hand a collected read to its consumer, then go idle.
void deliver(uint32_t len) {
	ParseFn parse = g_pending.parse;
	DoneFn onDone = g_pending.onDone;
	void* ctx = g_pending.ctx;
	finish();
	if (parse != nullptr) {
		parse(ctx, g_buffer, len);
	}
	if (onDone != nullptr) {
		onDone(ctx, SettingsOutcome::Parsed);
	}
}

} // namespace

bool submitRead(const char* path, const char* legacyPath, ParseFn parse, AbsentFn onAbsent, void* ctx, DoneFn onDone) {
	if (!usingByteChannel() || !keyable(path)) {
		return false;
	}
	g_waiting.push_back(WaitingOp{.first = Step::ReadRequest,
	                              .path = path,
	                              .legacyPath = legacyPath,
	                              .parse = parse,
	                              .onAbsent = onAbsent,
	                              .onDone = onDone,
	                              .ctx = ctx});
	return true;
}

bool submitMkdirAll(const char* path, DoneFn onDone, void* ctx) {
	if (!usingByteChannel() || !keyable(path)) {
		return false;
	}
	g_waiting.push_back(WaitingOp{.first = Step::MkdirAllRequest, .path = path, .onDone = onDone, .ctx = ctx});
	return true;
}

bool submitUnlink(const char* path, DoneFn onDone, void* ctx) {
	if (!usingByteChannel() || !keyable(path)) {
		return false;
	}
	g_waiting.push_back(WaitingOp{.first = Step::UnlinkRequest, .path = path, .onDone = onDone, .ctx = ctx});
	return true;
}

bool submitWrite(const char* path, const char* bytes, uint32_t len, DoneFn onDone, void* ctx) {
	if (!usingByteChannel() || !keyable(path)) {
		return false;
	}
	if (bytes == nullptr || len > sizeof(g_buffer)) {
		return false;
	}
	// A newer whole file supersedes a WAITING write to the same file: it already carries every change the older
	// one did. Only when both would tell the same submitter the same thing, or a replaced callback would never
	// fire. The operation in flight is never in `g_waiting`, so it is never replaced.
	for (WaitingOp& waiting : g_waiting) {
		if (waiting.first == Step::WriteRequest && waiting.path == path && waiting.onDone == onDone
		    && waiting.ctx == ctx) {
			waiting.bytes.assign(bytes, bytes + len);
			return true;
		}
	}
	g_waiting.push_back(WaitingOp{.first = Step::WriteRequest,
	                              .path = path,
	                              .onDone = onDone,
	                              .ctx = ctx,
	                              .bytes = std::vector<char>(bytes, bytes + len)});
	return true;
}

void serviceRoutine() {
	DelugeStatus status = DELUGE_OK;
	uint32_t len = 0;

	// The oldest waiting submission starts here, and only here: a callback that submits from inside the step
	// machine below only appends, so nothing re-enters it.
	promoteHead();

	// One tick's grace accounting, before any of the steps below. A step that returns without
	// finishing has spent a tick; enough of those and the channel is not going to answer.
	if (g_pending.step != Step::Idle && ++g_pendingTicks >= kMaxPendingTicks) {
		abandonChannel();
		return;
	}

	switch (g_pending.step) {
	case Step::Idle:
		return;

	case Step::ReadRequest:
	case Step::ReadRetryRequest:
		switch (requestRead(g_pending.path)) {
		case ByteIo::Pending:
			g_pending.step = (g_pending.step == Step::ReadRequest) ? Step::ReadCollect : Step::ReadRetryCollect;
			return;
		case ByteIo::Unknown:
			return; // Refused this tick; ask again on the next one.
		case ByteIo::Ready:
		case ByteIo::Failed:
			finishWith(SettingsOutcome::Unreadable); // Unkeyable path: re-requesting will never help.
			return;
		}
		return;

	case Step::ReadCollect:
	case Step::ReadRetryCollect:
		switch (collectRead(g_pending.path, g_buffer, sizeof(g_buffer), &len, &status)) {
		case ByteIo::Ready:
			deliver(len);
			return;
		case ByteIo::Pending:
			return;
		case ByteIo::Unknown:
			// The answer was displaced before we took it. Ask again -- that is what UNKNOWN means,
			// and a read is idempotent.
			g_pending.step = (g_pending.step == Step::ReadCollect) ? Step::ReadRequest : Step::ReadRetryRequest;
			return;
		case ByteIo::Failed:
			// Only a genuinely absent file earns the migration. Anything else -- an unreadable card,
			// a file too large for the channel -- must NOT be treated as "not there yet", or a card
			// that is failing silently turns into a card whose settings get replaced.
			if (g_pending.step == Step::ReadCollect && status == DELUGE_ERR_NOT_FOUND
			    && g_pending.legacyPath != nullptr) {
				g_pending.step = Step::RenameRequest;
				return;
			}
			if (status == DELUGE_ERR_NOT_FOUND) {
				finishAbsent();
				return;
			}
			finishWith(SettingsOutcome::Unreadable);
			return;
		}
		return;

	case Step::RenameRequest:
		switch (requestRename(g_pending.legacyPath, g_pending.path)) {
		case ByteIo::Pending:
			g_pending.step = Step::RenameCollect;
			return;
		case ByteIo::Unknown:
			return; // Refused this tick; ask again on the next one.
		case ByteIo::Ready:
		case ByteIo::Failed:
			finishWith(SettingsOutcome::Unreadable);
			return;
		}
		return;

	case Step::RenameCollect:
		switch (collectRename(g_pending.path, &status)) {
		case ByteIo::Ready:
			// Moved. Read it again -- and only once: `ReadRetryCollect`'s failure path has no
			// migration branch, so a file that is still absent ends the operation instead of looping.
			g_pending.step = Step::ReadRetryRequest;
			return;
		case ByteIo::Pending:
			return;
		case ByteIo::Unknown:
			g_pending.step = Step::RenameRequest;
			return;
		case ByteIo::Failed:
			// No legacy file to move, which is the ordinary case and not an error to show anyone. The
			// file is genuinely absent in both locations, which is exactly what `onAbsent` is for.
			finishAbsent();
			return;
		}
		return;

	case Step::MkdirAllRequest:
		switch (requestMkdirAll(g_pending.path)) {
		case ByteIo::Pending:
			g_pending.step = Step::MkdirAllCollect;
			return;
		case ByteIo::Unknown:
			return; // Refused this tick; ask again on the next one.
		case ByteIo::Ready:
		case ByteIo::Failed:
			finish(); // Unkeyable path: re-requesting will never help.
			return;
		}
		return;

	case Step::MkdirAllCollect:
		switch (collectMkdirAll(g_pending.path, &status)) {
		case ByteIo::Ready:
		case ByteIo::Failed:
			// Terminal either way. The caller is long gone -- this is a fire-and-forget creation --
			// so there is nobody to report a failure to; what the collect is FOR is taking the answer
			// out of the slot and letting the served count see that the channel did the work.
			finish();
			return;
		case ByteIo::Pending:
			return;
		case ByteIo::Unknown:
			g_pending.step = Step::MkdirAllRequest;
			return;
		}
		return;

	case Step::WriteRequest:
		switch (requestWrite(g_pending.path, g_buffer, g_pending.writeLen)) {
		case ByteIo::Pending:
			g_pending.step = Step::WriteCollect;
			return;
		case ByteIo::Unknown:
			return; // Refused this tick; the payload is still held, so ask again on the next one.
		case ByteIo::Ready:
		case ByteIo::Failed:
			finishWith(SettingsOutcome::WriteFailed); // Unkeyable path: re-requesting will never help.
			return;
		}
		return;

	case Step::WriteCollect:
		switch (collectWrite(g_pending.path, &status)) {
		case ByteIo::Ready:
			finishWith(SettingsOutcome::Written);
			return;
		case ByteIo::Failed:
			// A failed write is terminal and is NOT retried. Retrying a write that the card refuses
			// would re-issue it every tick for as long as the card stays refusing.
			finishWith(SettingsOutcome::WriteFailed);
			return;
		case ByteIo::Pending:
			return;
		case ByteIo::Unknown:
			// Displaced before we took it. The bytes are still held, so re-issue -- writing the same
			// bytes twice reaches the same state.
			g_pending.step = Step::WriteRequest;
			return;
		}
		return;

	case Step::UnlinkRequest:
		switch (requestUnlink(g_pending.path)) {
		case ByteIo::Pending:
			g_pending.step = Step::UnlinkCollect;
			return;
		case ByteIo::Unknown:
			return; // Refused this tick; ask again on the next one.
		case ByteIo::Ready:
		case ByteIo::Failed:
			finishWith(SettingsOutcome::WriteFailed); // Unkeyable path: re-requesting will never help.
			return;
		}
		return;

	case Step::UnlinkCollect:
		switch (collectUnlink(g_pending.path, &status)) {
		case ByteIo::Ready:
			finishWith(SettingsOutcome::Written);
			return;
		case ByteIo::Failed:
			// A file that was never there is exactly the state a removal asks for.
			finishWith(status == DELUGE_ERR_NOT_FOUND ? SettingsOutcome::Written : SettingsOutcome::WriteFailed);
			return;
		case ByteIo::Pending:
			return;
		case ByteIo::Unknown:
			// Displaced before we took it. Removing a file twice reaches the same state.
			g_pending.step = Step::UnlinkRequest;
			return;
		}
		return;
	}
}

int32_t channelServedOps() {
	return g_channelServed;
}

bool idle() {
	return g_pending.step == Step::Idle && g_waiting.empty();
}

bool runInlineWhereLegal(OwnerFn fn, void* ctx) {
	if (deluge::storage::Owner::on_owner() || !deluge::storage::Owner::started()) {
		fn(ctx);
		return true;
	}
	g_illegalInline++;
	return false;
}

int32_t illegalInline() {
	return g_illegalInline;
}

ByteIo requestUnlink(const char* path) {
	if (!keyable(path)) {
		return ByteIo::Failed;
	}
	if (!deluge_file_bytes_request_unlink(path)) {
		// REFUSED because the channel is busy: nothing was queued and nothing was deleted. `Unknown`
		// is the documented "ask again", and the caller must still be holding its intent.
		return ByteIo::Unknown;
	}
	return ByteIo::Pending;
}

ByteIo collectUnlink(const char* path, DelugeStatus* outStatus) {
	if (!keyable(path) || outStatus == nullptr) {
		return ByteIo::Failed;
	}
	return counted(fromAbi(deluge_file_bytes_collect_unlink(path, outStatus)));
}

ByteIo requestExists(const char* path) {
	if (!keyable(path)) {
		return ByteIo::Failed;
	}
	deluge_file_bytes_request_exists(path);
	return ByteIo::Pending;
}

ByteIo collectExists(const char* path, bool* outPresent, DelugeStatus* outStatus) {
	if (!keyable(path) || outPresent == nullptr || outStatus == nullptr) {
		return ByteIo::Failed;
	}
	return counted(fromAbi(deluge_file_bytes_collect_exists(path, outPresent, outStatus)));
}

ByteIo requestMkdirAll(const char* path) {
	if (!keyable(path)) {
		return ByteIo::Failed;
	}
	if (!deluge_file_bytes_request_mkdir_all(path)) {
		// REFUSED because the channel is busy: nothing was queued and nothing was created. `Unknown`
		// is the documented "ask again", and the caller must still be holding its intent.
		return ByteIo::Unknown;
	}
	return ByteIo::Pending;
}

ByteIo collectMkdirAll(const char* path, DelugeStatus* outStatus) {
	if (!keyable(path) || outStatus == nullptr) {
		return ByteIo::Failed;
	}
	return counted(fromAbi(deluge_file_bytes_collect_mkdir_all(path, outStatus)));
}

ByteIo requestSetTime(const char* path, DelugeTimestamp timestamp) {
	if (!keyable(path)) {
		return ByteIo::Failed;
	}
	if (!deluge_file_bytes_request_set_time(path, timestamp)) {
		return ByteIo::Unknown;
	}
	return ByteIo::Pending;
}

ByteIo collectSetTime(const char* path, DelugeStatus* outStatus) {
	if (!keyable(path) || outStatus == nullptr) {
		return ByteIo::Failed;
	}
	return counted(fromAbi(deluge_file_bytes_collect_set_time(path, outStatus)));
}

ByteIo requestReadAt(const char* path, uint32_t offset, uint32_t len) {
	if (!keyable(path) || len > DELUGE_FILE_BYTES_BLOCK) {
		// Terminal, not a retry: a request above the block bound will be above it next tick too.
		return ByteIo::Failed;
	}
	if (!deluge_file_bytes_request_read_at(path, offset, len)) {
		return ByteIo::Unknown;
	}
	return ByteIo::Pending;
}

ByteIo collectReadAt(const char* path, char* buf, uint32_t cap, uint32_t* outLen, DelugeStatus* outStatus) {
	if (!keyable(path) || buf == nullptr || outLen == nullptr || outStatus == nullptr) {
		return ByteIo::Failed;
	}
	return counted(
	    fromAbi(deluge_file_bytes_collect_read_at(path, reinterpret_cast<uint8_t*>(buf), cap, outLen, outStatus)));
}

ByteIo requestWriteAt(const char* path, uint32_t offset, const char* bytes, uint32_t len) {
	if (!keyable(path) || bytes == nullptr || len > DELUGE_FILE_BYTES_BLOCK) {
		return ByteIo::Failed;
	}
	if (!deluge_file_bytes_request_write_at(path, offset, reinterpret_cast<const uint8_t*>(bytes), len)) {
		return ByteIo::Unknown;
	}
	return ByteIo::Pending;
}

ByteIo collectWriteAt(const char* path, uint32_t* outWritten, DelugeStatus* outStatus) {
	if (!keyable(path) || outWritten == nullptr || outStatus == nullptr) {
		return ByteIo::Failed;
	}
	return counted(fromAbi(deluge_file_bytes_collect_write_at(path, outWritten, outStatus)));
}

ByteIo requestStat(const char* path) {
	if (!keyable(path)) {
		return ByteIo::Failed;
	}
	deluge_file_bytes_request_stat(path);
	return ByteIo::Pending;
}

ByteIo collectStat(const char* path, uint32_t* outSize, DelugeStatus* outStatus) {
	if (!keyable(path) || outSize == nullptr || outStatus == nullptr) {
		return ByteIo::Failed;
	}
	return counted(fromAbi(deluge_file_bytes_collect_stat(path, outSize, outStatus)));
}

ByteIo requestCopy(const char* from, const char* to) {
	if (!keyable(from) || !keyable(to)) {
		return ByteIo::Failed;
	}
	if (!deluge_file_bytes_request_copy(from, to)) {
		return ByteIo::Unknown;
	}
	return ByteIo::Pending;
}

ByteIo collectCopy(const char* to, DelugeStatus* outStatus) {
	if (!keyable(to) || outStatus == nullptr) {
		return ByteIo::Failed;
	}
	return counted(fromAbi(deluge_file_bytes_collect_copy(to, outStatus)));
}

int32_t interactionTierIo(IoTier which) {
	return g_interactionTierIo[static_cast<size_t>(which)];
}

void noteIoTier(IoTier which) {
	if (!deluge::storage::Owner::on_owner() && deluge::storage::Owner::started()) {
		// Counted as well as frozen: the count is what
		// let each gate report its pre-conversion number, which is the evidence that the conversion
		// did something. The freeze is what keeps it from coming back.
		g_interactionTierIo[static_cast<size_t>(which)]++;
		if (ALPHA_OR_BETA_VERSION && kFreezeOnViolation[static_cast<size_t>(which)]) {
			FREEZE_WITH_ERROR("TIR6");
		}
	}
}

} // namespace deluge::storage::bytes
