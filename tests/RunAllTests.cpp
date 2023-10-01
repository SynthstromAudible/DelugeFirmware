#include "CppUTest/CommandLineTestRunner.h"
#include <iostream>
int main(int argc, char** argv) {
	std::cout << "hello" << std::endl;
	return CommandLineTestRunner::RunAllTests(argc, argv);
}
