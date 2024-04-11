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

#include "task_scheduler.h"
#include "RZA1/ostm/ostm.h"

struct Task {
	void (*handle)();
	uint8_t priority;
	uint32_t lastCallTime;
	uint32_t averageDuration;
	uint32_t minTimeBetweenCalls;
	uint32_t maxTimeBetweenCalls;
};

class TaskManager {

	struct Task list[20];
	uint8_t capacity;

	Task_Handle chooseBestTask();

public:
	TaskManager() = default;
	void start();
	uint8_t addTask(void (*task)(), uint8_t priority, uint32_t minTimeBetweenCalls, uint32_t maxTimeBetweenCalls,
	                bool runToCompletion);
	void removeTask(uint8_t id);
};

TaskManager taskManager;

Task_Handle TaskManager::chooseBestTask() {
	uint32_t currentTime = getTimerValue(0);
	Task_Handle bestTask = 0;
	if (taskManager.capacity > 0) {

		uint8_t bestPriority = 0;
		for (int i = 0; i < taskManager.capacity; i++) {
			struct Task t = taskManager.list[i];
			uint32_t timeToCall = t.lastCallTime + t.maxTimeBetweenCalls - t.averageDuration - 1 << 8;
			if (timeToCall < currentTime) {
				if (t.priority > bestPriority) {
					bestTask = t.handle;
					bestPriority = t.priority;
				}
			}
		}
	}
	return bestTask;
}

uint8_t TaskManager::addTask(void (*task)(), uint8_t priority, uint32_t minTimeBetweenCalls,
                             uint32_t maxTimeBetweenCalls, bool runToCompletion) {
	list[capacity] = Task{task, priority, 0, 0, minTimeBetweenCalls, maxTimeBetweenCalls};
	return capacity;
}

void TaskManager::removeTask(uint8_t id) {
	list[id] = Task{0, 0, 0, 0, 0, 0};
	return;
}

void TaskManager::start() {
	// set up os timer 0 as a free running timer
	static uint32_t currentTime = 0;
	disableTimer(0);
	setTimerValue(0, 0);
	// just let it count - a full loop is 2 minutes or so and we'll handle that case manually
	setOperatingMode(0, FREE_RUNNING, false);
	enableTimer(0);
	currentTime = getTimerValue(0);
	while (getTimerValue(0) < currentTime + 10) {
		;
	}
	currentTime = getTimerValue(0);
}

// C api starts here
void startTaskManager() {
	taskManager.start();
}

uint8_t registerTask(void (*task)(), uint8_t priority, uint32_t minTimeBetweenCalls, uint32_t maxTimeBetweenCalls,
                     bool runToCompletion) {
	return taskManager.addTask(task, priority, minTimeBetweenCalls, maxTimeBetweenCalls, runToCompletion);
}
void removeTask(uint8_t id) {
	return taskManager.removeTask(id);
}