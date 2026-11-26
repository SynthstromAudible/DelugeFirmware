#include "CppUTest/CommandLineTestRunner.h"
#include "CppUTest/TestFailure.h"
#include "CppUTest/TestHarness.h"
#include "CppUTest/TestPlugin.h"
#include "CppUTest/TestRegistry.h"
#include "CppUTest/TestResult.h"
#include "mocks/memory.h"

class AllocationChecks final : public TestPlugin {
public:
	AllocationChecks() : TestPlugin("Parameter allocation checks") {}
	void preTestAction(UtestShell& /*test*/, TestResult& /*result*/) override { parameter_test::resetAllocator(); }
	void postTestAction(UtestShell& test, TestResult& result) override {
		size_t leaked = parameter_test::finishAllocatorTest();
		result.countCheck();
		if (leaked != 0) {
			result.addFailure(TestFailure(&test, __FILE__, __LINE__,
			                              SimpleString("Leaked firmware allocations: ")
			                                  + StringFrom(static_cast<unsigned long>(leaked))));
		}
	}
};
int main(int argc, char** argv) {
	AllocationChecks checks;
	TestRegistry::getCurrentRegistry()->installPlugin(&checks);
	int result = CommandLineTestRunner::RunAllTests(argc, argv);
	TestRegistry::getCurrentRegistry()->removePluginByName(checks.getName());
	return result;
}
