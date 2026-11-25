#include "CppUTest/TestHarness.h"
#include "modulation/params/param_node.h"
#include "modulation/params/param_node_vector.h"
#include <cstring>

TEST_GROUP(LazyParamNodeVectorTest){};

namespace {
void addNode(LazyParamNodeVector& nodes, int32_t pos, int32_t value) {
	int32_t i = nodes.insertAtKey(pos);
	CHECK(i >= 0);
	nodes.getElement(i)->offset = value;
}
} // namespace

TEST(LazyParamNodeVectorTest, isJustAPointer) {
	CHECK_EQUAL(sizeof(void*), sizeof(LazyParamNodeVector));
}

TEST(LazyParamNodeVectorTest, heldRampIsStoredBeforeTheCurveIsUsed) {
	LazyParamNodeVector nodes;
	addNode(nodes, 0, 10);
	ParamNodeVector* curve = nodes.peek();
	curve->value.setOffsetForBase(10, 100);
	curve->value.markCurrent();
	int32_t value_slot = 110;
	int32_t step_slot = 0;
	nodes.holdRamp(&value_slot, &step_slot, 3);
	CHECK_EQUAL(3, step_slot);
	value_slot = 150;
	// Node queries leave the ramp held.
	CHECK_EQUAL(1, nodes.getNumElements());
	CHECK_EQUAL(3, step_slot);
	CHECK_EQUAL(10, curve->value.getOffset());
	// Any other use of the curve stores it, relative to the base it was started from.
	CHECK(nodes.get() == curve);
	CHECK_EQUAL(0, step_slot);
	CHECK_EQUAL(50, curve->value.getOffset());
	CHECK_EQUAL(150, curve->value.currentValue());
	CHECK(curve->value.isCurrent());
	nodes.empty();
}

TEST(LazyParamNodeVectorTest, heldRampFollowsSwapsAndClones) {
	LazyParamNodeVector nodes;
	addNode(nodes, 0, 10);
	nodes.peek()->value.setOffsetForBase(0, 200);
	int32_t value_slot = 200;
	int32_t step_slot = 0;
	nodes.holdRamp(&value_slot, &step_slot, 1);
	value_slot = 260;

	// A memcpy clone takes its own copy of the curve at the held value; the original keeps holding.
	alignas(LazyParamNodeVector) unsigned char storage[sizeof(LazyParamNodeVector)];
	std::memcpy(storage, &nodes, sizeof(LazyParamNodeVector));
	auto* copy = reinterpret_cast<LazyParamNodeVector*>(storage);
	CHECK(copy->beenCloned() == Error::NONE);
	CHECK(copy->peek() != nodes.peek());
	CHECK(copy->peek()->heldStep_ == nullptr);
	CHECK_EQUAL(60, copy->peek()->value.getOffset());
	CHECK_EQUAL(1, step_slot);

	LazyParamNodeVector other;
	nodes.swapStateWith(&other);
	CHECK_EQUAL(0, nodes.getNumElements());
	CHECK_EQUAL(0, step_slot);
	CHECK_EQUAL(60, other.peek()->value.getOffset());
	other.empty();
	copy->empty();
}

TEST(LazyParamNodeVectorTest, destroyingTheOwnerDiscardsAHeldRamp) {
	int32_t value_slot = 5;
	int32_t step_slot = 0;
	{
		LazyParamNodeVector nodes;
		addNode(nodes, 0, 10);
		nodes.holdRamp(&value_slot, &step_slot, 2);
		// The owning collection is going too, so the slots are left alone.
		value_slot = 0;
	}
	CHECK_EQUAL(2, step_slot);
}

TEST(LazyParamNodeVectorTest, unallocatedBehavesLikeEmpty) {
	LazyParamNodeVector nodes;
	ParamNodeVector reference;

	POINTERS_EQUAL(nullptr, nodes.get());
	CHECK_EQUAL(0, nodes.getNumElements());
	POINTERS_EQUAL(nullptr, nodes.getElement(0));
	POINTERS_EQUAL(nullptr, nodes.getFirst());
	POINTERS_EQUAL(nullptr, nodes.getLast());
	CHECK_EQUAL(reference.search(100, GREATER_OR_EQUAL), nodes.search(100, GREATER_OR_EQUAL));
	CHECK_EQUAL(reference.search(100, LESS), nodes.search(100, LESS));
	CHECK_EQUAL(reference.searchExact(100), nodes.searchExact(100));
	CHECK(nodes.generateRepeats(96, 192));

	int32_t terms[2] = {10, 20};
	int32_t expected[2] = {-1, -1};
	int32_t actual[2] = {-1, -1};
	reference.searchDual(terms, expected);
	nodes.searchDual(terms, actual);
	CHECK_EQUAL(expected[0], actual[0]);
	CHECK_EQUAL(expected[1], actual[1]);

	nodes.deleteAtIndex(0); // Must be harmless
	POINTERS_EQUAL(nullptr, nodes.get());
}

