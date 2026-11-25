#include "CppUTest/TestHarness.h"
#include "modulation/automation/auto_param_data.h"
#include "modulation/params/param_render_values.h"
#include <array>

// The render cache is production code instantiated with the tests' AutoParamData. A stale resolved array
// fails these tests, since every read goes through ParamRenderValues.
TEST_GROUP(ParamRenderValuesTests) {
	static constexpr int32_t kCount = 3;
	std::array<int32_t, kCount> preset{100, 200, 300};
	std::array<ClipParamValue, kCount> clip{};
	std::array<AutoParamData, kCount> params{};
	std::array<int32_t, kCount> values{};
	uint32_t version = 0;
	std::array<uint32_t, 1> automated{};

	void setup() override {
		for (int32_t p = 0; p < kCount; ++p) {
			// Binding keeps a clip value, so start each at its baseline.
			clip[p].setValue(preset[p]);
			clip[p].bindPreset(&preset[p]);
			params[p].bindClipValue(&clip[p]);
		}
		clip[1].setValue(250);
	}
	void teardown() override {
		for (auto& param : params) {
			param.deleteAutomationBasicForSetup();
		}
	}
	[[nodiscard]] ParamRenderSource<AutoParamData> source() {
		return {.params = params.data(),
		        .clip_values = clip.data(),
		        .values = values.data(),
		        .version = &version,
		        .automated = automated.data(),
		        .count = kCount};
	}
	[[nodiscard]] int32_t read(int32_t p) {
		return ParamRenderValues(source()).get(p);
	}
	void automate(int32_t p) {
		CHECK(params[p].setNodeAtPos(0, clip[p].getValue() + 10, false) >= 0);
		CHECK(params[p].setNodeAtPos(16, clip[p].getValue() + 30, true) >= 0);
		automated[0] |= uint32_t{1} << p;
		params[p].grabValueFromPos(0, 32);
	}
};

TEST(ParamRenderValuesTests, FirstReadResolvesEveryValue) {
	CHECK_EQUAL(100, read(0));
	CHECK_EQUAL(250, read(1));
	CHECK_EQUAL(300, read(2));
	CHECK_EQUAL(parameter_value_epoch, version);
}

TEST(ParamRenderValuesTests, ReadsReuseResolvedValuesUntilAValueChanges) {
	CHECK_EQUAL(100, read(0));
	// Nothing announced a change, so the resolved value is read as it stands.
	values[0] = 999;
	CHECK_EQUAL(999, read(0));
	parameterValuesChanged();
	CHECK_EQUAL(100, read(0));
}

TEST(ParamRenderValuesTests, ClipValueEditAfterPopulationIsRead) {
	CHECK_EQUAL(250, read(1));
	clip[1].setValue(260);
	CHECK_EQUAL(260, read(1));
	CHECK_EQUAL(100, read(0));
}

TEST(ParamRenderValuesTests, BaselineEditAfterPopulationIsRead) {
	CHECK_EQUAL(250, read(1));
	preset[1] = 220;
	parameterBaselinesChanged();
	// The clip keeps its offset of 50 above the baseline.
	CHECK_EQUAL(270, read(1));
	preset[0] = 120;
	parameterBaselinesChanged();
	CHECK_EQUAL(120, read(0));
}

TEST(ParamRenderValuesTests, AutomatedParameterReadsItsCurve) {
	automate(2);
	CHECK_EQUAL(310, read(2));
	CHECK_EQUAL(params[2].getValueForRender(), read(2));
	CHECK_EQUAL(100, read(0));
}

TEST(ParamRenderValuesTests, PlaybackUpdatesTheResolvedValueWithoutARefresh) {
	automate(2);
	CHECK_EQUAL(310, read(2));
	uint32_t epoch = parameter_value_epoch;
	{
		ParameterPlaybackScope scope(params[2], &values[2]);
		params[2].grabValueFromPos(8, 32);
	}
	CHECK_EQUAL(epoch, parameter_value_epoch);
	CHECK_EQUAL(epoch, version);
	CHECK_EQUAL(320, read(2));
	CHECK_EQUAL(params[2].getValueForRender(), read(2));
	// A change of another parameter outside playback still refreshes every value.
	clip[0].setValue(110);
	CHECK_EQUAL(110, read(0));
	CHECK_EQUAL(320, read(2));
}

TEST(ParamRenderValuesTests, CloneResolvesFromItsOwnLayers) {
	CHECK_EQUAL(250, read(1));
	// A cloned set copies its source's storage, then points at its own values with a cleared version.
	std::array<ClipParamValue, kCount> clone_clip = clip;
	std::array<AutoParamData, kCount> clone_params{};
	for (int32_t p = 0; p < kCount; ++p) {
		clone_params[p].bindClipValue(&clone_clip[p]);
	}
	clone_clip[1].setValue(275);
	CHECK_EQUAL(250, read(1));
	std::array<int32_t, kCount> clone_values = values;
	uint32_t clone_version = 0;
	ParamRenderSource<AutoParamData> clone{.params = clone_params.data(),
	                                       .clip_values = clone_clip.data(),
	                                       .values = clone_values.data(),
	                                       .version = &clone_version,
	                                       .automated = automated.data(),
	                                       .count = kCount};
	CHECK_EQUAL(275, ParamRenderValues(clone).get(1));
	CHECK_EQUAL(250, read(1));
	for (auto& param : clone_params) {
		param.deleteAutomationBasicForSetup();
	}
}
