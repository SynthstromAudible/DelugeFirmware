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

#include "storage/startup_song.h"
#include <cstdio>

#include "definitions_cxx.hpp"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "io/file.hpp"
#include "libdeluge/file_write.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "storage/byte_channel.h"
#include "storage/existence_policy.h"
#include "storage/flash_storage.h"
#include "storage/startup_song_policy.h"
#include "storage/storage_manager.h"
#include "util/functions.h"

#include <cstring>
#include <expected>
#include <string>

namespace deluge::storage {

namespace {

/// The template song, and the fallback when the recorded startup song has gone.
constexpr const char* kDefaultSongFullPath = "SONGS/DEFAULT.XML";

/// How many consecutive unproductive attempts before the machine stops asking the channel.
///
/// The channel is single-flight across all operation kinds, so a settings write in flight refuses
/// this machine's request. Retrying is right -- the machine is already ticking -- but an unbounded
/// retry would let a busy channel wedge boot. After this many the machine runs the operation inline
/// where blocking is legal; after boot, off the owner, that is refused, and the operation fails
/// through the machine's own cautious paths.
///
/// **It counts re-asks as well as refusals, and that is the half that matters.** A refusal is a
/// `false` from a request that returns one; but the read and existence requests return nothing at
/// all, so a channel that ACCEPTS a request and never answers it produces neither a refusal nor a
/// result -- just `UNKNOWN` forever, and a machine that re-asks forever. That is exactly the livelock
/// shape an earlier rung shipped, and it is what a dead channel looks like from here.
///
/// As many as the stall bound below, and for the same reason. After boot there is no owner to fall
/// back to, so giving up on a channel that is merely busy would skip a startup song that would have
/// loaded a few ticks later: only a channel that has made no progress for as long as a stalled one is
/// given up on.
constexpr int32_t kMaxAttemptsWithoutProgress = 600;

/// How many ticks one channel operation may sit PENDING before the machine gives up on the channel.
///
/// A separate, far larger bound than the one above, because it is guarding a different failure. A
/// refusal is instant and means "busy"; PENDING means "working", and a real card operation is
/// legitimately allowed to take a while. But a channel whose driver is not running answers PENDING
/// **forever** -- the request sits in its slot with nothing to take it -- and that is neither a
/// refusal nor an answer, so the bound above never sees it. Without this one the machine re-asks
/// until the end of time and boot never completes.
///
/// That is not hypothetical: it is exactly what leaving the Rust worker unspawned produced, and it is
/// the same livelock shape an earlier rung shipped. Six hundred ticks is several seconds at this
/// routine's rate -- far beyond any real operation, far short of forever.
constexpr int32_t kMaxPendingTicks = 600;

/// @name The machine's state
/// @{
StartupPhase g_phase = StartupPhase::Idle;
/// Whether the song about to be loaded is the TEMPLATE, so the completion wipes its name.
bool g_usingTemplate = false;
/// The staged template document, for the synchronous arm's lambda (which takes no captures).
const uint8_t* g_templateBytes = nullptr;
uint32_t g_templateLength = 0;
/// The canary for this attempt. Outlives `armStartupSong` because the load's completion removes it.
std::string g_canaryPath;
/// The song this attempt means to load. Reassigned when the fallback template is used.
std::string g_songPath;
/// Whether the current phase has issued its request yet.
bool g_requested = false;
/// Consecutive attempts at the current request that produced neither an answer nor progress.
/// See @ref kMaxAttemptsWithoutProgress.
int32_t g_attemptsWithoutProgress = 0;
/// Consecutive ticks the current request has spent PENDING. See @ref kMaxPendingTicks.
int32_t g_pendingTicks = 0;
/// @}

/// @name The synchronous arm's answers
///
/// The synchronous arm runs inline where blocking is legal, or is refused, and the collect reads its
/// answer on the next tick either way. These hold it in between.
/// @{
bool g_ownerDone = false;
std::expected<bool, deluge::io::Status> g_ownerPresence;
bool g_ownerOk = false;
/// @}

/// Whether the canary write was CONFIRMED terminal before the machine advanced past it.
///
/// The ordering guarantee stated as observable state. The card check beside it in the gate proves the
/// canary is there; this proves the machine WAITED for it rather than arriving there by luck --
/// which on a host where the channel completes within a tick is the only way to tell the two apart.
bool g_canaryConfirmed = false;

/// Whether the startup song was loaded from the template, so its name must be wiped.
bool g_startupSongWasTemplate = false;
/// The canary to remove once the load is done. Held here rather than on the arming call's stack,
/// which is long gone by the time the load finishes.
std::string g_startupFailSafePath;

/// Enter `phase` with a clean request state.
void enter(StartupPhase phase) {
	g_phase = phase;
	g_requested = false;
	g_attemptsWithoutProgress = 0;
	g_pendingTicks = 0;
	g_ownerDone = false;
}

/// Whether the channel should carry this operation, or the synchronous arm should.
///
/// Falls back permanently once `kMaxAttemptsWithoutProgress` consecutive unproductive attempts have
/// been seen for the current phase -- see that constant.
bool viaChannel() {
	return deluge::storage::bytes::usingByteChannel() && g_attemptsWithoutProgress < kMaxAttemptsWithoutProgress
	       && g_pendingTicks < kMaxPendingTicks;
}

/// Note one more tick spent waiting, and abandon the channel for this phase once that has gone on too
/// long. Returns whether the caller should keep waiting.
///
/// The re-request on giving up is what makes the fallback take effect: `viaChannel()` is consulted
/// again at the next issue, and by then it answers false.
bool stillWaiting() {
	if (++g_pendingTicks < kMaxPendingTicks) {
		return true;
	}
	g_requested = false; // re-issue, on the synchronous arm this time
	return false;
}

/// Issue an existence check for `path` on whichever arm is in force.
void issueExists(const char* path) {
	if (viaChannel()) {
		if (deluge::storage::bytes::requestExists(path) == deluge::storage::bytes::ByteIo::Pending) {
			g_requested = true;
		}
		else {
			g_attemptsWithoutProgress++;
		}
		return;
	}
	// The synchronous arm, where blocking is legal. `noteIoTier` lives inside the body, not here: what
	// the gate counts is where the card is touched.
	static std::string ownerPath;
	ownerPath = path;
	g_ownerDone = false;
	if (!deluge::storage::bytes::runInlineWhereLegal(
	        [](void*) {
		        deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Startup);
		        g_ownerPresence = StorageManager::fileExists(ownerPath.c_str());
		        g_ownerDone = true;
	        },
	        nullptr)) {
		// Refused: after boot, off the owner, only a stalled channel reaches here. The question has no
		// answer -- "could not tell", which `startup_action` reads as Unusable and the policy never acts on.
		g_ownerPresence = std::unexpected(deluge::io::Status::TIMEOUT);
		g_ownerDone = true;
	}
	g_requested = true;
}

/// Collect the existence check for `path`, or `nullopt` while it is still running.
std::optional<StartupPresence> collectExists(const char* path) {
	if (viaChannel()) {
		bool present = false;
		DelugeStatus status = DELUGE_OK;
		switch (deluge::storage::bytes::collectExists(path, &present, &status)) {
		case deluge::storage::bytes::ByteIo::Pending:
			(void)stillWaiting();
			return std::nullopt;
		case deluge::storage::bytes::ByteIo::Unknown:
			// "Ask again" -- the documented meaning. Counted, because a channel that accepts and never
			// answers looks exactly like this, forever.
			g_requested = false;
			g_attemptsWithoutProgress++;
			return std::nullopt;
		case deluge::storage::bytes::ByteIo::Ready:
			// A real answer, either way. `startup_action` is what keeps "known absent" apart from
			// "could not tell", which is the whole point of this operation being three-state.
			return startup_action(std::expected<bool, deluge::io::Status>(present));
		case deluge::storage::bytes::ByteIo::Failed:
			return startup_action(std::unexpected(deluge::io::to_status(status)));
		}
		return StartupPresence::Unusable;
	}
	if (!g_ownerDone) {
		return std::nullopt;
	}
	return startup_action(g_ownerPresence);
}

/// Issue the canary write on whichever arm is in force. A zero-byte file: only its EXISTENCE means
/// anything.
void issueCanaryWrite() {
	deluge_file_invalidate_cache();
	if (viaChannel()) {
		if (deluge::storage::bytes::requestWrite(g_canaryPath.c_str(), "", 0)
		    == deluge::storage::bytes::ByteIo::Pending) {
			g_requested = true;
		}
		else {
			g_attemptsWithoutProgress++;
		}
		return;
	}
	g_ownerDone = false;
	if (!deluge::storage::bytes::runInlineWhereLegal(
	        [](void*) {
		        deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Startup);
		        auto canary = deluge::io::File::open(g_canaryPath, DELUGE_FILE_WRITE_CREATE);
		        if (canary.has_value()) {
			        (void)canary->close(); // RAII would close anyway; explicit for parity with f_close
			        g_ownerOk = true;
		        }
		        else {
			        g_ownerOk = false;
		        }
		        g_ownerDone = true;
	        },
	        nullptr)) {
		// Refused: no canary. The machine already treats that as "skip the startup song", with nothing to
		// clean up.
		g_ownerOk = false;
		g_ownerDone = true;
	}
	g_requested = true;
}