TEST(LazyParamNodeVectorTest, allocatesOnInsertAndFreesWhenEmptied) {
	LazyParamNodeVector nodes;
	addNode(nodes, 48, 5);
	addNode(nodes, 0, 3);

	CHECK(nodes.get() != nullptr);
	CHECK_EQUAL(2, nodes.getNumElements());
	CHECK_EQUAL(0, nodes.getFirst()->pos);
	CHECK_EQUAL(48, nodes.getLast()->pos);
	CHECK_EQUAL(1, nodes.searchExact(48));

	nodes.deleteAtIndex(0, 2);
	POINTERS_EQUAL(nullptr, nodes.get());

	addNode(nodes, 12, 1);
	nodes.empty();
	POINTERS_EQUAL(nullptr, nodes.get());
}

TEST(LazyParamNodeVectorTest, keepsVectorWhenNotAllowedToShorten) {
	LazyParamNodeVector nodes;
	addNode(nodes, 0, 1);
	nodes.deleteAtIndex(0, 1, false);
	CHECK(nodes.get() != nullptr);
	CHECK_EQUAL(0, nodes.getNumElements());
}

TEST(LazyParamNodeVectorTest, cloneFromIsDeep) {
	LazyParamNodeVector original;
	addNode(original, 0, 7);
	addNode(original, 24, 9);

	LazyParamNodeVector clone;
	CHECK(clone.cloneFrom(&original));
	CHECK(clone.get() != original.get());
	CHECK_EQUAL(2, clone.getNumElements());
	CHECK_EQUAL(9, clone.getElement(1)->offset);

	clone.getElement(1)->offset = 100;
	CHECK_EQUAL(9, original.getElement(1)->offset);

	LazyParamNodeVector empty;
	LazyParamNodeVector cloneOfEmpty;
	CHECK(cloneOfEmpty.cloneFrom(&empty));
	POINTERS_EQUAL(nullptr, cloneOfEmpty.get());
}

TEST(LazyParamNodeVectorTest, beenClonedAfterMemcpyMakesIndependentCopy) {
	LazyParamNodeVector original;
	addNode(original, 0, 7);
	addNode(original, 24, 9);

	alignas(LazyParamNodeVector) unsigned char storage[sizeof(LazyParamNodeVector)];
	memcpy(storage, &original, sizeof(LazyParamNodeVector));
	auto* copy = reinterpret_cast<LazyParamNodeVector*>(storage);
	CHECK(copy->beenCloned() == Error::NONE);

	CHECK(copy->get() != nullptr);
	CHECK(copy->get() != original.get());
	CHECK_EQUAL(2, copy->getNumElements());
	CHECK_EQUAL(7, copy->getElement(0)->offset);
	CHECK_EQUAL(2, original.getNumElements());
	copy->empty();
}

TEST(LazyParamNodeVectorTest, swapStateWithSwapsOwnership) {
	LazyParamNodeVector a;
	LazyParamNodeVector b;
	addNode(a, 0, 1);
	ParamNodeVector* aVector = a.get();

	a.swapStateWith(&b);
	POINTERS_EQUAL(nullptr, a.get());
	POINTERS_EQUAL(aVector, b.get());
}

TEST(LazyParamNodeVectorTest, CurveSwapsAndDeletionDoNotMoveScalarBinding) {
	LazyParamNodeVector bound;
	LazyParamNodeVector unbound;
	bound.setHasClipValue(true);
	addNode(bound, 0, 10);
	addNode(unbound, 16, 20);
	bound.get()->renewedOverridingAtTime = 123;
	unbound.get()->renewedOverridingAtTime = 456;
	bound.swapStateWith(&unbound);
	CHECK(bound.hasClipValue());
	CHECK_FALSE(unbound.hasClipValue());
	CHECK_EQUAL(20, bound.getFirst()->offset);
	CHECK_EQUAL(456, bound.get()->renewedOverridingAtTime);
	CHECK_EQUAL(10, unbound.getFirst()->offset);
	bound.empty();
	CHECK(bound.hasClipValue());
	POINTERS_EQUAL(nullptr, bound.get());
	CHECK_EQUAL(0, bound.getNumElements());
}

TEST(LazyParamNodeVectorTest, CloneOwnsPlaybackStateAsWellAsNodes) {
	LazyParamNodeVector source;
	LazyParamNodeVector copy;
	addNode(source, 0, 10);
	source.get()->valueIncrementPerHalfTick = 4294967295LL;
	source.get()->renewedOverridingAtTime = 123;
	CHECK(copy.cloneFrom(&source));
	CHECK_EQUAL(4294967295LL, copy.get()->valueIncrementPerHalfTick);
	CHECK_EQUAL(123, copy.get()->renewedOverridingAtTime);
	copy.get()->renewedOverridingAtTime = 456;
	CHECK_EQUAL(123, source.get()->renewedOverridingAtTime);
}
