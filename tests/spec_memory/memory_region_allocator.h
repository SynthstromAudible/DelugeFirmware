#pragma once

#include "allocator_under_test.h"
#include "definitions.h"
#include "memory/cache_manager.h"
#include "memory/memory_region.h"
#include <cstdlib>
#include <cstring>
#include <new>

// AllocatorUnderTest adapter over the current C++ `MemoryRegion` free-list
// allocator. Owns its own malloc-backed arena and empty-space metadata, so each
// instance is an isolated heap the conformance suite can churn and drain.
//
// MemoryRegion has no native realloc; this adapter synthesizes one
// (alloc-copy-free), which is a valid realloc and all the suite needs. It only
// guarantees 16-byte alignment (the free-list's structural floor), so
// max_align() == 16.
class MemoryRegionAllocator final : public AllocatorUnderTest {
public:
	explicit MemoryRegionAllocator(size_t arenaBytes = kDefaultArena) : arenaBytes_(arenaBytes) {
		emptySpacesMem_ = std::malloc(kEmptySpacesBytes);
		rawMem_ = std::malloc(arenaBytes_);
		std::memset(rawMem_, 0, arenaBytes_);
		std::memset(emptySpacesMem_, 0, kEmptySpacesBytes);
		new (&cm_) CacheManager();
		region_.setup(emptySpacesMem_, kEmptySpacesBytes, reinterpret_cast<uint32_t>(rawMem_),
		              reinterpret_cast<uint32_t>(rawMem_) + arenaBytes_, &cm_);
#if ALPHA_OR_BETA_VERSION
		// MemoryRegion reads `name` in its out-of-space diagnostic; give it a real
		// value so a failing alloc doesn't deref an uninitialized pointer.
		region_.name = "conformance";
#endif
	}

	~MemoryRegionAllocator() override {
		std::free(rawMem_);
		// emptySpacesMem_ is intentionally NOT freed here: ResizeableArray's
		// destructor frees its backing store via delugeDealloc (== free() under
		// IN_UNIT_TESTS) regardless of setStaticMemory, so freeing it here too
		// would be a double free. Matches the 32bit memory_tests convention.
	}

	void* alloc(size_t size, size_t /*align*/) override {
		// MemoryRegion delivers a fixed 16-byte alignment; it has no per-call
		// align parameter. The suite only requests align <= max_align().
		return region_.alloc(static_cast<uint32_t>(size), false, nullptr);
	}

	void free(void* ptr) override {
		if (ptr != nullptr) {
			region_.dealloc(ptr);
		}
	}

	void* realloc(void* ptr, size_t new_size, size_t align) override {
		if (ptr == nullptr) {
			return alloc(new_size, align);
		}
		size_t old = usable_size(ptr);
		void* np = alloc(new_size, align);
		if (np == nullptr) {
			return nullptr; // original left intact
		}
		std::memcpy(np, ptr, old < new_size ? old : new_size);
		free(ptr);
		return np;
	}

	size_t usable_size(void* ptr) override {
		uint32_t header = *(reinterpret_cast<uint32_t*>(ptr) - 1);
		return header & SPACE_SIZE_MASK;
	}

	size_t arena_size() const override { return arenaBytes_; }
	size_t max_align() const override { return 16; }

	// Exposed for the allocator-specific (stealable) spec section.
	MemoryRegion& region() { return region_; }
	CacheManager& cacheManager() { return cm_; }

private:
	static constexpr size_t kDefaultArena = 10000000;
	static constexpr size_t kEmptySpacesBytes = sizeof(EmptySpaceRecord) * 512;

	size_t arenaBytes_;
	void* rawMem_ = nullptr;
	void* emptySpacesMem_ = nullptr;
	MemoryRegion region_;
	CacheManager cm_;
};
