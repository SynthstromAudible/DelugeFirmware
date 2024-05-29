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
// currently 14 are in use
constexpr int kMaxTasks = 25;
constexpr double rollTime = ((double)(UINT32_MAX) / DELUGE_CLOCKS_PERf);
struct Task {
	Task() = default;
	Task(TaskHandle _handle, uint8_t _priority, double _minTimeBetweenCalls, double _targetTimeBetweenCalls,
	     double _maxTimeBetweenCalls, const char* _name) {
		handle = _handle;
		priority = _priority;
		backOffTime = _minTimeBetweenCalls;
		targetTimeBetweenCalls = _targetTimeBetweenCalls;
		maxTimeBetweenCalls = _maxTimeBetweenCalls;
		name = _name;
		removeAfterUse = false;
	}
	Task(TaskHandle _handle, uint8_t _priority, double timeNow, double _timeToWait, const char* _name) {
		handle = _handle;
		priority = _priority;
		lastCallTime = timeNow;
		backOffTime = _timeToWait;
		targetTimeBetweenCalls = _timeToWait;
		maxTimeBetweenCalls = _timeToWait * 2;
		name = _name;
		removeAfterUse = true;
	}
	TaskHandle handle{nullptr};
	uint8_t priority{};
	double lastCallTime{0};
	double lastFinishTime{0};
	double averageDuration{0};
	double backOffTime{0};
	double targetTimeBetweenCalls{0};
	double maxTimeBetweenCalls{0};
	bool removeAfterUse{false};
	const char* name{};

	double highestLatency{0};

	double totalTime{0};
	double timesCalled{0};
};

struct SortedTask {
	uint8_t priority = UINT8_MAX;
	TaskID task = -1;
	// priorities are descending, so this puts low pri tasks first
	bool operator<(const SortedTask& another) const { return priority > another.priority; }
};

/// internal only to the task scheduler, hence all public. External interaction to use the api
struct TaskManager {
	// All current tasks
	// Not all entries are filled - removed entries have a null task handle
	std::array<Task, kMaxTasks> list{};
	// Sorted list of the current numActiveTasks, lowest priority first
	std::array<SortedTask, kMaxTasks> sortedList;
	uint8_t numActiveTasks = 0;
	double mustEndBefore = 128;
	void start(double duration = 0);
	void removeTask(TaskID id);
	void runTask(TaskID id);
	TaskID chooseBestTask(double deadline);
	TaskID addRepeatingTask(TaskHandle task, uint8_t priority, double backOffTime,
	                        double targetTimeBetweenCalls, double maxTimeBetweenCalls, const char* name);
	TaskID addOnceTask(TaskHandle task, uint8_t priority, double timeToWait, const char* name);
	void createSortedList();
	void clockRolledOver();
	bool running{false};
	TaskID insertTaskToList(Task task);
	double cpuTime{0};
};

TaskManager taskManager;

void TaskManager::createSortedList() {
	int j = 0;
	for (TaskID i = 0; i < kMaxTasks; i++) {
		if (list[i].handle != nullptr) {
			sortedList[j] = (SortedTask{list[i].priority, i});
			j++;
		}
	}
	if (numActiveTasks > 1) {
		// &array[size] is UB, or at least Clang + MSVC manages to choke
		// on it. So grab what is at worst size-1, and then increment
		// the pointer.
		std::sort(&sortedList[0], (&sortedList[numActiveTasks - 1]) + 1);
	}
}

TaskID TaskManager::chooseBestTask(double deadline) {
	double currentTime = getTimerValueSeconds(0);
	double nextFinishTime = currentTime;
	TaskID bestTask = -1;
	uint8_t bestPriority = INT8_MAX;
	/// Go through all tasks. If a task needs to be called before the current best task finishes, and has a higher
	/// priority than the current best task, it becomes the best task

	for (int i = 0; i < numActiveTasks; i++) {
		struct Task t = list[sortedList[i].task];
		double timeToCall = t.lastCallTime + t.targetTimeBetweenCalls - t.averageDuration;
		double maxTimeToCall = t.lastCallTime + t.maxTimeBetweenCalls - t.averageDuration;
		double timeSinceFinish = currentTime - t.lastFinishTime;
		// ensure every routine is within its target
		if (currentTime - t.lastCallTime > t.maxTimeBetweenCalls) {
			return sortedList[i].task;
		}
		if (timeToCall < currentTime || maxTimeToCall < nextFinishTime) {
			if (currentTime + t.averageDuration < deadline) {

				if (t.priority < bestPriority && t.handle) {
					if (timeSinceFinish > t.backOffTime) {
						bestTask = sortedList[i].task;
						nextFinishTime = currentTime + t.averageDuration;
					}
					else {
						bestTask = -1;
						nextFinishTime = maxTimeToCall;
					}
					bestPriority = t.priority;
				}
			}
		}
	}
	// if we didn't find a task because something high priority needs to wait to run, find the next task we can do
	// before it needs to start
	if (bestTask == -1) {
		// first look based on target time
		for (int i = (numActiveTasks - 1); i >= 0; i--) {
			struct Task t = list[sortedList[i].task];
			if (currentTime + t.averageDuration < nextFinishTime
			    && currentTime - t.lastFinishTime > t.targetTimeBetweenCalls
			    && currentTime - t.lastFinishTime > t.backOffTime) {
				return sortedList[i].task;
			}
		}
		// then look based on min time just to avoid busy waiting
		for (int i = (numActiveTasks - 1); i >= 0; i--) {
			struct Task t = list[sortedList[i].task];
			if (currentTime + t.averageDuration < nextFinishTime && currentTime - t.lastFinishTime > t.backOffTime) {
				return sortedList[i].task;
			}
		}
	}
	return bestTask;
}

