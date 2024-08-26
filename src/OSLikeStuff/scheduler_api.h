/*
 * Copyright © 2014-2023 Mark Adams
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
#ifndef DELUGE_SCHEDULER_API_H
#define DELUGE_SCHEDULER_API_H

// this is the external API for the task scheduler. The internal implementation is in C++ but as the scheduler is
// involved in the C portions of the codebase it needs a C api
#ifdef __cplusplus
extern "C" {
#endif
#include "stdint.h"
#include "timers_interrupts/clock_type.h"

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
int8_t addRepeatingTask(TaskHandle task, uint8_t priority, double backOffTime, double targetTimeBetweenCalls,
                        double maxTimeBetweenCalls, const char* name, ResourceID resource);

/// Add a task to run once, aiming to run at current time + timeToWait and worst case run at timeToWait*10
int8_t addOnceTask(TaskHandle task, uint8_t priority, double timeToWait, const char* name, ResourceID resources);

/// add a task that runs only after the condition returns true. Condition checks should be very fast or they could
/// interfere with scheduling
int8_t addConditionalTask(TaskHandle task, uint8_t priority, RunCondition condition, const char* name,
                          ResourceID resources);
void ignoreForStats();
double getAverageRunTimeForTask(TaskID id);
double getAverageRunTimeforCurrentTask();
double getSystemTime();
void setNextRunTimeforCurrentTask(double seconds);
void removeTask(TaskID id);
void boostTask(TaskID id);
void runTask(TaskID id);
void yield(RunCondition until);
/// timeout in seconds, returns whether the condition was met
bool yieldWithTimeout(RunCondition until, double timeout);
/// yield until the condition is met, but return immediately if the scheduler is idle
/// use if you're yielding in a loop such as the sd routine
bool yieldToIdle(RunCondition until);
/// start the task scheduler
void startTaskManager();
#ifdef __cplusplus
}
#endif
#endif // DELUGE_SCHEDULER_API_H
