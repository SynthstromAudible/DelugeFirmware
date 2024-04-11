#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "OSLikeStuff/task_scheduler.cpp"
#include "cstdint"
#include <iostream>
#include <stdlib.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#define NUM_TEST_ALLOCATIONS 1024
#define MEM_SIZE 10000000

namespace {

void sleep_10ms() {
	std::cout << "sleeping" << std::endl;
	uint32_t now = getTimerValue(0);
	while (getTimerValue(0) < now + 0.0001 * DELUGE_CLOCKS_PER) {
		;
	}
}

TEST_GROUP(Scheduler) {
	TaskManager testTaskManager;

	void setup() {
		testTaskManager = TaskManager();
	}
};

TEST(Scheduler, schedule) {
	testTaskManager.addTask(sleep_10ms, 0, 10, 10, true);
	std::cout << "starting tests at " << getTimerValue(0) << std::endl;
	testTaskManager.start(0.001 * DELUGE_CLOCKS_PER);
	std::cout << "ending tests at " << getTimerValue(0) << std::endl;
	std::cout << "ending tests at " << getTimerValue(0) << std::endl;
};

} // namespace
