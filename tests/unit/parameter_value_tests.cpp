#include "CppUTest/TestHarness.h"
#include "modulation/params/param_value.h"

TEST_GROUP(ParameterValueTests) {
	int32_t preset = 100;
	ClipParamValue clip;
	void setup() override {
		clip.setValue(preset);
		clip.bindPreset(&preset);
	}
};

TEST(ParameterValueTests, UneditedClipUsesPreset) {
	CHECK_EQUAL(0, clip.getOffset());
	CHECK_EQUAL(100, clip.getValue());
	preset = -200;
	CHECK_EQUAL(-200, clip.getValue());
}

TEST(ParameterValueTests, ScalarEditDoesNotChangePreset) {
	clip.setValue(125);
	CHECK_EQUAL(100, preset);
	CHECK_EQUAL(25, clip.getOffset());
	CHECK_EQUAL(125, clip.getValue());
}

TEST(ParameterValueTests, SharedPresetChangePreservesIndependentClipOffsets) {
	ClipParamValue other;
	other.setValue(preset);
	other.bindPreset(&preset);
	clip.setValue(125);
	other.setValue(80);
	preset = 110;
	CHECK_EQUAL(135, clip.getValue());
	CHECK_EQUAL(90, other.getValue());
}

TEST(ParameterValueTests, FullWidthClipDifferencesAreRetained) {
	preset = INT32_MAX;
	clip.setValue(INT32_MIN);
	CHECK_EQUAL(-4294967295LL, clip.getOffset());
	CHECK_EQUAL(INT32_MIN, clip.getValue());
}

TEST(ParameterValueTests, ReboundCopyHasIndependentClipValue) {
	ClipParamValue copiedClip = clip;
	copiedClip.setValue(200);
	CHECK_EQUAL(200, copiedClip.getValue());
	CHECK_EQUAL(100, clip.getValue());
}

TEST(ParameterValueTests, BindingDifferentPresetPreservesClipValue) {
	int32_t replacement = 200;
	clip.setValue(125);
	clip.bindPreset(&replacement);
	CHECK_EQUAL(125, clip.getValue());
	CHECK_EQUAL(-75, clip.getOffset());
}

TEST(ParameterValueTests, RebindingSamePresetPreservesClippedOvershoot) {
	clip.setValue(INT32_MAX);
	preset += 100;
	CHECK_EQUAL(INT32_MAX, clip.getValue());
	clip.bindPreset(&preset);
	preset -= 200;
	CHECK_EQUAL(INT32_MAX - 100, clip.getValue());
}

TEST(ParameterValueTests, InterpolationDoesNotOverflowOnLongFullWidthCurves) {
	CHECK_EQUAL(4294967290LL, interpolateAutomationOffset(-4294967295LL, 4294967295LL, INT32_MAX - 1, INT32_MAX));
}

TEST(ParameterValueTests, SaturatingPlaybackIncrementRetainsWideEndpoints) {
	constexpr int64_t limit = 2 * int64_t{UINT32_MAX};
	CHECK_EQUAL(123 * 96, saturatingAutomationIncrement(123, 96));
	CHECK_EQUAL(-123 * 96, saturatingAutomationIncrement(-123, 96));
	CHECK_EQUAL(limit, saturatingAutomationIncrement(INT32_MAX, INT32_MAX));
	CHECK_EQUAL(-limit, saturatingAutomationIncrement(INT32_MIN, INT32_MAX));
	CHECK_EQUAL(limit, saturatingAutomationIncrement(4294967295LL, 2));
	CHECK_EQUAL(-limit, saturatingAutomationIncrement(-4294967295LL, 3));
	CHECK_EQUAL(0, saturatingAutomationIncrement(4294967295LL, 0));
}

TEST_GROUP(AutomationValueTests) {
	AutoParamValue value;
};

TEST(AutomationValueTests, ZeroOffsetFollowsSuppliedBase) {
	CHECK_EQUAL(0, value.getOffset());
	CHECK_EQUAL(123, value.getValueForBase(123));
	CHECK_EQUAL(-456, value.getValueForBase(-456));
}

TEST(AutomationValueTests, CachedValueTracksBaseAndOffsetChanges) {
	value.setOffsetForBase(10, 125);
	CHECK_EQUAL(135, value.getValueForBase(125));
	CHECK_EQUAL(135, value.getValueForBase(125));
	CHECK_EQUAL(155, value.getValueForBase(145));
	CHECK_EQUAL(-40, value.getValueForBase(-50));
	value.setOffsetForBase(-20, -50);
	CHECK_EQUAL(-70, value.getValueForBase(-50));
	value.setOffsetForBase(0, -50);
	CHECK_EQUAL(-50, value.getValueForBase(-50));
}

TEST(AutomationValueTests, FullWidthDifferencesAreRetained) {
	value.setOffsetForBase(4294967295LL, INT32_MIN);
	CHECK_EQUAL(4294967295LL, value.getOffset());
	CHECK_EQUAL(INT32_MAX, value.getValueForBase(INT32_MIN));
	value.setOffsetForBase(-4294967295LL, INT32_MAX);
	CHECK_EQUAL(INT32_MIN, value.getValueForBase(INT32_MAX));
}

