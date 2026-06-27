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

#include "definitions_cxx.hpp"      // OutputType
#include "gui/views/session_view.h" // sessionView, gridCreateInstrumentClipWithNewTrack
#include "memory/reason_check.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
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

extern "C" void deluge_host_debug_install(void) {}
extern "C" void deluge_host_debug_poll(void) {}

#endif
