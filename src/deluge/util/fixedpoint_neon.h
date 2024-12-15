/*
 * Copyright (c) 2024 Katherine Whitlock
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
#include "fixedpoint.h"
#include <arm_neon.h>

constexpr int32x4_t q31_from_float(float32x4_t x) {
	return vcvtq_n_s32_f32(x, 31);
}

constexpr int32x2_t q31_from_float(float32x2_t x) {
	return vcvt_n_s32_f32(x, 31);
}

constexpr float32x4_t q31_to_float(int32x4_t x) {
	return vcvtq_n_f32_s32(x, 31);
}

constexpr float32x2_t q31_to_float(int32x2_t x) {
	return vcvt_n_f32_s32(x, 31);
}
