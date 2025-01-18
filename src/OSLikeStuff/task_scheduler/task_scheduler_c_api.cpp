//
// Created by Mark Adams on 2024-12-05.
//
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

double getAverageRunTimeforCurrentTask() {
	return taskManager.getAverageRunTimeForCurrentTask();
}

void setNextRunTimeforCurrentTask(double seconds) {
	taskManager.setNextRunTimeforCurrentTask(seconds);
}

uint8_t addRepeatingTask(TaskHandle task, uint8_t priority, double backOffTime, double targetTimeBetweenCalls,
                         double maxTimeBetweenCalls, const char* name, ResourceID resources) {
	return taskManager.addRepeatingTask(
	    task, TaskSchedule{priority, backOffTime, targetTimeBetweenCalls, maxTimeBetweenCalls}, name,
	    ResourceChecker{resources});
}
uint8_t addOnceTask(TaskHandle task, uint8_t priority, double timeToWait, const char* name, ResourceID resources) {
	return taskManager.addOnceTask(task, priority, timeToWait, name, ResourceChecker{resources});
}

uint8_t addConditionalTask(TaskHandle task, uint8_t priority, RunCondition condition, const char* name,
                           ResourceID resources) {
	return taskManager.addConditionalTask(task, priority, condition, name, ResourceChecker{resources});
}

void yield(RunCondition until) {
	taskManager.yield(until);
}

bool yieldWithTimeout(RunCondition until, double timeout) {
	return taskManager.yield(until, timeout);
}

void removeTask(TaskID id) {
	return taskManager.removeTask(id);
}
double getSystemTime() {
	return taskManager.getSecondsFromStart();
}
}