#include "CppUTest/CommandLineTestRunner.h"

IMPORT_TEST_GROUP(MemoryAllocation);

int main(int argc, char** argv) {
	return CommandLineTestRunner::RunAllTests(argc, argv);
}
