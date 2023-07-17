#include "heap.h"
#include "memory/general_memory_allocator.h"
#include <cstring>
#include <umm_malloc.h>

static void* wren_heap_ptr = nullptr;
static umm_heap wren_heap;

void wren_heap_init() {
	wren_heap_ptr = generalMemoryAllocator.alloc(kWrenHeapSize);
	umm_multi_init_heap(&wren_heap, wren_heap_ptr, kWrenHeapSize);
}

void wren_heap_deinit() {
	generalMemoryAllocator.dealloc(wren_heap_ptr);
	wren_heap_ptr = nullptr;
}

extern "C" void* wren_heap_realloc(void* ptr, size_t newSize, void* userData [[maybe_unused]]) {
	if (ptr == nullptr) {
		return umm_multi_malloc(&wren_heap, newSize);
	}

	if (newSize == 0) {
		umm_multi_free(&wren_heap, ptr);
		return nullptr;
	}

	return umm_multi_realloc(&wren_heap, ptr, newSize);
}
