/*===---- arm_neon.h - ARM Neon intrinsics ---------------------------------===
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef ARM_NEON_SHIM_H
#define ARM_NEON_SHIM_H
// this exists to make clang happy because it doesn't use the same types as gcc neon.
// clangd defins __GNUC__ for us so can't check on that
#ifndef __clang__
#include "arm_neon.h" // IWYU pragma: export
#else

#include <stdint.h>
typedef float float32_t;
typedef __fp16 float16_t;
#ifdef __aarch64__
typedef double float64_t;
#endif
#ifdef __aarch64__
typedef uint8_t poly8_t;
typedef uint16_t poly16_t;
typedef uint64_t poly64_t;
#else
typedef int8_t poly8_t;
typedef int16_t poly16_t;
#endif
typedef __attribute__((neon_vector_type(8))) int8_t int8x8_t;
typedef __attribute__((neon_vector_type(16))) int8_t int8x16_t;
typedef __attribute__((neon_vector_type(4))) int16_t int16x4_t;
typedef __attribute__((neon_vector_type(8))) int16_t int16x8_t;
typedef __attribute__((neon_vector_type(2))) int32_t int32x2_t;
typedef __attribute__((neon_vector_type(4))) int32_t int32x4_t;
typedef __attribute__((neon_vector_type(1))) int64_t int64x1_t;
typedef __attribute__((neon_vector_type(2))) int64_t int64x2_t;
typedef __attribute__((neon_vector_type(8))) uint8_t uint8x8_t;
typedef __attribute__((neon_vector_type(16))) uint8_t uint8x16_t;
typedef __attribute__((neon_vector_type(4))) uint16_t uint16x4_t;
typedef __attribute__((neon_vector_type(8))) uint16_t uint16x8_t;
typedef __attribute__((neon_vector_type(2))) uint32_t uint32x2_t;
typedef __attribute__((neon_vector_type(4))) uint32_t uint32x4_t;
typedef __attribute__((neon_vector_type(1))) uint64_t uint64x1_t;
typedef __attribute__((neon_vector_type(2))) uint64_t uint64x2_t;
typedef __attribute__((neon_vector_type(4))) float16_t float16x4_t;
typedef __attribute__((neon_vector_type(8))) float16_t float16x8_t;
typedef __attribute__((neon_vector_type(2))) float32_t float32x2_t;
typedef __attribute__((neon_vector_type(4))) float32_t float32x4_t;
#ifdef __aarch64__
typedef __attribute__((neon_vector_type(1))) float64_t float64x1_t;
typedef __attribute__((neon_vector_type(2))) float64_t float64x2_t;
#endif
typedef __attribute__((neon_polyvector_type(8))) poly8_t poly8x8_t;
typedef __attribute__((neon_polyvector_type(16))) poly8_t poly8x16_t;
typedef __attribute__((neon_polyvector_type(4))) poly16_t poly16x4_t;
typedef __attribute__((neon_polyvector_type(8))) poly16_t poly16x8_t;
#ifdef __aarch64__
typedef __attribute__((neon_polyvector_type(1))) poly64_t poly64x1_t;
typedef __attribute__((neon_polyvector_type(2))) poly64_t poly64x2_t;
#endif
typedef struct int8x8x2_t {
	int8x8_t val[2];
} int8x8x2_t;
typedef struct int8x16x2_t {
	int8x16_t val[2];
} int8x16x2_t;
typedef struct int16x4x2_t {
	int16x4_t val[2];
} int16x4x2_t;
typedef struct int16x8x2_t {
	int16x8_t val[2];
} int16x8x2_t;
typedef struct int32x2x2_t {
	int32x2_t val[2];
} int32x2x2_t;
typedef struct int32x4x2_t {
	int32x4_t val[2];
} int32x4x2_t;
typedef struct int64x1x2_t {
	int64x1_t val[2];
} int64x1x2_t;
typedef struct int64x2x2_t {
	int64x2_t val[2];
} int64x2x2_t;
typedef struct uint8x8x2_t {
	uint8x8_t val[2];
} uint8x8x2_t;
typedef struct uint8x16x2_t {
	uint8x16_t val[2];
} uint8x16x2_t;
typedef struct uint16x4x2_t {
	uint16x4_t val[2];
} uint16x4x2_t;
typedef struct uint16x8x2_t {
	uint16x8_t val[2];
} uint16x8x2_t;
typedef struct uint32x2x2_t {
	uint32x2_t val[2];
} uint32x2x2_t;
typedef struct uint32x4x2_t {
	uint32x4_t val[2];
} uint32x4x2_t;
typedef struct uint64x1x2_t {
	uint64x1_t val[2];
} uint64x1x2_t;
typedef struct uint64x2x2_t {
	uint64x2_t val[2];
} uint64x2x2_t;
typedef struct float16x4x2_t {
	float16x4_t val[2];
} float16x4x2_t;
typedef struct float16x8x2_t {
	float16x8_t val[2];
} float16x8x2_t;
typedef struct float32x2x2_t {
	float32x2_t val[2];
} float32x2x2_t;
typedef struct float32x4x2_t {
	float32x4_t val[2];
} float32x4x2_t;
#ifdef __aarch64__
typedef struct float64x1x2_t {
	float64x1_t val[2];
} float64x1x2_t;
typedef struct float64x2x2_t {
	float64x2_t val[2];
} float64x2x2_t;
#endif
typedef struct poly8x8x2_t {
	poly8x8_t val[2];
} poly8x8x2_t;
typedef struct poly8x16x2_t {
	poly8x16_t val[2];
} poly8x16x2_t;
typedef struct poly16x4x2_t {
	poly16x4_t val[2];
} poly16x4x2_t;
typedef struct poly16x8x2_t {
	poly16x8_t val[2];
} poly16x8x2_t;
#ifdef __aarch64__
typedef struct poly64x1x2_t {
	poly64x1_t val[2];
} poly64x1x2_t;
typedef struct poly64x2x2_t {
	poly64x2_t val[2];
} poly64x2x2_t;
#endif
typedef struct int8x8x3_t {
	int8x8_t val[3];
} int8x8x3_t;
typedef struct int8x16x3_t {
	int8x16_t val[3];
} int8x16x3_t;
typedef struct int16x4x3_t {
	int16x4_t val[3];
} int16x4x3_t;
typedef struct int16x8x3_t {
	int16x8_t val[3];
} int16x8x3_t;
typedef struct int32x2x3_t {
	int32x2_t val[3];
} int32x2x3_t;
typedef struct int32x4x3_t {
	int32x4_t val[3];
} int32x4x3_t;
typedef struct int64x1x3_t {
	int64x1_t val[3];
} int64x1x3_t;
typedef struct int64x2x3_t {
	int64x2_t val[3];
} int64x2x3_t;
typedef struct uint8x8x3_t {
	uint8x8_t val[3];
} uint8x8x3_t;
typedef struct uint8x16x3_t {
	uint8x16_t val[3];
} uint8x16x3_t;
typedef struct uint16x4x3_t {
	uint16x4_t val[3];
} uint16x4x3_t;
typedef struct uint16x8x3_t {
	uint16x8_t val[3];
} uint16x8x3_t;
typedef struct uint32x2x3_t {
	uint32x2_t val[3];
} uint32x2x3_t;
typedef struct uint32x4x3_t {
	uint32x4_t val[3];
} uint32x4x3_t;
typedef struct uint64x1x3_t {
	uint64x1_t val[3];
} uint64x1x3_t;
typedef struct uint64x2x3_t {
	uint64x2_t val[3];
} uint64x2x3_t;
typedef struct float16x4x3_t {
	float16x4_t val[3];
} float16x4x3_t;
typedef struct float16x8x3_t {
	float16x8_t val[3];
} float16x8x3_t;
typedef struct float32x2x3_t {
	float32x2_t val[3];
} float32x2x3_t;
typedef struct float32x4x3_t {
	float32x4_t val[3];
} float32x4x3_t;
#ifdef __aarch64__
typedef struct float64x1x3_t {
	float64x1_t val[3];
} float64x1x3_t;
typedef struct float64x2x3_t {
	float64x2_t val[3];
} float64x2x3_t;
#endif
typedef struct poly8x8x3_t {
	poly8x8_t val[3];
} poly8x8x3_t;
typedef struct poly8x16x3_t {
	poly8x16_t val[3];
} poly8x16x3_t;
typedef struct poly16x4x3_t {
	poly16x4_t val[3];
} poly16x4x3_t;
typedef struct poly16x8x3_t {
	poly16x8_t val[3];
} poly16x8x3_t;
#ifdef __aarch64__
typedef struct poly64x1x3_t {
	poly64x1_t val[3];
} poly64x1x3_t;
typedef struct poly64x2x3_t {
	poly64x2_t val[3];
} poly64x2x3_t;
#endif
typedef struct int8x8x4_t {
	int8x8_t val[4];
} int8x8x4_t;
typedef struct int8x16x4_t {
	int8x16_t val[4];
} int8x16x4_t;
typedef struct int16x4x4_t {
	int16x4_t val[4];
} int16x4x4_t;
typedef struct int16x8x4_t {
	int16x8_t val[4];
} int16x8x4_t;
typedef struct int32x2x4_t {
	int32x2_t val[4];
} int32x2x4_t;
typedef struct int32x4x4_t {
	int32x4_t val[4];
} int32x4x4_t;
typedef struct int64x1x4_t {
	int64x1_t val[4];
} int64x1x4_t;
typedef struct int64x2x4_t {
	int64x2_t val[4];
} int64x2x4_t;
typedef struct uint8x8x4_t {
	uint8x8_t val[4];
} uint8x8x4_t;
typedef struct uint8x16x4_t {
	uint8x16_t val[4];
} uint8x16x4_t;
typedef struct uint16x4x4_t {
	uint16x4_t val[4];
} uint16x4x4_t;
typedef struct uint16x8x4_t {
	uint16x8_t val[4];
} uint16x8x4_t;
typedef struct uint32x2x4_t {
	uint32x2_t val[4];
} uint32x2x4_t;
typedef struct uint32x4x4_t {
	uint32x4_t val[4];
} uint32x4x4_t;
typedef struct uint64x1x4_t {
	uint64x1_t val[4];
} uint64x1x4_t;
typedef struct uint64x2x4_t {
	uint64x2_t val[4];
} uint64x2x4_t;
typedef struct float16x4x4_t {
	float16x4_t val[4];
} float16x4x4_t;
typedef struct float16x8x4_t {
	float16x8_t val[4];
} float16x8x4_t;
typedef struct float32x2x4_t {
	float32x2_t val[4];
} float32x2x4_t;
typedef struct float32x4x4_t {
	float32x4_t val[4];
} float32x4x4_t;
#ifdef __aarch64__
typedef struct float64x1x4_t {
	float64x1_t val[4];
} float64x1x4_t;
typedef struct float64x2x4_t {
	float64x2_t val[4];
} float64x2x4_t;
#endif
typedef struct poly8x8x4_t {
	poly8x8_t val[4];
} poly8x8x4_t;
typedef struct poly8x16x4_t {
	poly8x16_t val[4];
} poly8x16x4_t;
typedef struct poly16x4x4_t {
	poly16x4_t val[4];
} poly16x4x4_t;
typedef struct poly16x8x4_t {
	poly16x8_t val[4];
} poly16x8x4_t;
#ifdef __aarch64__
typedef struct poly64x1x4_t {
	poly64x1_t val[4];
} poly64x1x4_t;
typedef struct poly64x2x4_t {
	poly64x2_t val[4];
} poly64x2x4_t;
#endif
#endif // ndef clang
#endif // ARM_NEON_SHIM_H
