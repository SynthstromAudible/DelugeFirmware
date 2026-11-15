// MEM_GUARD's side table of live allocations (src/deluge/memory/alloc_metadata.cpp). Every edit of it runs with the
// audio render held off, so its probe chains must stay short however long the session: a removal must leave nothing
// behind that later lookups have to probe past.
#include "definitions.h"
#include "memory/alloc_metadata.h"

#include <cstdint>
#include <cstdlib>
#include <set>
#include <utility>
#include <vector>

#include "cppspec.hpp"
#include "support/spec_main.h"

#if MEM_GUARD

namespace {
// Synthetic keys: the table never dereferences them. 16-byte aligned like every heap block, well away from 0 and 1.
void* key(uint32_t i) {
	return reinterpret_cast<void*>(static_cast<uintptr_t>(0x10000000u) + static_cast<uintptr_t>(i) * 16u);
}
} // namespace

// One expect per check, over the keys that fail it: CppSpec keeps a result per expect, and these loops check hundreds
// of thousands of times.
// clang-format off
describe mem_guard_table_group("MEM_GUARD's side table", ${
	it("removals leave nothing behind", _ {
		const uint32_t liveBefore = mem_guard::liveAllocations();
		// A long session's churn: 100000 allocations, never more than 32 live at once.
		std::vector<uint32_t> notRemoved;
		for (uint32_t i = 0; i < 100000; i++) {
			mem_guard::recordAlloc(key(i), 64, nullptr);
			if (i >= 32 && !mem_guard::removeAlloc(key(i - 32))) {
				notRemoved.push_back(i - 32);
			}
		}
		expect(notRemoved).to_equal(std::vector<uint32_t>{});
		std::vector<uint32_t> lastNotRemoved;
		for (uint32_t i = 100000 - 32; i < 100000; i++) {
			if (!mem_guard::removeAlloc(key(i))) {
				lastNotRemoved.push_back(i);
			}
		}
		expect(lastNotRemoved).to_equal(std::vector<uint32_t>{});
		expect(mem_guard::liveAllocations()).to_equal(liveBefore);
		expect(mem_guard::emptyBuckets()).to_equal(mem_guard::tableCapacity() - mem_guard::liveAllocations());
	});

	it("every live entry is found through churn", _ {
		// A deterministic mix of inserts and removals at a high load, checked against a plain set after every step.
		std::set<uint32_t> live;
		uint32_t x = 12345;
		const uint32_t kKeys = 24000; // up to ~0.73 of the table's capacity
		std::vector<uint32_t> notRemoved;
		std::vector<uint32_t> misfound;
		std::vector<std::pair<uint32_t, uint32_t>> missized; // (key, requestedSize found)
		std::vector<uint32_t> notRemovedAtEnd;
		for (uint32_t step = 0; step < 200000; step++) {
			x ^= x << 13;
			x ^= x >> 17;
			x ^= x << 5;
			const uint32_t k = x % kKeys;
			if (live.contains(k)) {
				if (!mem_guard::removeAlloc(key(k + 1000000))) {
					notRemoved.push_back(k);
				}
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
					if (found != live.contains(i)) {
						misfound.push_back(i);
					}
					if (found && info.requestedSize != i) {
						missized.emplace_back(i, info.requestedSize);
					}
				}
			}
		}
		for (uint32_t k : live) {
			if (!mem_guard::removeAlloc(key(k + 1000000))) {
				notRemovedAtEnd.push_back(k);
			}
		}
		expect(notRemoved).to_equal(std::vector<uint32_t>{});
		expect(misfound).to_equal(std::vector<uint32_t>{});
		expect(missized).to_equal(std::vector<std::pair<uint32_t, uint32_t>>{});
		expect(notRemovedAtEnd).to_equal(std::vector<uint32_t>{});
	});
});

DELUGE_SPEC(mem_guard_spec, mem_guard_table_group)
#elif defined(__SANITIZE_ADDRESS__) || defined(DELUGE_VALGRIND)
// MEM_GUARD is off under a sanitizer (definitions.h), which owns heap-corruption detection there: there is no side
// table to check.
int mem_guard_spec(int, char**) {
	return EXIT_SUCCESS;
}
#else
#error "MEM_GUARD is off outside a sanitizer (ALPHA_OR_BETA_VERSION is 0): the side table's spec would check nothing"
#endif
