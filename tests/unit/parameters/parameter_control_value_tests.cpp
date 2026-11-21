#include "CppUTest/TestHarness.h"
#include "mocks/attribute_io.h"
#include "modulation/automation/auto_param_access.h"
#include "modulation/params/param_control_value.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"

#include <type_traits>
#include <utility>

using namespace deluge::modulation::params;

// AutoParam and the roles other than live input do not expose the live setter.
template <typename T>
concept HasPublicLiveSetter =
    requires(T& param, ModelStackWithAutoParam* stack) { param.setValueForLiveInput(0, stack); };
static_assert(!HasPublicLiveSetter<AutoParam>);

template <typename T>
concept HasPublicScalarRestore = requires(T& param) { param.restoreClipValue(0); };
template <typename T>
concept HasPublicCurveRepresentation = requires(T& param) { param.getCurveValue(); };
template <typename T>
concept HasPublicNodeBase = requires(T& param) { param.getNodeBase(); };
static_assert(!HasPublicScalarRestore<AutoParam>);
static_assert(!HasPublicCurveRepresentation<AutoParam>);
static_assert(!HasPublicNodeBase<AutoParam>);

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
static_assert(!HasPublicLiveSetter<AutoParamSetup>);
static_assert(!HasPublicLiveSetter<AutoParamOwnership>);
static_assert(!HasPublicLiveSetter<AutoParamPlayback>);
static_assert(!HasPublicLiveSetter<AutoParamAutomation>);
static_assert(std::is_same_v<decltype(std::declval<AutoParam&>().nodeAt(0)), const ParamNode*>);
static_assert(!HasMutableNodes<const AutoParamData>);
template <typename T>
concept HasSetupSetter = requires(T& param) { param.setCurrentValueBasicForSetup(0); };
static_assert(!HasSetupSetter<AutoParam>);
static_assert(!HasSetupSetter<AutoParamAutomation>);
static_assert(!HasSetupSetter<AutoParamPlayback>);

// Ordinary fixed-parameter consumers must choose the render or control reader.
// The unsummarized fallback remains available only inside the owning containers.
template <typename T>
concept HasPublicUnsummarizedRead = requires(const T& params) { params.getValue(0); };
static_assert(!HasPublicUnsummarizedRead<ParamSet>);
static_assert(!HasPublicUnsummarizedRead<PatchedParamSet>);
static_assert(!HasPublicUnsummarizedRead<UnpatchedParamSet>);
static_assert(!HasPublicUnsummarizedRead<ExpressionParamSet>);
template <typename T>
concept HasPublicRenderRead =
    requires(const T& params, const ParamCollectionSummary& summary) { params.getValueForRender(0, summary); };
static_assert(!HasPublicRenderRead<ParamSet>);
static_assert(!HasPublicRenderRead<PatchedParamSet>);
static_assert(!HasPublicRenderRead<UnpatchedParamSet>);
static_assert(std::is_same_v<decltype(std::declval<ParamManager&>().patchedRenderValues()), ParamRenderValues>);
static_assert(std::is_same_v<decltype(std::declval<ParamManager&>().unpatchedRenderValues()), ParamRenderValues>);
static_assert(!std::is_constructible_v<ParamRenderValues, const ParamCollectionSummary&>);
static_assert(!std::is_default_constructible_v<ParamRenderValues>);
static_assert(std::is_same_v<decltype(std::declval<const ParamRenderValues&>().get(0)), int32_t>);
static_assert(sizeof(ParamRenderValues) == 2 * sizeof(void*));
static_assert(requires(const ExpressionParamSet& params) { params.getExpressionValue(0); });

TEST_GROUP(ParameterControlValueTests) {
	int32_t preset = 100;
	ClipParamValue scalar;
	AutoParamData automation;

	void setup() override {
		scalar.bindPreset(&preset);
		scalar.setValue(125);
		automation.bindClipValue(&scalar);
	}
	void teardown() override {
		automation.deleteAutomationBasicForSetup();
	}

	void addCurve() {
		CHECK(automation.setNodeAtPos(0, 135, false) >= 0);
		CHECK(automation.setNodeAtPos(16, 155, true) >= 0);
		CHECK(automation.grabValueFromPos(8, 32));
	}
};

