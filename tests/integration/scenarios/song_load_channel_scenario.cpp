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

#include "scenarios/song_load_channel_scenario.h"

#include "gui/ui/browser/browser.h"
#include "gui/ui/load/cancel_result.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/save/save_song_ui.h"
#include "gui/ui/save/save_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/session_view.h"
#include "hid/button.h"
#include "hid/display/oled.h"
#include "io/file.hpp"
#include "libdeluge/file_bytes.h" // deluge_file_bytes_set_latency_ms
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"
#include <cstdio>
#include <string>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded
// `printf_`. That implementation emits through `putchar_`, which the Rust BSP builds do not define --
// this harness links the Rust BSP and has no provider, so the alias turns every line this file
// writes into an undefined symbol at link time. Every one of those lines IS the gate's output.
#ifdef printf
#undef printf
#endif

/// Reaches `SaveUI::enterKeyPress()`, which is protected: the gate drives the REAL gesture rather
/// than the save machinery underneath it. Mirrors `SaveRoundtripProbe` in the save round-trip scenario.
class SongLoadChannelProbe {
public:
	/// Put the save browser on a folder, exactly as reaching the save screen does.
	static bool openOn(const char* dir) {
		Browser::currentDir = dir;
		Browser::enteredText.clear();
		// openUI() rather than beginListing() alone: a finished save ENDS in close(), which pops the UI
		// stack, so the save UI has to actually be on it or the success path underflows.
		if (!openUI(&saveSongUI)) {
			return false;
		}
		for (int32_t t = 0; Browser::isListingInProgress() && t < 200; t++) {
			saveSongUI.graphicsRoutine();
		}
		return !Browser::isListingInProgress();
	}
	/// Tick the browser just opened while its listing runs, and say whether it still is. Nothing else in the
	/// headless harness ticks a browser, and the listing runs on the browser's tick surface.
	static bool listingInProgress() {
		if (Browser::isListingInProgress()) {
			getCurrentUI()->graphicsRoutine();
		}
		return Browser::isListingInProgress();
	}
	static void typeName(const char* name) { Browser::enteredText = name; }
	static void pressEnter() { saveSongUI.enterKeyPress(); }
	/// The load browser's ENTER on a song: the gesture whose completion writes "last opened".
	static void pressLoadEnter() { loadSongUI.enterKeyPress(); }

	/// Whether the song load is staging with its whole-file read outstanding: the byte channel holds the buffer.
	static bool stagingReadOutstanding() {
		return loadSongUI.loadPhase_ == LoadSongUI::LoadPhase::STAGE && loadSongUI.songStaging_.issued;
	}
	/// Whether the song load is past STAGE: BEGIN or later.
	static bool pastStaging() {
		return loadSongUI.loadPhase_ != LoadSongUI::LoadPhase::NONE
		       && loadSongUI.loadPhase_ != LoadSongUI::LoadPhase::STAGE;
	}
	static int32_t phase() { return static_cast<int32_t>(loadSongUI.loadPhase_); }
};

