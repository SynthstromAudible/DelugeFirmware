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
#include "deluge/util/fixedpoint.h"
#include <argon.hpp>
#include <cstdint>
#include <limits>

namespace deluge::dsp::waves {

/// @brief Generate a pulse wave
/// @param phase The phase of the waveform, in the range [0, 1)
/// @param pulse_width The width of the pulse, in the range [0, 1)
/// @return The value of the pulse wave at the given phase, in the range [-1, 1]
/// @details The pulse width determines the duration of the high state (1) in the pulse wave.
/// A pulse width of 0.5 is equivalent to a square wave.
/// A pulse width of 0.0 or 1.0 results in a constant value of -1 or 1. So don't do that!
template <typename T>
[[gnu::always_inline]] constexpr T pulse(T phase, float pulse_width = 0.5f) {
	return (phase < T(pulse_width)) ? T(1) : T(-1);
}

/// @copydoc pulse
template <>
constexpr Argon<float> pulse(Argon<float> phase, float pulse_width) {
	return argon::ternary(phase < pulse_width, 1.f, -1.f);
}

/// @brief Generate a square wave
/// @param phase The phase of the waveform, in the range [0, 1)
/// @return The value of the square wave at the given phase, in the range [-1, 1]
/// @details A square wave alternates between -1 and 1, with a 50% duty cycle.
template <typename T>
constexpr T square(T phase) {
	return (phase < T(0.5)) ? T(1) : T(-1);
}

/// @copydoc square
template <>
constexpr Argon<float> square(Argon<float> phase) {
	return argon::ternary(phase < 0.5f, 1.f, -1.f);
}

/// @copydoc square
/// @deprecated
constexpr Argon<q31_t> square(Argon<uint32_t> phase) {
	return argon::ternary(phase < 0x80000000u, FixedPoint<31>{1.f}.raw(), FixedPoint<31>{-1.f}.raw());
}

/// @brief Basic waveform generation functions
/// @param phase The phase of the waveform, typically in the range [0, 1)
/// @return The value of the waveform at the given phase in the range [-1, 1]
template <typename T>
constexpr T triangle(T phase) {
	phase = phase > T(0.5) ? T(1) : T(0);
	return (4 * phase) - 1.f;
}

/// @copydoc triangle
template <>
Argon<float> triangle(Argon<float> phase) {
	phase = argon::ternary(phase > 0.5f, 1.f, 0.f) - phase;
	return Argon{-1.f}.MultiplyAdd(phase.Absolute(), 4.f);
}

/// @copydoc triangle
Argon<q31_t> triangleSIMD(Argon<uint32_t> phase) {
	constexpr uint32_t midpoint = 0x80000000u;
	phase = argon::ternary(phase >= midpoint, std::numeric_limits<uint32_t>::max() - phase, phase) * 2;
	return (phase - midpoint).As<q31_t>(); // Q31 range [-1, 1]
}

/// @deprecated
constexpr FixedPoint<30> triangleFast(uint32_t phase) {
	if (phase >= 0x80000000u) {
		phase = -phase;
	}
	return FixedPoint<30>::from_raw(static_cast<int32_t>(phase - 0x80000000u)); // Q30 range [-1, 1]
}

/// @brief Generate a ramp wave
/// @param phase The phase of the waveform, in the range [0, 1)
/// @return The value of the ramp wave at the given phase, in the range [-1, 1]
template <typename T>
constexpr T ramp(T phase) {
	return phase * 2 - 1; // Scale to range [-1, 1]
}

/// @copydoc ramp
template <>
Argon<float> ramp(Argon<float> phase) {
	return Argon{-1.f}.MultiplyAdd(phase, 2.f);
}

/// @brief Generate a saw wave
/// @param phase The phase of the waveform, in the range [0, 1)
/// @return The value of the saw wave at the given phase, in the range [-1, 1]
/// @note A saw differs from a ramp in that its phase is offset by 50%
/// i.e. reset occurs at 0.5 instead of 0
template <typename T>
constexpr T saw(T phase) {
	return (2 * phase) - (phase < T(0.5) ? T(1) : T(3));
}

/// @copydoc saw
template <>
Argon<float> saw(Argon<float> phase) {
	auto addend = argon::ternary(phase < 0.5f, -1.f, -3.f);
	return Argon{addend}.MultiplyAdd(phase, 2.f); // Scale to range [-1, 1]
}

/// @copydoc saw
/// @deprecated
Argon<q31_t> saw(Argon<uint32_t> phase) {
	return (phase >> 1).As<q31_t>(); // Q31 range [-1, 1]
}
} // namespace deluge::dsp::waves