/// Serialise the template song into the active writer's memory buffer.
///
/// Same staging SaveSongUI uses (`setMemoryBased()` then `writeToFile()`): the document is built in
/// RAM so the card only ever sees one whole-file write, which is what lets that write go through the
/// byte channel instead of a blocking open-write-close.
bool stageTemplateDocument(const uint8_t** outBytes, uint32_t* outLen) {
	currentSong->name = "DEFAULT";
	FileWriter& writer = static_cast<FileWriter&>(smSerializer);
	writer.setMemoryBased();
	currentSong->writeToFile();
	const int32_t written = writer.bytesWritten();
	if (written <= 0) {
		return false; // An empty document is a failed write, not a small one.
	}
	*outBytes = reinterpret_cast<const uint8_t*>(writer.getBufferPtr());
	*outLen = static_cast<uint32_t>(written);
	return true;
}

/// Issue the template write on whichever arm is in force.
void issueTemplateWrite() {
	deluge_file_invalidate_cache();
	const uint8_t* bytes = nullptr;
	uint32_t len = 0;
	const bool staged = stageTemplateDocument(&bytes, &len);
	g_templateLength = len;
	if (!staged) {
		g_ownerOk = false;
		g_ownerDone = true;
		g_requested = true;
		return;
	}
	if (viaChannel()) {
		// The WRITE channel, not the byte channel: a song document runs to some 11 KB, and the byte
		// channel refuses anything past DELUGE_FILE_BYTES_MAX_BYTES (8192) -- it would answer Failed
		// forever. This is the same channel SaveSongUI puts a song document on, for the same reason.
		if (deluge_file_write_request_write_file(kDefaultSongFullPath, bytes, len, /*may_overwrite=*/false)) {
			g_requested = true;
		}
		else {
			g_attemptsWithoutProgress++;
		}
		return;
	}
	// The synchronous arm, where blocking is legal -- boot, before the channel's task runs.
	g_ownerDone = false;
	g_templateBytes = bytes;
	if (!deluge::storage::bytes::runInlineWhereLegal(
	        [](void*) {
		        deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Startup);
		        auto file = deluge::io::File::open(kDefaultSongFullPath, DELUGE_FILE_WRITE_CREATE);
		        if (file.has_value()) {
			        const auto put = file->write(std::span<const std::byte>(
			            reinterpret_cast<const std::byte*>(g_templateBytes), g_templateLength));
			        g_ownerOk = put.has_value() && *put == g_templateLength;
			        (void)file->close();
		        }
		        else {
			        g_ownerOk = false;
		        }
		        g_ownerDone = true;
	        },
	        nullptr)) {
		g_ownerOk = false;
		g_ownerDone = true;
	}
	g_requested = true;
}

