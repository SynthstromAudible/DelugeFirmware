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
#include "RZA1/intc/devdrv_intc.h" // intc_func_active — HAL state read by the ISR guard
#include "libdeluge/storage_wait.h"
#include "resource_checker.h" // pulls in <extern.h> for the app-visible sdRoutineLock

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

// Cooperative yield hooks for slow-storage busy-waits (the <libdeluge/storage_wait.h>
// contract). The HAL/BSP calls these while spinning on SD / SPI-flash / USB so the
// scheduler keeps audio + UI + USB alive. Relocated here from the application: they
// are pure concurrency logic (an ISR guard, a reentrancy lock, a scheduler yield) and
// name no app code. sdRoutineLock stays app-visible (the UI reads it to gate user
// actions); intc_func_active is HAL state (the active interrupt id, 0 outside an ISR).
bool yieldingRoutineWithTimeoutForSD(RunCondition until, double timeoutSeconds) {
	if (intc_func_active != 0) {
		return false;
	}
	auto timeNow = getSystemTime();
	// We lock this to prevent multiple entry. Otherwise a storage wait could re-enter itself
	// through the work it yields to: SD wait -> audio task -> USB -> SD wait.
	if (sdRoutineLock) {
		// busy wait - matches running sdroutine in a loop while checking for condition
		while (!until()) {
			if (getSystemTime() > timeNow + timeoutSeconds) {
				return false;
			}
		}
		return true;
	}
	sdRoutineLock = true;
	bool ret = yieldWithTimeout(until, timeoutSeconds);
	sdRoutineLock = false;
	return ret;
}

void yieldingRoutineForSD(RunCondition until) {
	if (intc_func_active != 0) {
		return;
	}

	// We lock this to prevent multiple entry. Otherwise a storage wait could re-enter itself
	// through the work it yields to: SD wait -> audio task -> USB -> SD wait.
	if (sdRoutineLock) {
		// busy wait - matches running sdroutine in a loop while checking for condition
		while (!until()) {
			asm volatile("nop");
		}
		return;
	}
	sdRoutineLock = true;
	yield(until);
	sdRoutineLock = false;
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

void blockTask(TaskID id) {
	taskManager.blockTask(id);
}

void unblockTask(TaskID id) {
	taskManager.unblockTask(id);
}

double getSystemTime() {
	return taskManager.getSecondsFromStart();
}
}
