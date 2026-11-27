#include "CppUTest/TestHarness.h"
#include "memory/general_memory_allocator.h"
#include "mocks/memory.h"
#include "modulation/params/param_manager.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "util/algorithm/quick_sorter.h"
#include <array>
#include <cstdint>

namespace {
using namespace parameter_test;
// Integer addresses let the test check alignment and disjoint ranges.
uintptr_t addressValue(void const* pointer) {
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
	return reinterpret_cast<uintptr_t>(pointer);
}
// Assertion macro expansion accounts for the reported branching.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void checkPatchedLayout(ParamManager& manager) {
	CHECK_EQUAL(3, manager.getExpressionParamSetOffset());
	auto* unpatched = manager.getUnpatchedParamSet();
	auto* patched = manager.getPatchedParamSet();
	auto* cables = manager.getPatchCableSet();
	CHECK(ownsBlock(unpatched));
	CHECK(!ownsBlock(patched));
	CHECK(!ownsBlock(cables));
	CHECK(contains(unpatched, sizeof(*unpatched)));
	CHECK(contains(patched, sizeof(*patched)));
	CHECK(contains(cables, sizeof(*cables)));
	CHECK_EQUAL(0, addressValue(unpatched) % alignof(UnpatchedParamSet));
	CHECK_EQUAL(0, addressValue(patched) % alignof(PatchedParamSet));
	CHECK_EQUAL(0, addressValue(cables) % alignof(PatchCableSet));
	CHECK(addressValue(unpatched) + sizeof(*unpatched) <= addressValue(patched));
	CHECK(addressValue(patched) + sizeof(*patched) <= addressValue(cables));
}
void addNode(AutoParam& param, int32_t value) {
	CHECK(param.setNodeAtPos(0, value, false) >= 0);
}
// Give every concrete collection owned data. The real destructors must release
// these allocations; clearing the manager's pointers alone cannot pass the test.
void populateCollections(ParamManager& manager) {
	addNode(manager.getUnpatchedParamSet()->params[0], 11);
	addNode(manager.getPatchedParamSet()->params[0], 22);
	auto* cables = manager.getPatchCableSet();
	cables->numUsablePatchCables = cables->numPatchCables = 1;
	addNode(cables->patchCables[0].param, 33);
}
} // namespace

// CppUTest macros generate owning factories, static registration, and assertion
// branches. These diagnostics describe framework expansion rather than test logic.
// NOLINTBEGIN(cppcoreguidelines-owning-memory,bugprone-throwing-static-initialization,readability-function-cognitive-complexity)
TEST_GROUP(ParamManagerTests){};

TEST(ParamManagerTests, PatchedSetupUsesOneAlignedAllocation) {
	ParamManager manager;
	CHECK(manager.setupWithPatching() == Error::NONE);
	CHECK_EQUAL(1, allocationAttempts());
	CHECK_EQUAL(1, liveAllocations());
	checkPatchedLayout(manager);
	manager.getUnpatchedParamSet()->params[0].setCurrentValueBasicForSetup(123);
	manager.getPatchedParamSet()->params[0].setCurrentValueBasicForSetup(456);
	CHECK_EQUAL(123, manager.getUnpatchedParamSet()->getValue(0));
	CHECK_EQUAL(456, manager.getPatchedParamSet()->getValue(0));
	CHECK_EQUAL(0, manager.getPatchCableSet()->numPatchCables);
}

TEST(ParamManagerTests, DestructionReleasesAllCollectionsAndSharedBlock) {
	{
		ParamManager manager;
		CHECK(manager.setupWithPatching() == Error::NONE);
		populateCollections(manager);
		CHECK(manager.ensureExpressionParamSetExists());
		addNode(manager.getExpressionParamSet()->params[0], 44);
		// The concrete PatchCableSet destructor also owns destination tables.
		for (auto& destination : manager.getPatchCableSet()->destinations) {
			destination = static_cast<Destination*>(GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(Destination)));
			CHECK(destination != nullptr);
		}
		CHECK(liveAllocations() > 2);
	}
	CHECK_EQUAL(0, liveAllocations());
	CHECK_EQUAL(allocationAttempts(), freeCount());
}

