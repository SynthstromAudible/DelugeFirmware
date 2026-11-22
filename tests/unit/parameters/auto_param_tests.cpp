#include "CppUTest/TestHarness.h"
#include "mocks/attribute_io.h"
#include "modulation/automation/auto_param_data.h"
#include "modulation/params/param_node.h"
#include "modulation/params/patch_cable_preset_values.h"
#include "modulation/patch/patch_cable.h"
#include "storage/preset_save_values.h"

TEST_GROUP(AutoParamTests) {
	int32_t preset = 100;
	ClipParamValue clip;
	AutoParamData param;

	void setup() override {
		clip.setValue(preset);
		clip.bindPreset(&preset);
		param.bindClipValue(&clip);
		clip.setValue(125);
	}

	void teardown() override {
		param.deleteAutomationBasicForSetup();
	}

	void addCurve() {
		CHECK(param.setNodeAtPos(0, 135, false) >= 0);
		CHECK(param.setNodeAtPos(16, 155, true) >= 0);
	}
};

TEST(AutoParamTests, UnautomatedValueFollowsClipScalar) {
	CHECK_EQUAL(125, param.getCurrentValue());
	clip.setValue(145);
	CHECK_EQUAL(145, param.getCurrentValue());
}

TEST(AutoParamTests, NodesStoreOffsetsFromClipScalar) {
	addCurve();
	CHECK_EQUAL(10, param.editNodes().getElement(0)->offset);
	CHECK_EQUAL(30, param.editNodes().getElement(1)->offset);
}

TEST(AutoParamTests, InterpolatesRelativeNodes) {
	addCurve();
	// The same core evaluator is called by the model-stack playback wrapper.
	CHECK_EQUAL(145, param.getValueAtPos(8, 32));
	CHECK(param.grabValueFromPos(8, 32));
	CHECK_EQUAL(145, param.getCurrentValue());
	CHECK_EQUAL(125, param.getClipValue());
}

TEST(AutoParamTests, ClipScalarChangeShiftsCurveWithoutRewritingNodes) {
	addCurve();
	clip.setValue(145);
	CHECK_EQUAL(165, param.getValueAtPos(8, 32));
	CHECK_EQUAL(10, param.editNodes().getElement(0)->offset);
	CHECK_EQUAL(30, param.editNodes().getElement(1)->offset);
}

TEST(AutoParamTests, PresetChangeShiftsCurveWithoutRewritingNodes) {
	addCurve();
	preset = 110;
	CHECK_EQUAL(135, param.getClipValue());
	CHECK_EQUAL(155, param.getValueAtPos(8, 32));
	CHECK_EQUAL(10, param.editNodes().getElement(0)->offset);
}

TEST(AutoParamTests, RestoringScalarToPresetRetainsRelativeCurve) {
	addCurve();
	clip.setValue(preset);
	CHECK_EQUAL(120, param.getValueAtPos(8, 32));
	CHECK_EQUAL(2, param.editNodes().getNumElements());
}

TEST(AutoParamTests, DeleteAutomationRestoresScalarFromBeforeAutomation) {
	addCurve();
	param.grabValueFromPos(8, 32);
	param.deleteAutomationBasicForSetup();
	CHECK_FALSE(param.isAutomated());
	CHECK_EQUAL(125, param.getCurrentValue());
	CHECK_EQUAL(0, param.getAutomationOffset());
}

TEST(AutoParamTests, WritesEvaluatedValueAndAbsoluteNodes) {
	addCurve();
	param.grabValueFromPos(8, 32);
	MemorySerializer writer;
	param.writeToFile(writer, true);
	STRCMP_EQUAL("0x0000009100000087000000000000009B80000010", writer.bytes.c_str());
}

TEST(AutoParamTests, ReadingAbsoluteNodesAndRestoringScalarReconstructsOffsets) {
	MemoryDeserializer reader;
	reader.bytes = "0x0000009100000087000000000000009B80000010";
	CHECK(param.readFromFile(reader, 32) == Error::NONE);
	param.restoreClipValueFromFile(125);
	CHECK_EQUAL(145, param.getCurrentValue());
	CHECK_EQUAL(125, param.getClipValue());
	CHECK_EQUAL(10, param.editNodes().getElement(0)->offset);
	CHECK_EQUAL(30, param.editNodes().getElement(1)->offset);
	CHECK_EQUAL(145, param.getValueAtPos(8, 32));
}

