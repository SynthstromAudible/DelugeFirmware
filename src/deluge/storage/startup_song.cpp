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
#include <algorithm>
#include <cstdio>

#include "definitions_cxx.hpp"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/ui.h"
#include "hid/display/oled_canvas/canvas.h"
#include "io/debug/log.h"
#include "io/file.hpp"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_follow.h"
#include "io/stall_bound.h"
#include "libdeluge/file_bytes.h"
#include "model/project_bridge/song_serialization.h"
#include "model/sample/recorder_write_policy.h" // kMaxUnproductiveUs
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "runtime/app_loop.h"
#include "storage/byte_channel.h"
#include "storage/existence_policy.h"
#include "storage/flash_storage.h"
#include "storage/startup_song_policy.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/notify.h"

#include <cstring>
#include <expected>
#include <string>

namespace deluge::storage {

namespace {

/// Where a missing template song is created.
constexpr const char* kNewTemplatePath = kTemplatePaths[0];

/// How long the machine's attempts may go unproductive, with the channel finishing nothing, before it stops asking
/// the channel, in microseconds.
///
/// The channel is single-flight across all operation kinds, so a settings write in flight refuses
/// this machine's request. Retrying is right -- the machine is already ticking -- but an unbounded
/// retry would let a busy channel wedge boot. After this many the machine runs the operation inline
/// where blocking is legal; after boot, that is refused, and the operation fails
/// through the machine's own cautious paths.
///
/// **It measures re-asks as well as refusals, and that is the half that matters.** A refusal is a
/// `false` from a request that returns one; but the read and existence requests return nothing at
/// all, so a channel that ACCEPTS a request and never answers it produces neither a refusal nor a
/// result -- just `UNKNOWN` forever, and a machine that re-asks forever. That livelock shape is what
/// a dead channel looks like from here.
///
/// As long as the stall bound below, and for the same reason. After boot the inline arm refuses to
/// run, so giving up on a channel that is merely busy would skip a startup song that would have
/// loaded a few ticks later: only a channel that has made no progress for as long as a stalled one is
/// given up on.
constexpr uint64_t kMaxWithoutProgressUs = deluge::io::kChannelStallUs;

/// @name The settings prelude
///
/// The three settings reads, run behind the same card-ready trigger the fail-safe machine already
/// uses. They are NOT phases of `StartupPhase`: that enum is driven by a
/// PURE policy over existence answers (`startup_song_policy.h`, unit-tested), and these are driven
/// by completion COUNTS, which that function has no way to express.
///
/// The ordering they exist to make explicit: `armStartupSong` reads
/// `runtimeFeatureSettings.getStartupSong()`, so the settings must have LANDED before it arms.
/// In `deluge_boot()` that was guaranteed only by the order of two lines.
/// @{
enum class SettingsPhase : uint8_t {
	Idle,    ///< Not armed.
	Reading, ///< Reads submitted; waiting for every accepted one to report.
	Done,    ///< All accounted for, or given up on. The song machine may arm.
};
SettingsPhase g_settingsPhase = SettingsPhase::Idle;
/// `submitRead` calls the channel accepted -- i.e. how many completions to expect.
int32_t g_settingsAccepted = 0;
/// Completions reported, whatever the outcome. Absent and Unreadable both count: the prelude's job
/// is to stop waiting, not to judge. Writing defaults because the card hiccuped would replace the
/// user's real settings, which is the line `byte_channel.h` already draws.
int32_t g_settingsDone = 0;
/// How long the prelude has gone without all its completions. Elapsed time, never a tick count --
/// a tick count means a different wait to every caller.
deluge::io::StallBound g_settingsBound{kMaxUnproductiveUs};
static_assert(int64_t{DELUGE_FILE_BYTES_PROTECTED_ANSWER_SPAN_MS} * 1000 * 3 <= kMaxUnproductiveUs,
              "the byte channel's protected-answer span must be well inside the shortest caller bound");
/// Whether the prelude gave up rather than hearing from everything it submitted. Reported by the
/// gate; a give-up still arms the song, because a Deluge that boots to nothing because a settings
/// read stalled is worse than one that boots with defaults.
bool g_settingsGaveUp = false;

void settingsReadDone(void* /*ctx*/, deluge::storage::bytes::SettingsOutcome outcome) {
	// 0 parsed, 1 absent, 2 unreadable (byte_channel.h's SettingsOutcome).
	D_PRINTLN("card settings: read outcome=%d", static_cast<int>(outcome));
	g_settingsDone++;
	deluge::runtime::appLoop.request(startupSongJob);
}
/// @}

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
/// Which of kTemplatePaths the template checks are asking about.
size_t g_templateProbe = 0;
/// Whether the current phase has issued its request yet.
bool g_requested = false;
/// How long the attempts at the current request have produced no answer while the channel finished nothing. See
/// @ref kMaxWithoutProgressUs.
deluge::io::StallBound g_attemptBound{kMaxWithoutProgressUs};
/// How long the current request has spent PENDING. A separate bound from @ref
/// kMaxWithoutProgressUs, because it guards a different failure: a refusal is instant and means "busy", but a
/// channel whose driver is not running answers PENDING forever, which is neither a refusal nor an answer.
deluge::io::StallBound g_pendingBound;
/// @}

/// @name The synchronous arm's answers
///
/// The synchronous arm runs inline where blocking is legal, or is refused, and the collect reads its
/// answer on the next tick either way. These hold it in between.
/// @{
bool g_inlineDone = false;
std::expected<bool, deluge::io::Status> g_inlinePresence;
bool g_inlineOk = false;
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
	g_attemptBound.reset();
	g_pendingBound.reset();
	g_inlineDone = false;
	// A new phase issues its request on its first run, so it runs now.
	deluge::runtime::appLoop.request(startupSongJob);
}

/// Whether the channel should carry this operation, or the synchronous arm should.
///
/// Falls back permanently, for the current phase, once its attempts have gone unproductive for
/// `kMaxWithoutProgressUs` or a request has sat PENDING for the stall bound -- see that constant.
bool viaChannel() {
	return deluge::storage::bytes::usingByteChannel() && !g_attemptBound.expired() && !g_pendingBound.expired();
}

/// Note one more tick spent waiting, and abandon the channel for this phase once that has gone on too
/// long. Returns whether the caller should keep waiting.
///
/// The re-request on giving up is what makes the fallback take effect: `viaChannel()` is consulted
/// again at the next issue, and by then it answers false.
bool stillWaiting() {
	if (!g_pendingBound.tick(deluge_file_bytes_progress())) {
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
			// The span starts at the issue: a job that runs only when woken has a deadline even if no answer comes.
			(void)g_pendingBound.tick(deluge_file_bytes_progress());
		}
		else {
			(void)g_attemptBound.tick(deluge_file_bytes_progress());
		}
		return;
	}
	// The synchronous arm, where blocking is legal. `noteIoTier` lives inside the body, not here: what
	// the gate counts is where the card is touched.
	static std::string inlinePath;
	inlinePath = path;
	g_inlineDone = false;
	if (!deluge::storage::bytes::runDuringBoot(
	        [](void*) {
		        deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Startup);
		        g_inlinePresence = StorageManager::fileExists(inlinePath.c_str());
		        g_inlineDone = true;
	        },
	        nullptr)) {
		// Refused: after boot, only a stalled channel reaches here. The question has no
		// answer -- "could not tell", which `startup_action` reads as Unusable and the policy never acts on.
		g_inlinePresence = std::unexpected(deluge::io::Status::TIMEOUT);
		g_inlineDone = true;
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
			(void)g_attemptBound.tick(deluge_file_bytes_progress());
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
	if (!g_inlineDone) {
		return std::nullopt;
	}
	return startup_action(g_inlinePresence);
}

