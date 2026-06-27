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

// C++14 sized deallocation. libstdc++ containers (std::string, std::vector, ...) call the sized form directly. On the
// device the default sized delete defers to the unsized one above, but a sanitizer substitutes its own sized-delete
// that does NOT defer - so without this override, std::string's buffer (allocated via our operator new) is freed
// through the sanitizer and reported as a bad free. Route it to the custom allocator like the unsized form.
void operator delete(void* p, std::size_t) noexcept {
	delugeDealloc(p);
}
#endif