TEST(AutoParamTests, ReadsLegacyDecimalScalarAndRemovesExistingCurve) {
	addCurve();
	MemoryDeserializer reader;
	reader.bytes = "-12345";
	CHECK(param.readFromFile(reader, 32) == Error::NONE);
	CHECK_EQUAL(-12345, param.getCurrentValue());
	CHECK_FALSE(param.isAutomated());
}

struct BoundParamData {
	ClipParamValue clipValue;
	AutoParamData param;
	BoundParamData() { param.bindClipValue(&clipValue); }
};

TEST_GROUP(BoundParamDataTests) {
	int32_t preset = 100;
	BoundParamData original;
	BoundParamData copy;

	void setup() override {
		original.clipValue.bindPreset(&preset);
		original.clipValue.setValue(125);
		copy.clipValue.bindPreset(&preset);
	}
	void teardown() override {
		original.param.deleteAutomationBasicForSetup();
		copy.param.deleteAutomationBasicForSetup();
	}
};

TEST(BoundParamDataTests, AutoParamCloneOwnsIndependentCurveAndScalar) {
	CHECK(original.param.setNodeAtPos(0, 135, false) >= 0);
	CHECK(original.param.setNodeAtPos(16, 155, true) >= 0);
	copy.param.cloneFrom(&original.param, true);
	CHECK(copy.param.editNodes().getElement(0) != original.param.editNodes().getElement(0));
	copy.clipValue.setValue(150);
	CHECK_EQUAL(170, copy.param.getValueAtPos(8, 32));
	CHECK_EQUAL(145, original.param.getValueAtPos(8, 32));
	copy.param.deleteAutomationBasicForSetup();
	CHECK_EQUAL(150, copy.param.getCurrentValue());
	CHECK_TRUE(original.param.isAutomated());
}

TEST(AutoParamTests, InterpolatesAcrossLoopBoundary) {
	CHECK(param.setNodeAtPos(4, 135, true) >= 0);
	CHECK(param.setNodeAtPos(20, 155, true) >= 0);
	CHECK_EQUAL(140, param.getValueAtPos(0, 32));
	CHECK_EQUAL(145, param.getValueAtPos(28, 32));
	CHECK_EQUAL(135, param.getValueAtPos(4, 32));
}

TEST(AutoParamTests, InfiniteTimelineHoldsEndpointsOutsideCurve) {
	CHECK(param.setNodeAtPos(4, 135, true) >= 0);
	CHECK(param.setNodeAtPos(20, 155, true) >= 0);
	CHECK_EQUAL(135, param.getValueAtPos(0, INT32_MAX));
	CHECK_EQUAL(155, param.getValueAtPos(28, INT32_MAX));
}

TEST(AutoParamTests, ReversePlaybackSelectsLeftValueAtSteppedNode) {
	CHECK(param.setNodeAtPos(0, 135, false) >= 0);
	CHECK(param.setNodeAtPos(16, 155, false) >= 0);
	CHECK_EQUAL(155, param.getValueAtPos(16, 32, false));
	CHECK_EQUAL(135, param.getValueAtPos(16, 32, true));
}

TEST(AutoParamTests, DeletingAndRecreatingCurveRetainsClipBinding) {
	addCurve();
	param.grabValueFromPos(8, 32);
	param.deleteAutomationBasicForSetup();
	POINTERS_EQUAL(nullptr, param.editNodes().get());
	preset = 110;
	CHECK_EQUAL(135, param.getCurrentValue());
	CHECK(param.setNodeAtPos(0, 155, false) >= 0);
	param.grabValueFromPos(0, 32);
	CHECK_EQUAL(155, param.getCurrentValue());
	CHECK_EQUAL(20, param.getAutomationOffset());
	param.deleteAutomationBasicForSetup();
	CHECK_EQUAL(135, param.getCurrentValue());
}

TEST(AutoParamTests, UnboundParametersKeepFullWidthValuesWithoutAllocating) {
	AutoParamData standalone;
	standalone.setCurrentValueBasicForSetup(INT32_MIN);
	CHECK_EQUAL(INT32_MIN, standalone.getCurrentValue());
	POINTERS_EQUAL(nullptr, standalone.editNodes().get());
	standalone.setCurrentValueBasicForSetup(INT32_MAX);
	CHECK_EQUAL(INT32_MAX, standalone.getCurrentValue());
	POINTERS_EQUAL(nullptr, standalone.editNodes().get());
}

