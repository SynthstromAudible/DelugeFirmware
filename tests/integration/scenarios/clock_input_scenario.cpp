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

#include "scenarios/clock_input_scenario.h"

#include "libdeluge/cv_gate.h"
#include "playback/playback_handler.h"
#include "playback/trigger_clock_input.h"

#include <cstdio>

// See staging_release_scenario.cpp: the app's printf alias has no provider in this harness.
#ifdef printf
#undef printf
#endif

namespace {

/// Edges to deliver. More than the ring holds, spaced so none is dropped.
constexpr int32_t kEdges = 8;
/// Ticks (virtual milliseconds) between edges: faster than the half-second auto-stop, slower than routine() runs.
constexpr int32_t kTicksBetweenEdges = 20;
/// Ticks after the last edge before the check, so routine() has drained the ring.
constexpr int32_t kSettleTicks = 10;

enum class Stage : uint8_t { Arm, Edges, Settle, Report, Done };

Stage g_stage = Stage::Arm;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_ticks = 0;
int32_t g_edges = 0;

void fail(const char* what) {
	printf("TC-FAIL %s\n", what);
	g_failures++;
}

void runTick() {
	switch (g_stage) {
	case Stage::Arm:
		if (playbackHandler.playbackState != 0) {
			fail("playing before any edge");
		}
		playbackHandler.analogClockInputAutoStart = true;
		g_stage = Stage::Edges;
		break;

	case Stage::Edges:
		if (++g_ticks >= kTicksBetweenEdges) {
			g_ticks = 0;
			deluge_trigger_clock_host_edge(0);
			if (++g_edges == kEdges) {
				g_stage = Stage::Settle;
			}
		}
		break;

	case Stage::Settle:
		if (++g_ticks >= kSettleTicks) {
			g_stage = Stage::Report;
		}
		break;

	case Stage::Report: {
		const bool playing = playbackHandler.playbackState != 0;
		const uint32_t dropped = deluge::playback::triggerClockEdges.dropped();
		printf("TC-INPUT playing=%d edges=%d dropped=%u\n", playing ? 1 : 0, static_cast<int>(g_edges),
		       static_cast<unsigned>(dropped));
		if (!playing) {
			fail("trigger-clock edges with auto-start on did not start playback");
		}
		if (dropped != 0) {
			fail("edges spaced wider than routine() runs were dropped");
		}
		if (playing) {
			playbackHandler.endPlayback();
		}
		playbackHandler.analogClockInputAutoStart = false;
		printf("TC-RESULT failures=%d\n", static_cast<int>(g_failures));
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

void deluge_scenario_clock_input_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Arm;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_ticks = 0;
	g_edges = 0;
}

bool deluge_scenario_clock_input_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_clock_input_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	g_stepDone = true;
}

bool deluge_scenario_clock_input_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_clock_input_failures(void) {
	return g_failures;
}

} // extern "C"
