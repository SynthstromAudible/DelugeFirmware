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

#include "model/settings/runtime_feature_settings.h"
#include "storage/owner.h"
#include "storage/settings_file.h"

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
		g_stage = Stage::Tier;
		break;

	case Stage::Tier: {
		const int32_t violations = deluge::storage::settings::interactionTierSettingsIo();
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
}

bool deluge_scenario_settings_channel_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_settings_channel_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	// A marker op behind whatever the tick queued: the owner queue is FIFO, so this runs after the
	// dispatched operation (and immediately if nothing was dispatched). That is what makes each
	// *Check stage read a result the operation has actually produced.
	if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
		g_stepDone = true;
	}
}

bool deluge_scenario_settings_channel_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_settings_channel_failures(void) {
	return g_failures;
}

} // extern "C"