TEST(ParameterControlValueTests, ScalarReadsDoNotRequireAutomationOrAllocateCurveStorage) {
	for (Kind kind : {Kind::PATCHED, Kind::UNPATCHED_SOUND, Kind::UNPATCHED_GLOBAL}) {
		CHECK_EQUAL(100, readControlValue(controlLayer(kind, true, true, true), &scalar, nullptr));
		CHECK_EQUAL(125, readControlValue(controlLayer(kind, true, true, false), &scalar, nullptr));
	}
	POINTERS_EQUAL(nullptr, automation.editNodes().get());
}

TEST(ParameterControlValueTests, AutomatedControlReadsChoosePresetClipOrLiveValue) {
	addCurve();
	for (Kind kind : {Kind::PATCHED, Kind::UNPATCHED_SOUND, Kind::UNPATCHED_GLOBAL}) {
		CHECK_EQUAL(100, readControlValue(controlLayer(kind, true, true, true), &scalar, nullptr));
		CHECK_EQUAL(125, readControlValue(controlLayer(kind, true, true, false), &scalar, nullptr));
		CHECK_EQUAL(145, readControlValue(controlLayer(kind, true, false, true), &scalar, &automation));
	}
	CHECK_EQUAL(145, readControlValue(controlLayer(Kind::EXPRESSION, true, true, true), &scalar, &automation));
}

TEST(ParameterControlValueTests, ClipEditPreservesCurveAndLiveInterpolationState) {
	addCurve();
	auto* curve = automation.editNodes().get();
	curve->valueIncrementPerHalfTick = 3;
	curve->renewedOverridingAtTime = 1234;
	CHECK_EQUAL(145, setClipValueFromControl(scalar, automation, 150));
	CHECK_EQUAL(100, preset);
	CHECK_EQUAL(150, scalar.getValue());
	CHECK_EQUAL(170, automation.getCurrentValue());
	CHECK_EQUAL(10, automation.editNodes().getElement(0)->offset);
	CHECK_EQUAL(30, automation.editNodes().getElement(1)->offset);
	POINTERS_EQUAL(curve, automation.editNodes().get());
	CHECK_EQUAL(3, curve->valueIncrementPerHalfTick);
	CHECK_EQUAL(1234, curve->renewedOverridingAtTime);
	automation.deleteAutomationBasicForSetup();
	CHECK_EQUAL(150, scalar.getValue());
	CHECK_EQUAL(150, automation.getCurrentValue());
}

TEST(ParameterControlValueTests, ClipEditRetainsUnclampedCurveOvershoot) {
	addCurve();
	CHECK_EQUAL(145, setClipValueFromControl(scalar, automation, INT32_MAX));
	CHECK_EQUAL(INT32_MAX, automation.getCurrentValue());
	CHECK_EQUAL(10, automation.editNodes().getElement(0)->offset);
	CHECK_EQUAL(30, automation.editNodes().getElement(1)->offset);
	CHECK_EQUAL(INT32_MAX, setClipValueFromControl(scalar, automation, 125));
	CHECK_EQUAL(145, automation.getCurrentValue());
}

TEST(ParameterControlValueTests, EditingInterfaceUpdatesTheLiveParameterWithReadOnlyInspection) {
	AutoParam live;
	const auto& data = live.data();
	CHECK_EQUAL(0, data.nodeCount());
	POINTERS_EQUAL(nullptr, live.nodeStorageIdentity());
	CHECK(live.automation().setNodeAtPos(0, 100, false) >= 0);
	CHECK(live.automation().setNodeAtPos(16, 200, true) >= 0);
	CHECK_EQUAL(2, data.nodeCount());
	CHECK_EQUAL(100, live.getNodeValue(data.nodeAt(0)));
	CHECK_EQUAL(150, live.getValueAtPos(8, 32));
}