/// Asks whether the template is there, at each of kTemplatePaths in turn until one is or none is left.
/// @return The answer for kTemplatePaths[g_templateProbe], or nothing while it is still being asked.
std::optional<StartupPresence> probeTemplate() {
	if (!g_requested) {
		issueExists(kTemplatePaths[g_templateProbe]);
		return std::nullopt;
	}
	auto answer = collectExists(kTemplatePaths[g_templateProbe]);
	if (!answer.has_value()) {
		return std::nullopt;
	}
	const TemplateProbe probe = nextTemplateProbe(g_templateProbe, *answer);
	if (probe.askAgain) {
		g_templateProbe = probe.index;
		enter(g_phase); // the same phase, asking about the next path
		return std::nullopt;
	}
	return answer;
}

/// Issue the canary write on whichever arm is in force. A zero-byte file: only its EXISTENCE means
/// anything.
void issueCanaryWrite() {
	if (viaChannel()) {
		if (deluge::storage::bytes::requestWrite(g_canaryPath.c_str(), "", 0)
		    == deluge::storage::bytes::ByteIo::Pending) {
			g_requested = true;
			(void)g_pendingBound.tick(deluge_file_bytes_progress()); // as issueExists
		}
		else {
			(void)g_attemptBound.tick(deluge_file_bytes_progress());
		}
		return;
	}
	g_inlineDone = false;
	if (!deluge::storage::bytes::runDuringBoot(
	        [](void*) {
		        deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Startup);
		        auto canary = deluge::io::File::open(g_canaryPath, DELUGE_FILE_WRITE_CREATE);
		        if (canary.has_value()) {
			        (void)canary->close(); // RAII would close anyway; explicit for parity with f_close
			        g_inlineOk = true;
		        }
		        else {
			        g_inlineOk = false;
		        }
		        g_inlineDone = true;
	        },
	        nullptr)) {
		// Refused: no canary. The machine already treats that as "skip the startup song", with nothing to
		// clean up.
		g_inlineOk = false;
		g_inlineDone = true;
	}
	g_requested = true;
}