/// Collect the template write, or `nullopt` while it is still running.
std::optional<bool> collectTemplateWrite() {
	if (viaChannel()) {
		uint32_t written = 0;
		uint8_t existed = 0;
		switch (deluge_file_write_collect_write_file(kDefaultSongFullPath, &written, &existed)) {
		case DELUGE_FILE_WRITE_PENDING:
			(void)stillWaiting();
			return std::nullopt;
		case DELUGE_FILE_WRITE_UNKNOWN:
			// Displaced before we took it. Re-staging writes the same document again, which is safe.
			g_requested = false;
			g_attemptsWithoutProgress++;
			return std::nullopt;
		case DELUGE_FILE_WRITE_READY:
			// A short write is a card error, not a success: the template would be half a document.
			return written == g_templateLength;
		default:
			return false;
		}
	}
	if (!g_ownerDone) {
		return std::nullopt;
	}
	return g_ownerOk;
}

/// Collect the canary write, or `nullopt` while it is still running.
///
/// **This is the ordering guarantee.** The caller advances to the load only on `true`, never on
/// "requested": a canary that has not landed when the device dies mid-load is precisely the failure
/// the fail-safe exists to prevent.
std::optional<bool> collectCanaryWrite() {
	if (viaChannel()) {
		DelugeStatus status = DELUGE_OK;
		switch (deluge::storage::bytes::collectWrite(g_canaryPath.c_str(), &status)) {
		case deluge::storage::bytes::ByteIo::Pending:
			(void)stillWaiting();
			return std::nullopt;
		case deluge::storage::bytes::ByteIo::Unknown:
			// Displaced before we took it; writing the same bytes twice is safe. Counted for the same
			// reason as the existence collect's.
			g_requested = false;
			g_attemptsWithoutProgress++;
			return std::nullopt;
		case deluge::storage::bytes::ByteIo::Ready:
			g_canaryConfirmed = true;
			return true;
		case deluge::storage::bytes::ByteIo::Failed:
			return false;
		}
		return false;
	}
	if (!g_ownerDone) {
		return std::nullopt;
	}
	g_canaryConfirmed = g_ownerOk;
	return g_ownerOk;
}

