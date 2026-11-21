#include "CppUTest/TestHarness.h"
#include "mocks/attribute_io.h"
#include "modulation/automation/auto_param_access.h"
#include "modulation/params/param_node.h"
#include <type_traits>
#include <utility>

// AutoParam and the roles other than live input do not expose the live setter.
template <typename T>
concept HasPublicLiveSetter =
    requires(T& param, ModelStackWithAutoParam* stack) { param.setValueForLiveInput(0, stack); };
static_assert(!HasPublicLiveSetter<AutoParam>);

static_assert(!HasPublicLiveSetter<AutoParamSetup>);
static_assert(!HasPublicLiveSetter<AutoParamOwnership>);
static_assert(!HasPublicLiveSetter<AutoParamPlayback>);
static_assert(!HasPublicLiveSetter<AutoParamAutomation>);

// Ordinary callers cannot acquire privileged roles or recover mutable live storage.
static_assert(!std::is_constructible_v<AutoParamSetup, AutoParam&>);
static_assert(!std::is_constructible_v<AutoParamOwnership, AutoParam&>);
static_assert(!std::is_constructible_v<AutoParamPlayback, AutoParam&>);
static_assert(!std::is_constructible_v<AutoParamLiveInput, AutoParam&>);
static_assert(!std::is_convertible_v<AutoParam*, AutoParamData*>);
static_assert(std::is_same_v<decltype(std::declval<AutoParam&>().data()), const AutoParamData&>);
template <typename T>
concept HasMutableNodes = requires(T& param) { param.editNodes(); };
static_assert(!HasMutableNodes<AutoParam>);
static_assert(!HasMutableNodes<AutoParamAutomation>);
static_assert(std::is_same_v<decltype(std::declval<AutoParam&>().nodeAt(0)), const ParamNode*>);
static_assert(!HasMutableNodes<const AutoParamData>);
template <typename T>
concept HasSetupSetter = requires(T& param) { param.setCurrentValueBasicForSetup(0); };
static_assert(!HasSetupSetter<AutoParam>);
static_assert(!HasSetupSetter<AutoParamAutomation>);
static_assert(!HasSetupSetter<AutoParamPlayback>);

TEST_GROUP(AutoParamTests) {
	AutoParamData param;
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

TEST(AutoParamTests, EditingInterfaceUpdatesTheLiveParameterWithReadOnlyInspection) {
	AutoParam live;
	const auto& data = live.data();
	CHECK_EQUAL(0, data.nodeCount());
	POINTERS_EQUAL(nullptr, live.nodeStorageIdentity());
	CHECK(live.automation().setNodeAtPos(0, 100, false) >= 0);
	CHECK(live.automation().setNodeAtPos(16, 200, true) >= 0);
	CHECK_EQUAL(2, data.nodeCount());
	CHECK_EQUAL(100, data.nodeAt(0)->value);
	CHECK_EQUAL(150, live.getValueAtPos(8, 32));
}
