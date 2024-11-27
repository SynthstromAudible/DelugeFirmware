#include "mem_functions.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool isAligned(const void* __restrict__ pointer, size_t byte_count) {
	return (uintptr_t)pointer % byte_count == 0;
}

void* memmove(void* dst, const void* src, size_t len) {
	if ((intptr_t)dst - (intptr_t)src >= (ptrdiff_t)len) {
		/* destination not in source data, so can safely use memcpy */
		return memcpy(dst, src, len);
	}

	// For the remaining cases, the destination range is _guaranteed_ to include the source range

	if (isAligned(src, 4) && isAligned(dst, 4) && len % 4 == 0) {
		/* copy backwards... */
		const uint32_t* end = (uint32_t*)dst;
		const uint32_t* s = (const uint32_t*)src + (len / 4);
		uint32_t* d = (uint32_t*)dst + (len / 4);
		while (d != end) {
			*--d = *--s;
		}
		return dst;
	}

	if (isAligned(src, 2) && isAligned(dst, 2) && len % 2 == 0) {
		/* copy backwards... */
		const uint16_t* end = (uint16_t*)dst;
		const uint16_t* s = (const uint16_t*)src + (len / 2);
		uint16_t* d = (uint16_t*)dst + (len / 2);
		while (d != end) {
			*--d = *--s;
		}
		return dst;
	}

	/* copy backwards... */
	const char* end = (char*)dst;
	const char* s = (const char*)src + len;
	char* d = (char*)dst + len;
	while (d != end) {
		*--d = *--s;
	}
	return dst;
}
