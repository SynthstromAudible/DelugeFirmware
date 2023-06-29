#include "memory/wren_heap.h"

#define USE_DL_PREFIX
#define ONLY_MSPACES 1
#include "dlmalloc/dlmalloc.h"

mspace wren_heap;

void wren_heap_init() {
	wren_heap = create_mspace_with_base((void*)kWrenHeapStartAddr, kWrenHeapSize, 0);
}

extern "C" void* wren_heap_realloc(void* ptr, size_t newSize, void* userData [[maybe_unused]]) {
	if (ptr == nullptr) {
		return mspace_malloc(wren_heap, newSize);
	}

	if (newSize == 0) {
		mspace_free(wren_heap, ptr);
		return nullptr;
	}

	return mspace_realloc(wren_heap, ptr, newSize);
}
