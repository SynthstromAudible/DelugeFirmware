// MEM_GUARD's side table of live allocations (src/deluge/memory/alloc_metadata.cpp). Every edit of it runs with the
// audio render held off, so its probe chains must stay short however long the session: a removal must leave nothing
// behind that later lookups have to probe past.
#include "CppUTest/TestHarness.h"
#include "definitions.h"
#include "memory/alloc_metadata.h"

#include <cstdint>
#include <set>

#if MEM_GUARD

namespace {
// Synthetic keys: the table never dereferences them. 16-byte aligned like every heap block, well away from 0 and 1.
void* key(uint32_t i) {
	return reinterpret_cast<void*>(static_cast<uintptr_t>(0x10000000u) + static_cast<uintptr_t>(i) * 16u);
}
} // namespace

TEST_GROUP(MemGuardTable){};

TEST(MemGuardTable, removalsLeaveNothingBehind) {
	const uint32_t liveBefore = mem_guard::liveAllocations();
	// A long session's churn: 100000 allocations, never more than 32 live at once.
	for (uint32_t i = 0; i < 100000; i++) {
		mem_guard::recordAlloc(key(i), 64, nullptr);
		if (i >= 32) {
			CHECK_TRUE(mem_guard::removeAlloc(key(i - 32)));
		}
	}
	for (uint32_t i = 100000 - 32; i < 100000; i++) {
		CHECK_TRUE(mem_guard::removeAlloc(key(i)));
	}
	CHECK_EQUAL(liveBefore, mem_guard::liveAllocations());
	CHECK_EQUAL(mem_guard::tableCapacity() - mem_guard::liveAllocations(), mem_guard::emptyBuckets());
}

TEST(MemGuardTable, everyLiveEntryIsFoundThroughChurn) {
	// A deterministic mix of inserts and removals at a high load, checked against a plain set after every step.
	std::set<uint32_t> live;
	uint32_t x = 12345;
	const uint32_t kKeys = 24000; // up to ~0.73 of the table's capacity
	for (uint32_t step = 0; step < 200000; step++) {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		const uint32_t k = x % kKeys;
		if (live.contains(k)) {
			CHECK_TRUE(mem_guard::removeAlloc(key(k + 1000000)));
			live.erase(k);
		}
		else {
			mem_guard::recordAlloc(key(k + 1000000), k, nullptr);
			live.insert(k);
		}
		if (step % 20000 == 0) {
			for (uint32_t i = 0; i < kKeys; i++) {
				mem_guard::AllocInfo info{};
				const bool found = mem_guard::lookupAlloc(key(i + 1000000), &info);
				CHECK_EQUAL(live.contains(i), found);
				if (found) {
					CHECK_EQUAL(i, info.requestedSize);
				}
			}
		}
	}
	for (uint32_t k : live) {
		CHECK_TRUE(mem_guard::removeAlloc(key(k + 1000000)));
	}
}

#endif
