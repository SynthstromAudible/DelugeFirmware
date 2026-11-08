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

#include "scenarios/audio_tick_hold_scenario.h"

#include "libdeluge/app.h"
#include "processing/engines/audio_engine.h"

#include <cstdio>
#include <optional>
#include <stdexcept>

// The engine headers reach `src/lib/printf.h`, which soft-aliases printf to an implementation this harness has no
// `putchar_` for.
#ifdef printf
#undef printf
#endif

namespace {

int32_t g_failures = 0;

/// How many times the routine body was reached since the last reset.
int32_t g_reached = 0;

void countReach() {
	g_reached++;
}

void throwFromRoutine() {
	throw std::runtime_error("injected");
}

/// The routine's return from inside the routine.
std::optional<uint32_t> g_innerRoutine;

void reenter() {
	g_reached++;
	// Armed again for the inner call: routine() takes the hook before running it, so only a re-entry the guard lets
	// through reaches it.
	AudioEngine::setRoutineInjection(&countReach);
	g_innerRoutine = AudioEngine::routine();
	AudioEngine::setRoutineInjection(nullptr);
}

void report(const char* tag, bool ok, const char* detail) {
	std::printf("%s ok=%d %s\n", tag, ok ? 1 : 0, detail);
	if (!ok) {
		g_failures++;
	}
}

void checkIdle() {
	report("ATH-IDLE", !AudioEngine::audioTicksHeld(), "no hold alive, so ticks are not held");
}

void checkHold() {
	bool inside = false;
	{
		AudioEngine::AudioTickHold hold;
		inside = AudioEngine::audioTicksHeld();
	}
	const bool after = AudioEngine::audioTicksHeld();
	report("ATH-HOLD", inside && !after, "held inside the hold's scope, released after it");
}

void checkNested() {
	bool afterInner = false;
	{
		AudioEngine::AudioTickHold outer;
		{ AudioEngine::AudioTickHold inner; }
		afterInner = AudioEngine::audioTicksHeld();
	}
	const bool afterOuter = AudioEngine::audioTicksHeld();
	report("ATH-NESTED", afterInner && !afterOuter, "still held after the inner hold ends, released after the outer");
}

void checkHeldTick() {
	g_reached = 0;
	AudioEngine::setRoutineInjection(&countReach);
	bool task = true;
	uint32_t renders = 1;
	{
		AudioEngine::AudioTickHold hold;
		task = AudioEngine::routine_task();
		renders = AudioEngine::routine();
	}
	AudioEngine::setRoutineInjection(nullptr);
	report("ATH-HELD", !task && renders == 0 && g_reached == 0,
	       "a held routine_task() is false and routine() 0, and neither reaches the routine body");
}

void checkReentry() {
	g_reached = 0;
	g_innerRoutine.reset();
	AudioEngine::setRoutineInjection(&reenter);
	(void)AudioEngine::routine();
	AudioEngine::setRoutineInjection(nullptr);
	report("ATH-REENTRY", g_reached == 1 && g_innerRoutine == 0u,
	       "routine() called from inside routine() returns 0 and does not reach the body again");
}

void checkThrow() {
	AudioEngine::setRoutineInjection(&throwFromRoutine);
	const bool thrownTick = deluge_app_audio_tick();
	g_reached = 0;
	AudioEngine::setRoutineInjection(&countReach);
	(void)deluge_app_audio_tick();
	AudioEngine::setRoutineInjection(nullptr);
	report("ATH-THROW", !thrownTick && g_reached == 1,
	       "the tick that threw rendered nothing, and the next tick reached the routine body");
}

} // namespace

void deluge_scenario_audio_tick_hold_begin(void) {
	checkIdle();
	checkHold();
	checkNested();
	checkHeldTick();
	checkReentry();
	checkThrow();
	std::printf("ATH-RESULT failures=%d\n", static_cast<int>(g_failures));
	std::fflush(stdout);
}

bool deluge_scenario_audio_tick_hold_complete(void) {
	return true;
}

void deluge_scenario_audio_tick_hold_dispatch_step(void) {
}

bool deluge_scenario_audio_tick_hold_step_done(void) {
	return true;
}

int32_t deluge_scenario_audio_tick_hold_failures(void) {
	return g_failures;
}