TEST(AutoParamTests, UndoAndRedoSwapCurveOwnershipWithoutLosingScalarBinding) {
	addCurve();
	param.grabValueFromPos(8, 32);
	AutoParamState state;
	state.value = 99;
	state.clipValue = 99;
	state.nodeBase = 99;
	param.swapStateBasicForSetup(&state);
	CHECK_FALSE(param.isAutomated());
	CHECK_EQUAL(99, param.getCurrentValue());
	CHECK_EQUAL(2, state.nodes.getNumElements());
	preset = 110;
	CHECK_EQUAL(109, param.getCurrentValue());
	param.swapStateBasicForSetup(&state);
	CHECK(param.isAutomated());
	CHECK_EQUAL(125, param.getClipValue());
	CHECK_EQUAL(145, param.getCurrentValue());
	CHECK_EQUAL(10, param.editNodes().getElement(0)->offset);
	CHECK_EQUAL(30, param.editNodes().getElement(1)->offset);
	param.swapStateBasicForSetup(&state);
	CHECK_FALSE(param.isAutomated());
	CHECK_EQUAL(109, param.getCurrentValue());
}

TEST(BoundParamDataTests, ClonePreservesEvaluatedCurveAndIndependentScalar) {
	CHECK(original.param.setNodeAtPos(0, 135, false) >= 0);
	CHECK(original.param.setNodeAtPos(16, 155, true) >= 0);
	original.param.grabValueFromPos(8, 32);
	copy.param.cloneFrom(&original.param, true);
	CHECK_EQUAL(145, copy.param.getCurrentValue());
	copy.clipValue.setValue(160);
	CHECK_EQUAL(180, copy.param.getCurrentValue());
	CHECK_EQUAL(145, original.param.getCurrentValue());
	copy.param.deleteAutomationBasicForSetup();
	CHECK_EQUAL(160, copy.param.getCurrentValue());
}

TEST(AutoParamTests, CollectionReadFollowsBaseChangesWithoutCacheInvalidation) {
	CHECK_EQUAL(125, param.getValueForClip(clip.getValue()));
	addCurve();
	param.grabValueFromPos(8, 32);
	CHECK_EQUAL(145, param.getValueForClip(clip.getValue()));
	preset = 110;
	CHECK_EQUAL(155, param.getValueForClip(clip.getValue()));
	clip.setValue(160);
	CHECK_EQUAL(180, param.getValueForClip(clip.getValue()));
	param.deleteAutomationBasicForSetup();
	CHECK_EQUAL(160, param.getValueForClip(clip.getValue()));
}

TEST(AutoParamTests, RenderReadReusesCurveValueUntilABaselineChanges) {
	addCurve();
	param.grabValueFromPos(8, 32);
	CHECK_EQUAL(145, param.getValueForRender());
	CHECK_EQUAL(145, param.getCurrentValue());
	// Baseline storage is written directly, then announced.
	preset = 110;
	parameterBaselinesChanged();
	CHECK_EQUAL(155, param.getValueForRender());
	CHECK_EQUAL(155, param.getCurrentValue());
	// Clip value edits announce themselves.
	clip.setValue(160);
	CHECK_EQUAL(180, param.getValueForRender());
	CHECK_EQUAL(180, param.getCurrentValue());
}

TEST(AutoParamTests, RenderReadOfAnEmptiedCurveFollowsTheClipValue) {
	addCurve();
	param.grabValueFromPos(8, 32);
	CHECK_EQUAL(145, param.getValueForRender());
	// An edit may briefly leave an allocated curve without nodes.
	param.editNodes().deleteAtIndex(0, param.nodeCount(), false);
	CHECK(param.nodeStorageIdentity() != nullptr);
	CHECK_EQUAL(125, param.getValueForRender());
}

