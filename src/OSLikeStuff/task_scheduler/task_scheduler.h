//
// Created by Mark Adams on 2024-12-05.
//

#ifndef DELUGE_TASK_SCHEDULER_H
#define DELUGE_TASK_SCHEDULER_H
#include "OSLikeStuff/scheduler_api.h"
#include "OSLikeStuff/task_scheduler/task.h"
#include <array>

struct SortedTask {
	uint8_t priority = UINT8_MAX;
	TaskID task = -1;
	// priorities are descending, so this puts low pri tasks first
	bool operator<(const SortedTask& another) const { return priority > another.priority; }
};
// currently 14 are in use
constexpr int kMaxTasks = 25;
/// internal only to the task scheduler, hence all public. External interaction to use the api
struct TaskManager {

	void start(Time duration = 0);
	void removeTask(TaskID id);
	void boostTask(TaskID id);
	void runTask(TaskID id);
	void runHighestPriTask();
	TaskID chooseBestTask(Time deadline);
	TaskID addRepeatingTask(TaskHandle task, TaskSchedule schedule, const char* name, ResourceChecker resources);

	TaskID addOnceTask(TaskHandle task, uint8_t priority, Time timeToWait, const char* name, ResourceChecker resources);
	TaskID addConditionalTask(TaskHandle task, uint8_t priority, RunCondition condition, const char* name,
	                          ResourceChecker resources);

	void createSortedList();
	TaskID insertTaskToList(Task task);
	void printStats();
	bool checkConditionalTasks();
	bool yield(RunCondition until, Time timeout = 0, bool returnOnIdle = false);
	Time getSecondsFromStart();

	void ignoreForStats();
	void setNextRunTimeforCurrentTask(Time seconds);

	[[nodiscard]] Time getAverageRunTimeForCurrentTask() const;
	[[nodiscard]] Time getAverageRunTimeForTask(TaskID id) const;

private:
	// All current tasks
	// Not all entries are filled - removed entries have a null task handle
	std::array<Task, kMaxTasks> list{};
	// Sorted list of the current numActiveTasks, lowest priority (highest number) first
	std::array<SortedTask, kMaxTasks> sortedList;
	uint8_t numActiveTasks = 0;
	uint8_t numRegisteredTasks = 0;
	Time mustEndBefore = -1; // use for testing or I guess if you want a second temporary task manager?
	bool running{false};
	Time cpuTime{0};
	Time overhead{0};
	Time lastFinishTime{0};
	Time lastPrintedStats{0};

	TaskID currentID{0};
	// for time tracking with rollover
	Time lastTime{0};
	Time runningTime{0};
	void resetStats();
	// needs to be volatile, GCC misses that the handle call can call ignoreForStats and optimizes the check away
	volatile bool countThisTask{true};
	void startClock();
};
#endif // DELUGE_TASK_SCHEDULER_H
