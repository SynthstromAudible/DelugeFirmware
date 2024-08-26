#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "OSLikeStuff/task_scheduler/task_scheduler.cpp"
#include "OSLikeStuff/task_scheduler/task_scheduler_c_api.cpp"
#include "cstdint"
#include "mocks/timer_mocks.h"
#include <iostream>
#include <print>
#include <stdlib.h>
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

uint8_t currentlyAccessingCard = false;
uint32_t usbLock = false;
bool sdRoutineLock = false;
namespace {

struct SelfRemoving {
	int timesCalled{0};
	TaskID id{-1};
	void runFiveTimes() {
		mock().actualCall("runFiveTimes");
		timesCalled += 1;
		passMockTime(0.001);
		if (timesCalled >= 5) {
			removeTask(id);
		}
	}
};

void sleep_50ns() {
	mock().actualCall("sleep_50ns");
	uint32_t now = getTimerValue(0);
	passMockTime(0.00005);
}
void sleep_20ns() {
	mock().actualCall("sleep_20ns");
	uint32_t now = getTimerValue(0);
	passMockTime(0.00002);
}
void sleep_2ms() {
	mock().actualCall("sleep_2ms");
	uint32_t now = getTimerValue(0);
	passMockTime(0.002);
}

Time started;
void yield_2ms() {
	mock().actualCall("yield_2ms");
	started = getTimerValueSeconds(0);
	yield([]() { return getTimerValueSeconds(0) > started + Time(0.002); });
}

void yield_2ms_with_lock() {
	mock().actualCall("yield_2ms");
	started = getTimerValueSeconds(0);
	usbLock = true;
	yield([]() { return getTimerValueSeconds(0) > started + Time(0.002); });
	usbLock = false;
}

TEST_GROUP(Scheduler){

    void setup(){taskManager = TaskManager();
} // namespace
}
;

TEST(Scheduler, schedule) {

	mock().clear();
	mock().expectNCalls(0.01 / 0.001, "sleep_50ns");
	addRepeatingTask(sleep_50ns, 0, 0.001, 0.001, 0.001, "sleep_50ns", RESOURCE_NONE);
	// run the scheduler for just under 10ms, calling the function to sleep 50ns every 1ms
	taskManager.start(0.0095);
	mock().checkExpectations();
};

TEST(Scheduler, remove) {
	static SelfRemoving selfRemoving;

	TaskID id = addRepeatingTask([]() { selfRemoving.runFiveTimes(); }, 0, 0.001, 0.001, 0.001, "run five times",
	                             RESOURCE_NONE);
	selfRemoving.id = id;
	mock().clear();
	mock().expectNCalls(5, "runFiveTimes");

	// run the scheduler for just under 10ms, calling the function to sleep 50ns every 1ms
	taskManager.start(0.0095);
	mock().checkExpectations();
};

TEST(Scheduler, scheduleOnce) {
	mock().clear();
	// will be called one less time due to the time the sleep takes not being zero
	mock().expectNCalls(1, "sleep_50ns");
	addOnceTask(sleep_50ns, 0, 0.001, "sleep 50ns", RESOURCE_NONE);
	// run the scheduler for just under 10ms, calling the function to sleep 50ns every 1ms
	taskManager.start(0.0095);
	mock().checkExpectations();
};

TEST(Scheduler, scheduleConditional) {
	mock().clear();
	mock().expectNCalls(1, "sleep_50ns");
	// will load as blocked but immediately pass condition
	addConditionalTask(sleep_50ns, 0, []() { return true; }, "sleep 50ns", RESOURCE_NONE);
	// run the scheduler for just under 10ms, calling the function to sleep 50ns every 1ms
	taskManager.start(0.0095);
	mock().checkExpectations();
};

TEST(Scheduler, scheduleConditionalDoesntRun) {
	mock().clear();
	mock().expectNCalls(0, "sleep_50ns");
	// will load as blocked but immediately pass condition
	addConditionalTask(sleep_50ns, 0, []() { return false; }, "sleep 50ns", RESOURCE_NONE);
	// run the scheduler for just under 10ms, calling the function to sleep 50ns every 1ms
	taskManager.start(0.0095);
	mock().checkExpectations();
};

TEST(Scheduler, backOffTime) {
	mock().clear();
	mock().expectNCalls(10, "sleep_50ns");
	addRepeatingTask(sleep_50ns, 1, 0.01, 0.001, 1.0, "sleep_50ns", RESOURCE_NONE);
	// run the scheduler for 100ms, calling the function to sleep 50ns every 10ms
	taskManager.start(0.1);
	mock().checkExpectations();
};

TEST(Scheduler, scheduleOnceWithRepeating) {
	mock().clear();
	// loses 1 call while sleep_2ms is running
	mock().expectNCalls(0.01 / 0.001 - 2, "sleep_50ns");
	mock().expectNCalls(1, "sleep_2ms");
	// every 1ms sleep for 50ns and 10ns
	addRepeatingTask(sleep_50ns, 10, 0.001, 0.001, 0.001, "sleep_50ns", RESOURCE_NONE);
	addOnceTask(sleep_2ms, 11, 0, "sleep 2ms", RESOURCE_NONE);
	// run the scheduler for 10ms
	taskManager.start(0.01);
	mock().checkExpectations();
};

TEST(Scheduler, yield) {
	mock().clear();
	mock().expectNCalls(0.01 / 0.001, "sleep_50ns");
	mock().expectNCalls(1, "yield_2ms");
	// every 1ms sleep for 50ns and 10ns
	addRepeatingTask(sleep_50ns, 10, 0.001, 0.001, 0.001, "sleep_50ns", RESOURCE_NONE);
	addOnceTask(yield_2ms, 2, 0, "sleep 2ms", RESOURCE_NONE);
	// run the scheduler for 10ms
	taskManager.start(0.01);
	mock().checkExpectations();
};

TEST(Scheduler, removeWithPriZero) {
	mock().clear();
	mock().expectNCalls((0.01 - 0.002) / 0.001, "sleep_50ns");
	mock().expectNCalls(2, "sleep_2ms");
	// every 1ms sleep for 50ns and 10ns
	addRepeatingTask(sleep_50ns, 10, 0.001, 0.001, 0.001, "sleep 50ns", RESOURCE_NONE);
	addRepeatingTask([]() { passMockTime(0.00001); }, 0, 0.001, 0.001, 0.001, "mock time", RESOURCE_NONE);
	addOnceTask(sleep_2ms, 11, 0.002, "sleep 2 ms", RESOURCE_NONE);
	addRepeatingTask([]() { passMockTime(0.00003); }, 0, 0.001, 0.001, 0.001, "mock time", RESOURCE_NONE);
	addOnceTask(sleep_2ms, 11, 0.009, "sleep 2ms", RESOURCE_NONE);
	// run the scheduler for 10ms
	taskManager.start(0.01);
	mock().checkExpectations();
};

/// Schedules more than kMaxTask tasks, checks on kMaxTask tasks run
TEST(Scheduler, tooManyTasks) {
	mock().clear();
	// will actually register kMaxTasks tasks, after that they're ignored
	mock().expectNCalls(kMaxTasks, "sleep_50ns");
	// more than allowed
	for (int i = 0; i <= kMaxTasks + 10; i++) {
		addOnceTask(sleep_50ns, 0, 0.001, "sleep 50ns", RESOURCE_NONE);
	}

	// run the scheduler for 10ms
	taskManager.start(0.01);

	mock().checkExpectations();
};
int numCalls = 0;
void reAdd50() {
	mock().actualCall("reAdd50");

	passMockTime(0.00002);
	numCalls += 1;
	if (numCalls < 50) {
		TaskID id = addOnceTask(reAdd50, 0, 0, "reAdd 50", RESOURCE_NONE);
	}
};
/// dynamically schedules more than kMaxTask tasks while remaining under kMaxTasks at all times
TEST(Scheduler, moreThanMaxTotal) {
	numCalls = 0;
	mock().clear();
	mock().expectNCalls(50, "reAdd50");
	addOnceTask(reAdd50, 0, 0, "reAdd50", RESOURCE_NONE);
	taskManager.start(0.01);
	mock().checkExpectations();
}

TEST(Scheduler, scheduleMultiple) {
	mock().clear();
	mock().expectNCalls(0.01 / 0.001 - 1, "sleep_50ns");
	mock().expectNCalls(0.01 / 0.001 - 1, "sleep_20ns");
	mock().expectNCalls(1, "sleep_2ms");
	// every 1ms sleep for 50ns and 10ns
	addRepeatingTask(sleep_50ns, 10, 0.001, 0.001, 0.001, "sleep 50ns", RESOURCE_NONE);
	addRepeatingTask(sleep_20ns, 0, 0.001, 0.001, 0.001, "sleep 20ns", RESOURCE_NONE);
	addOnceTask(sleep_2ms, 11, 0.0094, "sleep 2ms", RESOURCE_NONE);
	// run the scheduler for 10ms
	taskManager.start(0.0095);
	mock().checkExpectations();
};

TEST(Scheduler, overSchedule) {
	mock().clear();

	// will take one call to get duration, second call at its maximum time between calls
	mock().expectNCalls(2, "sleep_2ms");
	// will be missing 4ms from the two sleeps
	mock().expectNCalls(0.006 / 0.001 + 1, "sleep_50ns");
	mock().expectNCalls(0.006 / 0.001 + 1, "sleep_20ns");

	// every 1ms sleep for 50ns and 10ns
	auto fiftynshandle = addRepeatingTask(sleep_50ns, 10, 0.001, 0.001, 0.001, "sleep 50ns", RESOURCE_NONE);
	auto tennshandle = addRepeatingTask(sleep_20ns, 0, 0.001, 0.001, 0.001, "sleep 20ns", RESOURCE_NONE);
	auto twomsHandle = addRepeatingTask(sleep_2ms, 100, 0.001, 0.002, 0.005, "sleep 2ms", RESOURCE_NONE);
	// run the scheduler for 10ms
	taskManager.start(0.01);
	mock().checkExpectations();
};
TEST(Scheduler, yield_with_lock) {
	mock().clear();
	// will be locked out by the usb lock
	mock().expectNCalls(0, "sleep_50ns");
	mock().expectNCalls(1, "yield_2ms");
	// every 1ms sleep for 50ns and 10ns
	addRepeatingTask(sleep_50ns, 10, 0.0001, 0.0001, 0.0001, "sleep_50ns", RESOURCE_USB);

	addOnceTask(yield_2ms_with_lock, 2, 0, "sleep 2ms", RESOURCE_USB);
	// run the scheduler for 2ms
	taskManager.start(0.002);
	mock().checkExpectations();
};
} // namespace
