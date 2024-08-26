/*
 * Copyright © 2025 Mark Adams
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

#include "OSLikeStuff/scheduler_api.h"
#include "OSLikeStuff/task_scheduler/task_scheduler.h"
#include "resource_checker.h"

extern TaskManager taskManager;

extern "C" {
void startTaskManager() {
	taskManager.start();
}
void ignoreForStats() {
	taskManager.ignoreForStats();
}
double getAverageRunTimeForTask(TaskID id) {
	return taskManager.getAverageRunTimeForTask(id);
}
double getAverageRunTimeforCurrentTask() {
	return taskManager.getAverageRunTimeForCurrentTask();
}

void setNextRunTimeforCurrentTask(double seconds) {
	taskManager.setNextRunTimeforCurrentTask(seconds);
}

int8_t addRepeatingTask(TaskHandle task, uint8_t priority, double backOffTime, double targetTimeBetweenCalls,
                        double maxTimeBetweenCalls, const char* name, ResourceID resources) {
	return taskManager.addRepeatingTask(task,
	                                    TaskSchedule{.priority = priority,
	                                                 .backOffPeriod = backOffTime,
	                                                 .targetInterval = targetTimeBetweenCalls,
	                                                 .maxInterval = maxTimeBetweenCalls},
	                                    name, ResourceChecker{resources});
}
int8_t addOnceTask(TaskHandle task, uint8_t priority, double timeToWait, const char* name, ResourceID resources) {
	return taskManager.addOnceTask(task, priority, timeToWait, name, ResourceChecker{resources});
}

int8_t addConditionalTask(TaskHandle task, uint8_t priority, RunCondition condition, const char* name,
                          ResourceID resources) {
	return taskManager.addConditionalTask(task, priority, condition, name, ResourceChecker{resources});
}

void yield(RunCondition until) {
	taskManager.yield(until);
}

bool yieldWithTimeout(RunCondition until, double timeout) {
	return taskManager.yield(until, timeout);
}

bool yieldToIdle(RunCondition until) {
	return taskManager.yield(until, 0, true);
}

void removeTask(TaskID id) {
	taskManager.removeTask(id);
}

void boostTask(TaskID id) {
	taskManager.boostTask(id);
}

void runTask(TaskID id) {
	taskManager.runTask(id);
}

double getSystemTime() {
	return taskManager.getSecondsFromStart();
}
}