TEST(AutoParamTests, RenderReadFollowsANewBinding) {
	addCurve();
	param.grabValueFromPos(8, 32);
	CHECK_EQUAL(145, param.getValueForRender());
	ClipParamValue other;
	other.bindPreset(&preset);
	other.setValue(200);
	param.bindClipValue(&other);
	// Nodes keep their absolute values when the binding changes.
	CHECK_EQUAL(param.getValueForClip(other.getValue()), param.getValueForRender());
	param.bindClipValue(&clip);
}

TEST(AutoParamTests, CloneDoesNotReuseTheSourceEvaluation) {
	addCurve();
	param.grabValueFromPos(8, 32);
	CHECK_EQUAL(145, param.getValueForRender());
	LazyParamNodeVector curve;
	CHECK(curve.cloneFrom(&param.editNodes()));
	CHECK_FALSE(curve.get()->value.isCurrent());
	curve.empty();
	ClipParamValue other_clip;
	other_clip.bindPreset(&preset);
	other_clip.setValue(150);
	AutoParamData copy;
	copy.bindClipValue(&other_clip);
	copy.cloneFrom(&param, true);
	CHECK_EQUAL(copy.getValueForClip(other_clip.getValue()), copy.getValueForRender());
	CHECK_EQUAL(145, param.getValueForRender());
	copy.deleteAutomationBasicForSetup();
}

TEST(AutoParamTests, UndoKeepsLiveInterpolationAndOverrideContext) {
	AutoParamState state;
	CHECK_EQUAL(0, param.setNodeAtPos(0, 100, false));
	param.captureState(state, false);
	param.editNodes().get()->valueIncrementPerHalfTick = 123;
	param.editNodes().get()->interpolation_increment_remainder_float_ = 0.25f;
	param.editNodes().get()->renewedOverridingAtTime = 456;
	param.swapStateBasicForSetup(&state);
	CHECK_EQUAL(123, param.editNodes().get()->valueIncrementPerHalfTick);
	DOUBLES_EQUAL(0.25, param.editNodes().get()->interpolation_increment_remainder_float_, 0);
	CHECK_EQUAL(456, param.editNodes().get()->renewedOverridingAtTime);
	state.nodes.empty();
}

TEST(AutoParamTests, ClearingBindingActivatesStandaloneValueAndRebasesNodes) {
	addCurve();
	param.bindClipValue(nullptr);
	CHECK_EQUAL(0, param.getCurrentValue());
	CHECK_EQUAL(135, param.getValueAtPos(0, 32));
	param.setCurrentValueBasicForSetup(INT32_MIN);
	CHECK_EQUAL(INT32_MIN, param.getCurrentValue());
}

TEST(AutoParamTests, UndoableTrimPreservesPlaybackStateAndSupportsUndoRedo) {
	addCurve();
	CHECK(param.setNodeAtPos(32, 175, true) >= 0);
	CHECK(param.grabValueFromPos(8, 48));
	param.editNodes().get()->valueIncrementPerHalfTick = 123;
	param.editNodes().get()->interpolation_increment_remainder_float_ = 0.25f;
	param.editNodes().get()->renewedOverridingAtTime = 456;

	AutoParamState before;
	LazyParamNodeVector trimmed;
	CHECK(param.copyNodesForTrim(2, trimmed) == Error::NONE);
	// Preparing the replacement must leave the original intact for the undo snapshot.
	CHECK_EQUAL(3, param.editNodes().getNumElements());
	CHECK_EQUAL(145, param.getCurrentValue());
	param.captureState(before, true);
	param.editNodes().swapStateWith(&trimmed);

	CHECK_EQUAL(2, param.editNodes().getNumElements());
	CHECK_EQUAL(0, param.editNodes().getElement(0)->pos);
	CHECK_EQUAL(16, param.editNodes().getElement(1)->pos);
	CHECK_EQUAL(10, param.editNodes().getElement(0)->offset);
	CHECK_EQUAL(30, param.editNodes().getElement(1)->offset);
	CHECK(param.editNodes().getElement(1)->interpolated);
	CHECK_EQUAL(125, param.getClipValue());
	CHECK_EQUAL(20, param.getAutomationOffset());
	CHECK_EQUAL(145, param.getCurrentValue());
	CHECK_EQUAL(123, param.editNodes().get()->valueIncrementPerHalfTick);
	DOUBLES_EQUAL(0.25, param.editNodes().get()->interpolation_increment_remainder_float_, 0);
	CHECK_EQUAL(456, param.editNodes().get()->renewedOverridingAtTime);

	param.swapStateBasicForSetup(&before);
	CHECK_EQUAL(3, param.editNodes().getNumElements());
	CHECK_EQUAL(175, param.getNodeValue(param.editNodes().getElement(2)));
	CHECK_EQUAL(145, param.getCurrentValue());
	param.swapStateBasicForSetup(&before);
	CHECK_EQUAL(2, param.editNodes().getNumElements());
	CHECK_EQUAL(145, param.getCurrentValue());
	CHECK_EQUAL(123, param.editNodes().get()->valueIncrementPerHalfTick);
	CHECK_EQUAL(456, param.editNodes().get()->renewedOverridingAtTime);
}

