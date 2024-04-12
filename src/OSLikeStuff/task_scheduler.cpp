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
	double lastCallTime;
	double averageDuration;
	double minTimeBetweenCalls;
	double maxTimeBetweenCalls;
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
	taskID addTask(Task_Handle task, uint8_t priority, double minTimeBetweenCalls, double targetTimeBetweenCalls,
	               double maxTimeBetweenCalls, bool runToCompletion);
	void createSortedList();
	void clockRolledOver();
};

TaskManager taskManager;

void TaskManager::createSortedList() {
	for (taskID i = 0; i < list.size(); i++) {
		sortedList[i] = (SortedTask{list[i].priority, i});
	}
	std::sort(sortedList.begin(), sortedList.end());
}

taskID TaskManager::chooseBestTask() {
	double currentTime = getTimerValueSeconds(0);
	double nextFinishTime = currentTime;
	taskID bestTask = -1;
	uint8_t bestPriority = INT8_MAX;
	/// Go through all tasks. If a task needs to be called before the current best task finishes, and has a higher
	/// priority than the current best task, it becomes the best task

	for (int i = 0; i < index; i++) {
		struct Task t = list[sortedList[i].task];
		double timeToCall = t.lastCallTime + t.maxTimeBetweenCalls - t.averageDuration;
		double timeSinceCall = currentTime - t.lastCallTime;
		// ensure every routine is within 10% of its target
		if (timeSinceCall > 1.1 * t.maxTimeBetweenCalls) {
			return sortedList[i].task;
		}
		if (timeToCall < nextFinishTime) {
			if (t.priority < bestPriority && t.handle) {
				if (timeSinceCall > t.minTimeBetweenCalls) {
					bestTask = sortedList[i].task;
					bestPriority = t.priority;
					nextFinishTime = currentTime + t.averageDuration;
				}
			}
		}
	}

	return bestTask;
}

taskID TaskManager::addTask(Task_Handle task, uint8_t priority, double minTimeBetweenCalls,
                            double targetTimeBetweenCalls, double maxTimeBetweenCalls, bool runToCompletion) {
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
	if (list[id].averageDuration < 0.000001 | list[id].averageDuration > 0.01) {
		list[id].averageDuration = (getTimerValueSeconds(0) - list[id].lastCallTime);
	}
	else {
		list[id].averageDuration = (list[id].averageDuration + (getTimerValueSeconds(0) - list[id].lastCallTime)) / 2;
	}
}
constexpr double rollTime = ((double)(UINT32_MAX) / DELUGE_CLOCKS_PERf);
void TaskManager::clockRolledOver() {
	for (int i = 0; i < index; i++) {
		list[i].lastCallTime -= rollTime;
	}
}

/// default duration of 0 signifies infinite loop, intended to be specified only for testing
void TaskManager::start(int32_t duration) {
	// set up os timer 0 as a free running timer

	disableTimer(0);
	setTimerValue(0, 0);
	// just let it count - a full loop is 2 minutes or so and we'll handle that case manually
	setOperatingMode(0, FREE_RUNNING, false);
	enableTimer(0);
	double startTime = getTimerValueSeconds(0);
	double lastLoop = startTime;
	while (duration == 0 || getTimerValue(0) < startTime + duration) {
		double newTime = getTimerValueSeconds(0);
		if (newTime < lastLoop) {
			clockRolledOver();
		}
		else {}
		lastLoop = newTime;
		taskID task = chooseBestTask();
		if (task >= 0) {
			runTask(task);
		}
	}
}

// C api starts here
void startTaskManager() {
	taskManager.start();
}

uint8_t registerTask(Task_Handle task, uint8_t priority, double minTimeBetweenCalls, double targetTimeBetweenCalls,
                     double maxTimeBetweenCalls, bool runToCompletion) {
	return taskManager.addTask(task, priority, minTimeBetweenCalls, targetTimeBetweenCalls, maxTimeBetweenCalls,
	                           runToCompletion);
}
void removeTask(uint8_t id) {
	return taskManager.removeTask(id);
}
