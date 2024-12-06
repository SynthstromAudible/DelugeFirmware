//
// Created by Mark Adams on 2024-12-05.
//
#include "OSLikeStuff/scheduler_api.h"
#include "OSLikeStuff/task_scheduler/task_scheduler.h"

extern TaskManager taskManager;

extern "C" {
void startTaskManager() {
	taskManager.start();
}
void ignoreForStats() {
	taskManager.ignoreForStats();
}

double getLastRunTimeforCurrentTask() {
	return taskManager.getLastRunTimeforCurrentTask();
}

void setNextRunTimeforCurrentTask(double seconds) {
	taskManager.setNextRunTimeforCurrentTask(seconds);
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