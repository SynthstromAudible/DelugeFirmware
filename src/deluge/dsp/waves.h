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
#include <argon.hpp>
#include <limits>

namespace deluge::dsp::waves {

template <typename T>
constexpr T pulse(T phase, float pulse_width = 0.5f) {
	return (phase < T(pulse_width)) ? T(1) : T(-1);
}

template <>
constexpr Argon<float> pulse(Argon<float> phase, float pulse_width) {
	return argon::ternary(phase < Argon{pulse_width}, Argon{1.f}, Argon{-1.f});
}

template <typename T>
constexpr T square(T phase) {
	return (phase < T(0.5)) ? T(1) : T(-1);
}

template <>
constexpr Argon<float> square(Argon<float> phase) {
	return argon::ternary(phase < Argon{0.5f}, Argon{1.f}, Argon{-1.f});
}

/// @deprecated
constexpr Argon<q31_t> square(Argon<uint32_t> phase) {
	return argon::ternary(phase < Argon<uint32_t>{0x80000000}, Argon{1.f}, Argon{-1.f}).As<q31_t>();
}

/// @brief Basic waveform generation functions
/// @param phase The phase of the waveform, typically in the range [0, 1)
/// @return The value of the waveform at the given phase in the range [-1, 1]
template <typename T>
constexpr T triangle(T phase) {
	phase = phase > T(0.5) ? T(1) : T(0);
	return (4 * phase) - 1.f;
}

template <>
Argon<float> triangle(Argon<float> phase) {
	phase = argon::ternary(phase >= 0.5f, Argon{1.f}, Argon{0.f}) - phase;
	return Argon{-1.f}.MultiplyAdd(phase.Absolute(), 4.f);
}

Argon<q31_t> triangle(Argon<uint32_t> phase) {
	phase = argon::ternary(phase >= Argon<uint32_t>{0x80000000},
	                       Argon<uint32_t>{std::numeric_limits<uint32_t>::max()} - phase, phase);
	return (phase - Argon<uint32_t>{0x80000000u}).As<q31_t>(); // Q31 range [-1, 1]
}

/// @brief Generate a triangle wave using a fixed-point representation
/// @param phase_uint The phase of the waveform as an unsigned integer (typically in the range [0, 2^32))
/// @return The value of the triangle wave at the given phase as a FixedPoint<31> type
constexpr FixedPoint<31> triangle(uint32_t phase_uint) {
	return triangle(FixedPoint<31>::from_raw(static_cast<int32_t>(phase_uint >> 1)));
}

/// @deprecated
constexpr FixedPoint<30> triangleFast(uint32_t phase) {
	if (phase >= FixedPoint<31>::one()) {
		phase = -phase;
	}
	return FixedPoint<30>::from_raw(static_cast<int32_t>(phase - FixedPoint<30>::one())); // Q30 range [-1, 1]
}

template <typename T>
constexpr T ramp(T phase) {
	return phase * 2 - 1; // Scale to range [-1, 1]
}

template <>
Argon<float> ramp(Argon<float> phase) {
	return Argon{-1.f}.MultiplyAdd(phase, 2.f);
}

/// @brief A saw differs from a ramp in that its phase is offset by 50%
/// i.e. reset occurs at 0.5 instead of 0
template <typename T>
constexpr T saw(T phase) {
	auto subtrahend = (phase < T(0.5)) ? T(1) : T(3); // Offset by 50%
	return (phase * 2) - subtrahend;                  // Scale to range [-1, 1]
}

template <>
Argon<float> saw(Argon<float> phase) {
	auto addend = argon::ternary(phase < Argon{0.5f}, Argon{-1.f}, Argon{-3.f});
	return Argon{addend}.MultiplyAdd(phase, 2.f); // Scale to range [-1, 1]
}

/// @deprecated
Argon<q31_t> saw(Argon<uint32_t> phase) {
	return (phase >> 1).As<q31_t>(); // Q31 range [-1, 1]
}
} // namespace deluge::dsp::waves
