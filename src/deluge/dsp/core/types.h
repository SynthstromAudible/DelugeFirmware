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

namespace deluge::dsp {

/// @brief A simple class to represent a value with a specific unit.
/// @tparam T The type of the value.
template <typename T>
struct Value {
	T value = 0;

	Value(T value) : value{value} {}
	constexpr Value() = default;
	constexpr operator T() { return value; }
	constexpr auto operator<=>(const Value<T>& o) const = default;
	constexpr bool operator==(const Value<T>& o) const = default;
};

struct Frequency : Value<float> {
	Frequency(float value) : Value{value} {}

	auto operator<=>(const Frequency& o) const { return this->value <=> o.value; };
	constexpr operator float() const { return this->value; }
};

/// @brief A class to represent a percentage value.
/// @tparam T The type of the value.
template <typename T>
struct Percentage;

template <>
struct Percentage<float> : Value<float> {
	using Value::Value;
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

/// @brief A class to represent a percentage value as a fixed-point number.
/// @tparam T The type of the value.
template <>
struct Percentage<FixedPoint<31>> : Value<FixedPoint<31>> {
	using Value::Value;
	Percentage(FixedPoint<31> value, FixedPoint<31> lower_bound, FixedPoint<31> upper_bound)
	    : Value<FixedPoint<31>>{value}, lower_bound{lower_bound}, upper_bound{upper_bound} {}

	Percentage(float value) : Value<FixedPoint<31>>{value} {}
	Percentage(float value, float lower_bound, float upper_bound)
	    : Value<FixedPoint<31>>{value}, lower_bound{lower_bound}, upper_bound{upper_bound} {}

	FixedPoint<31> lower_bound = 0.f;
	FixedPoint<31> upper_bound = 1.f;
};

/// @brief A class to represent the Q-factor of a filter.
/// @tparam T The type of the value.
template <typename T>
struct QFactor : Value<T> {
	using Value<T>::Value;
};

} // namespace deluge::dsp
