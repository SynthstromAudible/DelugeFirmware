#include "memory/general_memory_allocator.h"
#include <new>

void* operator new(std::size_t n) noexcept(false) {
	return delugeAlloc(n);
}

void operator delete(void* p) {
	delugeDealloc(p);
}