/// Serialise the template song into the active writer's memory buffer.
///
/// Same staging SaveSongUI uses (`setMemoryBased()` then `deluge::bridge::saveSong()`): the document is built in
/// RAM so the card only ever sees one whole-file write, which is what lets that write go through the
/// byte channel instead of a blocking open-write-close.
bool stageTemplateDocument(const uint8_t** outBytes, uint32_t* outLen) {
	currentSong->project.metadata.name = "DEFAULT";
	FileWriter& writer = fileWriterFor(deluge::storage::kSongSaveFormat);
	writer.setMemoryBased();
	deluge::bridge::saveSong(*currentSong, serializerFor(deluge::storage::kSongSaveFormat));
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
	const uint8_t* bytes = nullptr;
	uint32_t len = 0;
	const bool staged = stageTemplateDocument(&bytes, &len);
	g_templateLength = len;
	if (!staged) {
		g_inlineOk = false;
		g_inlineDone = true;
		g_requested = true;
		return;
	}
	if (viaChannel()) {
		// Write-from, not the copying write: a song document runs to some 11 KB, and the copying write
		// refuses anything past DELUGE_FILE_BYTES_MAX_BYTES (8192) -- it would answer Failed forever.
		// This is the operation SaveSongUI puts a song document on, for the same reason.
		if (deluge_file_bytes_request_write_from(kNewTemplatePath, bytes, len, /*may_overwrite=*/false)) {
			g_requested = true;
		}
		else {
			(void)g_attemptBound.tick(deluge_file_bytes_progress());
		}
		return;
	}
	// The synchronous arm, where blocking is legal -- boot, before the channel's task runs.
	g_inlineDone = false;
	g_templateBytes = bytes;
	if (!deluge::storage::bytes::runDuringBoot(
	        [](void*) {
		        deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Startup);
		        auto file = deluge::io::File::open(kNewTemplatePath, DELUGE_FILE_WRITE_CREATE);
		        if (file.has_value()) {
			        const auto put = file->write(std::span<const std::byte>(
			            reinterpret_cast<const std::byte*>(g_templateBytes), g_templateLength));
			        g_inlineOk = put.has_value() && *put == g_templateLength;
			        (void)file->close();
		        }
		        else {
			        g_inlineOk = false;
		        }
		        g_inlineDone = true;
	        },
	        nullptr)) {
		g_inlineOk = false;
		g_inlineDone = true;
	}
	g_requested = true;
}

