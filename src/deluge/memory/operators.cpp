#include "memory/general_memory_allocator.h"
#include <new>

void* operator new(std::size_t n) noexcept(false) {
	//allocate on external RAM
	return GeneralMemoryAllocator::get().allocNonAudio(n);
}

void operator delete(void* p) {
	delugeDealloc(p);
}
