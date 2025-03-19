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
#include "util/fixedpoint.h"
#include <string_view>

template <typename T>
struct Value {
	T value = 0;
	constexpr operator T() { return value; }
	constexpr auto operator<=>(const Value<T>& o) const = default;
	constexpr bool operator==(const Value<T>& o) const = default;
};

struct Frequency : Value<float> {
	Frequency() = default;
	Frequency(float value) : Value{value} {}

	auto operator<=>(const Frequency& o) const { return this->value <=> o.value; };
	constexpr operator float() const { return this->value; }

	static constexpr std::string_view unit = "Hz";
};

template <typename T>
struct Percentage;

template <>
struct Percentage<float> : Value<float> {
	static constexpr std::string_view unit = "%";

	Percentage() = default;
	Percentage(float value) : Value<float>{value} {}
	Percentage(float value, float lower_bound, float upper_bound)
	    : Value<float>{value}, lower_bound{lower_bound}, upper_bound{upper_bound} {}

	bool operator==(const Percentage& o) const {
		if (this->lower_bound == o.lower_bound && this->upper_bound == o.upper_bound) {
			return this->value == o.value;
		}

		float diff = upper_bound - lower_bound;
		float val_scaled = (this->value - lower_bound) / diff;

		float o_diff = o.upper_bound - o.lower_bound;
		float o_val_scaled = (o.value - o.lower_bound) / diff;

		return (val_scaled + lower_bound) == (o.value + o.lower_bound);
	}

	float lower_bound = 0.f;
	float upper_bound = 1.f;
};

template <>
struct Percentage<q31_t> : Value<q31_t> {
	static constexpr std::string_view unit = "%";

	Percentage() = default;
	Percentage(q31_t value) : Value<q31_t>{value} {}
	Percentage(q31_t value, q31_t lower_bound, q31_t upper_bound)
	    : Value<q31_t>{value}, lower_bound{lower_bound}, upper_bound{upper_bound} {}

	Percentage(float value) : Value<q31_t>{q31_from_float(value)} {}
	Percentage(float value, float lower_bound, float upper_bound)
	    : Value<q31_t>{q31_from_float(value)}, lower_bound{q31_from_float(lower_bound)},
	      upper_bound{q31_from_float(upper_bound)} {}

	q31_t lower_bound = q31_from_float(0.f);
	q31_t upper_bound = q31_from_float(1.f);
};

template <typename T>
struct QFactor : Value<T> {
	QFactor(T value) : Value<T>{value} {}
};

struct Milliseconds : Value<int32_t> {
	static constexpr std::string_view unit = "ms";
	auto operator<=>(const Milliseconds& o) const { return this->value <=> o.value; }
	constexpr bool operator==(const Milliseconds& o) const { return this->value == o.value; }
};
