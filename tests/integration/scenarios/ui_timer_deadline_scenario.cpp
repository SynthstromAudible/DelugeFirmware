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

#include "scenarios/ui_timer_deadline_scenario.h"

#include "gui/ui_timer_manager.h"
#include "processing/engines/audio_engine.h"
#include "runtime/app_loop.h"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstdio>

// See staging_release_scenario.cpp: the app's printf alias has no provider in this harness.
#ifdef printf
#undef printf
#endif

namespace {

using deluge::runtime::appLoop;

/// Driver steps (about 2 virtual ms each) after boot before anything is measured, so boot's own timers have settled.
constexpr int32_t kSettleTicks = 100;
/// Driver steps the job's run count is measured over for UT-RATE.
constexpr int32_t kRateTicks = 250;
/// UT-RATE's bounds, in runs per second of the sample clock. The graphics routine alone re-arms every 15 ms (about 67
/// a second), so the job must run at least this often or the timers have stopped. A deadline can take a few runs: the
/// sample clock moves a block (2.9 ms) at a time, so a run at the deadline may find it not yet passed and look again
/// 1 ms later. Polling, the job ran every 0.8 ms, over 1,000 times a second.
constexpr int64_t kMinRunsPerSecond = 30;
constexpr int64_t kMaxRunsPerSecond = 400;
/// The timer UT-EARLIER sets: when it fires it only clears the tap-tempo count, which nothing here uses.
constexpr TimerName kProbe = TimerName::TAP_TEMPO_SWITCH_OFF;
constexpr int32_t kProbeMs = 2;
/// How far ahead, in samples, every other timer must be when the probe is set (about 13.6 ms), so the job's pending
/// run is that far off too.
constexpr int32_t kMinLeadSamples = 600;
/// The latest the probe may fire, in samples after it was set: its own 2 ms plus two blocks, one for the sample clock
/// to pass its time and one for this scenario to see it. A probe that waited for the job's pending run fires at least
/// kMinLeadSamples less a block after it was set; the job may run up to a block before a deadline on the sample
/// clock, which moves a block at a time.
constexpr int32_t kMaxFireSamples = kProbeMs * 44 + 2 * 128;
static_assert(kMaxFireSamples < kMinLeadSamples - 128);
/// Ticks to find a moment with that lead, and ticks for the probe to fire.
constexpr int32_t kArmTicks = 500;
constexpr int32_t kFireTicks = 50;

enum class Stage : uint8_t { Settle, Rate, Arm, Fire, Done };

Stage g_stage = Stage::Settle;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_ticks = 0;
uint32_t g_runsAtStart = 0;
uint32_t g_samplesAtStart = 0;
uint32_t g_graphicsTrigger = 0;
uint32_t g_probeSetAt = 0;
int32_t g_lead = 0;

void fail(const char* what) {
	printf("UT-FAIL %s\n", what);
	g_failures++;
}

uint32_t jobRuns() {
	return appLoop.stats(uiTimerManager.jobId).runs;
}

/// The time to the soonest active timer, in samples; INT32_MAX when none is active.
int32_t soonestTimer() {
	int32_t soonest = INT32_MAX;
	for (int32_t i = 0; i < static_cast<int32_t>(TimerName::NUM_TIMERS); i++) {
		const Timer& timer = uiTimerManager.getTimer(static_cast<TimerName>(i));
		if (timer.active) {
			soonest = std::min(soonest, static_cast<int32_t>(timer.triggerTime - AudioEngine::audioSampleTimer));
		}
	}
	return soonest;
}

void runTick() {
	switch (g_stage) {
	case Stage::Settle:
		if (++g_ticks >= kSettleTicks) {
			g_ticks = 0;
			g_runsAtStart = jobRuns();
			g_samplesAtStart = AudioEngine::audioSampleTimer;
			g_stage = Stage::Rate;
		}
		break;

	case Stage::Rate: {
		if (++g_ticks < kRateTicks) {
			break;
		}
		// A stats reset mid-window only lowers the count, which the upper bound tolerates.
		const int64_t runs = static_cast<int64_t>(jobRuns()) - g_runsAtStart;
		const int64_t samples = static_cast<uint32_t>(AudioEngine::audioSampleTimer - g_samplesAtStart);
		const int64_t perSecond = samples > 0 ? runs * kSampleRate / samples : 0;
		printf("UT-RATE runs=%lld samples=%lld perSecond=%lld\n", static_cast<long long>(runs),
		       static_cast<long long>(samples), static_cast<long long>(perSecond));
		if (perSecond < kMinRunsPerSecond) {
			fail("the ui routine job ran too seldom for the graphics routine's 15 ms timer");
		}
		if (perSecond > kMaxRunsPerSecond) {
			fail("the ui routine job runs far more often than its timers are due: it is still polling");
		}
		g_ticks = 0;
		g_graphicsTrigger = uiTimerManager.getTimer(TimerName::GRAPHICS_ROUTINE).triggerTime;
		g_stage = Stage::Arm;
		break;
	}

	// Set the probe just after the graphics routine has re-armed itself, so the job's pending run is that far off.
	case Stage::Arm: {
		const uint32_t graphics = uiTimerManager.getTimer(TimerName::GRAPHICS_ROUTINE).triggerTime;
		const bool rearmed = graphics != g_graphicsTrigger;
		g_graphicsTrigger = graphics;
		if (rearmed && !uiTimerManager.isTimerSet(kProbe)) {
			g_lead = soonestTimer();
			if (g_lead >= kMinLeadSamples) {
				g_probeSetAt = AudioEngine::audioSampleTimer;
				uiTimerManager.setTimer(kProbe, kProbeMs);
				g_ticks = 0;
				g_stage = Stage::Fire;
				break;
			}
		}
		if (++g_ticks >= kArmTicks) {
			printf("UT-EARLIER armed=0\n");
			fail("no moment with every timer at least 600 samples off");
			g_stage = Stage::Done;
		}
		break;
	}

	case Stage::Fire: {
		const bool fired = !uiTimerManager.isTimerSet(kProbe);
		if (!fired && ++g_ticks < kFireTicks) {
			break;
		}
		const auto elapsed = static_cast<int32_t>(AudioEngine::audioSampleTimer - g_probeSetAt);
		printf("UT-EARLIER armed=1 fired=%d lead=%d elapsed=%d max=%d\n", fired ? 1 : 0, static_cast<int>(g_lead),
		       static_cast<int>(elapsed), static_cast<int>(kMaxFireSamples));
		if (!fired) {
			fail("a 2 ms timer never fired");
		}
		else if (elapsed > kMaxFireSamples) {
			fail("a timer set earlier than the job's pending run waited for that run");
		}
		g_stage = Stage::Done;
		break;
	}

	case Stage::Done:
		printf("UT-RESULT failures=%d\n", static_cast<int>(g_failures));
		g_summaryPrinted = true;
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_ui_timer_deadline_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Settle;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_ticks = 0;
}

bool deluge_scenario_ui_timer_deadline_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_ui_timer_deadline_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	g_stepDone = true;
}

bool deluge_scenario_ui_timer_deadline_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_ui_timer_deadline_failures(void) {
	return g_failures;
}

} // extern "C"
