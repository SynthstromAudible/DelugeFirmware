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
#ifndef DELUGE_TASK_SCHEDULER_H
#define DELUGE_TASK_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif
#include "stdint.h"

/// void function with no arguments
typedef void (*TaskHandle)();
typedef bool (*RunCondition)();
typedef int8_t TaskID;
struct TaskSchedule {
	// 0 is highest priority
	uint8_t priority;
	// time to wait between return and calling the function again
	double backOffPeriod;
	// target time between function calls
	double targetInterval;
	// maximum time between function calls
	double maxInterval;
};
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
uint8_t addRepeatingTask(TaskHandle task, uint8_t priority, double backOffTime, double targetTimeBetweenCalls,
                         double maxTimeBetweenCalls, const char* name);

/// Add a task to run once, aiming to run at current time + timeToWait and worst case run at timeToWait*10
uint8_t addOnceTask(TaskHandle task, uint8_t priority, double timeToWait, const char* name);

/// add a task that runs only after the condition returns true. Condition checks should be very fast or they could
/// interfere with scheduling
uint8_t addConditionalTask(TaskHandle task, uint8_t priority, RunCondition condition, const char* name);
void ignoreForStats();
double getLastRunTimeforCurrentTask();
double getSystemTime();
void setNextRunTimeforCurrentTask(double seconds);
void removeTask(TaskID id);
void yield(RunCondition until);
/// timeout in seconds, returns whether the condition was met
bool yieldWithTimeout(RunCondition until, double timeout);
/// start the task scheduler
void startTaskManager();
#ifdef __cplusplus
}
#endif
#endif // DELUGE_TASK_SCHEDULER_H
