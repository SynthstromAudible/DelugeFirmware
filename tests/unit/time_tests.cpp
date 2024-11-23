#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"
#include "OSLikeStuff/timers_interrupts/clock_type.h"
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

TEST_GROUP(Clock){

};

TEST(Clock, doubleConversion) {
	dClock one = 0.0;
	dClock two = dClock(0, 0);
	CHECK_EQUAL(one, two);
};

TEST(Clock, doubleDoubleConversion) {
	dClock one = 123.5;
	dTime t = one.toTime();
	CHECK_EQUAL((double)one, 123.5);
};

TEST(Clock, convertWithRolls) {
	double base = 1234.12345; // 9 clock rollovers
	dClock one = base;
	dTime t = one.toTime();
	double diff = std::abs((double)one - base);
	CHECK(diff < 0.00001);
};

} // namespace
