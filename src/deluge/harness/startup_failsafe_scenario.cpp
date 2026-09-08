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

#include "harness/startup_failsafe_scenario.h"

#include "definitions_cxx.hpp"
#include "io/file.hpp"
#include "storage/byte_channel.h"
#include "storage/flash_storage.h"
#include "storage/owner.h"
#include "storage/startup_song.h"

#include <cstdio>

// The headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded `printf_`.
// That implementation emits through `putchar_`, which only the C host BSP defines -- this harness
// links the Rust BSP and has no provider, so the alias turns every line this file writes into an
// undefined symbol at link time. Every one of those lines IS the gate's output.
#ifdef printf
#undef printf
#endif

namespace {

/// The template the fail-safe falls back to. Removed before the run so the song-missing path is
/// reached rather than the load.
constexpr const char* kDefaultSong = "SONGS/DEFAULT.XML";

/// Every stage has a `*Check` successor that runs one tick later.
///
/// Not decoration. The fail-safe's operations become owner-dispatched and channel-backed, and in both
/// shapes the work is only ENQUEUED when the entry point runs -- it executes on a later worker poll.
/// Reading the result in the same tick reads the state from before the operation and reports success
/// for something that has not happened.
enum class Stage : uint8_t {
	Fixture,
	FixtureCheck,
	Drive,
	DriveCheck,
	Tier,
	Summary,
	Done,
};

/// How many ticks the machine gets to finish before the gate calls it wedged.
constexpr int32_t kMaxDriveTicks = 500;

Stage g_stage = Stage::Fixture;
int32_t g_driveTicks = 0;
/// Whether the machine has been observed in its canary-write phase yet.
bool g_sawCanaryPhase = false;
/// Whether the canary was readable ON THE CARD at the instant the machine left that phase. -1 until
/// the transition is observed, so a run that never reached it cannot look like a pass.
int32_t g_canaryOnCard = -1;

/// Observe the ordering guarantee: by the time the machine leaves its canary-write phase, everything
/// downstream leads to the load, so the canary must ALREADY be on the card.
///
/// Checked by opening the file, not by asking the machine -- asking the machine would only confirm
/// that it believes the write landed, which is the very thing under test. Called every tick, from
/// beside the machine's own pump.
void observeCanaryOrdering() {
	if (deluge::storage::startupSongAwaitingCanary()) {
		g_sawCanaryPhase = true;
		return;
	}
	if (!g_sawCanaryPhase || g_canaryOnCard >= 0) {
		return;
	}
	const char* path = deluge::storage::startupSongCanaryPath();
	auto canary = deluge::io::File::open(path, DELUGE_FILE_READ);
	g_canaryOnCard = canary.has_value() ? 1 : 0;
	if (canary.has_value()) {
		(void)canary->close();
	}
}
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;

void runTick() {
	switch (g_stage) {
	case Stage::Fixture:
		// LASTOPENED with no startup song recorded, and no template to fall back to. That combination
		// reaches the canary creation, every existence check and the cleanup unlink, and returns
		// before `openUI` -- which this harness cannot survive, having no UI stack.
		FlashStorage::defaultStartupSongMode = StartupSongMode::LASTOPENED;
		(void)deluge::io::unlink(kDefaultSong);
		g_stage = Stage::FixtureCheck;
		break;

	case Stage::FixtureCheck:
		g_stage = Stage::Drive;
		break;

	case Stage::Drive:
		// The REAL entry point, never the layer underneath it: the whole subject of this rung is which
		// tier that entry point does its I/O on, so a gate that reached past it would keep testing the
		// unconverted path forever.
		deluge::storage::armStartupSong();
		g_stage = Stage::DriveCheck;
		break;

	case Stage::DriveCheck:
		// The machine is phased now, so one tick is not enough: wait until it has finished before
		// reading the tier count, or the count would be read partway through and report whatever
		// happened to have run. Bounded so a wedged machine fails the gate rather than hanging it.
		if (!deluge::storage::startupSongIdle() && ++g_driveTicks < kMaxDriveTicks) {
			break;
		}
		if (g_driveTicks >= kMaxDriveTicks) {
			printf("SF-DRIVE FAIL the fail-safe machine did not finish within %d ticks\n", kMaxDriveTicks);
			g_failures++;
		}
		g_stage = Stage::Tier;
		break;

	case Stage::Tier: {
		// Reported, never asserted here. This gate runs in both configurations, and with the channel
		// compiled out the honest answer is 0; failing on that would make the default build fail for
		// doing exactly what it is supposed to do. The channel-on leg of `startup_failsafe.sh` is what
		// requires this to be non-zero.
		const int32_t confirmed = deluge::storage::startupSongCanaryConfirmed() ? 1 : 0;
		printf("SF-ORDER canaryOnCard=%d confirmedBeforeAdvancing=%d\n", g_canaryOnCard, confirmed);
		if (confirmed != 1) {
			// The half that can be tested. The card check below proves the canary is THERE; this
			// proves the machine WAITED for it rather than reaching that state by luck, which on a
			// host where the channel completes within a tick is the only way to tell the two apart.
			printf("SF-ORDER FAIL the machine advanced past the canary write without a terminal answer\n");
			g_failures++;
		}
		if (g_canaryOnCard != 1) {
			// THE ORDERING GUARANTEE. A canary that has not landed when the machine allows the load to
			// proceed is precisely the failure the fail-safe exists to prevent: the device dies
			// mid-load, no canary survives, and the next boot retries the same song.
			printf("SF-ORDER FAIL the canary was not on the card when the load was allowed to proceed\n");
			g_failures++;
		}
		printf("SF-CHANNEL served=%d\n", deluge::storage::bytes::channelServedOps());

		const int32_t violations = deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Startup);
		printf("SF-TIER violations=%d\n", violations);
		if (violations != 0) {
			// Before the conversion this is the DEFECT the rung exists to remove, and the gate is
			// expected to fail on it. After the conversion a non-zero count is a regression.
			printf("SF-TIER FAIL startup fail-safe I/O ran on the interaction tier\n");
			g_failures++;
		}
		g_stage = Stage::Summary;
		break;
	}

	case Stage::Summary:
		printf("SF-RESULT failures=%d\n", g_failures);
		// Piped into a script stdout is block-buffered and the harness exits without flushing C
		// stdio, so without this the entire gate output vanishes while the run reports success.
		fflush(stdout);
		g_summaryPrinted = true;
		g_stage = Stage::Done;
		break;

	case Stage::Done:
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_startup_failsafe_begin(void) {
	// Line-buffer stdout: piped into a script it is block-buffered by default. See the flush above.
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Fixture;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_driveTicks = 0;
	g_sawCanaryPhase = false;
	g_canaryOnCard = -1;
}

bool deluge_scenario_startup_failsafe_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_startup_failsafe_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	// The machine's pump. In the firmware this is a repeating task registered in `deluge.cpp`; here
	// the scenario owns the tick. Called before AND after the stage below so the machine gets a tick
	// per dispatch whatever the stage does -- without it the fail-safe would be armed and never
	// advanced, and a dead channel would pass.
	deluge::storage::startupSongRoutine();
	observeCanaryOrdering();
	runTick();
	// A marker op behind whatever the tick queued: the owner queue is FIFO, so this runs after the
	// dispatched operation (and immediately if nothing was dispatched). That is what makes each
	// *Check stage read a result the operation has actually produced.
	if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
		g_stepDone = true;
	}
}

bool deluge_scenario_startup_failsafe_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_startup_failsafe_failures(void) {
	return g_failures;
}

} // extern "C"
