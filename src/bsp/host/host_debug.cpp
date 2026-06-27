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

#include "memory/reason_check.h"

#include <csignal>
#include <cstdio>
#include <unistd.h>

#if REASON_CHECK

namespace {
volatile std::sig_atomic_t g_request = 0; // 0 = nothing, 1 = snapshot, 2 = report
reason_check::Snapshot g_baseline = 0;

void onSignal(int sig) {
	g_request = (sig == SIGUSR1) ? 1 : 2;
}
} // namespace

extern "C" void deluge_host_debug_install(void) {
	std::signal(SIGUSR1, onSignal);
	std::signal(SIGUSR2, onSignal);
	std::printf("[reason-leak] pid %ld: kill -USR1 to snapshot, kill -USR2 to report pinned clusters since\n",
	            (long)getpid());
	std::fflush(stdout);
}

extern "C" void deluge_host_debug_poll(void) {
	int req = g_request;
	if (req == 0) {
		return;
	}
	g_request = 0;

	if (req == 1) {
		g_baseline = reason_check::snapshot();
		std::printf("[reason-leak] snapshot at epoch %u (%u clusters pinned)\n", (unsigned)g_baseline,
		            (unsigned)reason_check::liveClusters());
		std::fflush(stdout);
	}
	else {
		std::printf("[reason-leak] pinned since snapshot %u:\n", (unsigned)g_baseline);
		std::fflush(stdout);
		reason_check::reportOutstanding(g_baseline);
	}
}

#else // no reason tracking in this build

extern "C" void deluge_host_debug_install(void) {}
extern "C" void deluge_host_debug_poll(void) {}

#endif