TEST(AutoParamTests, UndoableTrimPreservesFloatInterpolationAndRelativeOffset) {
	addCurve();
	CHECK(param.setNodeAtPos(32, 175, true) >= 0);
	CHECK(param.grabValueFromPos(8, 48));
	param.editNodes().get()->value_increment_per_half_tick_float = 1.5f;
	param.editNodes().get()->interpolation_increment_remainder_float_ = 0.75f;
	param.editNodes().get()->renewedOverridingAtTime = 789;
	LazyParamNodeVector trimmed;
	CHECK(param.copyNodesForTrim(2, trimmed) == Error::NONE);
	param.editNodes().swapStateWith(&trimmed);
	CHECK_EQUAL(145, param.getCurrentValue());
	DOUBLES_EQUAL(1.5, param.editNodes().get()->value_increment_per_half_tick_float, 0);
	DOUBLES_EQUAL(0.75, param.editNodes().get()->interpolation_increment_remainder_float_, 0);
	CHECK_EQUAL(789, param.editNodes().get()->renewedOverridingAtTime);
	preset += 10;
	CHECK_EQUAL(155, param.getCurrentValue());
	CHECK_EQUAL(20, param.getAutomationOffset());
}

TEST(AutoParamTests, UndoPreservesScalarOvershootAfterLinkedBaselineChange) {
	for (int direction : {-1, 1}) {
		preset = 0;
		clip.setValue(direction * 100);
		preset = direction > 0 ? INT32_MAX : INT32_MIN;
		AutoParamState state;
		param.captureState(state, false);
		clip.setValue(0);
		param.swapStateBasicForSetup(&state);
		CHECK_EQUAL(direction * 100, clip.getOffset());
		preset = 0;
		CHECK_EQUAL(direction * 100, param.getCurrentValue());
		param.swapStateBasicForSetup(&state);
		param.swapStateBasicForSetup(&state);
		CHECK_EQUAL(direction * 100, param.getCurrentValue());
	}
}

TEST(AutoParamTests, ReusingZeroPresetRouteDoesNotChangeDetachedAutomation) {
	PatchCablePresetValues values;
	values.count = kMaxNumPatchCables;
	for (int i = 0; i < values.count; ++i)
		values.entries[i] = {static_cast<uint32_t>(i), i + 1};
	values.entries[0].value = 0;
	clip.bindPreset(values.find(0));
	clip.setValue(25);
	addCurve();
	const int32_t* surviving = values.find(1);
	for (uint32_t id = 100; id < 200; ++id) {
		clip.detachZeroPreset();
		auto* slot = values.reuseZeroEntry(id);
		CHECK(slot);
		*slot = 200;
		CHECK_EQUAL(25, param.getClipValue());
		CHECK_EQUAL(135, param.getValueAtPos(0, 32));
		CHECK_EQUAL(2, *surviving);
		*slot = 0;
	}
	CHECK_EQUAL(kMaxNumPatchCables, values.count);
}

TEST(AutoParamTests, ReusingAZeroPresetSlotLeavesOtherZeroRoutesBound) {
	PatchCablePresetValues values;
	values.count = kMaxNumPatchCables;
	for (int i = 0; i < values.count; ++i) {
		values.entries[i] = {static_cast<uint32_t>(i), i + 1};
	}
	values.entries[0].value = 0;
	values.entries[1].value = 0;
	ClipParamValue reused;
	ClipParamValue unrelated;
	reused.bindPreset(values.find(0));
	unrelated.bindPreset(values.find(1));
	const int32_t* slot = values.zeroEntry();
	CHECK(slot == values.find(0));
	reused.detachFromPreset(slot);
	unrelated.detachFromPreset(slot);
	auto* storage = values.reuseZeroEntry(100);
	CHECK(storage == slot);
	*storage = 300;
	CHECK_EQUAL(0, reused.getValue());
	// The other zero route still follows its own baseline, e.g. a later Track Link edit.
	values.entries[1].value = 40;
	CHECK_EQUAL(40, unrelated.getValue());
}