/// Collect the template write, or `nullopt` while it is still running.
std::optional<bool> collectTemplateWrite() {
	if (viaChannel()) {
		uint32_t written = 0;
		switch (deluge_file_bytes_collect_write_from(kNewTemplatePath, &written, nullptr)) {
		case DELUGE_CHANNEL_PENDING:
			// Waited out, and not counted against the bound: the write is copying from the staged template and
			// cannot be cancelled, so falling back would re-serialise into its buffer and write the file twice.
			return std::nullopt;
		case DELUGE_CHANNEL_UNKNOWN:
			// Nothing queued, running or answered: the request was dropped unrun by a card change, or its answer
			// was displaced. A write-from's answer is protected from displacement only up to the byte channel's
			// age bound (PROTECTED_ANSWER_SPAN); past it, re-staging can meet the file this
			// write made, and the exclusive create then fails rather than overwriting it.
			g_requested = false;
			(void)g_attemptBound.tick(deluge_file_bytes_progress());
			return std::nullopt;
		case DELUGE_CHANNEL_READY:
			// A short write is a card error, not a success: the template would be half a document.
			return written == g_templateLength;
		default:
			return false;
		}
	}
	if (!g_inlineDone) {
		return std::nullopt;
	}
	return g_inlineOk;
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
			(void)g_attemptBound.tick(deluge_file_bytes_progress());
			return std::nullopt;
		case deluge::storage::bytes::ByteIo::Ready:
			g_canaryConfirmed = true;
			return true;
		case deluge::storage::bytes::ByteIo::Failed:
			return false;
		}
		return false;
	}
	if (!g_inlineDone) {
		return std::nullopt;
	}
	g_canaryConfirmed = g_inlineOk;
	return g_inlineOk;
}

/// Issue the canary removal. The machine collects it (`unlinkFinished`). A canary that outlives its attempt by a
/// few ticks costs nothing, and one never removed costs a single skipped startup song next boot, which is the
/// fail-safe being conservative rather than failing.
void issueUnlink(const std::string& path) {
	if (viaChannel()) {
		if (deluge::storage::bytes::requestUnlink(path.c_str()) == deluge::storage::bytes::ByteIo::Pending) {
			g_requested = true;
			(void)g_pendingBound.tick(deluge_file_bytes_progress()); // as issueExists
		}
		else {
			(void)g_attemptBound.tick(deluge_file_bytes_progress());
		}
		return;
	}
	static std::string inlinePath;
	inlinePath = path;
	g_inlineDone = false;
	if (!deluge::storage::bytes::runDuringBoot(
	        [](void*) {
		        deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Startup);
		        (void)deluge::io::unlink(inlinePath);
		        g_inlineDone = true;
	        },
	        nullptr)) {
		// Refused: the canary stays. The next boot takes the cautious route -- a skipped song, never a
		// crash loop.
		g_inlineDone = true;
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
			(void)g_attemptBound.tick(deluge_file_bytes_progress());
			return false;
		case deluge::storage::bytes::ByteIo::Ready:
		case deluge::storage::bytes::ByteIo::Failed:
			return true;
		}
		return true;
	}
	return g_inlineDone;
}

} // namespace

void armStartupSettings() {
	g_settingsAccepted = 0;
	g_settingsDone = 0;
	g_settingsGaveUp = false;
	g_settingsBound.reset();
	g_settingsPhase = SettingsPhase::Reading;
	deluge::runtime::appLoop.request(startupSongJob);
	// Each consumer's own entry point. It already preferred the channel and fell back to blocking
	// only because `started()` was false during boot; from here `started()` is true, so the channel
	// takes them. Each returns whether it was ACCEPTED -- an early-out ("we only want to do this
	// once") submits nothing and so is not waited for.
	g_settingsAccepted += runtimeFeatureSettings.readSettingsFromFile(&settingsReadDone) ? 1 : 0;
	g_settingsAccepted += MIDIDeviceManager::readDevicesFromFile(&settingsReadDone) ? 1 : 0;
	g_settingsAccepted += midiFollow.readDefaultsFromFile(&settingsReadDone) ? 1 : 0;
}

