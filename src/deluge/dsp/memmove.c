#include "mem_functions.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void* memmove(void* dst, const void* src, size_t len) {
	ptrdiff_t result;
	asm("sub %0, %1, %2" : "=r"(result) : "r"(dst), "r"(src));
	if (abs(result) >= (ptrdiff_t)len) {
		/* destination not in source data, so can safely use memcpy */
		return memcpy(dst, src, len);
	}

	// For the remaining cases, the destination range is _guaranteed_ to include the source range
	// single byte to do
	const char* s = (const char*)(src + len);
	char* d = (char*)(dst + len);
	if (len % 2) {
		*--d = *--s;
	}

	// halfword to do
	const uint16_t* s_16 = (const uint16_t*)s;
	uint16_t* d_16 = (uint16_t*)d;
	if (len % 4) {
		*--d_16 = *--s_16;
	}

	// copy backwards by word...
	const uint32_t* s_32 = (const uint32_t*)s_16;
	uint32_t* d_32 = (uint32_t*)d_16;
	while ((intptr_t)d_32 > (intptr_t)dst) {
		*--d_32 = *--s_32;
	}

	return dst;
}