TEST(AutoParamTests, UndoPreservesAutomatedScalarOvershootAndRelativeNodes) {
	preset = 0;
	clip.setValue(100);
	CHECK(param.setNodeAtPos(0, 110, false) >= 0);
	param.setCurrentValueBasicForSetup(110);
	preset = INT32_MAX;
	AutoParamState state;
	param.captureState(state, false);
	clip.setValue(0);
	param.swapStateBasicForSetup(&state);
	CHECK_EQUAL(100, clip.getOffset());
	CHECK_EQUAL(10, param.editNodes().getElement(0)->offset);
	preset = 0;
	CHECK_EQUAL(110, param.getCurrentValue());
	param.swapStateBasicForSetup(&state);
	param.swapStateBasicForSetup(&state);
	CHECK_EQUAL(110, param.getCurrentValue());
}

TEST(AutoParamTests, RelativeFileMetadataPreservesSaturatedCurveAndScalar) {
	clip.setValue(INT32_MAX - 100);
	CHECK(param.setNodeAtPos(0, INT32_MAX, false) >= 0);
	CHECK(param.setNodeAtPos(16, INT32_MAX - 50, true) >= 0);
	preset += 1000;
	param.grabValueFromPos(0, 32);
	int64_t scalar = param.getUnclampedClipValue();
	MemorySerializer legacy;
	MemorySerializer relative;
	param.writeToFile(legacy, true);
	param.writeRelativeToFile(relative);
	MemoryDeserializer reader;
	reader.bytes = legacy.bytes;
	CHECK(param.readFromFile(reader, 32) == Error::NONE);
	reader.bytes = relative.bytes;
	reader.position = 0;
	CHECK(param.readRelativeFromFile(reader));
	CHECK_EQUAL(scalar, param.getUnclampedClipValue());
	CHECK_EQUAL(100, param.editNodes().getElement(0)->offset);
	CHECK_EQUAL(50, param.editNodes().getElement(1)->offset);
	CHECK_EQUAL(100, param.getAutomationOffset());
	preset -= 1000;
	CHECK_EQUAL(INT32_MAX - 100, param.getClipValue());
	CHECK_EQUAL(INT32_MAX - 50, param.getValueAtPos(16, 32));
}

TEST(AutoParamTests, RelativeFileMetadataPreservesNegativeOvershootAndTruncatedTimeline) {
	clip.setValue(INT32_MIN + 100);
	CHECK(param.setNodeAtPos(0, INT32_MIN, false) >= 0);
	CHECK(param.setNodeAtPos(16, INT32_MIN + 50, true) >= 0);
	preset -= 1000;
	MemorySerializer legacy;
	MemorySerializer relative;
	param.writeToFile(legacy, true);
	param.writeRelativeToFile(relative);
	MemoryDeserializer reader;
	reader.bytes = legacy.bytes;
	CHECK(param.readFromFile(reader, 8) == Error::NONE);
	reader.bytes = relative.bytes;
	reader.position = 0;
	CHECK(param.readRelativeFromFile(reader));
	CHECK_EQUAL(1, param.editNodes().getNumElements());
	CHECK_EQUAL(-100, param.editNodes().getElement(0)->offset);
	preset += 1000;
	CHECK_EQUAL(INT32_MIN + 100, param.getClipValue());
	CHECK_EQUAL(INT32_MIN, param.getValueAtPos(0, 8));
}

TEST(AutoParamTests, CloningPreservesUnclampedScalar) {
	clip.setUnclampedValue(int64_t{INT32_MAX} + 1000);
	ClipParamValue destination;
	AutoParamData cloned;
	cloned.bindClipValue(&destination);
	cloned.cloneFrom(&param, true);
	CHECK_EQUAL(param.getUnclampedClipValue(), cloned.getUnclampedClipValue());
	cloned.deleteAutomationBasicForSetup();
}

