#pragma once

// Test fixture for the resource manager C ABI: a malloc-backed DelugeHeap + a
// DelugeResource manager + a mock Source whose "file contents" are a deterministic
// byte pattern per (owner, chunk index) so a reload is verifiable. Mirrors the Rust
// unit tests, but drives everything through the FFI boundary (deluge_resource.h).

#include "deluge_resource.h"
#include "libdeluge/alloc.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>

inline uint8_t pattern_byte(void* owner, uint32_t index) {
	return static_cast<uint8_t>(reinterpret_cast<uintptr_t>(owner) * 31u + index + 0x5Au);
}

// Single-threaded specs — a process-global eviction counter is fine.
inline int& evict_count() {
	static int n = 0;
	return n;
}

extern "C" inline bool mock_materialize(void* /*ctx*/, void* owner, uint32_t index, void* dest, size_t len) {
	std::memset(dest, pattern_byte(owner, index), len);
	return true;
}
extern "C" inline void mock_on_evict(void* /*ctx*/, void* /*owner*/, uint32_t /*index*/) {
	evict_count()++;
}
extern "C" inline bool failing_materialize(void*, void*, uint32_t, void*, size_t) {
	return false; // source vanished
}

class ResourceFixture {
public:
	explicit ResourceFixture(size_t arena = (1u << 20), size_t chunk_cap = 64) {
		raw_ = std::malloc(arena);
		heap_ = deluge_heap_create(raw_, arena);
		mgr_ = deluge_resource_create(heap_, /*asset_capacity=*/16, chunk_cap);
		evict_count() = 0;
	}
	~ResourceFixture() { std::free(raw_); }

	[[nodiscard]] bool valid() const { return mgr_ != nullptr; }

	uint32_t define_asset(void* owner, DelugeResourceMaterializeFn m = mock_materialize) {
		return deluge_resource_define_asset(mgr_, owner, m, mock_on_evict, nullptr, DELUGE_RESOURCE_COST_IO,
		                                    DELUGE_RESOURCE_BACKING_HEAP);
	}
	void* acquire(uint32_t a, uint32_t i, size_t sz) { return deluge_resource_acquire(mgr_, a, i, sz); }
	void release(void* p) { deluge_resource_release(mgr_, p); }
	void reference(uint32_t a) { deluge_resource_reference(mgr_, a); }
	void mark_dirty(void* p, bool d) { deluge_resource_mark_dirty(mgr_, p, d); }

	static bool pattern_ok(void* p, void* owner, uint32_t index, size_t len) {
		uint8_t want = pattern_byte(owner, index);
		auto* b = static_cast<uint8_t*>(p);
		for (size_t k = 0; k < len; k++) {
			if (b[k] != want) {
				return false;
			}
		}
		return true;
	}

	// Churn: acquire+release `count` distinct chunks of `asset` to drive eviction.
	void churn(uint32_t asset, uint32_t from, uint32_t count, size_t sz) {
		for (uint32_t n = from; n < from + count; n++) {
			void* p = acquire(asset, n, sz);
			if (p != nullptr) {
				release(p);
			}
		}
	}

private:
	void* raw_ = nullptr;
	DelugeHeap* heap_ = nullptr;
	DelugeResource* mgr_ = nullptr;
};
