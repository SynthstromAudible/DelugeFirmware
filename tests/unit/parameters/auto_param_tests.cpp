#include "CppUTest/TestHarness.h"
#include "mocks/attribute_io.h"
#include "modulation/automation/auto_param.h"

TEST_GROUP(AutoParamTests) {
	AutoParam param;
	void teardown() override {
		param.deleteAutomationBasicForSetup();
	}
	void addCurve() {
		CHECK(param.setNodeAtPos(0, 135, false) >= 0);
		CHECK(param.setNodeAtPos(16, 155, true) >= 0);
	}
};
TEST(AutoParamTests, InterpolatesNodes) {
	addCurve();
	CHECK_EQUAL(145, param.getValueAtPos(8, 32));
	CHECK(param.grabValueFromPos(8, 32));
	CHECK_EQUAL(145, param.getCurrentValue());
}
TEST(AutoParamTests, WritesCurrentValueAndNodes) {
	addCurve();
	param.grabValueFromPos(8, 32);
	MemorySerializer writer;
	param.writeToFile(writer, true);
	STRCMP_EQUAL("0x0000009100000087000000000000009B80000010", writer.bytes.c_str());
}
TEST(AutoParamTests, ReadsCurrentValueAndNodes) {
	MemoryDeserializer reader;
	reader.bytes = "0x0000009100000087000000000000009B80000010";
	CHECK(param.readFromFile(reader, 32) == Error::NONE);
	CHECK_EQUAL(145, param.getCurrentValue());
	CHECK_EQUAL(145, param.getValueAtPos(8, 32));
}
TEST(AutoParamTests, ReadsLegacyScalar) {
	MemoryDeserializer reader;
	reader.bytes = "-12345";
	CHECK(param.readFromFile(reader, 32) == Error::NONE);
	CHECK_EQUAL(-12345, param.getCurrentValue());
}
