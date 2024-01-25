#include "memory/general_memory_allocator.h"
#include <new>

// todo - make this work in unit tests, need to remove hard coded addresses in GMA
#if !IN_UNIT_TESTS
void* operator new(std::size_t n) noexcept(false) {
	// allocate on external RAM
	return GeneralMemoryAllocator::get().allocExternal(n);
}

void operator delete(void* p) {
	delugeDealloc(p);
}
#endif
