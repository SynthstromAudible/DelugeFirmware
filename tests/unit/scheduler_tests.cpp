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

namespace {

struct SelfRemoving {
	int timesCalled{0};
	TaskID id{-1};
	void runFiveTimes() {
		std::cout << "running!/n" << std::endl;
		mock().actualCall("runFiveTimes");
		timesCalled += 1;
		if (timesCalled >= 5) {
			std::cout << "removing!/n" << std::endl;
			removeTask(id);
		}
	}
};

void sleep_50ns() {
	mock().actualCall("sleep_50ns");
	uint32_t now = getTimerValue(0);
	while (getTimerValue(0) < now + 0.00005 * DELUGE_CLOCKS_PER) {
		__asm__ __volatile__("nop");
	}
}
void sleep_20ns() {
	mock().actualCall("sleep_20ns");
	uint32_t now = getTimerValue(0);

	while (getTimerValue(0) < now + 0.00002 * DELUGE_CLOCKS_PER) {
		__asm__ __volatile__("nop");
	}
}
void sleep_2ms() {
	mock().actualCall("sleep_2ms");
	uint32_t now = getTimerValue(0);
	while (getTimerValue(0) < now + 0.002 * DELUGE_CLOCKS_PER) {
		__asm__ __volatile__("nop");
	}
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
