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
#include <cmath>
#include <string_view>
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
	using Value::Value;

	auto operator<=>(const Frequency& o) const { return this->value <=> o.value; };
	constexpr operator float() const { return this->value; }

	// std::string_view unit() const { return "Hz"; }
	static constexpr std::string_view unit = "Hz";
};

/// @brief A class to represent a percentage value.
struct Percentage : Value<float> {
	using Value::Value;
	Percentage(float value, float lower_bound, float upper_bound)
	    : Value<float>{value}, lower_bound{lower_bound}, upper_bound{upper_bound} {}

	bool operator==(const Percentage& o) const {
		if (this->lower_bound == o.lower_bound && this->upper_bound == o.upper_bound) {
			return this->value == o.value;
		}

		float diff = upper_bound - lower_bound;
		float val_scaled = (this->value - lower_bound) / diff;

		return (val_scaled + lower_bound) == (o.value + o.lower_bound);
	}

	float lower_bound = 0.f;
	float upper_bound = 1.f;

	static constexpr std::string_view unit = "%";
};

/// @brief A class to represent the Q-factor of a filter.
/// @tparam T The type of the value.
using QFactor = Value<float>;

struct Decibels : Value<float> {
	using Value::Value;
	// std::string_view unit() const { return "dB"; }
	static constexpr std::string_view unit = "dB";

	[[nodiscard]] float toGain(float lower_limit = -100.f) const {
		return (this->value > lower_limit) ? std::pow(10.0f, this->value * 0.05f) : 0.0f;
	}
};

/// @brief A class to represent an interval of frequency, measured in cents (1/100th of a semitone).
struct Cents : Value<float> {
	using Value::Value;
	// static constexpr std::string_view unit = "¢";

	[[nodiscard]] float toRatio() const { return std::pow(2.f, this->value / 1200.0); }
};

struct Semitones : Value<float> {
	using Value::Value;
	// static constexpr std::string_view unit = "st";

	[[nodiscard]] float toRatio() const { return std::pow(2.f, this->value / 12.0); }
};
} // namespace deluge::dsp
