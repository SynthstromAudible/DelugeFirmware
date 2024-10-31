/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "util/fixedpoint.h"
#include <cstdint>
namespace deluge::dsp::filter {
class BasicFilterComponent {
public:
	BasicFilterComponent() { reset(); }
	// moveability is tan(f)/(1+tan(f))
	[[gnu::always_inline]] inline q31_t doFilter(q31_t input, q31_t moveability) {
		q31_t a = multiply_32x32_rshift32_rounded(input - memory, moveability) << 1;
		q31_t b = a + memory;
		memory = b + a;
		return b;
	}
	[[gnu::always_inline]] inline int32_t doAPF(q31_t input, int32_t moveability) {
		q31_t a = multiply_32x32_rshift32_rounded(input - memory, moveability) << 1;
		q31_t b = a + memory;
		memory = a + b;
		return b * 2 - input;
	}
	[[gnu::always_inline]] inline void affectFilter(q31_t input, int32_t moveability) {
		memory += multiply_32x32_rshift32_rounded(input - memory, moveability) << 2;
	}
	[[gnu::always_inline]] inline void reset() { memory = 0; }
	[[gnu::always_inline]] inline q31_t getFeedbackOutput(int32_t feedbackAmount) {
		return multiply_32x32_rshift32_rounded(memory, feedbackAmount) << 2;
	}
	[[gnu::always_inline]] inline q31_t getFeedbackOutputWithoutLshift(int32_t feedbackAmount) {
		return multiply_32x32_rshift32_rounded(memory, feedbackAmount);
	}

	q31_t memory = 0;
};
} // namespace deluge::dsp::filter
