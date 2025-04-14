/**
 * Copyright (c) 2025 Katherine Whitlock
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
#include "definitions_cxx.hpp"
#include "deluge/util/fixedpoint.h"
#include "dsp_ng/core/processor.hpp"
#include "dsp_ng/core/types.hpp"
#include <array>

namespace deluge::dsp::filters {

/// @brief A DC blocker filter that removes DC offset from audio signals.
/// @tparam T The sample type of the filter
template <typename T = float>
class DCBlocker;

template <>
class DCBlocker<float> : public Processor<float> {
public:
	DCBlocker() { set_cutoff(1.f - (20 / kSampleRate)); } // 20Hz
	DCBlocker(float pole) { set_cutoff(pole); };

	void set_cutoff(float pole) { pole_ = pole; }

	/// y[n] = a * y[n-1] + x[n] - x[n - 1]
	Sample<float> render(Sample<float> in) final {
		y_ = (y_ * pole_) + in - x_;
		x_ = in;
		return y_;
	}

private:
	float pole_;
	float x_ = 0;
	float y_ = 0;
};

template <>
class DCBlocker<fixed_point::Sample> : public Processor<fixed_point::Sample> {
	void set_cutoff(float pole) { pole_ = pole; }

	fixed_point::Sample render(fixed_point::Sample in) final {
		in -= x_;
		y_ = in.MultiplyAdd(y_, pole_);
		x_ = in;
		return y_;
	}

private:
	FixedPoint<31> pole_;
	fixed_point::Sample x_ = 0.f;
	fixed_point::Sample y_ = 0.f;
};

template <>
class DCBlocker<Argon<float>> : public Processor<Argon<float>> {
public:
	DCBlocker() {
		set_cutoff(1.f - 20 / kSampleRate); // 20Hz
	}
	DCBlocker(float pole) { set_cutoff(pole); };

	/// y[n] = a * y[n-1] + x[n] - x[n - 1]
	Argon<float> render(Argon<float> curr) final {
		// load curr = { x[n], x[n + 1], x[n + 2], x[n + 3] }
		// load prev = { x[n - 1], x[n], x[n + 1], x[n + 2] }
		auto prev = Argon<float>{x_}.Extract<3>(curr);

		// x = { (x[n] - x[n - 1]), (x[n + 1] - x[n]), (x[n + 2] - x[n + 1]), (x[n + 3] - x[n + 2]) }
		auto x = curr - prev;

		// a_arr = { 0, 0, 0, 1, a^1, a^2, a^3, a^4 }
		auto a_0 = Argon<float>::Load(&a_[0]); // load a_0 = { 0,   0,   0,   1   } from a_arr[0]
		auto a_1 = Argon<float>::Load(&a_[1]); // load a_1 = { 0,   0,   1,   a^1 } from a_arr[1]
		auto a_2 = Argon<float>::Load(&a_[2]); // load a_2 = { 0,   1,   a^1, a^2 } from a_arr[2]
		auto a_3 = Argon<float>::Load(&a_[3]); // load a_3 = { 1,   a^1, a^2, a^3 } from a_arr[3]
		auto a_4 = Argon<float>::Load(&a_[4]); // load a_4 = { a^1, a^2, a^3, a^4 } from a_arr[4]

		// Do this:                Sample:____y0____y1___y2___y3_ | __terms__
		auto y = a_0.Multiply(x[3]);  // a0 = 0,   0,   0,   1   | x[n + 3] - x[n + 2]
		y = y.MultiplyAdd(a_1, x[2]); // a1 = 0,   0,   1,   a   | x[n + 2] - x[n + 1]
		y = y.MultiplyAdd(a_2, x[1]); // a2 = 0,   1,   a,   a^2 | x[n + 3] - x[n]
		y = y.MultiplyAdd(a_3, x[0]); // a3 = 1,   a,   a^2, a^3 | x[n]     - x[n - 1]
		y = y.MultiplyAdd(a_4, y_);   // a4 = a,   a^2, a^3, a^4 | y[n - 1]

		x_ = curr[3];
		y_ = y[3];
		return y;
	}

	inline void set_cutoff(float pole) {
		a_[4] = pole;         // a^1
		a_[5] = a_[4] * pole; // a^2
		a_[6] = a_[5] * pole; // a^3
		a_[7] = a_[6] * pole; // a^4
	}

private:
	std::array<float, 8> a_ = {0, 0, 0, 1};
	float x_{0};
	float y_{0};
};

template <>
class DCBlocker<Argon<q31_t>> : public Processor<Argon<q31_t>> {
public:
	DCBlocker() {
		set_cutoff(1.f - 20 / kSampleRate); // 20Hz
	}
	DCBlocker(float pole) { set_cutoff(pole); };

	/// y[n] = a * y[n-1] + x[n] - x[n - 1]
	Argon<q31_t> render(Argon<q31_t> curr) final {
		// load curr = { x[n], x[n + 1], x[n + 2], x[n + 3] }
		// load prev = { x[n - 1], x[n], x[n + 1], x[n + 2] }
		auto prev = Argon<q31_t>{x_}.Extract<3>(curr);

		// x = { (x[n] - x[n - 1]), (x[n + 1] - x[n]), (x[n + 2] - x[n + 1]), (x[n + 3] - x[n + 2]) }
		auto x = curr - prev;

		// a_arr = { 0, 0, 0, 1, a^1, a^2, a^3, a^4 }
		auto a_0 = Argon<q31_t>::Load(&a_[0]); // load a_0 = { 0,   0,   0,   1   } from a_arr[0]
		auto a_1 = Argon<q31_t>::Load(&a_[1]); // load a_1 = { 0,   0,   1,   a^1 } from a_arr[1]
		auto a_2 = Argon<q31_t>::Load(&a_[2]); // load a_2 = { 0,   1,   a^1, a^2 } from a_arr[2]
		auto a_3 = Argon<q31_t>::Load(&a_[3]); // load a_3 = { 1,   a^1, a^2, a^3 } from a_arr[3]
		auto a_4 = Argon<q31_t>::Load(&a_[4]); // load a_4 = { a^1, a^2, a^3, a^4 } from a_arr[4]

		// Do this:                               Sample:____y0____y1___y2___y3_ | __terms__
		auto y = a_0.MultiplyRoundFixedPoint(x[3]);  // a0 = 0,   0,   0,   1   | x[n + 3] - x[n + 2]
		y = y.MultiplyRoundAddFixedPoint(a_1, x[2]); // a1 = 0,   0,   1,   a   | x[n + 2] - x[n + 1]
		y = y.MultiplyRoundAddFixedPoint(a_2, x[1]); // a2 = 0,   1,   a,   a^2 | x[n + 3] - x[n]
		y = y.MultiplyRoundAddFixedPoint(a_3, x[0]); // a3 = 1,   a,   a^2, a^3 | x[n]     - x[n - 1]
		y = y.MultiplyRoundAddFixedPoint(a_4, y_);   // a4 = a,   a^2, a^3, a^4 | y[n - 1]

		x_ = curr[3];
		y_ = y[3];
		return y;
	}

	inline void set_cutoff(float a) {
		a_[4] = float_to_q31(a);             // a^1
		a_[5] = float_to_q31(a * a);         // a^2
		a_[6] = float_to_q31(a * a * a);     // a^3
		a_[7] = float_to_q31(a * a * a * a); // a^4
	}

private:
	std::array<q31_t, 8> a_ = {0, 0, 0, 1};
	q31_t x_{0};
	q31_t y_{0};
};

} // namespace deluge::dsp::filters

// clang-format off
/*
FILTER MATH (DC blocker aka HPF)

// simplified
y[n] = a * y[n - 1] + x[n] - x[n - 1]

// unrolled
y[n]     = a * y[n - 1] + x[n]     - x[n - 1]
y[n + 1] = a * y[n]     + x[n + 1] - x[n]
y[n + 2] = a * y[n + 1] + x[n + 2] - x[n + 1]
y[n + 3] = a * y[n + 2] + x[n + 3] - x[n + 2]

y[n + 3] = a * (a * (a * (a * y[n - 1] + x[n] - x[n - 1]) + x[n + 1] - x[n + 2]) + x[n + 2] - x[n + 1]) + x[n + 3] - x[n + 2]                       // substitution

         = a^4 y(n - 1) - a^3 x(n - 1) + a^3 x(n)     - a^2 x(n)     + a^2 x(n + 1)  - a x(n + 1)   + a x(n + 2)   - x(n + 2)     + x(n + 3)        // expansion
         = a^4 y(n - 1) - a^3 x(n - 1) + a^3 x(n)     - a^2 x(n)     + a^2 x(n + 1)  - a^1 x(n + 1) + a^1 x(n + 2) - a^0 x(n + 2) + a^0 x(n + 3)    // add extra coefficients
         = a^4 y(n - 1) + a^3 x(n)     - a^3 x(n - 1) + a^2 x(n + 1) - a^2 x(n)      + a^1 x(n + 2) - a^1 x(n + 1) + a^0 x(n + 3) - a^0 x(n + 2)    // reorder Add/Sub

         = a^4 y(n - 1) + a^3 (x(n) - x(n - 1)) + a^2 (x(n + 1) - x(n)) + a^1 (x(n + 2) - x(n + 1)) + a^0 (x(n + 3) - x(n + 2))                     // factor out coefficients

         = a4 y(n - 1)                // separate ops
         + a3 (x(n)     - x(n - 1))
         + a2 (x(n + 1) - x(n))
         + a1 (x(n + 2) - x(n + 1))
         + a0 (x(n + 3) - x(n + 2))


Coefficients (reverse order )
y[x]     = { a4, a3,  a2,  a1, a0 }

y[n]     = { a,   1,   0,   0,  0 }
y[n + 1] = { a^2, a,   1,   0,  0 }
y[n + 2] = { a^3, a^2, a,   1,  0 }
y[n + 3] = { a^4, a^3, a^2, a,  1 }

____y0____y1___y2___y3_ | __terms__
a0 = 0,   0,   0,   1   | x[n + 3] - x[n + 2]
a1 = 0,   0,   1,   a   | x[n + 2] - x[n + 1]
a2 = 0,   1,   a,   a^2 | x[n + 3] - x[n]
a3 = 1,   a,   a^2, a^3 | x[n]     - x[n - 1]
a4 = a,   a^2, a^3, a^4 | y[n - 1]


x_arr = {x[n - 1], x[n], x[n + 1], x[n + 2], x[n + 3]}

load prev = { x[n - 1], x[n],     x[n + 1], x[n + 2] } from x_arr[0]
load curr = { x[n],     x[n + 1], x[n + 2], x[n + 3] } from x_arr[1]

x = { (x[n] - x[n - 1]), (x[n + 1] - x[n]), (x[n + 2] - x[n + 1]), (x[n + 3] - x[n + 2]) }
x = curr - prev

a_arr = { 0, 0, 0, 1, a^1, a^2, a^3, a^4 }
load a_0 = { 0,   0,   0,   1   } from a_arr[0]
load a_1 = { 0,   0,   1,   a^1 } from a_arr[1]
load a_2 = { 0,   1,   a^1, a^2 } from a_arr[2]
load a_3 = { 1,   a^1, a^2, a^3 } from a_arr[3]
load a_4 = { a^1, a^2, a^3, a^4 } from a_arr[4]

y = a_0 * x[3]
y = y + (a_1 * x[2])
y = y + (a_2 * x[1])
y = y + (a_3 * x[0])
y = y + (a_4 * y[n - 1])

output[0:4] = y

*/
// clang-format on