TEST(ParamManagerTests, RepeatedSetupPreservesExpressionAndReleasesOldCollections) {
	ParamManager manager;
	CHECK(manager.setupWithPatching() == Error::NONE);
	populateCollections(manager);
	CHECK(manager.ensureExpressionParamSetExists());
	auto* expression = manager.getExpressionParamSet();
	expression->params[0].setCurrentValueBasicForSetup(77);
	manager.getExpressionParamSetSummary()->whichParamsAreAutomated[0] = 1;
	CHECK(manager.setupUnpatched() == Error::NONE);
	CHECK_EQUAL(2, liveAllocations());
	CHECK_EQUAL(1, manager.getExpressionParamSetOffset());
	POINTERS_EQUAL(expression, manager.getExpressionParamSet());
	CHECK(manager.setupWithPatching() == Error::NONE);
	CHECK_EQUAL(2, liveAllocations());
	POINTERS_EQUAL(expression, manager.getExpressionParamSet());
	CHECK_EQUAL(77, expression->getValue(0));
	CHECK_EQUAL(1, manager.getExpressionParamSetSummary()->whichParamsAreAutomated[0]);
	checkPatchedLayout(manager);
}

TEST(ParamManagerTests, RemovingMainCollectionsRetainsExpression) {
	ParamManager manager;
	CHECK(manager.setupWithPatching() == Error::NONE);
	populateCollections(manager);
	CHECK(manager.ensureExpressionParamSetExists());
	auto* expression = manager.getExpressionParamSet();
	addNode(expression->params[0], 44);
	manager.destructAndForgetMainParamCollections();
	CHECK(!manager.containsAnyMainParamCollections());
	POINTERS_EQUAL(expression, manager.getExpressionParamSet());
	CHECK_EQUAL(44, expression->params[0].nodes.getFirst()->value);
	manager.destructAndForgetParamCollections();
	CHECK_EQUAL(0, liveAllocations());
	// Destruction after explicit cleanup must be harmless.
}

TEST(ParamManagerTests, CloneOwnsIndependentCollectionsAndAutomation) {
	ParamManager source;
	CHECK(source.setupWithPatching() == Error::NONE);
	populateCollections(source);
	CHECK(source.ensureExpressionParamSetExists());
	addNode(source.getExpressionParamSet()->params[0], 44);
	size_t original_live = liveAllocations();
	{
		ParamManager clone;
		CHECK(clone.cloneParamCollectionsFrom(&source, true, true) == Error::NONE);
		checkPatchedLayout(clone);
		CHECK(clone.getUnpatchedParamSet() != source.getUnpatchedParamSet());
		CHECK(clone.getExpressionParamSet() != source.getExpressionParamSet());
		CHECK_EQUAL(11, clone.getUnpatchedParamSet()->params[0].nodes.getFirst()->value);
		CHECK_EQUAL(22, clone.getPatchedParamSet()->params[0].nodes.getFirst()->value);
		CHECK_EQUAL(33, clone.getPatchCableSet()->patchCables[0].param.nodes.getFirst()->value);
		CHECK_EQUAL(44, clone.getExpressionParamSet()->params[0].nodes.getFirst()->value);
		clone.getPatchedParamSet()->params[0].nodes.getFirst()->value = 99;
		CHECK_EQUAL(22, source.getPatchedParamSet()->params[0].nodes.getFirst()->value);
	}
	CHECK_EQUAL(original_live, liveAllocations());
	CHECK_EQUAL(11, source.getUnpatchedParamSet()->params[0].nodes.getFirst()->value);
}

