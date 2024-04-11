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
#include "util/container/static_vector.hpp"

struct Task {
	Task_Handle handle;
	uint8_t priority;
	uint32_t lastCallTime;
	uint32_t averageDuration;
	uint32_t minTimeBetweenCalls;
	uint32_t maxTimeBetweenCalls;
};

class TaskManager {

	std::array<Task, 20> list;
	taskID index = 0;
	taskID chooseBestTask();

public:
	TaskManager() = default;
	void start(int32_t duration = 0);
	taskID addTask(Task_Handle task, uint8_t priority, uint32_t minTimeBetweenCalls, uint32_t maxTimeBetweenCalls,
	               bool runToCompletion);
	void removeTask(taskID id);
	void runTask(taskID id);
};

TaskManager taskManager;

taskID TaskManager::chooseBestTask() {
	int32_t currentTime = getTimerValue(0);
	taskID bestTask = -1;

	uint8_t bestPriority = INT8_MAX;
	for (int i = 0; i < list.size(); i++) {
		struct Task t = list[i];
		int32_t timeToCall = t.lastCallTime + t.maxTimeBetweenCalls - t.averageDuration - (1 << 8);
		if (timeToCall < currentTime) {
			if (t.priority < bestPriority && t.handle) {
				bestTask = i;
				bestPriority = t.priority;
			}
		}
	}

	return bestTask;
}

taskID TaskManager::addTask(Task_Handle task, uint8_t priority, uint32_t minTimeBetweenCalls,
                            uint32_t maxTimeBetweenCalls, bool runToCompletion) {
	list[index] = (Task{task, priority, 0, 0, minTimeBetweenCalls, maxTimeBetweenCalls});
	return index++;
}

void TaskManager::removeTask(taskID id) {
	list[id] = Task{0, 0, 0, 0, 0, 0};
	return;
}

void TaskManager::runTask(taskID id) {
	list[id].lastCallTime = getTimerValue(0);
	list[id].handle();
	list[id].averageDuration = (list[id].averageDuration + (getTimerValue(0) - list[id].lastCallTime)) / 2;
}

void TaskManager::start(int32_t duration) {
	// set up os timer 0 as a free running timer
	static uint32_t currentTime = 0;
	disableTimer(0);
	setTimerValue(0, 0);
	// just let it count - a full loop is 2 minutes or so and we'll handle that case manually
	setOperatingMode(0, FREE_RUNNING, false);
	enableTimer(0);
	currentTime = getTimerValue(0);
	while (duration == 0 || getTimerValue(0) < currentTime + duration) {
		taskID task = chooseBestTask();
		if (task >= 0) {
			runTask(task);
		}
	}
	currentTime = getTimerValue(0);
}

// C api starts here
void startTaskManager() {
	taskManager.start();
}

uint8_t registerTask(Task_Handle task, uint8_t priority, uint32_t minTimeBetweenCalls, uint32_t maxTimeBetweenCalls,
                     bool runToCompletion) {
	return taskManager.addTask(task, priority, minTimeBetweenCalls, maxTimeBetweenCalls, runToCompletion);
}
void removeTask(uint8_t id) {
	return taskManager.removeTask(id);
}