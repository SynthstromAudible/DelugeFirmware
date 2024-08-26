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
	Time one = 0.0;
	Time two = Time(0, 0);
	CHECK_EQUAL(double(one), double(two));
};

TEST(Clock, doubleDoubleConversion) {
	Time one = 123.5;
	CHECK_EQUAL((double)one, 123.5);
};

TEST(Clock, convertWithRolls) {
	double base = 1234.123456; // 9 clock rollovers
	Time one = base;
	double diff = std::abs((double)one - base);
	CHECK(diff < 0.00001);
};

} // namespace