/// Issue the canary removal. Fire-and-forget in spirit: a canary that outlives its attempt by a few
/// ticks costs nothing, and one never removed costs a single skipped startup song next boot, which is
/// the fail-safe being conservative rather than failing.
void issueUnlink(const std::string& path) {
	deluge_file_invalidate_cache();
	if (viaChannel()) {
		if (deluge::storage::bytes::requestUnlink(path.c_str()) == deluge::storage::bytes::ByteIo::Pending) {
			g_requested = true;
		}
		else {
			g_attemptsWithoutProgress++;
		}
		return;
	}
	static std::string ownerPath;
	ownerPath = path;
	g_ownerDone = false;
	if (!deluge::storage::bytes::runInlineWhereLegal(
	        [](void*) {
		        deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Startup);
		        (void)deluge::io::unlink(ownerPath);
		        g_ownerDone = true;
	        },
	        nullptr)) {
		// Refused: the canary stays. The next boot takes the cautious route -- a skipped song, never a
		// crash loop.
		g_ownerDone = true;
	}
	g_requested = true;
}

/// Collect the canary removal. A path that was already gone reports FAILED with NOT_FOUND, which for
/// a cleanup is success by another name, so both terminal statuses finish the phase.
bool unlinkFinished(const std::string& path) {
	if (viaChannel()) {
		DelugeStatus status = DELUGE_OK;
		switch (deluge::storage::bytes::collectUnlink(path.c_str(), &status)) {
		case deluge::storage::bytes::ByteIo::Pending:
			(void)stillWaiting();
			return false;
		case deluge::storage::bytes::ByteIo::Unknown:
			g_requested = false;
			g_attemptsWithoutProgress++;
			return false;
		case deluge::storage::bytes::ByteIo::Ready:
		case deluge::storage::bytes::ByteIo::Failed:
			return true;
		}
		return true;
	}
	return g_ownerDone;
}

} // namespace

