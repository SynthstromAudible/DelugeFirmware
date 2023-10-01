#include "CppUTest/CommandLineTestRunner.h"
#include <iostream>
int main(int argc, char** argv) {
	std::cout << "hello";
	return CommandLineTestRunner::RunAllTests(argc, argv);
}