/// insert task into the first empty spot in the list
TaskID TaskManager::insertTaskToList(Task task) {
	int8_t index = 0;
	while (list[index].handle && index < kMaxTasks) {
		index += 1;
	}
	if (index < kMaxTasks) {
		list[index] = task;
		numActiveTasks++;
	}

	return index;
};

TaskID TaskManager::addRepeatingTask(TaskHandle task, uint8_t priority, double backOffTime,
                                     double targetTimeBetweenCalls, double maxTimeBetweenCalls, const char* name) {
	if (numActiveTasks >= (kMaxTasks)) {
		return -1;
	}

	TaskID index =
	    insertTaskToList(Task{task, priority, backOffTime, targetTimeBetweenCalls, maxTimeBetweenCalls, name});

	createSortedList();
	return index;
}

TaskID TaskManager::addOnceTask(TaskHandle task, uint8_t priority, double timeToWait, const char* name) {
	if (numActiveTasks >= (kMaxTasks)) {
		return -1;
	}
	double timeToStart = running ? getTimerValueSeconds(0) : 0;
	TaskID index = insertTaskToList(Task{task, priority, timeToStart, timeToWait, name});

	createSortedList();
	return index;
}

void TaskManager::removeTask(TaskID id) {
	list[id] = Task{};
	numActiveTasks--;
	createSortedList();
	return;
}

void TaskManager::runTask(TaskID id) {
	auto task = &list[id];
	auto timeNow = getTimerValueSeconds(0);
	auto latency = task->lastCallTime - timeNow;
	if (latency > task->highestLatency) {
		task->highestLatency = latency;
	}
	task->lastCallTime = timeNow;
	task->handle();
	timeNow = getTimerValueSeconds(0);
	if (task->removeAfterUse) {
		removeTask(id);
	}
	else {
		task->lastFinishTime = timeNow;
		double runtime = (task->lastFinishTime - task->lastCallTime);
		if (runtime < 0) {
			runtime += rollTime;
		}

		task->averageDuration = (task->averageDuration + runtime) / 2;
		task->totalTime += runtime;
		task->timesCalled += 1;
		cpuTime += runtime;
	}
}
void TaskManager::clockRolledOver() {
	for (int i = 0; i < kMaxTasks; i++) {
		// just check it exists, otherwise tasks that never run will overflow
		if (list[i].handle != nullptr) {
			list[i].lastCallTime -= rollTime;
		}
	}
}

/// default duration of 0 signifies infinite loop, intended to be specified only for testing
void TaskManager::start(double duration) {
	// set up os timer 0 as a free running timer

	disableTimer(0);
	setTimerValue(0, 0);
	// just let it count - a full loop is 2 minutes or so and we'll handle that case manually
	setOperatingMode(0, FREE_RUNNING, false);
	enableTimer(0);
	double startTime = getTimerValueSeconds(0);
	double lastLoop = startTime;
	if (duration != 0) {
		mustEndBefore = startTime + duration;
	}

	running = true;
	while (duration == 0 || getTimerValueSeconds(0) < startTime + duration) {
		double newTime = getTimerValueSeconds(0);
		if (newTime < lastLoop) {
			clockRolledOver();
		}
		lastLoop = newTime;
		TaskID task = chooseBestTask(mustEndBefore);
		if (task >= 0) {
			runTask(task);
		}
	}
}

// C api starts here
void startTaskManager() {
	taskManager.start();
}

uint8_t addRepeatingTask(TaskHandle task, uint8_t priority, double backOffTime, double targetTimeBetweenCalls,
                         double maxTimeBetweenCalls, const char* name) {
	return taskManager.addRepeatingTask(task, priority, backOffTime, targetTimeBetweenCalls,
	                                    maxTimeBetweenCalls, name);
}
uint8_t addOnceTask(TaskHandle task, uint8_t priority, double timeToWait, const char* name) {
	return taskManager.addOnceTask(task, priority, timeToWait, name);
}

void removeTask(TaskID id) {
	return taskManager.removeTask(id);
}