TEST(ParamManagerTests, CloneAllocatesOneMainBlockAndSeparateExpressionBlock) {
	ParamManager source;
	CHECK(source.setupWithPatching() == Error::NONE);
	CHECK(source.ensureExpressionParamSetExists());
	ParamManager clone;
	size_t before = allocationAttempts();
	CHECK(clone.cloneParamCollectionsFrom(&source, true, true) == Error::NONE);
	CHECK_EQUAL(2, allocationAttempts() - before);
	CHECK(ownsBlock(clone.getExpressionParamSet()));
	checkPatchedLayout(clone);
}

TEST(ParamManagerTests, CloneReplacesDestinationMainCollectionsButRetainsExpression) {
	ParamManager source;
	CHECK(source.setupUnpatched() == Error::NONE);
	source.getUnpatchedParamSet()->params[0].setCurrentValueBasicForSetup(55);
	CHECK(source.ensureExpressionParamSetExists());
	ParamManager destination;
	CHECK(destination.setupWithPatching() == Error::NONE);
	populateCollections(destination);
	CHECK(destination.ensureExpressionParamSetExists());
	auto* expression = destination.getExpressionParamSet();
	CHECK(destination.cloneParamCollectionsFrom(&source, true, true) == Error::NONE);
	POINTERS_EQUAL(expression, destination.getExpressionParamSet());
	CHECK_EQUAL(55, destination.getUnpatchedParamSet()->getValue(0));
	CHECK_EQUAL(4, liveAllocations());
	CHECK_EQUAL(1, destination.getExpressionParamSetOffset());
	for (int i = 2; i < PARAM_COLLECTIONS_STORAGE_NUM; ++i) {
		POINTERS_EQUAL(nullptr, destination.summaries[i].paramCollection);
	}
}

TEST(ParamManagerTests, StealReleasesDestinationAndKeepsBothExpressionsByDefault) {
	ParamManager source;
	CHECK(source.setupWithPatching() == Error::NONE);
	populateCollections(source);
	CHECK(source.ensureExpressionParamSetExists());
	auto* source_main = source.getUnpatchedParamSet();
	auto* source_expression = source.getExpressionParamSet();
	ParamManager destination;
	CHECK(destination.setupWithPatching() == Error::NONE);
	populateCollections(destination);
	CHECK(destination.ensureExpressionParamSetExists());
	auto* destination_expression = destination.getExpressionParamSet();
	size_t before = allocationAttempts();
	destination.stealParamCollectionsFrom(&source);
	CHECK_EQUAL(before, allocationAttempts());
	POINTERS_EQUAL(source_main, destination.getUnpatchedParamSet());
	POINTERS_EQUAL(source_expression, source.getExpressionParamSet());
	POINTERS_EQUAL(destination_expression, destination.getExpressionParamSet());
	CHECK(!source.containsAnyMainParamCollections());
	CHECK_EQUAL(33, destination.getPatchCableSet()->patchCables[0].param.nodes.getFirst()->value);
	source.destructAndForgetParamCollections();
	CHECK_EQUAL(11, destination.getUnpatchedParamSet()->params[0].nodes.getFirst()->value);
}

TEST(ParamManagerTests, StealTransfersExpressionWhenDestinationHasNone) {
	ParamManager source;
	CHECK(source.setupWithPatching() == Error::NONE);
	CHECK(source.ensureExpressionParamSetExists());
	auto* expression = source.getExpressionParamSet();
	ParamManager destination;
	destination.stealParamCollectionsFrom(&source, true);
	POINTERS_EQUAL(expression, destination.getExpressionParamSet());
	CHECK(!source.containsAnyParamCollectionsIncludingExpression());
	CHECK_EQUAL(2, liveAllocations());
}

TEST(ParamManagerTests, StealDiscardsSourceExpressionWhenDestinationAlreadyHasOne) {
	ParamManager source;
	CHECK(source.setupWithPatching() == Error::NONE);
	CHECK(source.ensureExpressionParamSetExists());
	addNode(source.getExpressionParamSet()->params[0], 88);
	ParamManager destination;
	CHECK(destination.ensureExpressionParamSetExists());
	auto* expression = destination.getExpressionParamSet();
	destination.stealParamCollectionsFrom(&source, true);
	POINTERS_EQUAL(expression, destination.getExpressionParamSet());
	CHECK(!source.containsAnyParamCollectionsIncludingExpression());
	CHECK_EQUAL(2, liveAllocations());
}