TEST(AutoParamTests, SavingPresetRebasesClipsWithoutChangingScalarOrCurve) {
	addCurve();
	param.grabValueFromPos(8, 32);
	ClipParamValue saturated;
	saturated.bindPreset(&preset);
	saturated.setUnclampedValue(int64_t{INT32_MAX} + 500);
	int32_t unrelatedPreset = 50;
	ClipParamValue unrelated;
	unrelated.bindPreset(&unrelatedPreset);
	unrelated.setValue(60);
	int32_t saved = param.getCurrentValue();
	clip.preserveBeforePresetChange(&preset, saved);
	saturated.preserveBeforePresetChange(&preset, saved);
	unrelated.preserveBeforePresetChange(&preset, saved);
	preset = saved;
	CHECK_EQUAL(145, preset);
	CHECK_EQUAL(125, clip.getValue());
	CHECK_EQUAL(145, param.getCurrentValue());
	CHECK_EQUAL(10, param.editNodes().getElement(0)->offset);
	CHECK_EQUAL(int64_t{INT32_MAX} + 500, saturated.getUnclampedValue());
	CHECK_EQUAL(60, unrelated.getValue());
	clip.setValue(preset);
	CHECK_EQUAL(145, param.getClipValue());
	CHECK_EQUAL(165, param.getCurrentValue());
}

TEST(AutoParamTests, RelativeMetadataDoesNotCreateNodesExcludedByLegacyReadLimit) {
	addCurve();
	MemorySerializer legacy;
	MemorySerializer relative;
	param.writeToFile(legacy, true);
	param.writeRelativeToFile(relative);
	MemoryDeserializer reader;
	reader.bytes = legacy.bytes;
	CHECK(param.readFromFile(reader, 0) == Error::NONE);
	reader.bytes = relative.bytes;
	reader.position = 0;
	CHECK(param.readRelativeFromFile(reader));
	CHECK_EQUAL(125, param.getClipValue());
	CHECK_FALSE(param.isAutomated());
}

TEST(AutoParamTests, TruncatedRelativeHeaderDoesNotChangeLoadedValue) {
	MemoryDeserializer reader;
	reader.bytes = "0x000000000000007D00000000";
	CHECK_FALSE(param.readRelativeFromFile(reader));
	CHECK_EQUAL(125, param.getClipValue());
}

TEST(AutoParamTests, PresetSaveRetainsSerializedValueWhileAutomationAdvances) {
	addCurve();
	param.grabValueFromPos(0, 32);
	PresetSaveValues saved;
	MemorySerializer writer;
	// Collection writers record the value they serialized under their own ID.
	PresetSaveValues::record(nullptr, 7, param.writeToFile(writer, false));
	param.grabValueFromPos(16, 32);
	CHECK_EQUAL(155, param.getCurrentValue());
	CHECK(saved.error() == Error::NONE);
	CHECK(saved.find(nullptr, 7).has_value());
	CHECK_EQUAL(135, *saved.find(nullptr, 7));
	STRCMP_EQUAL("0x00000087", writer.bytes.c_str());
	CHECK_EQUAL(100, preset);
}

TEST(AutoParamTests, SavingSongDoesNotCaptureOrChangePresetBaseline) {
	addCurve();
	PresetSaveValues saved;
	MemorySerializer writer;
	// Only collection writers record, and only when not writing automation.
	param.writeToFile(writer, true);
	CHECK_FALSE(saved.find(nullptr, 0).has_value());
	CHECK_EQUAL(100, preset);
}

TEST(BoundParamDataTests, RelativeMetadataSurvivesCableLoadAndClone) {
	original.clipValue.setUnclampedValue(int64_t{INT32_MAX} + 1000);
	CHECK(original.param.setNodeAtPos(0, INT32_MAX - 30, false) >= 0);
	MemorySerializer legacy;
	MemorySerializer relative;
	original.param.writeToFile(legacy, true);
	original.param.writeRelativeToFile(relative);
	ClipParamValue temporary_scalar;
	AutoParamData temporary;
	temporary.bindClipValue(&temporary_scalar);
	MemoryDeserializer reader;
	reader.bytes = legacy.bytes;
	CHECK(temporary.readFromFile(reader, 32) == Error::NONE);
	reader.bytes = relative.bytes;
	reader.position = 0;
	CHECK(temporary.readRelativeFromFile(reader));
	copy.param.cloneFrom(&temporary, true);
	temporary.deleteAutomationBasicForSetup();
	CHECK_EQUAL(int64_t{INT32_MAX} + 1000, copy.clipValue.getUnclampedValue());
	CHECK_EQUAL(-30, copy.param.editNodes().getElement(0)->offset);
	preset -= 2000;
	CHECK_EQUAL(INT32_MAX - 1030, copy.param.getValueAtPos(0, 32));
}

