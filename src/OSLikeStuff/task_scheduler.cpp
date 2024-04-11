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
#include <algorithm>
#include <iostream>
struct Task {
	Task_Handle handle;
	uint8_t priority;
	float lastCallTime;
	float averageDuration;
	float minTimeBetweenCalls;
	float maxTimeBetweenCalls;
};

struct SortedTask {
	uint8_t priority = UINT8_MAX;
	taskID task = -1;
	// priorities are descending, so this puts low pri tasks first
	bool operator<(const SortedTask& another) const { return priority > another.priority; }
};

/// internal only to the task scheduler, hence all public. External interaction to use the api
struct TaskManager {

	std::array<Task, 20> list;
	std::array<SortedTask, 20> sortedList;
	taskID index = 0;
	void start(int32_t duration = 0);
	void removeTask(taskID id);
	void runTask(taskID id);
	taskID chooseBestTask();
	taskID addTask(Task_Handle task, uint8_t priority, float minTimeBetweenCalls, float maxTimeBetweenCalls,
	               bool runToCompletion);
	void createSortedList();
};

TaskManager taskManager;

void TaskManager::createSortedList() {
	for (taskID i = 0; i < list.size(); i++) {
		sortedList[i] = (SortedTask{list[i].priority, i});
	}
	std::sort(sortedList.begin(), sortedList.end());
}

taskID TaskManager::chooseBestTask() {
	float currentTime = getTimerValueSeconds(0);
	float nextFinishTime = currentTime;
	taskID bestTask = -1;
	uint8_t bestPriority = INT8_MAX;
	/// Go through all tasks. If a task needs to be called before the current best task finishes, and has a higher
	/// priority than the current best task, it becomes the best task

	for (int i = 0; i < sortedList.size(); i++) {
		struct Task t = list[sortedList[i].task];
		float timeToCall = t.lastCallTime + t.maxTimeBetweenCalls - t.averageDuration;
		float timeSinceCall = currentTime - t.lastCallTime;
		if (timeToCall < nextFinishTime) {
			if (t.priority < bestPriority && t.handle) {
				if (timeSinceCall > t.minTimeBetweenCalls) {
					bestTask = sortedList[i].task;
				}
				else {
					bestTask = -1;
				}

				bestPriority = t.priority;
				nextFinishTime = currentTime + t.averageDuration;
			}
		}
	}

	return bestTask;
}

taskID TaskManager::addTask(Task_Handle task, uint8_t priority, float minTimeBetweenCalls, float maxTimeBetweenCalls,
                            bool runToCompletion) {
	list[index] = (Task{task, priority, 0, 0, minTimeBetweenCalls, maxTimeBetweenCalls});
	createSortedList();
	return index++;
}

void TaskManager::removeTask(taskID id) {
	list[id] = Task{0, 0, 0, 0, 0, 0};
	return;
}

void TaskManager::runTask(taskID id) {
	list[id].lastCallTime = getTimerValueSeconds(0);
	list[id].handle();
	if (list[id].averageDuration < 0.00001) {
		list[id].averageDuration = (getTimerValueSeconds(0) - list[id].lastCallTime);
	}
	else {
		list[id].averageDuration = (list[id].averageDuration + (getTimerValueSeconds(0) - list[id].lastCallTime)) / 2;
	}
}

/// default duration of 0 signifies infinite loop, intended to be specified only for testing
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