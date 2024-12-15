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
#include <cmath>
#include <cstddef>

enum class InterpolationType {
	Linear,
	Multiplicative,
};

template <typename T, InterpolationType U = InterpolationType::Linear>
class InterpolatedValue {
public:
	constexpr InterpolatedValue(T value) { Init(value, value, 1); }

	constexpr InterpolatedValue(T value, T target_value, size_t num_steps) { Init(value, target_value, num_steps); }

	constexpr void Init(T value, T target_value, size_t num_steps) {
		orig_value_ = value;
		value_ = value;
		target_value_ = target_value;
		num_steps_ = num_steps;
		increment_ = is_interpolating() ? calc_increment() : identity_increment();
	}

	constexpr float Next() {
		if (is_interpolating()) {
			if constexpr (U == InterpolationType::Linear) {
				value_ += increment_;
			}
			else if constexpr (U == InterpolationType::Multiplicative) {
				value_ *= increment_;
			}
		}
		return value_;
	}

	constexpr void Reset(float sample_rate, float ramp_length_in_seconds) {
		Reset(static_cast<size_t>(std::floor(ramp_length_in_seconds * sample_rate)));
	}

	constexpr void Reset(size_t num_steps) {
		num_steps_ = num_steps;
		value_ = orig_value_;
		increment_ = calc_increment();
	}

	constexpr void Set(T target_value, size_t num_steps) {
		target_value_ = target_value;
		num_steps_ = num_steps;
		increment_ = (is_interpolating()) ? calc_increment() : identity_increment();
	}

	constexpr void Set(T value) { set_current_and_target(value); }

	[[nodiscard]] constexpr T value() const { return value_; }
	[[nodiscard]] constexpr T target() const { return target_value_; }
	[[nodiscard]] constexpr bool is_interpolating() const { return (value_ != target_value_); }

private:
	constexpr void set_steps(size_t num_steps) {
		num_steps_ = num_steps;
		if (is_interpolating()) {
			increment_ = calc_increment();
		}
	}

	constexpr void set_target(T target_value) {
		target_value_ = target_value;
		increment_ = calc_increment();
	}

	constexpr void set_current_and_target(T value) {
		value_ = value;
		target_value_ = value;
	}

	[[nodiscard]] constexpr T calc_increment() const {
		auto num_steps_f = static_cast<float>(num_steps_);
		if constexpr (U == InterpolationType::Linear) {
			return (target_value_ - value_) / num_steps_f;
		}
		else if constexpr (U == InterpolationType::Multiplicative) {
			return std::exp((std::log(std::abs(target_value_)) - std::log(std::abs(value_))) / num_steps_f);
		}
	}

	constexpr T identity_increment() {
		if constexpr (U == InterpolationType::Linear) {
			return T{0};
		}
		else if constexpr (U == InterpolationType::Multiplicative) {
			return T{1};
		}
	}

	T value_ = 0;
	T orig_value_ = 0;
	T target_value_ = 0;
	float increment_ = 0.f;
	size_t num_steps_ = 1;
};
