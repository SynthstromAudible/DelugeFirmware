#include "mock_source.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"

// The Rust resource manager (src/storage/resource) driven through its C ABI
// (deluge_resource.h) from C++ — the same cache invariants the Rust unit tests
// assert, validated across the FFI boundary apples-to-apples with how the firmware
// will call it. See docs/dev/resource_manager.md.

constexpr size_t kChunk = 64 * 1024;
constexpr size_t kSmallHeap = 256 * 1024; // ~3 chunks → forces eviction

// clang-format off
describe deluge_resource_conformance("resource manager (Rust cache) conformance", $ {
	it("materializes a chunk and serves cache hits from the same pointer", _{
		ResourceFixture f;
		expect(f.valid()).to_be_true();
		void* owner = reinterpret_cast<void*>(1);
		uint32_t a = f.define_asset(owner);
		void* p = f.acquire(a, 0, 4096);
		expect(p).not_().to_be_null();
		expect(reinterpret_cast<uintptr_t>(p) % 16).to_equal(0);
		expect(ResourceFixture::pattern_ok(p, owner, 0, 4096)).to_be_true();
		void* p2 = f.acquire(a, 0, 4096); // cache hit
		expect(p).to_equal(p2);
	});

	it("never evicts a leased chunk, and keeps its bytes under churn", _{
		ResourceFixture f(kSmallHeap);
		void* owner = reinterpret_cast<void*>(7);
		uint32_t a = f.define_asset(owner);
		void* pinned = f.acquire(a, 0, kChunk); // lease held (not released)
		expect(pinned).not_().to_be_null();
		f.churn(a, 1, 20, kChunk);
		expect(ResourceFixture::pattern_ok(pinned, owner, 0, kChunk)).to_be_true();
	});

	it("re-materializes the correct bytes after an eviction", _{
		ResourceFixture f(kSmallHeap);
		void* owner = reinterpret_cast<void*>(3);
		uint32_t a = f.define_asset(owner);
		void* p0 = f.acquire(a, 0, kChunk);
		f.release(p0);
		f.churn(a, 1, 20, kChunk);
		to_be_at_least(expect(evict_count()), 1);
		void* again = f.acquire(a, 0, kChunk);
		expect(again).not_().to_be_null();
		expect(ResourceFixture::pattern_ok(again, owner, 0, kChunk)).to_be_true();
	});

	it("never evicts a dirty (unsaved) chunk", _{
		ResourceFixture f(kSmallHeap);
		void* owner = reinterpret_cast<void*>(9);
		uint32_t a = f.define_asset(owner);
		void* d = f.acquire(a, 0, kChunk);
		f.mark_dirty(d, true);
		f.release(d); // unleased, but dirty
		f.churn(a, 1, 20, kChunk);
		void* d2 = f.acquire(a, 0, kChunk); // still resident → same pointer
		expect(d).to_equal(d2);
	});

	it("fails cleanly (null, no eviction) when everything is leased", _{
		ResourceFixture f(192 * 1024);
		void* owner = reinterpret_cast<void*>(5);
		uint32_t a = f.define_asset(owner);
		int held = 0;
		for (uint32_t n = 0; n < 100; n++) {
			if (f.acquire(a, n, kChunk) == nullptr) {
				break;
			}
			held++; // leave leased
		}
		to_be_at_least(expect(held), 1);
		evict_count() = 0;
		void* p = f.acquire(a, 999, kChunk);
		expect(p).to_be_null();
		expect(evict_count()).to_equal(0);
	});

	it("rolls back to null when reconstruction fails", _{
		ResourceFixture f;
		uint32_t a = f.define_asset(reinterpret_cast<void*>(1), failing_materialize);
		void* p = f.acquire(a, 0, 4096);
		expect(p).to_be_null();
		// The slot was reclaimed — a working asset can still acquire afterwards.
		uint32_t b = f.define_asset(reinterpret_cast<void*>(2));
		void* q = f.acquire(b, 0, 4096);
		expect(q).not_().to_be_null();
	});
});

CPPSPEC_SPEC(deluge_resource_conformance)