namespace {

constexpr const char* kSaveDir = "SONGS";
constexpr const char* kSaveName = "SLTEST01";
constexpr const char* kSavePath = "SONGS/SLTEST01.XML";
/// The one sample the gate's song carries. Without it the load's sample passes walk a song that demands nothing,
/// and every leg about them would pass by having nothing asked of it (measured: the boot song is one instrument
/// clip and no audio clips at all).
constexpr const char* kSongSample = "SAMPLES/SL/SLSONG.WAV";

/// The value stamped into the song before saving, and required back after loading. Distinctive
/// enough that no default produces it.
constexpr int32_t kProbeMagnitude = 3;
/// Stamped over it AFTER the save and BEFORE the load, so a correct reading afterwards can only come
/// from bytes that were read and parsed -- not from the value simply never having been disturbed.
constexpr int32_t kDecoyMagnitude = -2;

enum class Stage : uint8_t {
	Fixture,
	FixtureWait,
	Mark,
	SaveWait,
	SaveCheck,
	Decoy,
	LoadBack,
	LoadBackListing,
	CancelStart,
	CancelWait,
	CancelAfter,
	RefuseStart,
	LoadBackWait,
	LastOpenedWait,
	Report,
	Done,
};

Stage g_stage = Stage::Fixture;
bool g_stepDone = false;
bool g_summaryPrinted = false;
bool g_setupFailed = false;
int32_t g_failures = 0;
int32_t g_expectedMagnitude = 0;
int32_t g_loadTicks = 0;
int32_t g_saveTicks = 0;
/// Whether the fixture's sample bound before the save. A song saved without it proves nothing on the way back.
bool g_fixtureBound = false;

/// The load is staged across ticks and then parsed and swapped in phases on this tier; 200000 ticks
/// is far past any real completion and exists only so a wedge ends as a FAILURE with output rather
/// than as a silent timeout with none.
constexpr int32_t kMaxLoadTicks = 200000;
/// Ticks to wait for the save's file to appear before calling it absent.
constexpr int32_t kMaxSaveTicks = 20000;

/// How long the byte channel holds each operation in the cancel-in-STAGE leg, so the staging read is still
/// outstanding when BACK is pressed. A tick is about a virtual millisecond.
constexpr uint32_t kHoldMs = 300;
/// Ticks the cancel-in-STAGE leg runs on after BACK, past every hold, to see that nothing of the dropped load comes
/// back.
constexpr int32_t kAfterTicks = 1000;

/// cancel_in_stage: the song before the load, and the staging buffers parked when BACK was pressed.
Song* g_songBefore = nullptr;
int32_t g_orphansBefore = 0;
int32_t g_cancelTicks = 0;
/// cancel_after_begin: whether a cancel was tried past STAGE, and how it answered.
bool g_refuseTried = false;
/// The startup-song mode before the cancel-in-STAGE leg set LASTOPENED, put back after it.
StartupSongMode g_startupModeBefore = StartupSongMode::BLANK;

/// Where the settings that carry "last opened" live. A load that never happened must not write them.
constexpr const char* kSettingsPath = "SETTINGS/CommunityFeatures.XML";

bool fileExists(const char* path) {
	return deluge::io::File::open(path, DELUGE_FILE_READ).has_value();
}

void runTick() {
	switch (g_stage) {
	case Stage::Fixture: {
		// Start from a known-absent target so the save is genuinely the "free name" case.
		(void)deluge::io::mkdir(kSaveDir);
		(void)deluge::io::unlink(kSavePath);
		// The save pushes UIs that need a root UI beneath them; this harness boots without one. Session view
		// rather than arranger view because an AudioClip is about to become the current clip, and the clip
		// views dereference getCurrentInstrumentClip() unconditionally when they draw (a SIGSEGV inside
		// setKnobIndicatorLevels()).
		changeRootUI(&sessionView);
		// Give the song its sample BEFORE the save, through the same gesture the session view's own
		// new-audio-clip does. The saved document then names the file, and loading it back has to bind it.
		if (auto* clip = static_cast<AudioClip*>(sessionView.createNewAudioClipForHarness(0)); clip != nullptr) {
			clip->sampleHolder.filePath = kSongSample;
			(void)clip->sampleHolder.loadFile(false, false, true);
			clip->syncSampleRecordFromEngine(); // the save writes the clip's record, not the holder
			g_fixtureBound = clip->sampleHolder.audioFile != nullptr;
		}
		(void)SongLoadChannelProbe::openOn(kSaveDir);
		g_stage = Stage::FixtureWait;
		break;
	}
	case Stage::FixtureWait: {
		if (!SongLoadChannelProbe::listingInProgress()) {
			g_stage = Stage::Mark;
		}
		break;
	}
	case Stage::Mark: {
		// Stamp the value, then save through the REAL ENTER gesture.
		currentSong->project.transport.inside_world_tick_magnitude = kProbeMagnitude;
		g_expectedMagnitude = currentSong->getInputTickMagnitude();
		SongLoadChannelProbe::typeName(kSaveName);
		SongLoadChannelProbe::pressEnter();
		g_stage = Stage::SaveWait;
		break;
	}
	case Stage::SaveWait: {
		// Wait for the save to FINISH, not merely for its file to appear. A song save writes its file,
		// then reads it back, then finishes -- and the finish closes the save screen. Moving on at the
		// first sight of the file started the load while that close() was still to come, and it then
		// indexed off the end of a UI stack the load had already rearranged. The bound stays: a save
		// that never lands must end the run with a report, not a timeout.
		const bool saveIdle = !saveSongUI.collectInFlight() && !saveSongUI.docInFlight() && !SaveUI::saveInFlight();
		if ((saveIdle && fileExists(kSavePath)) || ++g_saveTicks > kMaxSaveTicks) {
			g_stage = Stage::SaveCheck;
		}
		break;
	}
	case Stage::SaveCheck: {
		if (!fileExists(kSavePath)) {
			printf("SL-SETUP FAIL the save wrote no file at %s after %d ticks; there is nothing to "
			       "load back\n",
			       kSavePath, g_saveTicks);
			g_failures++;
			// The decoy never gets stamped on this path, so the magnitude still reads correct and
			// SL-LOADED below would report a PASS for a load that never ran. Say so instead.
			g_setupFailed = true;
			g_stage = Stage::Report;
			break;
		}
		printf("SL-SETUP saved=%s magnitude=%d\n", kSavePath, g_expectedMagnitude);
		g_stage = Stage::Decoy;
		break;
	}
	case Stage::Decoy: {
		// Overwrite the value in memory. From here, reading it back correctly REQUIRES a real parse.
		currentSong->project.transport.inside_world_tick_magnitude = kDecoyMagnitude;
		g_stage = Stage::LoadBack;
		break;
	}
	case Stage::LoadBack: {
		currentSong->setSongFullPath(kSavePath);
		(void)openUI(&loadSongUI);
		g_stage = Stage::LoadBackListing;
		break;
	}
	case Stage::LoadBackListing: {
		// The browser's own listing lands first, as a LOAD press waits for it. Loading over a listing still
		// suspended would leave its machine active for the next browser to resume as its own.
		if (Browser::isListingInProgress()) {
			loadSongUI.graphicsRoutine();
			if (++g_loadTicks > kMaxLoadTicks) {
				printf("SL-LOADED FAIL the load browser's listing never finished (%d ticks)\n", g_loadTicks);
				g_failures++;
				g_stage = Stage::Report;
			}
			break;
		}
		// The browser's scroll-in runs on the UI timer, which nothing fires in the harness; BACK is a browser gesture
		// only once it has finished.
		for (int32_t t = 0; currentUIMode == UI_MODE_VERTICAL_SCROLL && t < kDisplayHeight * 4; t++) {
			(void)loadSongUI.timerCallback();
		}
		if (currentUIMode != UI_MODE_NONE) {
			// Still waiting to scroll in, or mid-scroll: ask again next tick.
			if (++g_loadTicks > kMaxLoadTicks) {
				printf("SL-CANCEL FAIL the load browser never finished scrolling in (mode=%d)\n",
				       static_cast<int>(currentUIMode));
				g_failures++;
				g_stage = Stage::Report;
			}
			break;
		}
		g_stage = Stage::CancelStart;
		break;
	}
	case Stage::CancelStart: {
		// cancel_in_stage: the song plays while the load stages, with the byte channel holding its read.
		playbackHandler.setupPlaybackUsingInternalClock(0, false);
		g_songBefore = currentSong;
		g_orphansBefore = StorageManager::orphanedStagingCount();
		// With LASTOPENED, a finished load records the song in the settings file. Start without one, so a write
		// for a load that never happened shows up as the file appearing.
		g_startupModeBefore = FlashStorage::defaultStartupSongMode;
		FlashStorage::defaultStartupSongMode = StartupSongMode::LASTOPENED;
		(void)deluge::io::unlink(kSettingsPath);
		deluge_file_bytes_set_latency_ms(kHoldMs);
		// The real ENTER, so the load carries the completion that gesture gives it.
		SongLoadChannelProbe::pressLoadEnter();
		g_cancelTicks = 0;
		g_stage = Stage::CancelWait;
		break;
	}
	case Stage::CancelWait: {
		if (!SongLoadChannelProbe::stagingReadOutstanding()) {
			if (++g_cancelTicks > kMaxLoadTicks || !loadSongUI.loadInFlight()) {
				printf("SL-CANCEL FAIL the load never reached its staging read (phase=%d inFlight=%d)\n",
				       SongLoadChannelProbe::phase(), loadSongUI.loadInFlight() ? 1 : 0);
				g_failures++;
				deluge_file_bytes_set_latency_ms(0);
				g_stage = Stage::Report;
			}
			break;
		}
		(void)getCurrentUI()->buttonAction(deluge::hid::button::BACK, true);
		(void)getCurrentUI()->buttonAction(deluge::hid::button::BACK, false);
		const bool dropped = !loadSongUI.loadInFlight();
		const bool browserStayed = getCurrentUI() == &loadSongUI && currentUIMode == UI_MODE_NONE;
		const bool songKept = currentSong == g_songBefore
		                      && currentSong->project.transport.inside_world_tick_magnitude == kDecoyMagnitude;
		const bool playing = playbackHandler.isEitherClockActive();
		const bool animation = deluge::hid::display::OLED::isWorkingAnimationPresent();
		const int32_t parked = StorageManager::orphanedStagingCount() - g_orphansBefore;
		printf("SL-CANCEL-BACK dropped=%d browserStayed=%d songKept=%d playing=%d animation=%d parked=%d\n",
		       dropped ? 1 : 0, browserStayed ? 1 : 0, songKept ? 1 : 0, playing ? 1 : 0, animation ? 1 : 0, parked);
		if (!dropped || !browserStayed) {
			printf(
			    "SL-CANCEL FAIL BACK while the song load staged did not drop it and leave the browser where it was\n");
			g_failures++;
		}
		if (!songKept || !playing || animation) {
			printf("SL-CANCEL FAIL the cancel did not leave the current song playing with nothing shown\n");
			g_failures++;
		}
		if (parked != 1) {
			printf("SL-CANCEL FAIL the staging buffer the byte channel still writes was not parked\n");
			g_failures++;
		}
		deluge_file_bytes_set_latency_ms(0);
		g_cancelTicks = 0;
		g_stage = Stage::CancelAfter;
		break;
	}
	case Stage::CancelAfter: {
		if (++g_cancelTicks < kAfterTicks) {
			break;
		}
		const bool songKept = currentSong == g_songBefore
		                      && currentSong->project.transport.inside_world_tick_magnitude == kDecoyMagnitude;
		const bool playing = playbackHandler.isEitherClockActive();
		const bool browserStayed = getCurrentUI() == &loadSongUI;
		const int32_t parked = StorageManager::orphanedStagingCount() - g_orphansBefore;
		const deluge::gui::CancelResult idle = loadSongUI.cancelLoad();
		printf("SL-CANCEL songKept=%d playing=%d browserStayed=%d inFlight=%d parked=%d idleCancel=%d\n",
		       songKept ? 1 : 0, playing ? 1 : 0, browserStayed ? 1 : 0, loadSongUI.loadInFlight() ? 1 : 0, parked,
		       static_cast<int>(idle));
		if (!songKept || !playing || !browserStayed || loadSongUI.loadInFlight()) {
			printf("SL-CANCEL FAIL the cancelled load came back, or the current song stopped playing\n");
			g_failures++;
		}
		if (parked != 0) {
			printf("SL-CANCEL FAIL the parked staging buffer was not freed once the byte channel let it go\n");
			g_failures++;
		}
		if (idle != deluge::gui::CancelResult::Stale) {
			printf("SL-CANCEL FAIL a cancel with no load in flight was not Stale\n");
			g_failures++;
		}
		const bool settingsWritten = fileExists(kSettingsPath);
		printf("SL-CANCEL-LASTOPENED settingsWritten=%d\n", settingsWritten ? 1 : 0);
		if (settingsWritten) {
			printf("SL-CANCEL FAIL the cancelled load recorded \"last opened\" for a song that never loaded\n");
			g_failures++;
		}
		playbackHandler.endPlayback();
		g_stage = Stage::RefuseStart;
		break;
	}
	case Stage::RefuseStart: {
		// cancel_after_begin: the load that brings the song back is asked to cancel once it is past STAGE. Through
		// the real ENTER as well, still with LASTOPENED: this load completes, and must record the song.
		SongLoadChannelProbe::pressLoadEnter();
		g_stage = Stage::LoadBackWait;
		break;
	}
	case Stage::LoadBackWait: {
		if (!g_refuseTried && SongLoadChannelProbe::pastStaging()) {
			g_refuseTried = true;
			const int32_t phase = SongLoadChannelProbe::phase();
			const deluge::gui::CancelResult result = loadSongUI.cancelLoad();
			const bool carriesOn = loadSongUI.loadInFlight() && SongLoadChannelProbe::pastStaging();
			printf("SL-REFUSED phase=%d result=%d carriesOn=%d\n", phase, static_cast<int>(result), carriesOn ? 1 : 0);
			if (result != deluge::gui::CancelResult::Refused || !carriesOn) {
				printf("SL-REFUSED FAIL a cancel past STAGE was not refused, or stopped the load\n");
				g_failures++;
			}
		}
		// loadInFlight(), NOT isLoadingSong(): the latter is true merely because the load UI is
		// showing, so with the browser open it would never go false and this gate would hang rather
		// than report.
		if (!loadSongUI.loadInFlight()) {
			if (!g_refuseTried) {
				printf("SL-REFUSED FAIL the load finished before a cancel past STAGE could be tried\n");
				g_failures++;
			}
			g_cancelTicks = 0;
			g_stage = Stage::LastOpenedWait;
			break;
		}
		if (++g_loadTicks > kMaxLoadTicks) {
			printf("SL-LOADED FAIL the load never finished (%d ticks)\n", g_loadTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
		break;
	}
	case Stage::LastOpenedWait: {
		// The settings write is handed to the byte channel as the load ends; give it time to land.
		if (++g_cancelTicks < kAfterTicks) {
			break;
		}
		const bool settingsWritten = fileExists(kSettingsPath);
		printf("SL-LOADED-LASTOPENED settingsWritten=%d\n", settingsWritten ? 1 : 0);
		if (!settingsWritten) {
			printf("SL-LOADED FAIL the completed load did not record \"last opened\"\n");
			g_failures++;
		}
		FlashStorage::defaultStartupSongMode = g_startupModeBefore;
		g_stage = Stage::Report;
		break;
	}
	case Stage::Report: {
		const int32_t observed = (currentSong != nullptr) ? currentSong->getInputTickMagnitude() : 0;
		if (g_setupFailed) {
			// The decoy was never stamped, so `observed` proves nothing either way.
			printf("SL-LOADED skipped -- the setup never produced a song to load\n");
		}
		else {
			printf("SL-LOADED magnitude=%d wanted=%d ticks=%d\n", observed, g_expectedMagnitude, g_loadTicks);
		}
		if (!g_setupFailed && observed != g_expectedMagnitude) {
			printf("SL-LOADED FAIL the song did not come back off the card -- a value stamped over in "
			       "memory before the load was not restored by it\n");
			g_failures++;
		}
		// SL-ORDER. The song's sample is bound by the time the load ends -- the pass that binds it moves tier in
		// this plan, and what it achieves must not.
		int32_t audioClips = 0;
		int32_t boundHolders = 0;
		if (currentSong != nullptr) {
			for (Clip* clip : AllClips::everywhere(currentSong)) {
				if (clip->type == ClipType::AUDIO) {
					audioClips++;
					boundHolders += (((AudioClip*)clip)->sampleHolder.audioFile != nullptr) ? 1 : 0;
				}
			}
		}
		printf("SL-ORDER fixtureBound=%d audioClips=%d bound=%d\n", g_fixtureBound ? 1 : 0, audioClips, boundHolders);
		if (!g_fixtureBound || audioClips != 1 || boundHolders != 1) {
			printf("SL-ORDER FAIL the song's sample did not come back bound\n");
			g_failures++;
		}
		printf("SL-SUMMARY served=%d channelTicks=%d\n", (int)StorageManager::stagingChannelServedOps(),
		       (int)AudioEngine::songStageTicks());
		// Leave the card as we found it. These harness images are keyed by fingerprint and SHARED with
		// other gates, so a song left behind here changes what THEIR browser listings see -- which is
		// exactly how this run broke save-roundtrip once, and the accumulation trap this project has
		// hit before with the streaming-scenario images.
		(void)deluge::io::unlink(kSavePath);
		printf("SL-RESULT failures=%d\n", g_failures);
		// The Rust runner ends a failing run with process::exit, which does NOT flush C stdio. These
		// lines ARE the gate's evidence, so losing them exactly when the gate fails is the one case
		// that must not happen.
		fflush(stdout);
		g_summaryPrinted = true;
		g_stage = Stage::Done;
		break;
	}
	case Stage::Done:
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_song_load_channel_begin(void) {
	// Line-buffer stdout: piped into a script it is block-buffered by default, and the harness exits
	// without flushing C stdio, so every SL- line -- which IS this gate's output -- would be lost
	// exactly when the gate fails. Same reason save_roundtrip_scenario.cpp does it.
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Fixture;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_setupFailed = false;
	g_failures = 0;
	g_expectedMagnitude = 0;
	g_loadTicks = 0;
	g_saveTicks = 0;
	g_fixtureBound = false;
	g_songBefore = nullptr;
	g_orphansBefore = 0;
	g_cancelTicks = 0;
	g_refuseTried = false;
	g_startupModeBefore = StartupSongMode::BLANK;
}

bool deluge_scenario_song_load_channel_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_song_load_channel_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	// Nothing this tick queued runs anywhere else: the step is done when the tick returns.
	g_stepDone = true;
}

bool deluge_scenario_song_load_channel_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_song_load_channel_failures(void) {
	return g_failures;
}

} // extern "C"
