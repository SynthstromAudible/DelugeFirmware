#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "OSLikeStuff/task_scheduler.cpp"
#include "cstdint"
#include "mocks/timer_mocks.h"
#include <iostream>
#include <stdlib.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

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

TEST_GROUP(Scheduler){

    void setup(){taskManager = TaskManager();
} // namespace
}
;

TEST(Scheduler, schedule) {

	mock().clear();
	// will be called one less time due to the time the sleep takes not being zero
	mock().expectNCalls(0.01 / 0.001 - 1, "sleep_50ns");
	taskManager.addRepeatingTask(sleep_50ns, 0, 0.001, 0.001, 0.001);
	// run the scheduler for just under 10ms, calling the function to sleep 50ns every 1ms
	taskManager.start(0.0095);
	std::cout << "ending tests at " << getTimerValueSeconds(0) << std::endl;
	mock().checkExpectations();
};

TEST(Scheduler, remove) {
	static SelfRemoving selfRemoving;

	TaskID id = addRepeatingTask([]() { selfRemoving.runFiveTimes(); }, 0, 0.001, 0.001, 0.001);
	selfRemoving.id = id;
	mock().clear();
	// will be called one less time due to the time the sleep takes not being zero
	mock().expectNCalls(5, "runFiveTimes");

	// run the scheduler for just under 10ms, calling the function to sleep 50ns every 1ms
	taskManager.start(0.0095);
	std::cout << "ending tests at " << getTimerValueSeconds(0) << std::endl;
	mock().checkExpectations();
};

TEST(Scheduler, scheduleOnce) {
	mock().clear();
	// will be called one less time due to the time the sleep takes not being zero
	mock().expectNCalls(1, "sleep_50ns");
	addOnceTask(sleep_50ns, 0, 0.001);
	// run the scheduler for just under 10ms, calling the function to sleep 50ns every 1ms
	taskManager.start(0.0095);
	std::cout << "ending tests at " << getTimerValueSeconds(0) << std::endl;
	mock().checkExpectations();
};

TEST(Scheduler, scheduleOnceWithRepeating) {
	mock().clear();
	mock().expectNCalls(0.01 / 0.001 - 1, "sleep_50ns");
	mock().expectNCalls(1, "sleep_2ms");
	// every 1ms sleep for 50ns and 10ns
	addRepeatingTask(sleep_50ns, 10, 0.001, 0.001, 0.001);
	addOnceTask(sleep_2ms, 11, 0.0094);
	// run the scheduler for 10ms
	taskManager.start(0.0095);
	std::cout << "ending tests at " << getTimerValueSeconds(0) << std::endl;
	mock().checkExpectations();
};

TEST(Scheduler, removeWithPriZero) {
	mock().clear();
	mock().expectNCalls((0.01 - 0.002) / 0.001 - 1, "sleep_50ns");
	mock().expectNCalls(2, "sleep_2ms");
	// every 1ms sleep for 50ns and 10ns
	addRepeatingTask(sleep_50ns, 10, 0.001, 0.001, 0.001);
	addRepeatingTask([]() { passMockTime(0.00001); }, 0, 0.001, 0.001, 0.001);
	addOnceTask(sleep_2ms, 11, 0.002);
	addRepeatingTask([]() { passMockTime(0.00003); }, 0, 0.001, 0.001, 0.001);
	addOnceTask(sleep_2ms, 11, 0.009);
	// run the scheduler for 10ms
	taskManager.start(0.01);
	std::cout << "ending tests at " << getTimerValueSeconds(0) << std::endl;
	mock().checkExpectations();
};

/// Schedules more than kMaxTask tasks, checks on kMaxTask tasks run
TEST(Scheduler, tooManyTasks) {
	mock().clear();
	// will actually register kMaxTasks tasks, after that they're ignored
	mock().expectNCalls(kMaxTasks, "sleep_50ns");
	// more than allowed
	for (int i = 0; i <= kMaxTasks + 10; i++) {
		addOnceTask(sleep_50ns, 0, 0.001);
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
		TaskID id = addOnceTask(reAdd50, 0, 0);
	}
};
/// dynamically schedules more than kMaxTask tasks while remaining under kMaxTasks at all times
TEST(Scheduler, moreThanMaxTotal) {
	numCalls = 0;
	mock().clear();
	mock().expectNCalls(50, "reAdd50");
	addOnceTask(reAdd50, 0, 0);
	taskManager.start(0.01);
	mock().checkExpectations();
}

TEST(Scheduler, scheduleMultiple) {
	mock().clear();
	mock().expectNCalls(0.01 / 0.001 - 1, "sleep_50ns");
	mock().expectNCalls(0.01 / 0.001 - 1, "sleep_20ns");
	mock().expectNCalls(1, "sleep_2ms");
	// every 1ms sleep for 50ns and 10ns
	addRepeatingTask(sleep_50ns, 10, 0.001, 0.001, 0.001);
	addRepeatingTask(sleep_20ns, 0, 0.001, 0.001, 0.001);
	addOnceTask(sleep_2ms, 11, 0.0094);
	// run the scheduler for 10ms
	taskManager.start(0.0095);
	std::cout << "ending tests at " << getTimerValueSeconds(0) << std::endl;
	mock().checkExpectations();
};

TEST(Scheduler, overSchedule) {
	mock().clear();

	// will take one call to get duration, second call at its maximum time between calls
	mock().expectNCalls(2, "sleep_2ms");
	// will be missing 4ms from the two sleeps
	mock().expectNCalls(0.006 / 0.001, "sleep_50ns");
	mock().expectNCalls(0.006 / 0.001, "sleep_20ns");

	// every 1ms sleep for 50ns and 10ns
	auto fiftynshandle = addRepeatingTask(sleep_50ns, 10, 0.001, 0.001, 0.001);
	auto tennshandle = addRepeatingTask(sleep_20ns, 0, 0.001, 0.001, 0.001);
	auto twomsHandle = addRepeatingTask(sleep_2ms, 100, 0.001, 0.002, 0.005);
	// run the scheduler for 10ms
	taskManager.start(0.0099);
	std::cout << "ending tests at " << getTimerValueSeconds(0) << std::endl;

	mock().checkExpectations();
};

} // namespace
