#include "memory/general_memory_allocator.h"
#include <cstring>
#include <new>

// todo - make this work in unit tests, need to remove hard coded addresses in GMA
#if !IN_UNIT_TESTS
void* operator new(std::size_t n) noexcept(false) {
	// allocate on external RAM
	void* address = GeneralMemoryAllocator::get().allocExternal(n);
#ifdef DELUGE_DETERMINISTIC_ALLOC
	// The other allocation entry point (GeneralMemoryAllocator::alloc) zeroes at its return sites; mirror that here
	// so std-container / `new` allocations are deterministic too — `new` goes straight to allocExternal, bypassing
	// alloc(). Sim/golden builds only (see sim/CMakeLists). Firmware leaves it off; the guard inlines away.
	if (address != nullptr) {
		memset(address, 0, n);
	}
#endif
	return address;
}

void operator delete(void* p) {
	delugeDealloc(p);
}
#endif
