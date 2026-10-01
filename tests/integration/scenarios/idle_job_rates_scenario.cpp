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

#include "scenarios/idle_job_rates_scenario.h"

#include "runtime/app_loop.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

// See staging_release_scenario.cpp: the app's printf alias has no provider in this harness.
#ifdef printf
#undef printf
#endif

namespace {

using deluge::runtime::appLoop;
using deluge::runtime::JobId;
using deluge::runtime::Loop;
using deluge::runtime::Micros;
using deluge::runtime::nowUs;

/// How long after boot the window starts, in µs of the app's clock: boot's own requests and loads have run by then.
constexpr Micros kSettleUs = 2'000'000;
/// The window the runs are counted over, in µs of the app's clock.
constexpr Micros kWindowUs = 10'000'000;
/// The jobs with no gap that only a wake or a request runs. On an idle app nothing asks for any of them.
constexpr std::array kWakeDriven = {
    "interpret encoders",    "reclaim retired", "buttons and pads", "song load",
    "document save",         "sample claim",    "folder import",    "reversion",
    "instrument load",       "settings io",     "startup failsafe", "Handle pending SysEx traffic.",
    "playback slow routine",
};
/// The ui routine's bounds, in runs per second: the graphics routine's 15 ms timer alone is about 67 a second, and a
/// deadline can take a few runs, since the sample clock moves a block (2.9 ms) at a time (ui-timer-deadline's UT-RATE
/// bounds).
constexpr double kUiMinPerSecond = 30.0;
constexpr double kUiMaxPerSecond = 400.0;

enum class Stage : uint8_t { Settle, Measure, Done };

Stage g_stage = Stage::Settle;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
Micros g_settleFrom = 0;
Micros g_windowFrom = 0;
/// Each job's run count at the previous step, and its runs counted in the window so far.
std::array<uint32_t, Loop::kMaxJobs> g_lastRuns{};
std::array<uint64_t, Loop::kMaxJobs> g_windowRuns{};

void fail(const char* what) {
	printf("IR-FAIL %s\n", what);
	g_failures++;
}

/// Adds each job's runs since the previous step. The debug "job stats" job zeroes every count every ten seconds; a
/// count lower than the last one read has been zeroed since, so all of it is new.
void accumulate() {
	for (JobId id = 0; id < appLoop.size(); ++id) {
		const uint32_t runs = appLoop.stats(id).runs;
		g_windowRuns[id] += runs >= g_lastRuns[id] ? runs - g_lastRuns[id] : runs;
		g_lastRuns[id] = runs;
	}
}

bool isWakeDriven(const char* name) {
	for (const char* wakeDriven : kWakeDriven) {
		if (std::strcmp(name, wakeDriven) == 0) {
			return true;
		}
	}
	return false;
}

void report(Micros elapsedUs) {
	for (JobId id = 0; id < appLoop.size(); ++id) {
		const auto s = appLoop.stats(id);
		const double perSecond = static_cast<double>(g_windowRuns[id]) * 1e6 / static_cast<double>(elapsedUs);
		printf("IR-JOB runs=%llu perSecond=%.1f name=%s\n", static_cast<unsigned long long>(g_windowRuns[id]),
		       perSecond, s.name);
		if (isWakeDriven(s.name) && g_windowRuns[id] != 0) {
			printf("IR-FAIL job=%s\n", s.name);
			fail("a wake-driven job ran on an idle app: something polls it");
		}
		if (std::strcmp(s.name, "ui routine") == 0 && (perSecond < kUiMinPerSecond || perSecond > kUiMaxPerSecond)) {
			fail("the ui routine ran outside its timers' rate");
		}
	}
}

void runTick() {
	switch (g_stage) {
	case Stage::Settle:
		if (nowUs() - g_settleFrom >= kSettleUs) {
			for (JobId id = 0; id < appLoop.size(); ++id) {
				g_lastRuns[id] = appLoop.stats(id).runs;
				g_windowRuns[id] = 0;
			}
			g_windowFrom = nowUs();
			g_stage = Stage::Measure;
		}
		break;

	case Stage::Measure: {
		accumulate();
		const Micros elapsed = nowUs() - g_windowFrom;
		if (elapsed >= kWindowUs) {
			printf("IR-WINDOW us=%llu\n", static_cast<unsigned long long>(elapsed));
			report(elapsed);
			g_stage = Stage::Done;
		}
		break;
	}

	case Stage::Done:
		printf("IR-RESULT failures=%d\n", static_cast<int>(g_failures));
		g_summaryPrinted = true;
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_idle_job_rates_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Settle;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_settleFrom = nowUs();
}

bool deluge_scenario_idle_job_rates_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_idle_job_rates_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	runTick();
	g_stepDone = true;
}

bool deluge_scenario_idle_job_rates_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_idle_job_rates_failures(void) {
	return g_failures;
}

} // extern "C"
