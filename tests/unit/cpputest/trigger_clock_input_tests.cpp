#include "CppUTest/TestHarness.h"
#include "playback/trigger_clock_input.h"

using deluge::playback::TriggerClockEdges;

TEST_GROUP(TriggerClockEdges){};

TEST(TriggerClockEdges, AnEmptyRingPopsNothing) {
	TriggerClockEdges edges;
	CHECK_FALSE(edges.pop().has_value());
}

TEST(TriggerClockEdges, EdgesPopInArrivalOrder) {
	TriggerClockEdges edges;
	edges.push(10);
	edges.push(20);
	CHECK_EQUAL(10u, edges.pop().value());
	CHECK_EQUAL(20u, edges.pop().value());
	CHECK_FALSE(edges.pop().has_value());
}

TEST(TriggerClockEdges, TwoPushesBeforeAPopBothArrive) {
	TriggerClockEdges edges;
	edges.push(7);
	edges.push(7);
	CHECK_EQUAL(7u, edges.pop().value());
	CHECK_EQUAL(7u, edges.pop().value());
	CHECK_EQUAL(0u, edges.dropped());
}

TEST(TriggerClockEdges, AFullRingDropsTheNewestAndCountsIt) {
	TriggerClockEdges edges;
	for (uint32_t i = 0; i < TriggerClockEdges::kCapacity + 2; i++) {
		edges.push(i);
	}
	CHECK_EQUAL(2u, edges.dropped());
	for (uint32_t i = 0; i < TriggerClockEdges::kCapacity; i++) {
		CHECK_EQUAL(i, edges.pop().value());
	}
	CHECK_FALSE(edges.pop().has_value());
}

TEST(TriggerClockEdges, TheRingKeepsWorkingPastCounterWrap) {
	TriggerClockEdges edges;
	for (uint32_t i = 0; i < 1000; i++) {
		edges.push(i);
		CHECK_EQUAL(i, edges.pop().value());
	}
}
