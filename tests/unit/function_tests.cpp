#include "CppUTest/TestHarness.h"
#include "util/const_functions.h"

TEST_GROUP(FunctionTests){};

TEST(FunctionTests, mod) {
	CHECK_EQUAL(0, mod(-3, 3));
	CHECK_EQUAL(1, mod(-2, 3));
	CHECK_EQUAL(2, mod(-1, 3));
	CHECK_EQUAL(0, mod(0, 3));
	CHECK_EQUAL(1, mod(1, 3));
	CHECK_EQUAL(2, mod(2, 3));
	CHECK_EQUAL(0, mod(3, 3));
}
