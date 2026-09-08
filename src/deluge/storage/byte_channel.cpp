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
#include "libdeluge/file_bytes.h"
#include "storage/owner.h"

#include <cstddef>
#include <cstring>

namespace deluge::storage::bytes {

namespace {

/// How many of each consumer's file operations have begun on the interaction tier. Indexed by
/// @ref IoTier. See @ref interactionTierIo.
int32_t g_interactionTierIo[2] = {0, 0};

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
	return fromAbi(deluge_file_bytes_collect_read(path, reinterpret_cast<uint8_t*>(buf), cap, outLen, outStatus));
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
	return fromAbi(deluge_file_bytes_collect_write(path, outStatus));
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
	return fromAbi(deluge_file_bytes_collect_rename(to, outStatus));
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
};

/// The one operation in flight, if any.
///
/// One rather than a queue because the channel underneath is single-flight, and because a second
/// submission is REFUSED rather than queued -- see `submitWrite`'s doc on why superseding is wrong
/// here.
struct PendingOp {
	Step step = Step::Idle;
	/// A compile-time literal in every caller (`SETTINGS/CommunityFeatures.XML` and its three
	/// siblings), so holding the pointer rather than a copy is safe and the buffer below can be spent
	/// entirely on contents.
	const char* path = nullptr;
	const char* legacyPath = nullptr;
	ParseFn parse = nullptr;
	AbsentFn onAbsent = nullptr;
	void* ctx = nullptr;
	uint32_t writeLen = 0;
};

PendingOp g_pending;

/// See @ref channelServedOps.
int32_t g_channelServed = 0;

/// The bytes in flight: a write's payload on the way out, a read's contents on the way in.
///
/// One buffer for both directions because only one operation is ever pending. Sized to the channel's
/// own bound, so anything the channel will carry fits here and nothing has to be refused twice for
/// the same reason.
char g_buffer[DELUGE_FILE_BYTES_MAX_BYTES];

/// Give up on the pending operation and go idle.
///
/// A read that ends here leaves the consumer's in-memory defaults standing, which is what
/// `bootstrap_action()`'s `UseInMemoryOnly` already does for an unreadable card: replacing a user's
/// real settings with defaults because the card hiccuped is the failure that policy exists to
/// prevent, and it is no more acceptable here.
///
/// @see finishAbsent for the one failure that is NOT this.
void finish() {
	if (g_pending.step != Step::Idle) {
		// Counted at every terminal exit, the failures included: what this proves is that the channel
		// RAN, not that the card was cooperative.
		g_channelServed++;
	}
	g_pending = PendingOp{};
}

/// End a read whose file is genuinely absent, telling the consumer so.
///
/// Split from @ref finish because only `DELUGE_ERR_NOT_FOUND` reaches here. Two of the four
/// consumers write their defaults on absence, and routing an unreadable card down this path would
/// have them overwrite settings that are still on the card.
void finishAbsent() {
	AbsentFn onAbsent = g_pending.onAbsent;
	void* ctx = g_pending.ctx;
	finish();
	if (onAbsent != nullptr) {
		onAbsent(ctx);
	}
}

/// Hand a collected read to its consumer, then go idle.
void deliver(uint32_t len) {
	ParseFn parse = g_pending.parse;
	void* ctx = g_pending.ctx;
	finish();
	if (parse != nullptr) {
		parse(ctx, g_buffer, len);
	}
}

} // namespace

bool submitRead(const char* path, const char* legacyPath, ParseFn parse, AbsentFn onAbsent, void* ctx) {
	if (!usingByteChannel() || g_pending.step != Step::Idle) {
		return false;
	}
	g_pending.step = Step::ReadRequest;
	g_pending.path = path;
	g_pending.legacyPath = legacyPath;
	g_pending.parse = parse;
	g_pending.onAbsent = onAbsent;
	g_pending.ctx = ctx;
	return true;
}

bool submitWrite(const char* path, const char* bytes, uint32_t len) {
	if (!usingByteChannel() || g_pending.step != Step::Idle) {
		return false;
	}
	if (bytes == nullptr || len > sizeof(g_buffer)) {
		return false;
	}
	memcpy(g_buffer, bytes, len);
	g_pending.step = Step::WriteRequest;
	g_pending.path = path;
	g_pending.writeLen = len;
	return true;
}

void runOnStorageOwner(OwnerFn fn, void* ctx) {
	// `!started()` is the boot case, and running INLINE there is not an optimisation -- it is the only
	// thing that works. `Owner::run` QUEUES when the worker has not started, and nothing pumps that
	// queue yet, so dispatching here would silently drop `deluge.cpp`'s three boot reads and the
	// device would come up with defaults for every setting. `midi_device_manager.cpp` already carried
	// a comment saying exactly this about its own read; this is that rule, moved to the one place
	// every consumer now goes through.
	if (deluge::storage::Owner::on_owner() || !deluge::storage::Owner::started()) {
		fn(ctx);
		return;
	}
	if (!deluge::storage::Owner::run_or_inline(fn, ctx)) {
		// The owner queue is full. Running it here would be the tier violation this whole rung exists
		// to remove, so the operation does not happen -- and `noteIoTier()` inside the body is
		// what would have reported it if it had.
		return;
	}
}

void serviceRoutine() {
	DelugeStatus status = DELUGE_OK;
	uint32_t len = 0;

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
			finish(); // Unkeyable path: re-requesting will never help.
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
			finish();
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
			finish();
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

	case Step::WriteRequest:
		switch (requestWrite(g_pending.path, g_buffer, g_pending.writeLen)) {
		case ByteIo::Pending:
			g_pending.step = Step::WriteCollect;
			return;
		case ByteIo::Unknown:
			return; // Refused this tick; the payload is still held, so ask again on the next one.
		case ByteIo::Ready:
		case ByteIo::Failed:
			finish();
			return;
		}
		return;

	case Step::WriteCollect:
		switch (collectWrite(g_pending.path, &status)) {
		case ByteIo::Ready:
		case ByteIo::Failed:
			// A failed write is terminal and is NOT retried. Retrying a write that the card refuses
			// would re-issue it every tick for as long as the card stays refusing.
			finish();
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
	}
}

int32_t channelServedOps() {
	return g_channelServed;
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
	return fromAbi(deluge_file_bytes_collect_unlink(path, outStatus));
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
	return fromAbi(deluge_file_bytes_collect_exists(path, outPresent, outStatus));
}

int32_t interactionTierIo(IoTier which) {
	return g_interactionTierIo[static_cast<size_t>(which)];
}

void noteIoTier(IoTier which) {
	if (!deluge::storage::Owner::on_owner() && deluge::storage::Owner::started()) {
		// Counted as well as frozen, for the reason `SaveUI::noteSaveTier()` gives: the count is what
		// let each gate report its pre-conversion number, which is the evidence that the conversion
		// did something. The freeze is what keeps it from coming back.
		g_interactionTierIo[static_cast<size_t>(which)]++;
		if (ALPHA_OR_BETA_VERSION) {
			FREEZE_WITH_ERROR("TIR6");
		}
	}
}

} // namespace deluge::storage::bytes
