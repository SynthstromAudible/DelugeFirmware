
#include <stddef.h>

extern void* delugeAlloc(unsigned int requiredSize, bool mayUseOnChipRam);
extern void delugeDealloc(void* address);

void* malloc(size_t size) {
	return delugeAlloc(size, true);
}

void free(void* addr) {
	delugeDealloc(addr);
}
