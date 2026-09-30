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

/// libdeluge/scheduler.h — cooperative task scheduling.
///
/// Registers tasks that run cooperatively from a single caller loop, paced by priority and a
/// target/back-off/max cadence (`addRepeatingTask`) or a predicate (`addConditionalTask`). The
/// scheduler itself is the BSP's (`src/bsp/rust/src/scheduler.rs`); the application is only ever
/// on the C side of this ABI. Also declares the clock tick type and rate (`dTime`,
/// `DELUGE_CLOCKS_PER*`) that the C++ `Time` wrapper (`task_scheduler/clock_type.h`) is built on.
///
/// ON EMBASSY: this same C API can be implemented on top of the Embassy executor
/// (Stage 2 in docs/dev/libdeluge_bsp_design.md) — `addRepeatingTask` -> spawn a
/// task with an embassy-time ticker, `RESOURCE_*` -> embassy-sync mutexes. The
/// application keeps this interface either way.
#ifndef LIBDELUGE_SCHEDULER_H
#define LIBDELUGE_SCHEDULER_H

#include "abi.h"
#include <stdint.h>
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// A tick count at the board's fixed clock rate (see DELUGE_CLOCKS_PER).
typedef int64_t dTime;
#define DELUGE_CLOCKS_PER 33330000
#define DELUGE_CLOCKS_PERf 33330000.

/// void function with no arguments
typedef void (*TaskHandle)();
typedef bool (*RunCondition)();
typedef int8_t TaskID;
typedef uint32_t ResourceID;
#define RESOURCE_NONE 0
// for actually reading the SD
#define RESOURCE_SD 1
#define RESOURCE_USB 2
// for things that can't run in the SD routine
#define RESOURCE_SD_ROUTINE 4

/// Schedule a task that will be called at a regular interval.
///
/// The scheduler will try to run the task at a regular cadence such that the time between start of calls to the
/// task is approximately targetTimeBetweenCalls. It will never call the task sooner than backOffTime seconds after it
/// last completed.
///
/// Tasks are selected to run based on priority and expected duration (computed via a running average of previous
/// invocations of the task). The task with the lowest priority that can complete before a task with higher
/// priority needs to start will run, without violation of the backOffTime.
///
/// @param task The task to call
/// @param priority Priority of the task. Tasks with lower numbers are given preference over tasks with higher numbers.
/// @param backOffTime Minimum time from completing the task to calling it again in seconds.
/// @param targetTimeBetweenCalls Desired time between calls to the task, including the runtime for the task itself.
/// @param resource
/// [task]
int8_t addRepeatingTask(TaskHandle task, uint8_t priority, double backOffTime, double targetTimeBetweenCalls,
                        double maxTimeBetweenCalls, const char* name, ResourceID resource) DELUGE_NOEXCEPT;

/// add a task that runs only after the condition returns true. Condition checks should be very fast or they could
/// interfere with scheduling
/// [task]
int8_t addConditionalTask(TaskHandle task, uint8_t priority, RunCondition condition, const char* name,
                          ResourceID resources) DELUGE_NOEXCEPT;

/// Exclude the current task invocation's run time from its scheduling average (e.g. this call did
/// no real work, so it should not count toward how long the task is expected to take). [task]
void ignoreForStats() DELUGE_NOEXCEPT;

/// Running average of how long `id`'s task takes to run per call, in seconds. [task]
double getAverageRunTimeForTask(TaskID id) DELUGE_NOEXCEPT;

/// Seconds elapsed since the scheduler started: a free-running monotonic clock. [task]
double getSystemTime() DELUGE_NOEXCEPT;

/// Wake `id`'s task to run as soon as the scheduler next has a chance, without waiting for its
/// normal cadence. [task]
void runTask(TaskID id) DELUGE_NOEXCEPT;

/// Give other tasks a chance to run, resuming when `until` returns true (or after giving them one
/// chance, if `until` is NULL). A cooperative checkpoint for a caller that must not hold the
/// current task indefinitely. [task]
void yield(RunCondition until) DELUGE_NOEXCEPT;

/// set the current task to blocked
/// [task]
void blockTask(TaskID id) DELUGE_NOEXCEPT;

/// Clear a task's blocked state set by blockTask, so it can be scheduled again. [task]
void unblockTask(TaskID id) DELUGE_NOEXCEPT;

/// True while a slow-storage (SD / SPI-flash / USB) busy-wait is yielding to the scheduler.
/// The application reads it to gate user actions that must not run re-entrantly inside a
/// storage routine. Owned by the
/// scheduler (the runtime concurrency layer), not the app — on Embassy this is a runtime flag.
/// [task]
bool isSDRoutineActive(void) DELUGE_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif // LIBDELUGE_SCHEDULER_H