TEST(AutoParamTests, PresetSaveSnapshotIndexesMultipleParametersAndKeepsLastWrittenValue) {
	// Only the identity of a collection is used; it is never dereferenced.
	std::array<int32_t, 2> owners{};
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
	const auto* first = reinterpret_cast<const ParamCollection*>(&owners[0]);
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
	const auto* second = reinterpret_cast<const ParamCollection*>(&owners[1]);
	PresetSaveValues saved;
	for (int32_t i = 63; i >= 0; --i) {
		PresetSaveValues::record(first, i, i * 10);
		PresetSaveValues::record(second, i, i * -10);
	}
	PresetSaveValues::record(first, 31, -123);
	CHECK(saved.error() == Error::NONE);
	for (int32_t i = 0; i < 64; ++i) {
		CHECK(saved.find(first, i).has_value());
		CHECK_EQUAL(i == 31 ? -123 : i * 10, *saved.find(first, i));
		CHECK_EQUAL(i * -10, *saved.find(second, i));
	}
	CHECK_FALSE(saved.find(first, 64).has_value());
}

TEST(AutoParamTests, RelativeMetadataRetainsValuesBeyondEndpointDifferences) {
	constexpr int64_t wide = int64_t{1} << 40;
	clip.setUnclampedValue(wide);
	CHECK(param.setNodeAtPos(0, 100, false) >= 0);
	param.editNodes().getElement(0)->offset = -wide;
	MemorySerializer legacy;
	MemorySerializer relative;
	param.writeToFile(legacy, true);
	param.writeRelativeToFile(relative);
	MemoryDeserializer reader;
	reader.bytes = legacy.bytes;
	CHECK(param.readFromFile(reader, 32) == Error::NONE);
	reader.bytes = relative.bytes;
	reader.position = 0;
	CHECK(param.readRelativeFromFile(reader));
	CHECK_EQUAL(wide, param.getUnclampedClipValue());
	CHECK_EQUAL(-wide, param.editNodes().getElement(0)->offset);
}

TEST(AutoParamTests, PackedNodeOffsetsRoundTripAndSaturate) {
	ParamNode node;
	for (int64_t value : {int64_t{0}, int64_t{-1}, int64_t{INT32_MIN} - INT32_MAX, int64_t{UINT32_MAX} + 1,
	                      ParamNodeOffset::kMin, ParamNodeOffset::kMax}) {
		node.offset = value;
		CHECK_EQUAL(value, static_cast<int64_t>(node.offset));
	}
	node.offset = INT64_MAX;
	CHECK_EQUAL(ParamNodeOffset::kMax, static_cast<int64_t>(node.offset));
	node.offset = INT64_MIN;
	CHECK_EQUAL(ParamNodeOffset::kMin, static_cast<int64_t>(node.offset));
	node.offset = ParamNodeOffset::kMax - 1;
	node.offset += 5;
	CHECK_EQUAL(ParamNodeOffset::kMax, static_cast<int64_t>(node.offset));
}

TEST(AutoParamTests, RelativeMetadataRejectsOffsetsBeyondNodeStorage) {
	CHECK(param.setNodeAtPos(0, 100, false) >= 0);
	MemorySerializer relative;
	param.writeRelativeToFile(relative);
	// Replace the node's 16-digit offset with a value node storage cannot hold.
	relative.bytes.replace(relative.bytes.size() - 16, 16, "0100000000000000");
	param.editNodes().getElement(0)->offset = 7;
	MemoryDeserializer reader;
	reader.bytes = relative.bytes;
	CHECK(param.readRelativeFromFile(reader));
	CHECK_EQUAL(7, static_cast<int64_t>(param.editNodes().getElement(0)->offset));
}
