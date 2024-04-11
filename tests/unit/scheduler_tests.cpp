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

void sleep_50ns() {
	mock().actualCall("sleep_50ns");
	uint32_t now = getTimerValue(0);
	while (getTimerValue(0) < now + 0.00005 * DELUGE_CLOCKS_PER) {
		;
	}
}
void sleep_10ns() {
	mock().actualCall("sleep_10ns");
	uint32_t now = getTimerValue(0);

	while (getTimerValue(0) < now + 0.00001 * DELUGE_CLOCKS_PER) {
		;
	}

}
void sleep_2ms() {
	mock().actualCall("sleep_2ms");
	uint32_t now = getTimerValue(0);
	while (getTimerValue(0) < now + 0.002 * DELUGE_CLOCKS_PER) {
		;
	}
}

TEST_GROUP(Scheduler) {
	TaskManager testTaskManager;

	void setup() {
		setTimerValue(0, 0);
		testTaskManager = TaskManager();
	}
};

TEST(Scheduler, schedule) {
	mock().clear();
	// will be called one less time due to the time the sleep takes not being zero
	mock().expectNCalls(0.01 / 0.001 - 1, "sleep_50ns");
	testTaskManager.addTask(sleep_50ns, 0, 0.001, 0.001, true);
	// run the scheduler for 10ms, calling the function to sleep 50ns every 1ms
	testTaskManager.start(0.01 * DELUGE_CLOCKS_PER);
	std::cout << "ending tests at " << getTimerValueSeconds(0) << std::endl;
	mock().checkExpectations();
};

TEST(Scheduler, scheduleMultiple) {
	mock().clear();
	mock().expectNCalls(0.01 / 0.001, "sleep_50ns");
	mock().expectNCalls(0.01 / 0.001, "sleep_10ns");

	// every 1ms sleep for 50ns and 10ns
	testTaskManager.addTask(sleep_50ns, 10, 0.001, 0.001, true);
	testTaskManager.addTask(sleep_10ns, 0, 0.001, 0.001, true);
	// run the scheduler for 10ms
	testTaskManager.start(0.011 * DELUGE_CLOCKS_PER);
	std::cout << "ending tests at " << getTimerValueSeconds(0) << std::endl;
	mock().checkExpectations();
};
TEST(Scheduler, overSchedule) {
	mock().clear();

	// will take one call to get duration
	mock().expectNCalls(1, "sleep_2ms");
	// will be missing 2ms from the sleep
	mock().expectNCalls(0.008 / 0.001, "sleep_50ns");
	mock().expectNCalls(0.008 / 0.001, "sleep_10ns");

	// every 1ms sleep for 50ns and 10ns
	auto fiftynshandle = testTaskManager.addTask(sleep_50ns, 10, 0.001, 0.001, true);
	auto tennshandle = testTaskManager.addTask(sleep_10ns, 0, 0.001, 0.001, true);
	auto twomsHandle = testTaskManager.addTask(sleep_2ms, 100, 0.001, 0.002, true);
	// run the scheduler for 10ms
	testTaskManager.start(0.01 * DELUGE_CLOCKS_PER);

	mock().checkExpectations();
};

} // namespace
