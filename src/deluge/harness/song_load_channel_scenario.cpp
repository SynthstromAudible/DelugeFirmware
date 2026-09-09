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

#include "harness/song_load_channel_scenario.h"

#ifdef DELUGE_HOST

#include "gui/ui/browser/browser.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/save/save_song_ui.h"
#include "gui/ui/save/save_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "io/file.hpp"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "storage/owner.h"
#include "storage/storage_manager.h"
#include <cstdio>
#include <string>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded
// `printf_`. That implementation emits through `putchar_`, which only the C host BSP defines --
// this harness links the Rust BSP and has no provider, so the alias turns every line this file
// writes into an undefined symbol at link time. Every one of those lines IS the gate's output.
#ifdef printf
#undef printf
#endif

/// Reaches `SaveUI::enterKeyPress()`, which is protected: the gate drives the REAL gesture rather
/// than `performSave` underneath it. Mirrors `SaveRoundtripProbe` in the save round-trip scenario.
class SongLoadChannelProbe {
public:
	/// Put the save browser on a folder, exactly as reaching the save screen does.
	static bool openOn(const char* dir) {
		Browser::currentDir = dir;
		Browser::enteredText.clear();
		// openUI() rather than beginListing() alone: performSave() ENDS in close(), which pops the UI
		// stack, so the save UI has to actually be on it or the success path underflows.
		if (!openUI(&saveSongUI)) {
			return false;
		}
		for (int32_t t = 0; Browser::isListingInProgress() && t < 200; t++) {
			saveSongUI.graphicsRoutine();
		}
		return !Browser::isListingInProgress();
	}
	static bool listingInProgress() { return Browser::isListingInProgress(); }
	static void typeName(const char* name) { Browser::enteredText = name; }
	static void pressEnter() { saveSongUI.enterKeyPress(); }
};

namespace {

constexpr const char* kSaveDir = "SONGS";
constexpr const char* kSaveName = "SLTEST01";
constexpr const char* kSavePath = "SONGS/SLTEST01.XML";

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
	LoadBackWait,
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

/// The load is staged across ticks and then runs on the owner; 200000 ticks is far past any real
/// completion and exists only so a wedge ends as a FAILURE with output rather than as a silent
/// timeout with none.
constexpr int32_t kMaxLoadTicks = 200000;
/// Ticks to wait for the save's file to appear before calling it absent.
constexpr int32_t kMaxSaveTicks = 20000;

bool fileExists(const char* path) {
	return deluge::io::File::open(path, DELUGE_FILE_READ).has_value();
}

void runTick() {
	switch (g_stage) {
	case Stage::Fixture: {
		// Start from a known-absent target so the save is genuinely the "free name" case.
		(void)deluge::io::mkdir(kSaveDir);
		(void)deluge::io::unlink(kSavePath);
		// The save pushes UIs that need a root UI beneath them; this harness boots without one.
		changeRootUI(&arrangerView);
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
		currentSong->insideWorldTickMagnitude = kProbeMagnitude;
		g_expectedMagnitude = currentSong->getInputTickMagnitude();
		SongLoadChannelProbe::typeName(kSaveName);
		SongLoadChannelProbe::pressEnter();
		g_stage = Stage::SaveWait;
		break;
	}
	case Stage::SaveWait: {
		// The save is dispatched onto the storage owner, so the file appears on a LATER tick than the
		// gesture. Wait for it rather than reading one tick later and calling its absence a failure.
		// One tick is enough now that dispatch_step() waits for the owner queue to drain, but the
		// bound stays: a save that never lands must end the run with a report, not a timeout.
		if (fileExists(kSavePath) || ++g_saveTicks > kMaxSaveTicks) {
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
		currentSong->insideWorldTickMagnitude = kDecoyMagnitude;
		g_stage = Stage::LoadBack;
		break;
	}
	case Stage::LoadBack: {
		currentSong->setSongFullPath(kSavePath);
		(void)openUI(&loadSongUI);
		loadSongUI.beginLoad();
		g_stage = Stage::LoadBackWait;
		break;
	}
	case Stage::LoadBackWait: {
		// loadInFlight(), NOT isLoadingSong(): the latter is true merely because the load UI is
		// showing, so with the browser open it would never go false and this gate would hang rather
		// than report.
		if (!loadSongUI.loadInFlight()) {
			g_stage = Stage::Report;
			break;
		}
		if (++g_loadTicks > kMaxLoadTicks) {
			printf("SL-LOADED FAIL the load never finished (%d ticks)\n", g_loadTicks);
			g_failures++;
			g_stage = Stage::Report;
		}
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
		printf("SL-SUMMARY served=%d offOwnerTicks=%d\n", (int)StorageManager::stagingChannelServedOps(),
		       (int)AudioEngine::offOwnerSongStageTicks());
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
	// A marker op behind whatever the tick queued: the owner queue is FIFO, so this runs after the
	// dispatched save or load (and immediately if nothing was dispatched). That is what makes each
	// check stage read a result the op has actually produced. Setting g_stepDone directly here
	// instead races the owner, and the scenario sails past work it has only just queued.
	if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
		g_stepDone = true;
	}
}

bool deluge_scenario_song_load_channel_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_song_load_channel_failures(void) {
	return g_failures;
}

} // extern "C"

#endif // DELUGE_HOST
