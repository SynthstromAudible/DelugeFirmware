#include "mem_functions.h"
#include <arm_neon.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

[[gnu::always_inline]] inline void copy_backwards_byte(uint8_t* restrict dst, const uint8_t* restrict src, size_t len) {
	const uint8_t* s = src + len;
	uint8_t* d = dst + len;
	while (d > dst) {
		*--d = *--s;
	}
}

[[gnu::always_inline]] inline void copy_backwards_word_aligned(uint8_t* restrict dst, const uint8_t* restrict src,
                                                               size_t len) {
	const uint32_t* s = __builtin_assume_aligned((const uint32_t*)(src + len), 4);
	uint32_t* d = __builtin_assume_aligned((uint32_t*)(dst + len), 4);
	while ((intptr_t)d > (intptr_t)dst) {
		*--d = *--s;
	}
}

[[gnu::always_inline]] inline void copy_backwards_word_fallback(uint8_t* restrict dst, const uint8_t* restrict src,
                                                                size_t len) {
	// check alignment and fall back to bytes
	if (((uintptr_t)dst & ~3) != (uintptr_t)dst || ((uintptr_t)src & ~3) != (uintptr_t)src || len % 4) {
		copy_backwards_byte(dst, src, len);
		return;
	}

	copy_backwards_word_aligned(dst, src, len);
}

[[gnu::always_inline]] inline void copy_backwards_doubleword_aligned(uint8_t* restrict dst, const uint8_t* restrict src,
                                                                     size_t len) {
	const uint8_t* s = __builtin_assume_aligned(src + len, 8);
	uint8_t* d = __builtin_assume_aligned(dst + len, 8);
	while (d > dst) {
		s -= 8;
		d -= 8;
		vst1_u8(d, vld1_u8(s));
	}
}

[[gnu::always_inline]] inline void copy_backwards_quadword_aligned(uint8_t* restrict dst, const uint8_t* restrict src,
                                                                   size_t len) {
	const uint8_t* s = __builtin_assume_aligned(src + len, 16);
	uint8_t* d = __builtin_assume_aligned(dst + len, 16);

	while (len >= 128) {
		s -= 64;
		uint8x16x4_t ld1 = vld1q_u8_x4(s);
		s -= 64;
		uint8x16x4_t ld2 = vld1q_u8_x4(s);
		d -= 64;
		vst1q_u8_x4(d, ld1);
		d -= 64;
		vst1q_u8_x4(d, ld2);
		len -= 128;
	}

	while (len >= 64) {
		s -= 64;
		d -= 64;
		vst1q_u8_x4(d, vld1q_u8_x4(s));
		len -= 64;
	}

	while (len >= 32) {
		s -= 32;
		d -= 32;
		vst1q_u8_x2(d, vld1q_u8_x2(s));
		len -= 32;
	}

	// compare d against dst here so that len doesn't underflow
	while (d > dst) {
		s -= 16;
		d -= 16;
		vst1q_u8(d, vld1q_u8(s));
	}
}

void* my_memmove(void* dst, const void* src, size_t len) {
	ptrdiff_t result;
	asm("sub %0, %1, %2" : "=r"(result) : "r"(dst), "r"(src));
	if (abs(result) >= (ptrdiff_t)len) {
		/* destination not in source data, so can safely use memcpy */
		return memcpy(dst, src, len);
	}

	size_t blocksize = 0;
	if (len < 4) {
		// if the blocksize is one (i.e. len is less than 3), just do it and exit
		copy_backwards_byte(dst, src, len);
		return dst;
	}
	else if (len < 8) {
		blocksize = 4;
	}
	else if (len < 16) {
		blocksize = 8;
	}
	else {
		blocksize = 16;
	}

	// Ensure blocksize is aligned between src and dst
	while (((uintptr_t)dst & (blocksize - 1)) != ((uintptr_t)src & (blocksize - 1))) {
		blocksize /= 2;
		if (blocksize <= 2) {
			copy_backwards_byte(dst, src, len);
			return dst;
		}
	}

align_start:
	// Align src to the blocksize boundary
	uintptr_t src_aligned_down = (uintptr_t)src & ~(blocksize - 1);
	const uint8_t* src_aligned =
	    (src_aligned_down == (uintptr_t)src) ? src : (const uint8_t*)(src_aligned_down + blocksize);

	// Align dst to the blocksize boundary
	uintptr_t dst_aligned_down = (uintptr_t)dst & ~(blocksize - 1);
	uint8_t* dst_aligned = (dst_aligned_down == (uintptr_t)dst) ? dst : (uint8_t*)(dst_aligned_down + blocksize);

	size_t padding_left = (intptr_t)dst_aligned - (intptr_t)dst;

	if (padding_left + blocksize > len) {
		blocksize /= 2;
		if (blocksize <= 2) {
			copy_backwards_byte(dst, src, len);
			return dst;
		}
		goto align_start;
	}

	const uint8_t* src_end = (const uint8_t*)src + len;
	uint8_t* dst_end = (uint8_t*)dst + len;
	const uint8_t* src_end_aligned = (const uint8_t*)((uintptr_t)src_end & ~(blocksize - 1));
	uint8_t* dst_end_aligned = (uint8_t*)((uintptr_t)dst_end & ~(blocksize - 1));

	// Copy the padding on the left
	size_t padding_right = dst_end - dst_end_aligned;
	copy_backwards_word_fallback(dst_end_aligned, src_end_aligned, padding_right);

	size_t num_bytes_aligned = src_end_aligned - src_aligned;

	// Perform the main copy
	switch (blocksize) {
	case 16:
		copy_backwards_quadword_aligned(dst_aligned, src_aligned, num_bytes_aligned);
		break;
	case 8:
		copy_backwards_doubleword_aligned(dst_aligned, src_aligned, num_bytes_aligned);
		break;
	case 4:
		copy_backwards_word_aligned(dst_aligned, src_aligned, num_bytes_aligned);
		break;
	default:
		copy_backwards_byte(dst_aligned, src_aligned, num_bytes_aligned);
		break;
	}

	// Finish the copy with the padding on the left
	copy_backwards_word_fallback(dst, src, padding_left);

	return dst;
}
