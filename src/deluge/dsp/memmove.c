#include "mem_functions.h"
#include <arm_neon.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

void* my_memmove(void* dst, const void* src, size_t len) {
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
	if (len % 8) {
		*--d_32 = *--s_32;
	}

	// doubleword
	s = (const char*)s_32;
	d = (char*)d_32;
	if (len % 16) {
		s -= 8;
		d -= 8;
		vst1_u8(d, vld1_u8(s));
	}

	// quadword
	if (len % 32) {
		s -= 16;
		d -= 16;
		vst1q_u8(d, vld1q_u8(s));
	}

	// quadword x2
	if (len % 64) {
		s -= 32;
		d -= 32;
		vst1q_u8_x2(d, vld1q_u8_x2(s));
	}

	// quadword x4
	if (len % 128) {
		s -= 64;
		d -= 64;
		vst1q_u8_x4(d, vld1q_u8_x4(s));
	}

	// quadword x8
	while ((intptr_t)d > (intptr_t)dst) {
		s -= 64;
		uint8x16x4_t ld1 = vld1q_u8_x4(s);
		s -= 64;
		uint8x16x4_t ld2 = vld1q_u8_x4(s);
		d -= 64;
		vst1q_u8_x4(d, ld1);
		d -= 64;
		vst1q_u8_x4(d, ld2);
	}

	return dst;
}
