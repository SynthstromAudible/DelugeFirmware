#pragma once

#include "allocator_under_test.h"
#include "libdeluge/alloc.h"
#include <cstdlib>

// AllocatorUnderTest adapter over the Rust TLSF core (crates/deluge_alloc) via
// its C ABI. Owns a malloc-backed arena and builds a DelugeHeap over it, so the
// same conformance suite the C++ MemoryRegion passes (tests/spec_memory) runs
// against the Rust allocator through the FFI boundary — apples-to-apples.
class RustAllocator final : public AllocatorUnderTest {
public:
	explicit RustAllocator(size_t arenaBytes = kDefaultArena) : arenaBytes_(arenaBytes) {
		raw_ = std::malloc(arenaBytes_);
		heap_ = deluge_heap_create(raw_, arenaBytes_);
	}
	~RustAllocator() override {
		deluge_heap_destroy(heap_);
		std::free(raw_);
	}

	void* alloc(size_t size, size_t align) override { return deluge_alloc(heap_, size, align); }
	void free(void* ptr) override { deluge_free(heap_, ptr); }
	void* realloc(void* ptr, size_t new_size, size_t align) override {
		return deluge_realloc(heap_, ptr, new_size, align);
	}
	size_t usable_size(void* ptr) override { return deluge_usable_size(heap_, ptr); }
	size_t arena_size() const override { return arenaBytes_; }
	// The TLSF core honors arbitrary power-of-two alignment by construction
	// (the conformance suite still only requests up to 16 — over-alignment is
	// covered on the Rust side by proptest/fuzz).
	size_t max_align() const override { return 4096; }

	bool valid() const { return heap_ != nullptr; }

private:
	static constexpr size_t kDefaultArena = 10000000;
	size_t arenaBytes_;
	void* raw_ = nullptr;
	DelugeHeap* heap_ = nullptr;
};
