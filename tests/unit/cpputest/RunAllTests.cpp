#include "CppUTest/CommandLineTestRunner.h"
#include <iostream>
int main(int argc, char** argv) {
	return CommandLineTestRunner::RunAllTests(argc, argv);
}
