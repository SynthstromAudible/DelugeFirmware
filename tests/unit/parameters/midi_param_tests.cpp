#include "CppUTest/TestHarness.h"
#include "mocks/attribute_io.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_vector.h"
#include "modulation/params/param_node.h"
#include <array>

TEST_GROUP(MIDIParamVectorTests) {
	std::array<int32_t, 256> preset;
	MIDIParamVector params;

	void setup() override {
		preset.fill(100);
		params.bindPresetValues(preset.data());
	}
	void teardown() override {
		for (int i = 0; i < params.getNumElements(); ++i)
			params.getElement(i)->clearAutomation();
	}
};

TEST(MIDIParamVectorTests, NewParameterUsesBoundPreset) {
	auto* entry = params.getOrCreateParamFromCC(74, 0, true);
	CHECK(entry);
	CHECK_EQUAL(100, entry->param.getCurrentValue());
}

TEST(MIDIParamVectorTests, AllKeysRemainUniqueAndSortedIncludingAbove127) {
	for (int cc = 255; cc >= 0; --cc)
		CHECK(params.getOrCreateParamFromCC(cc, 0, true));
	CHECK_EQUAL(256, params.getNumElements());
	for (int cc = 0; cc < 256; ++cc) {
		auto* found = params.getParamFromCC(cc);
		CHECK(found);
		CHECK_EQUAL(cc, found->cc);
		POINTERS_EQUAL(params.getElement(cc), found);
		POINTERS_EQUAL(found, params.getOrCreateParamFromCC(cc, 0, true));
	}
	CHECK_EQUAL(256, params.getNumElements());
}

TEST(MIDIParamVectorTests, InsertionRelocationPreservesScalarAndAutomationBinding) {
	auto* entry = params.getOrCreateParamFromCC(74);
	CHECK(entry);
	entry->clipValue.setValue(125);
	CHECK(entry->param.automation().setNodeAtPos(0, 135, false) >= 0);
	CHECK(entry->grabValueFromPos(0, 32));
	for (int cc = 255; cc >= 0; --cc)
		CHECK(params.getOrCreateParamFromCC(cc));
	entry = params.getParamFromCC(74);
	CHECK(entry);
	CHECK_EQUAL(125, entry->param.getClipValue());
	CHECK_EQUAL(135, entry->param.getCurrentValue());
	preset[74] += 20;
	CHECK_EQUAL(145, entry->param.getClipValue());
	CHECK_EQUAL(155, entry->param.getCurrentValue());
}

TEST(MIDIParamVectorTests, DeletionRelocationRebindsSurvivingParameter) {
	CHECK(params.getOrCreateParamFromCC(1));
	auto* entry = params.getOrCreateParamFromCC(74);
	CHECK(entry);
	entry->clipValue.setValue(125);
	CHECK(entry->param.automation().setNodeAtPos(0, 135, false) >= 0);
	CHECK(entry->grabValueFromPos(0, 32));
	params.deleteAtKey(1);
	entry = params.getParamFromCC(74);
	CHECK(entry);
	entry->clipValue.setValue(150);
	CHECK_EQUAL(160, entry->param.getCurrentValue());
	entry->clearAutomation();
	CHECK_EQUAL(150, entry->param.getCurrentValue());
}

TEST(MIDIParamVectorTests, RejectsOutOfRangeKeys) {
	POINTERS_EQUAL(nullptr, params.getOrCreateParamFromCC(-1));
	POINTERS_EQUAL(nullptr, params.getOrCreateParamFromCC(256));
	CHECK_EQUAL(0, params.getNumElements());
}

TEST(MIDIParamVectorTests, DetachedBackupSurvivesPresetDestructionAndRebinding) {
	auto* oldPreset = new std::array<int32_t, 256>;
	oldPreset->fill(200);
	params.bindPresetValues(oldPreset->data());
	auto* entry = params.getOrCreateParamFromCC(74);
	entry->clipValue.setValue(225);
	CHECK(entry->param.automation().setNodeAtPos(0, 235, false) >= 0);
	CHECK(entry->grabValueFromPos(0, 32));
	params.bindPresetValues(nullptr);
	delete oldPreset;
	CHECK_EQUAL(225, entry->param.getClipValue());
	CHECK_EQUAL(235, entry->param.getCurrentValue());
	params.bindPresetValues(preset.data());
	CHECK_EQUAL(225, entry->param.getClipValue());
	CHECK_EQUAL(235, entry->param.getCurrentValue());
	preset[74] += 10;
	CHECK_EQUAL(245, entry->param.getCurrentValue());
	CHECK_EQUAL(100, params.getOrCreateParamFromCC(75)->param.getCurrentValue());
}

