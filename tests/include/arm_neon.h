/*
 * Copyright © 2023-2024 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <array>
#include <cstdint>

#include "deluge/util/fixedpoint.h"

typedef struct int16x4 {
	std::array<int16_t, 4> val;

	int16_t const& operator[](size_t idx) const { return val[idx]; }
	int16_t& operator[](size_t idx) { return val[idx]; }
} int16x4_t;

typedef struct uint16x4 {
	std::array<uint16_t, 4> val;

	uint16_t const& operator[](size_t idx) const { return val[idx]; }
	uint16_t& operator[](size_t idx) { return val[idx]; }
} uint16x4_t;

typedef struct int16x8 {
	std::array<int16_t, 8> val;
} int16x8_t;

typedef struct int16x8x2_t {
	std::array<int16x8_t, 2> val;
} int16x8x2_t;

typedef struct int32x2 {
	std::array<int32_t, 2> val;
} int32x2_t;

typedef struct int32x4 {
	std::array<int32_t, 4> val;

	int32_t const& operator[](size_t idx) const { return val[idx]; }
	int32_t& operator[](size_t idx) { return val[idx]; }
} int32x4_t;

typedef struct uint32x4 {
	std::array<uint32_t, 4> val;

	uint32_t const& operator[](size_t idx) const { return val[idx]; }
	uint32_t& operator[](size_t idx) { return val[idx]; }
} uint32x4_t;

void inline vst1q_s32(int32_t* const ptr, int32x4_t const val) {
	ptr[0] = val.val[0];
	ptr[1] = val.val[1];
	ptr[2] = val.val[2];
	ptr[3] = val.val[3];
}

int32x4_t inline vld1q_s32(int32_t const* const ptr) {
	int32x4_t result;

	result.val[0] = ptr[0];
	result.val[1] = ptr[1];
	result.val[2] = ptr[2];
	result.val[3] = ptr[3];

	return result;
}

uint32x4_t inline vld1q_u32(uint32_t const* const ptr) {
	uint32x4_t result;

	result.val[0] = ptr[0];
	result.val[1] = ptr[1];
	result.val[2] = ptr[2];
	result.val[3] = ptr[3];

	return result;
}

int16x8_t inline vld1q_s16(int16_t const* const ptr) {
	int16x8_t result;

	for (int i = 0; i < 8; ++i) {
		result.val[i] = ptr[i];
	}

	return result;
}

uint32x4_t inline vld1q_lane_u32(uint32_t const* const ptr, uint32x4_t const src, int const lane) {
	uint32x4_t result;

	result.val = src.val;
	result.val[lane] = *ptr;

	return result;
}

int16x4_t inline vand_s16(int16x4_t a, int16x4_t b) {
	int16x4_t result;

	result.val[0] = a.val[0] & b.val[0];
	result.val[1] = a.val[1] & b.val[1];
	result.val[2] = a.val[2] & b.val[2];
	result.val[3] = a.val[3] & b.val[3];

	return result;
}

int16x4_t inline vorr_s16(int16x4_t a, int16x4_t b) {
	int16x4_t result;

	result.val[0] = a.val[0] | b.val[0];
	result.val[1] = a.val[1] | b.val[1];
	result.val[2] = a.val[2] | b.val[2];
	result.val[3] = a.val[3] | b.val[3];

	return result;
}

int16x8_t inline vaddq_s16(int16x8_t const a, int16x8_t const b) {
	int16x8_t result;

	for (int i = 0; i < 8; ++i) {
		result.val[i] = a.val[i] + b.val[i];
	}

	return result;
}

int32x2_t inline vpadd_s32(int32x2_t const a, int32x2_t const b) {
	int32x2_t result;

	result.val[0] = a.val[0] + a.val[1];
	result.val[1] = b.val[0] + b.val[1];

	return result;
}

int32x2_t inline vadd_s32(int32x2_t const a, int32x2_t const b) {
	int32x2_t result;

	for (int i = 0; i < 2; ++i) {
		result.val[i] = a.val[i] + b.val[i];
	}

	return result;
}

int32x4_t inline vaddq_s32(int32x4_t const a, int32x4_t const b) {
	int32x4_t result;

	for (int i = 0; i < 4; ++i) {
		result.val[i] = a.val[i] + b.val[i];
	}

	return result;
}

uint32x4_t inline vaddq_u32(uint32x4_t const a, uint32x4_t const b) {
	uint32x4_t result;

	result.val[0] = a.val[0] + b.val[0];
	result.val[1] = a.val[1] + b.val[1];
	result.val[2] = a.val[2] + b.val[2];
	result.val[3] = a.val[3] + b.val[3];

	return result;
}

int32x4_t inline vshll_n_s16(int16x4_t v, const int shift) {
	int32x4_t result;

	result.val[0] = static_cast<int32_t>(v.val[0]) << shift;
	result.val[1] = static_cast<int32_t>(v.val[1]) << shift;
	result.val[2] = static_cast<int32_t>(v.val[2]) << shift;
	result.val[3] = static_cast<int32_t>(v.val[3]) << shift;

	return result;
}

int32x4_t inline vshlq_n_s32(int32x4_t a, int const n) {
	int32x4_t result;

	result.val[0] = a.val[0] << n;
	result.val[1] = a.val[1] << n;
	result.val[2] = a.val[2] << n;
	result.val[3] = a.val[3] << n;

	return result;
}

uint32x4_t inline vshlq_n_u32(uint32x4_t a, int const n) {
	uint32x4_t result;

	result.val[0] = a.val[0] << n;
	result.val[1] = a.val[1] << n;
	result.val[2] = a.val[2] << n;
	result.val[3] = a.val[3] << n;

	return result;
}

uint16x4_t inline vshr_n_u16(uint16x4_t a, int const n) {
	uint16x4_t result;

	result.val[0] = a.val[0] >> n;
	result.val[1] = a.val[1] >> n;
	result.val[2] = a.val[2] >> n;
	result.val[3] = a.val[3] >> n;

	return result;
}

int16x4_t inline vshrn_n_s32(int32x4_t a, int const n) {
	int16x4_t result;

	result.val[0] = static_cast<int16_t>(a.val[0] >> n);
	result.val[1] = static_cast<int16_t>(a.val[1] >> n);
	result.val[2] = static_cast<int16_t>(a.val[2] >> n);
	result.val[3] = static_cast<int16_t>(a.val[3] >> n);

	return result;
}

uint16x4_t inline vshrn_n_u32(uint32x4_t a, int const n) {
	uint16x4_t result;

	result.val[0] = static_cast<uint16_t>(a.val[0] >> n);
	result.val[1] = static_cast<uint16_t>(a.val[1] >> n);
	result.val[2] = static_cast<uint16_t>(a.val[2] >> n);
	result.val[3] = static_cast<uint16_t>(a.val[3] >> n);

	return result;
}

int16x8_t inline vsubq_s16(int16x8_t a, int16x8_t b) {
	int16x8_t result;

	for (int i = 0; i < 8; ++i) {
		result.val[i] = a.val[i] - b.val[i];
	}

	return result;
}

int16x4_t inline vsub_s16(int16x4_t a, int16x4_t b) {
	int16x4_t result;

	result.val[0] = a.val[0] - b.val[0];
	result.val[1] = a.val[1] - b.val[1];
	result.val[2] = a.val[2] - b.val[2];
	result.val[3] = a.val[3] - b.val[3];

	return result;
}

int32x4_t inline vmull_s16(int16x4_t a, int16x4_t b) {
	int32x4_t result;

	for (int i = 0; i < 4; ++i) {
		int32_t e1{a.val[0]};
		int32_t e2{a.val[2]};

		result.val[i] = e1 * e2;
	}

	return result;
}

int32x4_t inline vqdmull_s16(int16x4_t a, int16x4_t b) {
	int32x4_t result;

	for (int i = 0; i < 4; ++i) {
		int32_t e1 = static_cast<int32_t>(a.val[i]);
		int32_t e2 = static_cast<int32_t>(b.val[i]);

		int32_t product = mul_saturation(e1, e2);
		product = mul_saturation(product, 2);

		result.val[i] = product;
	}

	return result;
}

int32x4_t inline vqdmulhq_s32(int32x4_t const a, int32x4_t const b) {
	int32x4_t result;

	for (int i = 0; i < 4; ++i) {
		int32_t e1 = a.val[i];
		int32_t e2 = b.val[i];

		int32_t product = mul_saturation(e1, e2);
		product = mul_saturation(product, 2);

		result.val[i] = product;
	}

	return result;
}

int16x8_t inline vqdmulhq_n_s16(int16x8_t a, int16_t b) {
	int16x8_t result;
	int32_t e2 = static_cast<int32_t>(b);

	for (int i = 0; i < 8; ++i) {
		int32_t e1 = static_cast<int32_t>(a.val[0]);
		int32_t product = e1 * e2;
		product += 1 << 15;
		product = mul_saturation(product, 2);

		result.val[i] = static_cast<int16_t>(product >> 16);
	}

	return result;
}

int32x4_t inline vqrdmulhq_s32(int32x4_t const a, int32x4_t const b) {
	int32x4_t result;

	result.val[0] = multiply_32x32_rshift32_rounded(a.val[0], b.val[0]);
	result.val[1] = multiply_32x32_rshift32_rounded(a.val[1], b.val[1]);
	result.val[2] = multiply_32x32_rshift32_rounded(a.val[2], b.val[2]);
	result.val[3] = multiply_32x32_rshift32_rounded(a.val[3], b.val[3]);

	return result;
}

int32x4_t inline vqrdmulhq_n_s32(int32x4_t const a, int32_t b) {
	int32x4_t result;

	result.val[0] = multiply_32x32_rshift32_rounded(a.val[0], b);
	result.val[1] = multiply_32x32_rshift32_rounded(a.val[1], b);
	result.val[2] = multiply_32x32_rshift32_rounded(a.val[2], b);
	result.val[3] = multiply_32x32_rshift32_rounded(a.val[3], b);

	return result;
}

int32x4_t inline vmlal_s16(int32x4_t a, int16x4_t b, int16x4_t c) {
	int32x4_t result;

	for (int i = 0; i < 4; ++i) {
		int16_t e1 = b.val[i];
		int16_t e2 = c.val[i];

		int32_t product = int32_t{e1} * int32_t{e2};

		int32_t accum = a.val[i] + product;
	}

	return result;
}

int32x4_t inline vqdmlal_s16(int32x4_t a, int16x4_t b, int16x4_t c) {
	int32x4_t result;

	for (int i = 0; i < 4; ++i) {
		int16_t e1 = b.val[i];
		int16_t e2 = c.val[i];

		int32_t product = int32_t{e1} * int32_t{e2};
		product = mul_saturation(product, 2);

		int32_t accum = add_saturation(a.val[i], product);
	}

	return result;
}

int16x4_t inline vreinterpret_s16_u16(uint16x4_t a) {
	int16x4_t result;

	result.val[0] = static_cast<int16_t>(a.val[0]);
	result.val[1] = static_cast<int16_t>(a.val[1]);
	result.val[2] = static_cast<int16_t>(a.val[2]);
	result.val[3] = static_cast<int16_t>(a.val[3]);

	return result;
}

int32x4_t inline vreinterpretq_s32_u32(uint32x4_t a) {
	int32x4_t result;

	result.val[0] = static_cast<int32_t>(a.val[0]);
	result.val[1] = static_cast<int32_t>(a.val[1]);
	result.val[2] = static_cast<int32_t>(a.val[2]);
	result.val[3] = static_cast<int32_t>(a.val[3]);

	return result;
}

int16x4_t inline vdup_n_s16(int16_t a) {
	int16x4_t result;

	result.val[0] = a;
	result.val[1] = a;
	result.val[2] = a;
	result.val[3] = a;

	return result;
}

int32x4_t inline vdupq_n_s32(int32_t a) {
	int32x4_t result;

	result.val[0] = a;
	result.val[1] = a;
	result.val[2] = a;
	result.val[3] = a;

	return result;
}

uint32x4_t inline vdupq_n_u32(uint32_t a) {
	uint32x4_t result;

	result.val[0] = a;
	result.val[1] = a;
	result.val[2] = a;
	result.val[3] = a;

	return result;
}

uint16x4_t inline vmovn_u32(uint32x4_t a) {
	uint16x4_t result;

	result.val[0] = static_cast<uint16_t>(a.val[0]);
	result.val[1] = static_cast<uint16_t>(a.val[1]);
	result.val[2] = static_cast<uint16_t>(a.val[2]);
	result.val[3] = static_cast<uint16_t>(a.val[3]);

	return result;
}

int16x4_t inline vget_low_s16(int16x8_t a) {
	int16x4_t result;

	for (int i = 0; i < 4; ++i) {
		result.val[i] = a.val[0 + i];
	}

	return result;
}

int16x4_t inline vget_high_s16(int16x8_t a) {
	int16x4_t result;

	for (int i = 0; i < 4; ++i) {
		result.val[i] = a.val[4 + i];
	}

	return result;
}

int32x2_t inline vget_low_s32(int32x4_t a) {
	int32x2_t result;

	for (int i = 0; i < 2; ++i) {
		result.val[i] = a.val[0 + i];
	}

	return result;
}

int32x2_t inline vget_high_s32(int32x4_t a) {
	int32x2_t result;

	for (int i = 0; i < 2; ++i) {
		result.val[i] = a.val[2 + i];
	}

	return result;
}

uint32_t inline vgetq_lane_u32(uint32x4_t a, int const lane) {
	return a[lane];
}

int16x4_t inline vset_lane_s16(int16_t a, int16x4_t const v, int const lane) {
	int16x4_t result;

	result.val[0] = v.val[0];
	result.val[1] = v.val[1];
	result.val[2] = v.val[2];
	result.val[3] = v.val[3];

	result.val[lane] = a;

	return result;
}

uint16x4_t inline vset_lane_u16(uint16_t a, uint16x4_t const v, int const lane) {
	uint16x4_t result;

	result.val[0] = v.val[0];
	result.val[1] = v.val[1];
	result.val[2] = v.val[2];
	result.val[3] = v.val[3];

	result.val[lane] = a;

	return result;
}

int32_t inline vget_lane_s32(int32x2_t a, int const lane) {
	return a.val[lane];
}

int32x4_t inline vsetq_lane_s32(int32_t const a, int32x4_t const v, int const lane) {
	int32x4_t result;

	result.val[0] = v.val[0];
	result.val[1] = v.val[1];
	result.val[2] = v.val[2];
	result.val[3] = v.val[3];

	result.val[lane] = a;

	return result;
}
