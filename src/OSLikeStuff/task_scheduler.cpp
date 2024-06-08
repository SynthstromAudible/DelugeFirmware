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
#include "io/debug/log.h"
#include "util/container/static_vector.hpp"
#include <algorithm>
#include <iostream>

#define SCHEDULER_DETAILED_STATS (1 && ENABLE_TEXT_OUTPUT)

struct StatBlock {
#if SCHEDULER_DETAILED_STATS
	double min{std::numeric_limits<double>::infinity()};
	double max{-std::numeric_limits<double>::infinity()};
#endif
	/// Running average, computed as (last + avg) / 2
	double average{0};

	[[gnu::hot]] void update(double v) {
#if SCHEDULER_DETAILED_STATS
		min = std::min(v, min);
		max = std::max(v, max);
#endif
		average = (average + v) / 2;
	}
};

// currently 14 are in use
constexpr int kMaxTasks = 25;
constexpr double rollTime = ((double)(UINT32_MAX) / DELUGE_CLOCKS_PERf);
struct Task {
	Task() = default;

	// constructor for making a "once" task
	Task(TaskHandle _handle, uint8_t _priority, double timeNow, double _timeToWait, const char* _name) {
		handle = _handle;
		lastCallTime = timeNow;
		schedule = TaskSchedule{_priority, _timeToWait, _timeToWait, _timeToWait * 2};
		name = _name;
		removeAfterUse = true;
	}
	// makes a repeating task
	Task(TaskHandle task, TaskSchedule _schedule, const char* _name) {
		handle = task;
		schedule = _schedule;
		name = _name;
		removeAfterUse = false;
	}
	// makes a conditional task
	Task(TaskHandle task, uint8_t priority, RunCondition _condition, const char* _name) {
		handle = task;
		// good to go as soon as it's marked as runnable
		schedule = {priority, 0, 0, 0};
		name = _name;
		removeAfterUse = true;
		runnable = false;
		condition = _condition;
	}
	TaskHandle handle{nullptr};
	TaskSchedule schedule{0, 0, 0, 0};
	double lastCallTime{0};
	double lastFinishTime{0};

	StatBlock durationStats;
#if SCHEDULER_DETAILED_STATS
	StatBlock latency;
#endif
	bool runnable{true};
	RunCondition condition{nullptr};
	bool removeAfterUse{false};
	const char* name{nullptr};

	double totalTime{0};
	int32_t timesCalled{0};
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
	// Sorted list of the current numActiveTasks, lowest priority (highest number) first
	std::array<SortedTask, kMaxTasks> sortedList;
	uint8_t numActiveTasks = 0;
	uint8_t numRegisteredTasks = 0;
	double mustEndBefore = 128; // use for testing or I guess if you want a second temporary task manager?
	bool running{false};
	double cpuTime{0};
	double lastPrintedStats{0};
	void start(double duration = 0);
	void removeTask(TaskID id);
	void runTask(TaskID id);
	TaskID chooseBestTask(double deadline);
	TaskID addRepeatingTask(TaskHandle task, TaskSchedule schedule, const char* name);

	TaskID addOnceTask(TaskHandle task, uint8_t priority, double timeToWait, const char* name);
	TaskID addConditionalTask(TaskHandle task, uint8_t priority, RunCondition condition, const char* name);

	void createSortedList();
	void clockRolledOver();
	TaskID insertTaskToList(Task task);
	void printStats();
	bool checkConditionalTasks();
	void yield(RunCondition until);

private:
	TaskID currentID;
};

TaskManager taskManager;

