/*
 * Copyright © 2024 Mark Adams
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

#include "OSLikeStuff/task_scheduler/task_scheduler.h"

#include "io/debug/log.h"
#include "util/container/static_vector.hpp"
#include <algorithm>
#include <iostream>

#if !IN_UNIT_TESTS
#include "memory/general_memory_allocator.h"
#endif

extern "C" {
#include "RZA1/ostm/ostm.h"
}

TaskManager taskManager;

void TaskManager::createSortedList() {
	int j = 0;
	for (TaskID i = 0; i < kMaxTasks; i++) {
		if (list[i].handle != nullptr) {
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
bool dumped = false;
// deadline < 0 means no deadline
TaskID TaskManager::chooseBestTask(Time deadline) {
	Time currentTime = getSecondsFromStart();
	Time latestFinishTime = deadline > Time(0) ? deadline : std::numeric_limits<Time>::max();
	TaskID bestTask = -1;
	uint8_t bestPriority = INT8_MAX;
	/// Go through all tasks. If a task needs to be called before the current best task finishes, and has a higher
	/// priority than the current best task, it becomes the best task

	for (int i = numActiveTasks - 1; i >= 0; i--) {
		struct Task* t = &list[sortedList[i].task];
		struct TaskSchedule* s = &t->schedule;

		if (!t->isRunnable()) {
			continue;
		}
		// ensure every routine is within its target
		if (currentTime > t->lastCallTime + s->maxInterval && t->isReady(currentTime)) {
			return sortedList[i].task;
		}
		if (t->idealCallTime < currentTime && currentTime + t->durationStats.average < latestFinishTime) {

			if (t->isReady(currentTime)) {
				bestTask = sortedList[i].task;
			}
			latestFinishTime = t->lastCallTime + s->maxInterval;
		}
		else {
			latestFinishTime = std::min(latestFinishTime, t->lastCallTime + s->maxInterval);
		}
	}
	// if we didn't find a task because something high priority needs to wait to run, find the next task we can do
	// before it needs to start
	if (bestTask == -1) {
		// first look based on target time
		for (int i = (numActiveTasks - 1); i >= 0; i--) {
			struct Task* t = &list[sortedList[i].task];
			struct TaskSchedule* s = &t->schedule;
			if (currentTime + t->durationStats.average < latestFinishTime
			    && currentTime - t->lastFinishTime > s->targetInterval && t->isReady(currentTime)) {
				return sortedList[i].task;
			}
		}
		// then look based on min time just to avoid busy waiting
		for (int i = (numActiveTasks - 1); i >= 0; i--) {
			struct Task* t = &list[sortedList[i].task];
			if (currentTime + t->durationStats.average < latestFinishTime && t->isReady(currentTime)) {
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

TaskID TaskManager::addOnceTask(TaskHandle task, uint8_t priority, Time timeToWait, const char* name) {
	if (numRegisteredTasks >= (kMaxTasks)) {
		return -1;
	}
	Time timeToStart = running ? getSecondsFromStart() : Time(0);
	TaskID index = insertTaskToList(Task{task, priority, timeToStart, timeToWait, name});

	createSortedList();
	return index;
}

TaskID TaskManager::addConditionalTask(TaskHandle task, uint8_t priority, RunCondition condition, const char* name) {
	if (numRegisteredTasks >= (kMaxTasks)) {
		return -1;
	}
	TaskID index = insertTaskToList(Task{task, priority, condition, name});
	createSortedList();
	return index;
}

void TaskManager::removeTask(TaskID id) {
	list[id] = Task{};
	numRegisteredTasks--;
	createSortedList();
	return;
}
void TaskManager::ignoreForStats() {
	countThisTask = false;
}

Time TaskManager::getAverageRunTimeForCurrentTask() {
	auto currentTask = &list[currentID];
	return currentTask->durationStats.average;
}

void TaskManager::setNextRunTimeforCurrentTask(Time seconds) {
	auto currentTask = &list[currentID];
	currentTask->schedule.maxInterval = seconds;
}

void TaskManager::runTask(TaskID id) {
	countThisTask = true;
	auto timeNow = getSecondsFromStart();
	Time startTime = timeNow;
	// this includes ISR time as well as the scheduler's own time, such as calculating and printing stats
	overhead += timeNow - lastFinishTime;
	currentID = id;
	auto currentTask = &list[currentID];

	currentTask->handle();
	timeNow = getSecondsFromStart();
	Time runtime = (timeNow - startTime);
	cpuTime += runtime;
	if (currentTask->removeAfterUse) {
		removeTask(id);
	}
	else {
		if (countThisTask) {
#if SCHEDULER_DETAILED_STATS
			currentTask->latency.update(startTime - currentTask->lastCallTime);
#endif
			currentTask->updateNextTimes(startTime, runtime);
		}
	}
	currentTask->lastFinishTime = timeNow;
	lastFinishTime = timeNow;
}

/// pause current task, continue to run scheduler loop until a condition is met, then return to it
/// current task can be called again if it's repeating - this is to match behaviour of busy waiting with routineForSD
/// returns whether the condition was met
bool TaskManager::yield(RunCondition until, Time timeout) {
	if (!running) [[unlikely]] {
		startClock();
	}
#if !IN_UNIT_TESTS
	GeneralMemoryAllocator::get().checkStack("ensure resizeable space");
#endif
	auto yieldingTask = &list[currentID];
	auto yieldingID = currentID;
	bool taskRemoved = false;
	auto timeNow = getSecondsFromStart();
	Time runtime = (timeNow - yieldingTask->lastCallTime);
	bool skipTimeout = timeout < Time(1 / 10000.);

	// for now we first end this as if the task finished - might be advantageous to replace with a context switch later
	if (yieldingTask->removeAfterUse) {
		removeTask(currentID);
		taskRemoved = true;
	}
	else {
		yieldingTask->lastFinishTime = timeNow; // update this so it's in its back off window
		if (countThisTask) {
			yieldingTask->durationStats.update(runtime);
			yieldingTask->totalTime += runtime;
			yieldingTask->lastRunTime = runtime;
			yieldingTask->timesCalled += 1;
		}
	}
	// continue the main loop. The yielding task is still on the stack but that should be fine
	// run at least once so this can be used for yielding a single call as well
	Time newTime = getSecondsFromStart();
	do {
		TaskID task = chooseBestTask(mustEndBefore);
		if (task >= 0) {
			runTask(task);
		}
		else {
			bool addedTask = checkConditionalTasks();
			// if we sorted our list then we should get back to running things and not print stats
			if (!addedTask && newTime > lastPrintedStats + Time(10.0)) {
				lastPrintedStats = newTime;
				// couldn't find anything so here we go
				printStats();
			}
		}
	} while (!until() && (skipTimeout || getSecondsFromStart() < timeNow + timeout));
	if (!taskRemoved) {

		auto finishTime = getSecondsFromStart();
		yieldingTask->lastCallTime = finishTime; // hack so it won't get called again immediately
	}
	return (getSecondsFromStart() < timeNow + timeout);
}

/// default duration of 0 signifies infinite loop, intended to be specified only for testing
void TaskManager::start(Time duration) {
	// set up os timer 0 as a free running timer

	startClock();
	Time startTime = getSecondsFromStart();
	if (duration != Time(0)) {
		mustEndBefore = startTime + duration;
	}
	else {
		mustEndBefore = -1;
	}
	while (duration == Time(0) || getSecondsFromStart() < startTime + duration) {
		Time newTime = getSecondsFromStart();
		TaskID task = chooseBestTask(mustEndBefore);
		if (task >= 0) {
			runTask(task);
		}
		else {
			bool addedTask = checkConditionalTasks();
			// if we sorted our list then we should get back to running things and not print stats
			if (!addedTask && newTime > lastPrintedStats + Time(10.0)) {
				lastPrintedStats = newTime;
				// couldn't find anything so here we go
				printStats();
			}
		}

	}
}
void TaskManager::startClock() {
	disableTimer(0);
	setTimerValue(0, 0);
	// just let it count - a full loop is 2 minutes or so and we'll handle that case manually
	setOperatingMode(0, FREE_RUNNING, false);
	enableTimer(0);
	running = true;
	lastTime = 0;
}
bool TaskManager::checkConditionalTasks() {
	bool addedTask = false;
	for (int i = 0; i < kMaxTasks; i++) {
		struct Task* t = &list[i];
		if (t->checkCondition()) {
			addedTask = true;
		}
	}
	if (addedTask) {
		createSortedList();
		return true;
	}
	return false;
}

void TaskManager::resetStats() {
	for (auto& task : list) {
		if (task.handle) {
			task.totalTime = 0;
			task.timesCalled = 0;
			task.durationStats.reset();
#if SCHEDULER_DETAILED_STATS
			task.latency.reset();
#endif
		}
	}
	cpuTime = 0;
	overhead = 0;
}

void TaskManager::printStats() {
	D_PRINTLN("Dumping task manager stats: (min/ average/ max)");
	for (auto task : list) {
		if (task.handle) {
			constexpr const Time latencyScale = 1000.0;
			constexpr const Time durationScale = 1000000.0;
#if SCHEDULER_DETAILED_STATS
			D_PRINTLN("Load: %5.2f, "                             //<
			          "Dur: %8.3f/%8.3f/%9.3f us "                //<
			          "Latency: %8.3f/%8.3f/%8.3f ms "            //<
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
#ifdef NEVER
			D_PRINTLN("Load: %5.2f "                  //<
			          "Average Duration: %9.3f "      //<
			          "Times Called: %10d, Task: %s", //<
			          100.0 * task.totalTime / cpuTime, durationScale * task.durationStats.average, task.timesCalled,
			          task.name);
#endif
#endif
		}
	}
	auto totalTime = cpuTime + overhead;
	D_PRINTLN("Working time: %5.2f, Overhead: %5.2f. Total running time: %5.2f seconds",
	          double(cpuTime * 100) / double(totalTime), double(overhead * 100) / double(totalTime), runningTime);
	resetStats();
}
Time getTimerValueSeconds(int timerNo) {
	Time seconds = getTimerValue(timerNo) * ONE_OVER_CLOCK;
	return seconds;
}
/// return a monotonic timer value in seconds from when the task manager started
Time TaskManager::getSecondsFromStart() {
	auto timeNow = getTimerValueSeconds(0);
	if (timeNow < lastTime) {
		runningTime += rollTime;
	}
	runningTime += timeNow - lastTime;
	lastTime = timeNow;
	return runningTime;
}
