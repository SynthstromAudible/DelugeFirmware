#include "conformance.h"
#include "rust_allocator.h"

#include "cppspec.hpp"

// Phase 3 of the allocator rewrite (docs/dev/allocator_implementation_plan.md):
// the Rust TLSF core (crates/deluge_alloc) run through its C ABI against the
// *same* allocator-agnostic conformance suite (tests/spec_memory/conformance.h)
// that the legacy C++ MemoryRegion passes. This proves the new core honors the
// 16-byte-alignment / non-overlap / data-integrity / realloc / drain invariants
// across the FFI boundary, apples-to-apples with the allocator it replaces.
//
// (The stealable/reclaim path has no analogue yet — that arrives with the slab
// cache pool in M2. Over-alignment is covered on the Rust side by proptest/fuzz.)

// clang-format off
describe deluge_alloc_conformance("deluge_alloc (Rust TLSF) conformance", $ {
	it("returns only 16-aligned pointers, never overlapping, preserving data", _{
		RustAllocator a;
		expect(a.valid()).to_be_true();
		conformance::Report r = conformance::runChurn(a, /*seed=*/1, /*iters=*/40000);
		expect(r.allocs).to_be_greater_than(0);
		expect(r.misaligned).to_equal(0);
		expect(r.overlaps).to_equal(0);
		expect(r.corrupted).to_equal(0);
	});

	it("preserves leading bytes across realloc", _{
		RustAllocator a;
		conformance::Report r = conformance::runChurn(a, /*seed=*/7, /*iters=*/40000);
		expect(r.reallocs).to_be_greater_than(0);
		expect(r.reallocLostData).to_equal(0);
	});

	it("survives a different seed identically", _{
		RustAllocator a;
		conformance::Report r = conformance::runChurn(a, /*seed=*/424242, /*iters=*/40000);
		expect(r.misaligned).to_equal(0);
		expect(r.overlaps).to_equal(0);
		expect(r.corrupted).to_equal(0);
		expect(r.reallocLostData).to_equal(0);
	});

	it("reclaims all freed space after a full drain (no capacity leak)", _{
		RustAllocator fresh;
		RustAllocator churned;
		conformance::Report r = conformance::runDrainCapacity(fresh, churned, /*seed=*/3, /*iters=*/40000);
		expect(r.pristineCapacity).to_be_greater_than(0);
		expect(r.drainedCapacity).to_equal(r.pristineCapacity);
	});
});

CPPSPEC_SPEC(deluge_alloc_conformance)