TEST(ParamManagerTests, SelfStealDoesNothing) {
	ParamManager manager;
	CHECK(manager.setupWithPatching() == Error::NONE);
	populateCollections(manager);
	auto* main = manager.getUnpatchedParamSet();
	size_t live = liveAllocations();
	manager.stealParamCollectionsFrom(&manager, true);
	POINTERS_EQUAL(main, manager.getUnpatchedParamSet());
	CHECK_EQUAL(live, liveAllocations());
	CHECK_EQUAL(0, freeCount());
}

TEST(ParamManagerTests, SetupFailurePreservesExistingCollections) {
	ParamManager manager;
	CHECK(manager.setupUnpatched() == Error::NONE);
	CHECK(manager.ensureExpressionParamSetExists());
	auto* main = manager.getUnpatchedParamSet();
	auto* expression = manager.getExpressionParamSet();
	failAllocationAfter(0);
	CHECK(manager.setupWithPatching() == Error::INSUFFICIENT_RAM);
	POINTERS_EQUAL(main, manager.getUnpatchedParamSet());
	POINTERS_EQUAL(expression, manager.getExpressionParamSet());
	CHECK_EQUAL(2, liveAllocations());
	CHECK_EQUAL(0, freeCount());
}

TEST(ParamManagerTests, ExpressionAllocationFailureLeavesMainCollectionsIntact) {
	ParamManager manager;
	CHECK(manager.setupWithPatching() == Error::NONE);
	failAllocationAfter(0);
	CHECK(!manager.ensureExpressionParamSetExists());
	POINTERS_EQUAL(nullptr, manager.getExpressionParamSet());
	CHECK_EQUAL(1, liveAllocations());
	checkPatchedLayout(manager);
}

TEST(ParamManagerTests, CloneAllocationFailuresPreserveSourceAndDestination) {
	// Fail either the main block or the subsequent expression allocation.
	for (size_t successful = 0; successful < 2; ++successful) {
		ParamManager source;
		CHECK(source.setupWithPatching() == Error::NONE);
		CHECK(source.ensureExpressionParamSetExists());
		ParamManager destination;
		CHECK(destination.setupUnpatched() == Error::NONE);
		auto* old_main = destination.getUnpatchedParamSet();
		size_t live = liveAllocations();
		failAllocationAfter(successful);
		CHECK(destination.cloneParamCollectionsFrom(&source, true, true) == Error::INSUFFICIENT_RAM);
		POINTERS_EQUAL(old_main, destination.getUnpatchedParamSet());
		CHECK_EQUAL(live, liveAllocations());
		checkPatchedLayout(source);
	}
}

TEST(ParamManagerTests, BeenClonedRepairsShallowCopyWithoutFreeingOriginal) {
	ParamManager source;
	CHECK(source.setupWithPatching() == Error::NONE);
	populateCollections(source);
	CHECK(source.ensureExpressionParamSetExists());
	size_t live = liveAllocations();
	{
		// Firmware uses this shallow-copy-then-beenCloned protocol for NoteRows.
		ParamManager clone = source;
		CHECK(clone.beenCloned() == Error::NONE);
		CHECK(clone.getUnpatchedParamSet() != source.getUnpatchedParamSet());
		CHECK(clone.getExpressionParamSet() != source.getExpressionParamSet());
		CHECK_EQUAL(22, clone.getPatchedParamSet()->params[0].nodes.getFirst()->value);
	}
	CHECK_EQUAL(live, liveAllocations());
	CHECK_EQUAL(22, source.getPatchedParamSet()->params[0].nodes.getFirst()->value);
}

