#include "memory/heaps.h"
#include <cstring>
#include <new>

#if !IN_UNIT_TESTS
void* operator new(std::size_t n) noexcept(false) {
	// allocate on external RAM (the unified SDRAM heap)
	void* address = deluge::memory::alloc_external(n, 16);
#ifdef DELUGE_DETERMINISTIC_ALLOC
	// The GeneralMemoryAllocator::alloc path zeroes at its return sites; mirror that here so std-container /
	// `new` allocations are deterministic too — `new` goes straight to the heap, bypassing alloc(). Sim/golden
	// builds only (see sim/CMakeLists). Firmware leaves it off; the guard inlines away.
	if (address != nullptr) {
		memset(address, 0, n);
	}
#endif
	return address;
}

void operator delete(void* p) {
	deluge::memory::dealloc(p);
}
#endif