void TaskManager::createSortedList() {
	int j = 0;
	for (TaskID i = 0; i < kMaxTasks; i++) {
		if (list[i].handle != nullptr && list[i].runnable) {
			sortedList[j] = (SortedTask{list[i].schedule.priority, i});
			j++;
		}
	}
	numActiveTasks = j;
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
		struct Task* t = &list[sortedList[i].task];
		struct TaskSchedule* s = &t->schedule;
		double timeToCall = t->lastCallTime + s->targetInterval - t->durationStats.average;
		double maxTimeToCall = t->lastCallTime + s->maxInterval - t->durationStats.average;
		double timeSinceFinish = currentTime - t->lastFinishTime;
		// ensure every routine is within its target
		if (currentTime - t->lastCallTime > s->maxInterval) {
			return sortedList[i].task;
		}
		if (timeToCall < currentTime || maxTimeToCall < nextFinishTime) {
			if (currentTime + t->durationStats.average < deadline) {

				if (s->priority < bestPriority && t->handle) {
					if (timeSinceFinish > s->backOffPeriod) {
						bestTask = sortedList[i].task;
						nextFinishTime = currentTime + t->durationStats.average;
					}
					else {
						bestTask = -1;
						nextFinishTime = maxTimeToCall;
					}
					bestPriority = s->priority;
				}
			}
		}
	}
	// if we didn't find a task because something high priority needs to wait to run, find the next task we can do
	// before it needs to start
	if (bestTask == -1) {
		// first look based on target time
		for (int i = (numActiveTasks - 1); i >= 0; i--) {
			struct Task* t = &list[sortedList[i].task];
			struct TaskSchedule* s = &t->schedule;
			if (currentTime + t->durationStats.average < nextFinishTime
			    && currentTime - t->lastFinishTime > s->targetInterval
			    && currentTime - t->lastFinishTime > s->backOffPeriod) {
				return sortedList[i].task;
			}
		}
		// then look based on min time just to avoid busy waiting
		for (int i = (numActiveTasks - 1); i >= 0; i--) {
			struct Task* t = &list[sortedList[i].task];
			struct TaskSchedule* s = &t->schedule;
			if (currentTime + t->durationStats.average < nextFinishTime
			    && currentTime - t->lastFinishTime > s->backOffPeriod) {
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
		numRegisteredTasks++;
	}

	return index;
};

TaskID TaskManager::addRepeatingTask(TaskHandle task, TaskSchedule schedule, const char* name) {
	if (numRegisteredTasks >= (kMaxTasks)) {
		return -1;
	}

	TaskID index = insertTaskToList(Task{task, schedule, name});

	createSortedList();
	return index;
}

TaskID TaskManager::addOnceTask(TaskHandle task, uint8_t priority, double timeToWait, const char* name) {
	if (numRegisteredTasks >= (kMaxTasks)) {
		return -1;
	}
	double timeToStart = running ? getTimerValueSeconds(0) : 0;
	TaskID index = insertTaskToList(Task{task, priority, timeToStart, timeToWait, name});

	createSortedList();
	return index;
}

TaskID TaskManager::addConditionalTask(TaskHandle task, uint8_t priority, RunCondition condition, const char* name) {
	if (numRegisteredTasks >= (kMaxTasks)) {
		return -1;
	}
	TaskID index = insertTaskToList(Task{task, priority, condition, name});
	//  don't sort since it's not runnable yet anyway, so no change to the active list
	return index;
}

void TaskManager::removeTask(TaskID id) {
	list[id] = Task{};
	numRegisteredTasks--;
	createSortedList();
	return;
}

void TaskManager::runTask(TaskID id) {
	currentID = id;
	auto currentTask = &list[currentID];
	auto timeNow = getTimerValueSeconds(0);
#if SCHEDULER_DETAILED_STATS
	currentTask->latency.update(timeNow - currentTask->lastCallTime);
#endif
	currentTask->lastCallTime = timeNow;
	currentTask->handle();
	timeNow = getTimerValueSeconds(0);
	if (currentTask->removeAfterUse) {
		removeTask(id);
	}
	else {
		currentTask->lastFinishTime = timeNow;
		double runtime = (currentTask->lastFinishTime - currentTask->lastCallTime);
		if (runtime < 0) {
			runtime += rollTime;
		}

		currentTask->durationStats.update(runtime);
		currentTask->totalTime += runtime;
		currentTask->timesCalled += 1;
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
	lastPrintedStats -= rollTime;
}

/// pause current task, continue to run scheduler loop until a condition is met, then return to it
/// current task can be called again if it's repeating - this is to match behaviour of busy waiting with routineForSD
void TaskManager::yield(RunCondition until) {
	auto currentTask = &list[currentID];
	// for now we first end this as if the task finished - might be advantageous to replace with a context switch later
	if (currentTask->removeAfterUse) {
		removeTask(currentID);
	}
	else {
		auto timeNow = getTimerValueSeconds(0);
		currentTask->lastFinishTime = timeNow;
		double runtime = (currentTask->lastFinishTime - currentTask->lastCallTime);
		if (runtime < 0) {
			runtime += rollTime;
		}

		currentTask->durationStats.update(runtime);
		currentTask->totalTime += runtime;
		currentTask->timesCalled += 1;
		cpuTime += runtime;
	}
	// continue the main loop. The yielding task is still on the stack but that should be fine
	while (!until()) {
		TaskID task = chooseBestTask(mustEndBefore);
		if (task >= 0) {
			runTask(task);
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
		else {
			bool addedTask = checkConditionalTasks();
			// if we sorted our list then we should get back to running things and not print stats
			if (!addedTask && newTime > lastPrintedStats + 10) {
				lastPrintedStats = newTime;
				// couldn't find anything so here we go
				printStats();
			}
		}
	}
}
bool TaskManager::checkConditionalTasks() {
	bool addedTask = false;
	for (int i = 0; i < kMaxTasks; i++) {
		struct Task* t = &list[i];
		if (t->condition != nullptr && !(t->runnable)) {
			t->runnable = t->condition();
			if (t->runnable) {
				addedTask = true;
			}
		}
	}
	if (addedTask) {
		createSortedList();
		return true;
	}
	return false;
}
void TaskManager::printStats() {
	D_PRINTLN("Dumping task manager stats:");
	for (auto task : list) {
		if (task.handle) {
			constexpr const double latencyScale = 1000.0;
			constexpr const double durationScale = 1000000.0;
#if SCHEDULER_DETAILED_STATS
			D_PRINTLN("Load: %5.2f, "                             //<
			          "Dur: %8.3f/%8.3f/%9.3f "                   //<
			          "Latency: %8.3f/%8.3f/%8.3f "               //<
			          "N: %10d, Task: %s",                        //<
			          100.0 * task.totalTime / cpuTime,           //<
			          durationScale * task.durationStats.min,     //<
			          durationScale * task.durationStats.average, //<
			          durationScale * task.durationStats.max,     //<
			          latencyScale * task.latency.min,            //<
			          latencyScale * task.latency.average,        //<
			          latencyScale * task.latency.max,            //<
			          task.timesCalled, task.name);
#else
			D_PRINTLN("Load: %5.2f "                  //<
			          "Average Duration: %9.3f "      //<
			          "Times Called: %10d, Task: %s", //<
			          100.0 * task.totalTime / cpuTime, durationScale * task.durationStats.average, task.timesCalled,
			          task.name);
#endif
		}
	}
}

// C api starts here
void startTaskManager() {
	taskManager.start();
}

uint8_t addRepeatingTask(TaskHandle task, uint8_t priority, double backOffTime, double targetTimeBetweenCalls,
                         double maxTimeBetweenCalls, const char* name) {
	return taskManager.addRepeatingTask(
	    task, TaskSchedule{priority, backOffTime, targetTimeBetweenCalls, maxTimeBetweenCalls}, name);
}
uint8_t addOnceTask(TaskHandle task, uint8_t priority, double timeToWait, const char* name) {
	return taskManager.addOnceTask(task, priority, timeToWait, name);
}

uint8_t addConditionalTask(TaskHandle task, uint8_t priority, RunCondition condition, const char* name) {
	return taskManager.addConditionalTask(task, priority, condition, name);
}

void yield(RunCondition until) {
	taskManager.yield(until);
}

void removeTask(TaskID id) {
	return taskManager.removeTask(id);
}
