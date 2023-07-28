#include "general_memory_allocator.h"
#include <new>

void* operator new(std::size_t n) noexcept(false) {
	generalMemoryAllocator.alloc(n, nullptr, false, true, false, nullptr, false);
}

void operator delete(void* p) {
	generalMemoryAllocator.dealloc(p);
}
