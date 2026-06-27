/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

// host_debug.cpp — host-sim developer triggers for the reason-leak tooling.
//
// The reason-leak workflow is a round-trip diff: snapshot at idle, drive the suspect operation, return to idle, report
// what's still pinned. On the device that's a sysex command (Debug subcommand 4); on the host-sim it's two signals:
//
//     kill -USR1 <pid>     # snapshot the current reason state
//     ...drive the suspect operation, then return to idle...
//     kill -USR2 <pid>     # report Clusters pinned since the snapshot (with the addReason call-site)
//
// The signal handlers only set a flag (async-signal-safe); the actual table walk runs from deluge_host_debug_poll(),
// which the host audio backend calls once per rendered block on the app thread, so it always sees a consistent table.

#include "definitions_cxx.hpp"        // OutputType
#include "gui/ui/load/load_song_ui.h" // loadSongUI
#include "gui/ui/ui.h"                // openUI
#include "gui/views/session_view.h"   // sessionView, gridCreateInstrumentClipWithNewTrack
#include "memory/reason_check.h"
#include "util/d_string.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

extern "C" {
#include "fatfs/ff.h"
}
#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/lsan_interface.h>
#endif

#if REASON_CHECK

namespace {
volatile std::sig_atomic_t g_request = 0; // 0 = nothing, 1 = snapshot, 2 = report
reason_check::Snapshot g_baseline = 0;

void onSignal(int sig) {
	g_request = (sig == SIGUSR1) ? 1 : 2;
}

// Kit-copy reproduction harness. With DELUGE_HOST_KIT_COPY_REPS=N, once the app has booted, create N new KIT tracks -
// each calls the path from the bug report (createNewTrackForInstrumentClip -> setPresetOrNextUnlaunchedOne, which loads
// the *next* unlaunched kit preset from KITS/ plus all its samples). The first loads 000 TR-808, then 001, 002 SDS-5,
// ... exactly as the device log showed. We snapshot reason_check around it; the FULL-internal pressure shows in the
// D_PRINTLN log, and any kit leaked on the INSUFFICIENT_RAM error path shows in the closing reportOutstanding().
int g_kitCopyReps = -1; // -1 = not yet read, 0 = disabled
uint32_t g_pollCount = 0;
bool g_kitCopyDone = false;

void runKitCopyHarnessIfDue() {
	if (g_kitCopyReps < 0) {
		const char* env = std::getenv("DELUGE_HOST_KIT_COPY_REPS");
		g_kitCopyReps = env ? std::atoi(env) : 0;
	}
	if (g_kitCopyReps <= 0 || g_kitCopyDone) {
		return;
	}
	// Let the app finish booting (song + browser + UI ready) before driving the operation.
	if (++g_pollCount < 200) {
		return;
	}
	g_kitCopyDone = true;

	printf("[kit-copy] creating %d new KIT tracks (each loads the next kit preset + its samples)\n", g_kitCopyReps);
	std::fflush(stdout);
	reason_check::Snapshot snap = reason_check::snapshot();

	for (int i = 0; i < g_kitCopyReps; i++) {
		Clip* clip = sessionView.reproCreateNewInstrumentClip(OutputType::KIT, 0);
		printf("[kit-copy] rep %d: clip=%p, pinned stealables now %u\n", i, (void*)clip,
		       (unsigned)reason_check::liveClusters());
		std::fflush(stdout);
		if (clip == nullptr) {
			printf("[kit-copy] rep %d returned null (INSUFFICIENT_RAM) - the leak-on-error path\n", i);
			std::fflush(stdout);
		}
	}

	printf("[kit-copy] done - reasons still outstanding across the run (leaks):\n");
	std::fflush(stdout);
	reason_check::reportOutstanding(snap);

#if defined(__SANITIZE_ADDRESS__)
	// The real leak is the raw Instrument structure leaked on the INSUFFICIENT_RAM error path (not a reason leak), so
	// ask LeakSanitizer to report unreachable allocations now (the host quick_exit()s, bypassing the at-exit check).
	printf("[kit-copy] LeakSanitizer check:\n");
	std::fflush(stdout);
	__lsan_do_recoverable_leak_check();
#endif
}

// ---- Song-load leak harness -------------------------------------------------
// With DELUGE_HOST_SONG_LOAD_DIR=<dir on card> (e.g. SONGS/problem_songs) and DELUGE_HOST_SONG_LOAD_REPS=N, once the
// app has booted, recursively find every .XML under that dir and load each one (full performLoad: read song -> swap in
// -> delete the old song), looping over the whole set N times. reason_check is snapshotted around the run and
// LeakSanitizer is asked to report at the end; any per-load leak (something not freed when a song is replaced) shows up
// as growth in the pinned-cluster count and as LSan findings. This is the "loading lots of projects" leak repro.
constexpr int kMaxSongPaths = 512;
char g_songPaths[kMaxSongPaths][256];
int g_numSongPaths = 0;
int g_songLoadReps = -1; // -1 = not yet read, 0 = disabled
uint32_t g_songPollCount = 0;
bool g_songLoadDone = false;

void collectXmlPaths(const char* dir, int depth) {
	if (depth > 6 || g_numSongPaths >= kMaxSongPaths) {
		return;
	}
	DIR dp;
	if (f_opendir(&dp, dir) != FR_OK) {
		return;
	}
	FILINFO fno;
	while (f_readdir(&dp, &fno) == FR_OK && fno.fname[0] != 0) {
		if (fno.fname[0] == '.' || (fno.fattrib & AM_SYS) || (fno.fattrib & AM_HID)) {
			continue;
		}
		char child[256];
		snprintf(child, sizeof(child), "%s/%s", dir, fno.fname);
		if (fno.fattrib & AM_DIR) {
			collectXmlPaths(child, depth + 1);
		}
		else {
			size_t n = strlen(fno.fname);
			if (n > 4 && strcasecmp(fno.fname + n - 4, ".XML") == 0 && g_numSongPaths < kMaxSongPaths) {
				strncpy(g_songPaths[g_numSongPaths], child, sizeof(g_songPaths[0]) - 1);
				g_numSongPaths++;
			}
		}
	}
	f_closedir(&dp);
}

void loadOneSong(const char* fullPath) {
	// Drive the same path the boot loader / enter-key uses: make loadSongUI the active UI (so the session view doesn't
	// try to render - and dereference the transiently-null currentSong - during performLoad's yields), point the
	// browser at this song, then perform the full load (read -> swap in -> delete the old song).
	openUI(&loadSongUI);
	Error e = loadSongUI.setFileByFullPath(OutputType::NONE, fullPath);
	if (e != Error::NONE) {
		printf("  [song-load] setFileByFullPath failed (%d) for %s\n", (int)e, fullPath);
		std::fflush(stdout);
		return;
	}
	loadSongUI.performLoad();
}

void runSongLoadHarnessIfDue() {
	if (g_songLoadReps < 0) {
		const char* reps = std::getenv("DELUGE_HOST_SONG_LOAD_REPS");
		g_songLoadReps = std::getenv("DELUGE_HOST_SONG_LOAD_DIR") ? (reps ? std::atoi(reps) : 1) : 0;
	}
	if (g_songLoadReps <= 0 || g_songLoadDone) {
		return;
	}
	if (++g_songPollCount < 200) { // let the app finish booting
		return;
	}
	g_songLoadDone = true;

	const char* dir = std::getenv("DELUGE_HOST_SONG_LOAD_DIR");
	collectXmlPaths(dir, 0);
	printf("[song-load] found %d .XML files under %s; loading each x%d\n", g_numSongPaths, dir, g_songLoadReps);
	std::fflush(stdout);

	reason_check::Snapshot snap = reason_check::snapshot();
	for (int rep = 0; rep < g_songLoadReps; rep++) {
		for (int i = 0; i < g_numSongPaths; i++) {
			loadOneSong(g_songPaths[i]);
			printf("[song-load] rep %d song %d/%d: %s | pinned stealables now %u\n", rep, i + 1, g_numSongPaths,
			       g_songPaths[i], (unsigned)reason_check::liveClusters());
			std::fflush(stdout);
		}
	}

	printf("[song-load] done - reasons still outstanding across the run (leaks):\n");
	std::fflush(stdout);
	reason_check::reportOutstanding(snap);

#if defined(__SANITIZE_ADDRESS__)
	printf("[song-load] LeakSanitizer check:\n");
	std::fflush(stdout);
	__lsan_do_recoverable_leak_check();
#endif

	// Loading many songs generates more than the default audio-capture window, so don't depend on the host's natural
	// exit (which could fire mid-run). We've done the leak checks above; leave now.
	printf("[song-load] complete, exiting\n");
	std::fflush(stdout);
	std::quick_exit(0);
}
} // namespace

extern "C" void deluge_host_debug_install(void) {
	std::signal(SIGUSR1, onSignal);
	std::signal(SIGUSR2, onSignal);
	printf("[reason-leak] pid %ld: kill -USR1 to snapshot, kill -USR2 to report pinned clusters since\n",
	       (long)getpid());
	std::fflush(stdout);
}

extern "C" void deluge_host_debug_poll(void) {
	runKitCopyHarnessIfDue();
	runSongLoadHarnessIfDue();

	int req = g_request;
	if (req == 0) {
		return;
	}
	g_request = 0;

	if (req == 1) {
		g_baseline = reason_check::snapshot();
		printf("[reason-leak] snapshot at epoch %u (%u clusters pinned)\n", (unsigned)g_baseline,
		       (unsigned)reason_check::liveClusters());
		std::fflush(stdout);
	}
	else {
		printf("[reason-leak] pinned since snapshot %u:\n", (unsigned)g_baseline);
		std::fflush(stdout);
		reason_check::reportOutstanding(g_baseline);
	}
}

#else // no reason tracking in this build

extern "C" void deluge_host_debug_install(void) {
}
extern "C" void deluge_host_debug_poll(void) {
}

#endif
