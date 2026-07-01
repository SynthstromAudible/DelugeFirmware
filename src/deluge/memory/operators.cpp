#include "memory/heaps.h"
#include "util/exceptions.h"
#include <cstring>
#include <new>

#if !IN_UNIT_TESTS
void* operator new(std::size_t n) noexcept(false) {
	// allocate on external RAM (the unified SDRAM heap)
	void* address = deluge::memory::alloc_external(n, 16);
	if (address == nullptr) [[unlikely]] {
		// The throwing operator new must never return null: std::string / std::vector and friends assume a non-null
		// buffer, so returning null silently corrupts them. Throw the same lightweight exception the custom STL
		// allocators (sdram_allocator / external_allocator) do, so existing `catch (deluge::exception)` out-of-memory
		// handlers can deal with it. Code that wants graceful failure uses the heap API directly.
		throw deluge::exception::BAD_ALLOC;
	}
#ifdef DELUGE_DETERMINISTIC_ALLOC
	// The GeneralMemoryAllocator::alloc path zeroes at its return sites; mirror that here so std-container /
	// `new` allocations are deterministic too — `new` goes straight to the heap, bypassing alloc(). Sim/golden
	// builds only (see sim/CMakeLists). Firmware leaves it off; the guard inlines away.
	memset(address, 0, n);
#endif
	return address;
}

void operator delete(void* p) {
	deluge::memory::dealloc(p);
}

// C++14 sized deallocation. libstdc++ containers (std::string, std::vector, ...) call the sized form directly. On the
// device the default sized delete defers to the unsized one above, but a sanitizer substitutes its own sized-delete
// that does NOT defer - so without this override, std::string's buffer (allocated via our operator new) is freed
// through the sanitizer and reported as a bad free. Route it to the custom allocator like the unsized form.
void operator delete(void* p, std::size_t) noexcept {
	deluge::memory::dealloc(p);
}
#endif