TEST(ParamManagerTests, FailedBeenClonedClearsBorrowedPointers) {
	for (size_t successful = 0; successful < 2; ++successful) {
		ParamManager source;
		CHECK(source.setupWithPatching() == Error::NONE);
		CHECK(source.ensureExpressionParamSetExists());
		size_t live = liveAllocations();
		{
			ParamManager clone = source;
			failAllocationAfter(successful);
			CHECK(clone.beenCloned() == Error::INSUFFICIENT_RAM);
			CHECK(!clone.containsAnyParamCollectionsIncludingExpression());
			CHECK_EQUAL(0, clone.getExpressionParamSetOffset());
		}
		CHECK_EQUAL(live, liveAllocations());
		checkPatchedLayout(source);
	}
}

TEST(ParamManagerTests, MIDISetupReplacesPatchedBlockAndPreservesExpression) {
	ParamManager manager;
	CHECK(manager.setupWithPatching() == Error::NONE);
	populateCollections(manager);
	CHECK(manager.ensureExpressionParamSetExists());
	auto* expression = manager.getExpressionParamSet();
	CHECK(manager.setupMIDI() == Error::NONE);
	CHECK_EQUAL(1, manager.getExpressionParamSetOffset());
	CHECK_EQUAL(2, liveAllocations());
	POINTERS_EQUAL(expression, manager.getExpressionParamSet());
	ParamManager clone;
	CHECK(clone.cloneParamCollectionsFrom(&manager, true, true) == Error::NONE);
	CHECK_EQUAL(4, liveAllocations());
	CHECK(clone.getMIDIParamCollection() != manager.getMIDIParamCollection());
	CHECK(clone.getExpressionParamSet() != expression);
}

TEST(ParamManagerTests, EmptyAndExpressionOnlyManagersCanBeCloned) {
	ParamManager source;
	ParamManager destination;
	CHECK(destination.cloneParamCollectionsFrom(&source, true, true) == Error::NONE);
	CHECK_EQUAL(0, allocationAttempts());
	CHECK(source.ensureExpressionParamSetExists());
	addNode(source.getExpressionParamSet()->params[0], 66);
	CHECK(destination.cloneParamCollectionsFrom(&source, true, true) == Error::NONE);
	CHECK(!destination.containsAnyMainParamCollections());
	CHECK(destination.getExpressionParamSet() != source.getExpressionParamSet());
	CHECK_EQUAL(66, destination.getExpressionParamSet()->params[0].nodes.getFirst()->value);
}

TEST(ParamManagerTests, CloneWithoutAutomationKeepsValuesButDoesNotShareNodes) {
	ParamManager source;
	CHECK(source.setupWithPatching() == Error::NONE);
	populateCollections(source);
	source.getPatchedParamSet()->params[0].setCurrentValueBasicForSetup(99);
	ParamManager destination;
	CHECK(destination.cloneParamCollectionsFrom(&source, false) == Error::NONE);
	CHECK_EQUAL(99, destination.getPatchedParamSet()->getValue(0));
	CHECK(!destination.getUnpatchedParamSet()->params[0].isAutomated());
	CHECK(!destination.getPatchedParamSet()->params[0].isAutomated());
	CHECK(!destination.getPatchCableSet()->patchCables[0].param.isAutomated());
	CHECK_EQUAL(22, source.getPatchedParamSet()->params[0].nodes.getFirst()->value);
}

// The collection source links QuickSorter. Exercise its host pointer arithmetic
// with records so the test also checks that sorting preserves payloads.
TEST(ParamManagerTests, ProductionSorterKeepsRecordsTogether) {
	struct Record {
		uint32_t key;
		uint32_t payload;
	};
	std::array records = {Record{.key = 3, .payload = 30}, Record{.key = 1, .payload = 10},
	                      Record{.key = 2, .payload = 20}};
	QuickSorter sorter(sizeof(Record), 32, records.data());
	sorter.sort(3);
	for (uint32_t i = 0; i < 3; ++i) {
		CHECK_EQUAL(i + 1, records.at(i).key);
		CHECK_EQUAL((i + 1) * 10, records.at(i).payload);
	}
}

// NOLINTEND(cppcoreguidelines-owning-memory,bugprone-throwing-static-initialization,readability-function-cognitive-complexity)
