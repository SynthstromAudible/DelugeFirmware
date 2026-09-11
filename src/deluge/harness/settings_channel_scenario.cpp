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

#include "harness/settings_channel_scenario.h"

#include "libdeluge/file_bytes.h"
#include "model/settings/runtime_feature_settings.h"
#include "storage/byte_channel.h"
#include "storage/owner.h"

#include <cstdio>

// The GUI headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded
// `printf_`. That implementation emits through `putchar_`, which only the C host BSP defines -- this
// harness links the Rust BSP and has no provider, so the alias turns every line this file writes
// into an undefined symbol at link time. Every one of those lines IS the gate's output.
#ifdef printf
#undef printf
#endif

namespace {

/// The setting the round trip carries. `RuntimeFeatureSettingType` values are plain integers in the
/// file, so any of them proves the path; this one is chosen because it is a simple toggle with no
/// side effects on anything else in the scenario.
constexpr RuntimeFeatureSettingType kProbeSetting = RuntimeFeatureSettingType::AlternativePlaybackStartBehaviour;

/// A value the file cannot already hold by accident.
///
/// Nonzero on purpose: 0 is the default for every toggle, so a round trip that asserted 0 would pass
/// against a file that was never written at all.
constexpr uint32_t kProbeValue = 1;

/// The burst's three values. The first must differ from the third: a seam that let the in-flight write be
/// replaced would lose the second and third and leave the first on the card, and only differing values show it.
constexpr uint32_t kBurstFirst = 2;
constexpr uint32_t kBurstSecond = 4;
constexpr uint32_t kBurstThird = 3;
/// What the order stage writes.
constexpr uint32_t kOrderValue = 5;
/// What the burst's read-back scrubs the in-memory value to first, so a read that did nothing cannot pass.
constexpr uint32_t kScrubValue = 99;
/// A bound on every idle wait, so a channel that never settles ends the run with a report.
constexpr int32_t kMaxWaitTicks = 20000;

/// Every stage has a `*Check` successor that runs one tick later.
///
/// Not decoration. A settings operation is only ENQUEUED when the consumer's entry point runs -- it
/// executes on a later worker poll -- so reading the result in the same tick reads the state from
/// before the operation and reports success for an operation that has not happened. The same shape
/// the save round-trip and the browser gates use, and for the same reason.
enum class Stage : uint8_t {
	Write,
	WriteCheck,
	Read,
	ReadCheck,
	BurstFirst,
	BurstWait,
	BurstReadWait,
	BurstCheck,
	OrderSubmit,
	OrderWait,
	OrderCheck,
	Fallback,
	Restore,
	RestoreWait,
	Tier,
	Summary,
	Done,
};

Stage g_stage = Stage::Write;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
uint32_t g_wrote = 0;
uint32_t g_read = 0;
uint32_t g_original = 0;
int32_t g_waitTicks = 0;
int32_t g_servedBefore = 0;
int32_t g_fallbacksBefore = 0;
int32_t g_burstServed = 0;
int32_t g_burstFallbacks = 0;
int32_t g_orderFallbacks = 0;
int32_t g_orderServed = 0;
/// Harness markers the owner queue refused. A refused marker ends a step before its owner work has run.
int32_t g_markerDrops = 0;
/// A marker the owner queue refused, still to be enqueued. See deluge_scenario_settings_channel_step_done.
bool g_markerPending = false;
int32_t g_dropsBefore = 0;
int32_t g_markerDropsBefore = 0;

/// Advance to `next` once the seam is idle; report and move to the restore if it never is.
void waitIdleThen(const char* marker, Stage next, Stage onTimeout) {
	if (deluge::storage::bytes::idle()) {
		g_waitTicks = 0;
		g_stage = next;
		return;
	}
	if (++g_waitTicks > kMaxWaitTicks) {
		printf("%s FAIL the seam never went idle (%d ticks)\n", marker, g_waitTicks);
		g_failures++;
		g_waitTicks = 0;
		g_stage = onTimeout;
	}
}

void runTick() {
	switch (g_stage) {
	case Stage::Write:
		// Through the REAL consumer entry point, never the layer underneath it: the whole subject of
		// this rung is which tier that entry point does its I/O on, so a gate that reached past it
		// would keep testing the unconverted path forever.
		g_wrote = kProbeValue;
		runtimeFeatureSettings.set(kProbeSetting, g_wrote);
		runtimeFeatureSettings.writeSettingsToFile();
		g_stage = Stage::WriteCheck;
		break;

	case Stage::WriteCheck:
		// Nothing to assert here yet -- the write's own outcome is what the read proves. This stage
		// exists to spend the tick the enqueued write needs.
		g_stage = Stage::Read;
		break;

	case Stage::Read:
		// Scrub the in-memory value first. Without this the read could do nothing at all and the
		// comparison below would still pass, which is exactly the false-passing shape rung 5's
		// round trip shipped with (it compared a file against itself).
		runtimeFeatureSettings.set(kProbeSetting, kProbeValue + 1);
		runtimeFeatureSettings.readSettingsFromFile();
		g_stage = Stage::ReadCheck;
		break;

	case Stage::ReadCheck:
		g_read = runtimeFeatureSettings.get(kProbeSetting);
		printf("SC-ROUNDTRIP subsystem=runtime_features wrote=%u read=%u\n", g_wrote, g_read);
		if (g_read != g_wrote) {
			printf("SC-ROUNDTRIP FAIL the value did not survive the write/read pair\n");
			g_failures++;
		}
		g_stage = Stage::BurstFirst;
		break;

	case Stage::BurstFirst:
		// Start from an empty seam, so the burst's served count is the burst's alone.
		if (!deluge::storage::bytes::idle()) {
			waitIdleThen("SC-BURST", Stage::BurstFirst, Stage::Restore);
			break;
		}
		g_servedBefore = deluge::storage::bytes::channelServedOps();
		g_fallbacksBefore = deluge::storage::bytes::ownerFallbacks();
		g_dropsBefore = deluge::storage::bytes::ownerDispatchesDropped();
		g_markerDropsBefore = g_markerDrops;
		runtimeFeatureSettings.set(kProbeSetting, kBurstFirst);
		runtimeFeatureSettings.writeSettingsToFile();
		// Drive the seam once, HERE, so the first write is in flight before the other two are submitted. Not a
		// tick later: this scenario's tick is not the seam's only pump -- the app's own "settings io" task calls
		// serviceRoutine() too -- so by the next tick the first write may already have finished. One call starts a
		// request and returns without collecting it, and nothing else can run inside this stage.
		deluge::storage::bytes::serviceRoutine();
		// The second waits and the third replaces it: two card writes, not three -- and not one, which is what
		// replacing the in-flight write would give.
		runtimeFeatureSettings.set(kProbeSetting, kBurstSecond);
		runtimeFeatureSettings.writeSettingsToFile();
		runtimeFeatureSettings.set(kProbeSetting, kBurstThird);
		runtimeFeatureSettings.writeSettingsToFile();
		g_waitTicks = 0;
		g_stage = Stage::BurstWait;
		break;

	case Stage::BurstWait:
		if (!deluge::storage::bytes::idle()) {
			waitIdleThen("SC-BURST", Stage::BurstWait, Stage::Restore);
			break;
		}
		g_burstServed = deluge::storage::bytes::channelServedOps() - g_servedBefore;
		g_burstFallbacks = deluge::storage::bytes::ownerFallbacks() - g_fallbacksBefore;
		runtimeFeatureSettings.set(kProbeSetting, kScrubValue);
		runtimeFeatureSettings.readSettingsFromFile();
		g_stage = Stage::BurstReadWait;
		break;

	case Stage::BurstReadWait:
		waitIdleThen("SC-BURST", Stage::BurstCheck, Stage::Restore);
		break;

	case Stage::BurstCheck: {
		const uint32_t value = runtimeFeatureSettings.get(kProbeSetting);
		const bool channel = deluge_file_bytes_async_active();
		printf("SC-BURST value=%u served=%d fallbacks=%d drops=%d markerDrops=%d\n", value, g_burstServed,
		       g_burstFallbacks, deluge::storage::bytes::ownerDispatchesDropped() - g_dropsBefore,
		       g_markerDrops - g_markerDropsBefore);
		if (value != kBurstThird) {
			printf("SC-BURST FAIL the last write of the burst did not land (read %u, wanted %u)\n", value, kBurstThird);
			g_failures++;
		}
		// Channel leg: exactly two card writes, all through the channel. Off leg: none through the channel, and
		// each of the three writes went to the owner.
		if (channel && g_burstServed != 2) {
			printf("SC-BURST FAIL the channel carried %d writes for a burst that should collapse to 2\n",
			       g_burstServed);
			g_failures++;
		}
		if (!channel && g_burstServed != 0) {
			printf("SC-BURST FAIL the channel carried %d operations on a leg without it\n", g_burstServed);
			g_failures++;
		}
		g_stage = Stage::OrderSubmit;
		break;
	}

	case Stage::OrderSubmit:
		// Write then read in ONE tick, so the read is submitted while the write is still waiting. No scrub of
		// the in-memory value in between, and deliberately: the channel-off leg's owner arm serialises the
		// settings when it RUNS, not when it was called, so a scrub made in this tick would be what it wrote.
		// A read that started ahead of the write would load the file's previous value over this one, which
		// is what the check below catches; the served count is what shows the read ran at all.
		g_fallbacksBefore = deluge::storage::bytes::ownerFallbacks();
		g_servedBefore = deluge::storage::bytes::channelServedOps();
		runtimeFeatureSettings.set(kProbeSetting, kOrderValue);
		runtimeFeatureSettings.writeSettingsToFile();
		runtimeFeatureSettings.readSettingsFromFile();
		g_waitTicks = 0;
		g_stage = Stage::OrderWait;
		break;

	case Stage::OrderWait:
		waitIdleThen("SC-ORDER", Stage::OrderCheck, Stage::Restore);
		break;

	case Stage::OrderCheck: {
		const uint32_t value = runtimeFeatureSettings.get(kProbeSetting);
		g_orderFallbacks = deluge::storage::bytes::ownerFallbacks() - g_fallbacksBefore;
		g_orderServed = deluge::storage::bytes::channelServedOps() - g_servedBefore;
		const bool channel = deluge_file_bytes_async_active();
		printf("SC-ORDER value=%u served=%d\n", value, g_orderServed);
		if (value != kOrderValue) {
			printf("SC-ORDER FAIL a read queued behind a write did not see it (read %u, wanted %u)\n", value,
			       kOrderValue);
			g_failures++;
		}
		if (channel && g_orderServed != 2) {
			printf("SC-ORDER FAIL the channel carried %d operations for one write and one read\n", g_orderServed);
			g_failures++;
		}
		g_stage = Stage::Fallback;
		break;
	}

	case Stage::Fallback: {
		const bool channel = deluge_file_bytes_async_active();
		printf("SC-FALLBACK burst=%d order=%d dropped=%d\n", g_burstFallbacks, g_orderFallbacks,
		       deluge::storage::bytes::ownerDispatchesDropped());
		// Channel leg: a busy channel must queue, so nothing reaches the owner. Off leg: every submission does --
		// three burst writes, then the order stage's write and read -- which is also the evidence the switch is
		// really off there.
		const bool dropsOk = deluge::storage::bytes::ownerDispatchesDropped() == 0;
		const bool ok = dropsOk
		                && (channel ? (g_burstFallbacks == 0 && g_orderFallbacks == 0)
		                            : (g_burstFallbacks == 3 && g_orderFallbacks == 2));
		if (!ok) {
			printf("SC-FALLBACK FAIL owner dispatches were not what this leg requires\n");
			g_failures++;
		}
		g_stage = Stage::Restore;
		break;
	}

	case Stage::Restore:
		// The card image is shared with other gates that read this file at boot: leave the setting as it was.
		runtimeFeatureSettings.set(kProbeSetting, g_original);
		runtimeFeatureSettings.writeSettingsToFile();
		g_waitTicks = 0;
		g_stage = Stage::RestoreWait;
		break;

	case Stage::RestoreWait:
		waitIdleThen("SC-RESTORE", Stage::Tier, Stage::Tier);
		break;

	case Stage::Tier: {
		// Reported, never asserted here. This gate runs in both configurations: with the channel
		// compiled out the honest answer is 0, and failing on it would make the default build fail
		// for doing exactly what it is supposed to do. `settings_channel.sh`'s channel-on leg is what
		// requires this to be non-zero -- see there.
		printf("SC-CHANNEL served=%d\n", deluge::storage::bytes::channelServedOps());
		const int32_t violations = deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Settings);
		printf("SC-TIER violations=%d\n", violations);
		if (violations != 0) {
			// Before the conversion this is the DEFECT the rung exists to remove, and the gate is
			// expected to fail on it. After the conversion a non-zero count is a regression.
			printf("SC-TIER FAIL settings I/O ran on the interaction tier\n");
			g_failures++;
		}
		g_stage = Stage::Summary;
		break;
	}

	case Stage::Summary:
		printf("SC-RESULT failures=%d\n", g_failures);
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

void deluge_scenario_settings_channel_begin(void) {
	// Line-buffer stdout: piped into a script it is block-buffered by default. See the flush above.
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Write;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_wrote = 0;
	g_read = 0;
	g_original = runtimeFeatureSettings.get(kProbeSetting);
	g_waitTicks = 0;
	g_burstServed = 0;
	g_burstFallbacks = 0;
	g_orderFallbacks = 0;
	g_markerDrops = 0;
	g_markerPending = false;
}

bool deluge_scenario_settings_channel_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_settings_channel_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	// The channel's pump. In the firmware this is a repeating task registered in `deluge.cpp`; here
	// the scenario owns the tick, so without this call the channel arm would be requested and never
	// collected -- and a dead channel would pass, which is the failure rung 3 shipped.
	deluge::storage::bytes::serviceRoutine();
	runTick();
	// A marker op behind whatever the tick queued: the owner queue is FIFO, so this runs after the
	// dispatched operation (and immediately if nothing was dispatched). That is what makes each
	// *Check stage read a result the operation has actually produced.
	if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
		// The owner queue is full -- NOT a reason to call the step done. Declaring it done here moved the scenario
		// on before the owner had run the work this tick queued (a burst of three settings writes plus any other
		// owner op fills the four slots), and the owner arms then serialised a later tick's in-memory state.
		// Keep asking from step_done until the marker is queued, behind that work.
		g_markerDrops++;
		g_markerPending = true;
	}
}

bool deluge_scenario_settings_channel_step_done(void) {
	if (g_markerPending) {
		if (deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
			g_markerPending = false;
		}
		return false;
	}
	return g_stepDone;
}

int32_t deluge_scenario_settings_channel_failures(void) {
	return g_failures;
}

} // extern "C"