TEST(AutomationValueTests, OvershootSurvivesClamping) {
	value.setOffsetForBase(100, INT32_MAX);
	CHECK_EQUAL(INT32_MAX, value.getValueForBase(INT32_MAX));
	CHECK_EQUAL(100, value.getValueForBase(0));
	value.setOffsetForBase(-100, INT32_MIN);
	CHECK_EQUAL(INT32_MIN, value.getValueForBase(INT32_MIN));
	CHECK_EQUAL(-100, value.getValueForBase(0));
}

TEST(AutomationValueTests, CopiedCacheUsesItsOwnBaseAndOffset) {
	value.setOffsetForBase(10, 125);
	AutoParamValue copy = value;
	CHECK_EQUAL(210, copy.getValueForBase(200));
	copy.setOffsetForBase(20, 200);
	CHECK_EQUAL(135, value.getValueForBase(125));
	CHECK_EQUAL(220, copy.getValueForBase(200));
}

TEST(AutomationValueTests, InterpolationPrecedesClamping) {
	value.setOffsetForBase(interpolateAutomationOffset(-100, 100, 1, 4), INT32_MAX);
	CHECK_EQUAL(INT32_MAX - 50, value.getValueForBase(INT32_MAX));
	value.setOffsetForBase(interpolateAutomationOffset(-100, 100, 3, 4), INT32_MAX);
	CHECK_EQUAL(INT32_MAX, value.getValueForBase(INT32_MAX));
}

TEST(ParameterValueTests, RebindingPreservesSaturatedClipAdjustment) {
	int32_t preset = 100;
	ClipParamValue value;
	value.bindPreset(&preset);
	value.setValue(INT32_MIN);
	preset = 0;
	CHECK_EQUAL(INT32_MIN, value.getValue());
	value.detachZeroPreset();
	CHECK_EQUAL(int64_t{INT32_MIN} - 100, value.getOffset());
	int32_t replacement = 0;
	value.bindPreset(&replacement);
	replacement = 100;
	CHECK_EQUAL(INT32_MIN, value.getValue());
	replacement = 200;
	CHECK_EQUAL(INT32_MIN + 100, value.getValue());
}

TEST(ParameterValueTests, DetachingFullWidthAdjustmentRetainsBothSigns) {
	for (bool negative : {false, true}) {
		int32_t original = negative ? INT32_MAX : INT32_MIN;
		int32_t edited = negative ? INT32_MIN : INT32_MAX;
		preset = original;
		clip.setValue(edited);
		preset = edited;
		int64_t unclamped = clip.getUnclampedValue();
		clip.bindPreset(nullptr);
		CHECK_EQUAL(unclamped, clip.getUnclampedValue());
		clip.bindPreset(&preset);
		CHECK_EQUAL(unclamped, clip.getUnclampedValue());
		preset = original;
		CHECK_EQUAL(edited, clip.getValue());
	}
}

TEST(ParameterValueTests, RebindingToOppositeExtremePreservesUnclampedValue) {
	preset = INT32_MIN;
	clip.setValue(INT32_MAX);
	preset = INT32_MAX;
	int64_t expected = clip.getUnclampedValue();
	int32_t other = INT32_MIN;
	clip.bindPreset(&other);
	CHECK_EQUAL(expected, clip.getUnclampedValue());
	clip.bindPreset(nullptr);
	clip.detachZeroPreset();
	CHECK_EQUAL(expected, clip.getUnclampedValue());
	clip.bindPreset(&preset);
	preset = INT32_MIN;
	CHECK_EQUAL(INT32_MAX, clip.getValue());
	// A regular edit must leave the extended representation cleanly.
	clip.setValue(-100);
	CHECK_EQUAL(-100, clip.getValue());
	CHECK_EQUAL(int64_t{-100} - INT32_MIN, clip.getOffset());
}

TEST(ParameterValueTests, AdvancingACurrentValueMatchesFullEvaluation) {
	constexpr int32_t bases[] = {INT32_MIN, -1000, 0, 1000, INT32_MAX};
	constexpr int64_t offsets[] = {-int64_t{UINT32_MAX}, -5000, -1, 0, 1, 5000, int64_t{UINT32_MAX}};
	constexpr int32_t increments[] = {INT32_MIN, -70000, -1, 1, 70000, INT32_MAX};
	for (int32_t base : bases) {
		for (int64_t offset : offsets) {
			for (int32_t increment : increments) {
				AutoParamValue fast;
				AutoParamValue full;
				fast.setOffsetForBase(offset, base);
				full.setOffsetForBase(offset + increment, base);
				// The fast path continues from the evaluation that the reader last saw.
				LONGS_EQUAL(full.getValueForBase(base), fast.advanceCurrent(increment));
				CHECK(full.getOffset() == fast.getOffset());
				LONGS_EQUAL(full.getValueForBase(base), fast.getValueForBase(base));
			}
		}
	}
}