TEST(MIDIParamVectorTests, SaturatedBackupSurvivesOwnerDestructionAndRebinding) {
	for (bool negative : {false, true}) {
		auto* original = new std::array<int32_t, 256>;
		original->fill(negative ? INT32_MAX : INT32_MIN);
		params.bindPresetValues(original->data());
		auto* entry = params.getOrCreateParamFromCC(74);
		entry->clipValue.setValue(negative ? INT32_MIN : INT32_MAX);
		CHECK(entry->param.automation().setNodeAtPos(0, 0, false) >= 0);
		int64_t node = entry->param.nodeAt(0)->offset;
		(*original)[74] = negative ? INT32_MIN : INT32_MAX;
		int64_t scalar = entry->clipValue.getUnclampedValue();
		params.bindPresetValues(nullptr);
		delete original;
		CHECK_EQUAL(scalar, entry->clipValue.getUnclampedValue());
		preset[74] = negative ? INT32_MIN : INT32_MAX;
		params.bindPresetValues(preset.data());
		CHECK_EQUAL(scalar, entry->clipValue.getUnclampedValue());
		preset[74] = negative ? INT32_MAX : INT32_MIN;
		CHECK_EQUAL(negative ? INT32_MIN : INT32_MAX, entry->param.getClipValue());
		CHECK_EQUAL(node, entry->param.nodeAt(0)->offset);
	}
}

TEST(MIDIParamVectorTests, RelativeMetadataSurvivesLoadAndVectorRelocation) {
	auto* entry = params.getOrCreateParamFromCC(74);
	CHECK(entry);
	entry->clipValue.setUnclampedValue(int64_t{INT32_MAX} + 1000);
	CHECK(entry->param.automation().setNodeAtPos(0, INT32_MAX - 30, false) >= 0);
	MemorySerializer legacy;
	MemorySerializer relative;
	entry->param.writeToFile(legacy, true);
	entry->param.writeRelativeToFile(relative);
	MemoryDeserializer reader;
	reader.bytes = legacy.bytes;
	CHECK(entry->readFromFile(reader, 32) == Error::NONE);
	reader.bytes = relative.bytes;
	reader.position = 0;
	CHECK(entry->readRelativeFromFile(reader));
	for (int cc = 255; cc >= 0; --cc) {
		CHECK(params.getOrCreateParamFromCC(cc));
	}
	entry = params.getParamFromCC(74);
	CHECK_EQUAL(int64_t{INT32_MAX} + 1000, entry->clipValue.getUnclampedValue());
	CHECK_EQUAL(-30, entry->param.nodeAt(0)->offset);
	preset[74] -= 2000;
	CHECK_EQUAL(INT32_MAX - 1030, entry->param.getValueAtPos(0, 32));
}

TEST(MIDIParamVectorTests, ControlReadDoesNotCreateMissingCCOrAllocateAutomation) {
	using deluge::modulation::params::ControlLayer;
	for (auto layer : {ControlLayer::PRESET, ControlLayer::CLIP, ControlLayer::LIVE}) {
		CHECK_EQUAL(100, params.getValueForControl(74, layer));
		CHECK_EQUAL(0, params.getNumElements());
	}
	CHECK_EQUAL(0, params.getValueForControl(-1, ControlLayer::PRESET));
	CHECK_EQUAL(0, params.getValueForControl(256, ControlLayer::CLIP));
}

TEST(MIDIParamVectorTests, ControlReadDistinguishesBaselineScalarAndAutomation) {
	using deluge::modulation::params::ControlLayer;
	auto* entry = params.getOrCreateParamFromCC(74);
	CHECK(entry);
	entry->clipValue.setValue(125);
	CHECK(entry->param.automation().setNodeAtPos(0, 145, false) >= 0);
	CHECK(entry->grabValueFromPos(0, 32));
	CHECK_EQUAL(100, params.getValueForControl(74, ControlLayer::PRESET));
	CHECK_EQUAL(125, params.getValueForControl(74, ControlLayer::CLIP));
	CHECK_EQUAL(145, params.getValueForControl(74, ControlLayer::LIVE));
}
