#include "conformance.h"
#include "memory/memory_region.h"
#include "memory_region_allocator.h"
#include "model/sample/sample.h"
#include "storage/cluster/cluster.h"
#include "storage/wave_table/wave_table.h"

#include "cppspec.hpp"

// Phase 0 of the allocator rewrite (docs/dev/allocator_implementation_plan.md):
// the allocator-agnostic conformance suite. The portable invariants
// (conformance.h) run against the current MemoryRegion here, and will run
// unchanged against the Rust TLSF/slab core once it lands behind deluge_alloc.
// A separate section exercises the MemoryRegion-specific stealable/reclaim path.

namespace {

// Count steals + reclaimed bytes for the stealable section.
uint32_t nSteals;
uint32_t totalAllocated;
uint32_t vtableAddress;

uint32_t allocatedSizeOf(void* address) {
	return *(reinterpret_cast<uint32_t*>(address) - 1) & SPACE_SIZE_MASK;
}

class StealableTest : public Stealable {
public:
	void steal(char const* errorCode) override {
		nSteals += 1;
		totalAllocated -= allocatedSizeOf(this);
	}
	bool mayBeStolen(void* thingNotToStealFrom) override { return true; }
	StealableQueue getAppropriateQueue() override { return StealableQueue{0}; }
	int32_t testIndex;
};

} // namespace

// clang-format off
describe allocator_conformance("allocator conformance (MemoryRegion)", $ {
	it("returns only 16-aligned pointers, never overlapping, preserving data", _{
		MemoryRegionAllocator a;
		conformance::Report r = conformance::runChurn(a, /*seed=*/1, /*iters=*/40000);
		expect(r.allocs).to_be_greater_than(0);
		expect(r.misaligned).to_equal(0);
		expect(r.overlaps).to_equal(0);
		expect(r.corrupted).to_equal(0);
	});

	it("preserves leading bytes across realloc", _{
		MemoryRegionAllocator a;
		conformance::Report r = conformance::runChurn(a, /*seed=*/7, /*iters=*/40000);
		expect(r.reallocs).to_be_greater_than(0);
		expect(r.reallocLostData).to_equal(0);
	});

	it("survives a different seed identically", _{
		MemoryRegionAllocator a;
		conformance::Report r = conformance::runChurn(a, /*seed=*/424242, /*iters=*/40000);
		expect(r.misaligned).to_equal(0);
		expect(r.overlaps).to_equal(0);
		expect(r.corrupted).to_equal(0);
		expect(r.reallocLostData).to_equal(0);
	});

	it("reclaims all freed space after a full drain (no capacity leak)", _{
		MemoryRegionAllocator fresh;
		MemoryRegionAllocator churned;
		conformance::Report r = conformance::runDrainCapacity(fresh, churned, /*seed=*/3, /*iters=*/40000);
		expect(r.pristineCapacity).to_be_greater_than(0);
		// Coalescing must return all churned space: drained capacity matches pristine.
		expect(r.drainedCapacity).to_equal(r.pristineCapacity);
	});

	context("stealable / reclaim path (MemoryRegion-specific)", _{
		it("evicts cold stealables under pressure instead of failing", _{
			MemoryRegionAllocator a;
			nSteals = 0;
			totalAllocated = 0;
			int32_t sizes[3] = {sizeof(Sample), sizeof(WaveTable), sizeof(Cluster) + (1 << 15)};
			srand(1);
			const int kAllocs = 1024;
			for (int i = 0; i < kAllocs; i++) {
				uint32_t size = sizes[2];
				if (i % 10 == 0) {
					size = sizes[rand() % 2];
				}
				void* p = a.region().alloc(size, /*makeStealable=*/true, nullptr);
				expect(p != nullptr).to_be_true();
				totalAllocated += size;
				auto* stealable = new (p) StealableTest();
				a.region().cache_manager().QueueForReclamation(StealableQueue{0}, stealable);
				vtableAddress = *reinterpret_cast<uint32_t*>(p);
				uint32_t actual = allocatedSizeOf(p);
				expect((*(reinterpret_cast<uint32_t*>(p) - 1) & SPACE_TYPE_MASK) == SPACE_HEADER_STEALABLE)
				    .to_be_true();
				(void)actual;
			}
			// Pressure forced evictions, and the running live total stayed near the arena (efficient).
			expect(nSteals).to_be_greater_than(0);
			float efficiency = float(totalAllocated) / float(a.arena_size());
			expect(efficiency).to_be_greater_than(0.99f);
		});
	});
});

CPPSPEC_SPEC(allocator_conformance)
