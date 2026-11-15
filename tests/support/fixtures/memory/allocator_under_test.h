#pragma once

#include <cstddef>
#include <cstdint>

// Allocator-agnostic test interface (Phase 0 of the allocator rewrite — see
// docs/dev/allocator_implementation_plan.md). The same conformance suite
// (conformance.h) runs against any implementation of this interface: today the
// C++ `MemoryRegion` (memory_region_allocator.h), later the Rust TLSF/slab core
// behind the `deluge_alloc` C ABI. Keep this surface minimal and free of any
// allocator-specific concepts (no stealables here — those are exercised by an
// allocator-specific spec section).
class AllocatorUnderTest {
public:
	virtual ~AllocatorUnderTest() = default;

	// Allocate `size` bytes aligned to at least `align` (a power of two). The
	// allocator additionally guarantees a 16-byte floor regardless of `align`
	// (the over-aligned-SIMD invariant). Returns nullptr on out-of-space.
	virtual void* alloc(size_t size, size_t align) = 0;

	// Free a pointer previously returned by alloc/realloc.
	virtual void free(void* ptr) = 0;

	// Resize, preserving the leading min(old_usable, new_size) bytes. Returns
	// nullptr (and leaves the original block untouched) on failure.
	virtual void* realloc(void* ptr, size_t new_size, size_t align) = 0;

	// Bytes the allocator guarantees are usable at `ptr` (>= the requested
	// size). Used by the suite for overlap and data-integrity checks.
	virtual size_t usable_size(void* ptr) = 0;

	// Total managed arena size, for the drain/leak capacity check.
	virtual size_t arena_size() const = 0;

	// Largest single-block alignment this allocator can honor. The MemoryRegion
	// backbone only guarantees 16; the Rust core will support more.
	virtual size_t max_align() const = 0;
};