void armStartupSong() {
	auto startupSongMode = FlashStorage::defaultStartupSongMode;
	if (startupSongMode == StartupSongMode::BLANK) {
		enter(StartupPhase::Done);
		return;
	}

	const char* filename =
	    startupSongMode == StartupSongMode::TEMPLATE ? kNewTemplatePath : runtimeFeatureSettings.getStartupSong();
	// The messages we show to the user are mostly plain language, but for non-user facing errors we
	// use codes. All messages are < 20 chars.
	if (strlen(filename) + 1 > 2048) {
		deluge::notify::notice("Startup path too long", deluge::notify::Notice::Kind::Console);
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
	g_templateProbe = 0;
	g_usingTemplate = (startupSongMode == StartupSongMode::TEMPLATE);
	enter(StartupPhase::CheckCanary);
}

namespace {

/// One step of the prelude or the machine.
void advanceStartup() {
	if (g_settingsPhase == SettingsPhase::Reading) {
		if (g_settingsDone < g_settingsAccepted) {
			if (!g_settingsBound.tick()) {
				return; // still waiting: the song machine must NOT arm on stale settings
			}
			g_settingsGaveUp = true;
		}
		g_settingsPhase = SettingsPhase::Done;
		// Applied HERE, not at boot: the setting is only known once the read lands. Until then the
		// canvas default stands, so the first frames render with rounded corners on regardless of
		// what the card says -- the visible cost of boot no longer waiting for the card.
		deluge::hid::display::oled_canvas::Canvas::roundedCornersEnabled =
		    runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::RoundedCorners);
		armStartupSong();
	}
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
			deluge::notify::notice("Startup fault F1", deluge::notify::Notice::Kind::Console);
		}
		auto step = nextStartupPhase(StartupPhase::CheckCanary, *answer, g_usingTemplate);
		g_usingTemplate = step.usingTemplate;
		enter(step.next); // Done keeps the canary, deliberately -- there is no unlink on this path.
		return;
	}

	case StartupPhase::CheckTemplate: {
		auto answer = probeTemplate();
		if (!answer.has_value()) {
			return;
		}
		if (*answer == StartupPresence::Confirmed) {
			g_songPath = kTemplatePaths[g_templateProbe];
		}
		if (*answer == StartupPresence::Missing) {
			deluge::notify::notice("Creating template", deluge::notify::Notice::Kind::Console);
			// The write itself is StartupPhase::WriteTemplate below. Done inline here, it would be a
			// blocking card write on whatever tier this routine was ticking on -- confirmed by
			// measurement to freeze the startup fail-safe.
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
			// Not fatal, and deliberately so: the machine carries on to the canary regardless of a
			// failed create. The canary is what protects the load.
			deluge::notify::notice("Startup fault F3", deluge::notify::Notice::Kind::Console);
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
			deluge::notify::notice("Startup fault F2", deluge::notify::Notice::Kind::Console);
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
				deluge::notify::notice("Startup fault F3", deluge::notify::Notice::Kind::Console);
				enter(StartupPhase::UnlinkCanary);
				return;
			}
			deluge::notify::notice("Song missing", deluge::notify::Notice::Kind::Console);
		}
		auto step = nextStartupPhase(StartupPhase::CheckSong, *answer, g_usingTemplate);
		g_usingTemplate = step.usingTemplate;
		enter(step.next);
		return;
	}

	case StartupPhase::CheckFallbackTemplate: {
		auto answer = probeTemplate();
		if (!answer.has_value()) {
			return;
		}
		if (*answer == StartupPresence::Confirmed) {
			// user didn't ask for the template, but if it exists let's use it instead
			deluge::notify::notice("Using template", deluge::notify::Notice::Kind::Console);
			g_songPath = kTemplatePaths[g_templateProbe];
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
			// Done BEFORE beginLoad(), not after: beginLoad() runs the load's first stage at once, and a load
			// that ends there calls finishStartupSong() before returning, which moves this machine on to
			// removing the canary. Entering Done afterwards would undo that and leave the canary on the card.
			enter(StartupPhase::Done);
			// The canary comes off however the load ended: a failure has already fallen back to a blank song.
			loadSongUI.beginLoad([](SongLoadOutcome) { finishStartupSong(); });
			return;
		}
		// what just failed??
		deluge::notify::notice("Startup fault F4", deluge::notify::Notice::Kind::Console);
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

/// Ask for the job's next run after one step, which began in @p phaseBefore with @p requestedBefore.
///
/// A phase entered during the step has already asked (see @ref enter). Otherwise a request that must be asked again
/// (its answer was displaced, or its pending bound gave up on the channel) or an answer the synchronous arm already
/// has runs at once; a refused request or one the channel holds waits for the channel's DELUGE_WAKE_STORAGE, with
/// the bound's deadline as the latest run.
void armNextRun(StartupPhase phaseBefore, bool requestedBefore) {
	using deluge::runtime::appLoop;
	if (g_settingsPhase == SettingsPhase::Reading) {
		// Each read's completion requests the job; the bound is how long it waits for the rest.
		appLoop.requestAt(startupSongJob, g_settingsBound.deadlineUs());
		return;
	}
	if (g_phase == StartupPhase::Idle || g_phase == StartupPhase::Done || g_phase != phaseBefore) {
		return;
	}
	if (!g_requested) {
		if (requestedBefore) {
			appLoop.request(startupSongJob);
		}
		else {
			// Refused: the answer that refused it raises no wake when it ages out of the channel, so ask again
			// after the protected span rather than at the attempt bound.
			appLoop.requestAt(startupSongJob,
			                  std::min(g_attemptBound.deadlineUs(),
			                           deluge::runtime::nowUs() + deluge::storage::bytes::kRefusedRecheckUs));
		}
		return;
	}
	if (!viaChannel()) {
		appLoop.request(startupSongJob);
		return;
	}
	if (g_phase == StartupPhase::WriteTemplate) {
		// The template write's PENDING is waited out unbounded (it cannot be cancelled), so its pending bound never
		// starts: its answer brings the job back, re-checked in case that wake is lost.
		appLoop.requestAt(startupSongJob, deluge::runtime::nowUs() + deluge::storage::bytes::kWriteFromRecheckUs);
		return;
	}
	appLoop.requestAt(startupSongJob, g_pendingBound.deadlineUs());
}

} // namespace