void armStartupSong() {
	auto startupSongMode = FlashStorage::defaultStartupSongMode;
	if (startupSongMode == StartupSongMode::BLANK) {
		enter(StartupPhase::Done);
		return;
	}

	const char* filename =
	    startupSongMode == StartupSongMode::TEMPLATE ? kDefaultSongFullPath : runtimeFeatureSettings.getStartupSong();
	// The messages we show to the user are mostly plain language, but for non-user facing errors we
	// use codes. All messages are < 20 chars.
	if (strlen(filename) + 1 > 2048) {
		display->consoleText("Startup path too long");
		enter(StartupPhase::Done);
		return;
	}

	g_canaryConfirmed = false;
	g_canaryPath = "SONGS/__STARTUP_OFF_CHECK_";
	{
		char replaced[sizeof(char) * strlen(filename) + 1]; // +1 for NULL terminator
		replace_char(replaced, filename, '/', '_');
		g_canaryPath.append(replaced);
	}
	g_songPath = filename;
	g_usingTemplate = (startupSongMode == StartupSongMode::TEMPLATE);
	enter(StartupPhase::CheckCanary);
}

void startupSongRoutine() {
	switch (g_phase) {
	case StartupPhase::Idle:
	case StartupPhase::Done:
		return;

	case StartupPhase::CheckCanary: {
		if (!g_requested) {
			issueExists(g_canaryPath.c_str());
			return;
		}
		auto answer = collectExists(g_canaryPath.c_str());
		if (!answer.has_value()) {
			return;
		}
		if (*answer == StartupPresence::Confirmed) {
			// canary exists, previous boot failed?
			display->consoleText("Startup fault F1");
		}
		auto step = nextStartupPhase(StartupPhase::CheckCanary, *answer, g_usingTemplate);
		g_usingTemplate = step.usingTemplate;
		enter(step.next); // Done keeps the canary, deliberately -- there is no unlink on this path.
		return;
	}

	case StartupPhase::CheckTemplate: {
		if (!g_requested) {
			issueExists(kDefaultSongFullPath);
			return;
		}
		auto answer = collectExists(kDefaultSongFullPath);
		if (!answer.has_value()) {
			return;
		}
		if (*answer == StartupPresence::Missing) {
			display->consoleText("Creating template");
			// The write itself is StartupPhase::WriteTemplate below. Done inline here it was a blocking
			// card write on whatever tier this routine was ticking on -- measured, it froze
			// startup-failsafe on TIR6.
		}
		auto step = nextStartupPhase(StartupPhase::CheckTemplate, *answer, g_usingTemplate);
		g_usingTemplate = step.usingTemplate;
		enter(step.next);
		return;
	}

	case StartupPhase::WriteTemplate: {
		if (!g_requested) {
			issueTemplateWrite();
			return;
		}
		auto landed = collectTemplateWrite();
		if (!landed.has_value()) {
			return;
		}
		if (!*landed) {
			// Not fatal, and deliberately so: the inline version returned early on a failed create and
			// the machine carried on to the canary regardless. The canary is what protects the load.
			display->consoleText("Startup fault F3");
		}
		// The presence argument is unused for this phase -- see nextStartupPhase -- so any value does.
		auto step = nextStartupPhase(StartupPhase::WriteTemplate, StartupPresence::Confirmed, g_usingTemplate);
		g_usingTemplate = step.usingTemplate;
		enter(step.next);
		return;
	}

	case StartupPhase::WriteCanary: {
		if (!g_requested) {
			issueCanaryWrite();
			return;
		}
		auto landed = collectCanaryWrite();
		if (!landed.has_value()) {
			return;
		}
		if (!*landed) {
			// Could not create canary, not a user-facing error so code is fine. We're going to skip
			// the startup song to avoid any issues.
			display->consoleText("Startup fault F2");
			enter(StartupPhase::Done); // no canary, no cleanup
			return;
		}
		// THE ORDERING GUARANTEE: only a canary confirmed on the card lets the load proceed.
		enter(StartupPhase::CheckSong);
		return;
	}

	case StartupPhase::CheckSong: {
		if (!g_requested) {
			issueExists(g_songPath.c_str());
			return;
		}
		auto answer = collectExists(g_songPath.c_str());
		if (!answer.has_value()) {
			return;
		}
		if (*answer == StartupPresence::Missing) {
			if (g_usingTemplate) {
				// we tried to create it earlier, but didn't happen? Handled here rather than in
				// `nextStartupPhase` because it is a MESSAGE difference, not a policy one: the
				// destination is the same cleanup either way, and the policy's job is the third state.
				display->consoleText("Startup fault F3");
				enter(StartupPhase::UnlinkCanary);
				return;
			}
			display->consoleText("Song missing");
		}
		auto step = nextStartupPhase(StartupPhase::CheckSong, *answer, g_usingTemplate);
		g_usingTemplate = step.usingTemplate;
		enter(step.next);
		return;
	}

	case StartupPhase::CheckFallbackTemplate: {
		if (!g_requested) {
			issueExists(kDefaultSongFullPath);
			return;
		}
		auto answer = collectExists(kDefaultSongFullPath);
		if (!answer.has_value()) {
			return;
		}
		if (*answer == StartupPresence::Confirmed) {
			// user didn't ask for the template, but if it exists let's use it instead
			display->consoleText("Using template");
			g_songPath = kDefaultSongFullPath;
		}
		auto step = nextStartupPhase(StartupPhase::CheckFallbackTemplate, *answer, g_usingTemplate);
		g_usingTemplate = step.usingTemplate;
		enter(step.next);
		return;
	}

	case StartupPhase::BeginLoad: {
		// Load song, if we got this far!
		currentSong->setSongFullPath(g_songPath.c_str());
		if (openUI(&loadSongUI)) {
			// beginLoad() returns as soon as the load has to wait, so the cleanup cannot stay here --
			// it goes in the completion instead.
			g_startupSongWasTemplate = g_usingTemplate;
			g_startupFailSafePath = g_canaryPath;
			loadSongUI.beginLoad(&finishStartupSong);
			enter(StartupPhase::Done);
			return;
		}
		// what just failed??
		display->consoleText("Startup fault F4");
		enter(StartupPhase::UnlinkCanary);
		return;
	}

	case StartupPhase::UnlinkCanary: {
		if (!g_requested) {
			issueUnlink(g_canaryPath);
			return;
		}
		if (!unlinkFinished(g_canaryPath)) {
			return;
		}
		enter(StartupPhase::Done);
		return;
	}
	}
}