deluge::runtime::JobId startupSongJob = deluge::runtime::kNoJob;

void startupSongRoutine() {
	const StartupPhase phaseBefore = g_phase;
	const bool requestedBefore = g_requested;
	advanceStartup();
	armNextRun(phaseBefore, requestedBefore);
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
		currentSong->project.metadata.name.clear();
	}
	// ...but we got this far, cleanup. Handed to the machine's own unlink phase, which requests and COLLECTS it: an
	// unlink's answer holds the byte channel until it is taken (a displaced unlink would rerun and report
	// NOT_FOUND), so leaving it uncollected would refuse every other request until the channel gave up on it.
	if (deluge::storage::bytes::usingByteChannel()) {
		g_canaryPath = g_startupFailSafePath;
		g_startupFailSafePath.clear();
		enter(StartupPhase::UnlinkCanary);
		return;
	}
	// During boot: blocking is legal here, so the body always runs.
	static std::string path;
	path = g_startupFailSafePath;
	(void)deluge::storage::bytes::runDuringBoot(
	    [](void*) {
		    deluge::storage::bytes::noteIoTier(deluge::storage::bytes::IoTier::Startup);
		    (void)deluge::io::unlink(path);
	    },
	    nullptr);
	g_startupFailSafePath.clear();
}

} // namespace deluge::storage