bool startupSongCanaryConfirmed() {
	return g_canaryConfirmed;
}

bool startupSongAwaitingCanary() {
	return g_phase == StartupPhase::WriteCanary;
}

const char* startupSongCanaryPath() {
	return g_canaryPath.c_str();
}

bool startupSongIdle() {
	return g_phase == StartupPhase::Idle || g_phase == StartupPhase::Done;
}

void finishStartupSong() {
	if (g_startupSongWasTemplate) {
		// Wipe the name so the Save action asks you for a new song
		currentSong->name.clear();
	}
	// ...but we got this far, cleanup. Requested and not collected: this runs from the load's
	// completion, which has nowhere to poll from, and an uncollected answer costs nothing -- see
	// `issueUnlink`.
	deluge_file_invalidate_cache();
	if (deluge::storage::bytes::usingByteChannel()) {
		if (deluge::storage::bytes::requestUnlink(g_startupFailSafePath.c_str())
		    == deluge::storage::bytes::ByteIo::Pending) {
			g_startupFailSafePath.clear();
			return;
		}
		// Refused because the channel is busy. Hand the cleanup to the machine's own unlink phase, which
		// keeps asking within its bounds: dropping it here would leave a canary that skips the startup song
		// on the next boot.
		g_canaryPath = g_startupFailSafePath;
		g_startupFailSafePath.clear();
		enter(StartupPhase::UnlinkCanary);
		return;
	}
	// On the owner, or during boot: blocking is legal here, so the body always runs.
	static std::string path;
	path = g_startupFailSafePath;
	(void)deluge::storage::bytes::runInlineWhereLegal(
	    [](void*) {
		    deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Startup);
		    (void)deluge::io::unlink(path);
	    },
	    nullptr);
	g_startupFailSafePath.clear();
}

} // namespace deluge::storage
