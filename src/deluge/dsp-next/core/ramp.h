/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include <cstdint>

namespace deluge::dsp {

/// @brief A linear ramp: a value advancing by a fixed per-sample increment — the amplitude-domain analogue of
/// @ref Phasor (which ramps phase). It carries the ramp's parameters (start value + slope); the VCA components
/// (@ref deluge::dsp::fixed::Amplitude / @ref deluge::dsp::fixed::CrudeAmplitude) are *built from* a Ramp and do
/// the actual per-sample advance during render. The headline use is a voice's per-block VCA amplitude.
class Ramp {
	int32_t value_;
	int32_t increment_;

public:
	/// @param value the ramp's start value.
	/// @param increment the per-sample step added to @p value.
	constexpr Ramp(int32_t value, int32_t increment) : value_{value}, increment_{increment} {}

	[[nodiscard]] constexpr int32_t value() const { return value_; }
	[[nodiscard]] constexpr int32_t increment() const { return increment_; }

	/// @brief The same ramp with both value and slope shifted left by @p bits — i.e. scaled by `2^bits`. The
	/// oscillator paths use this for the legacy per-wave amplitude doublings (`amplitude << 1`).
	[[nodiscard]] constexpr Ramp operator<<(int bits) const { return Ramp{value_ << bits, increment_ << bits}; }
};

} // namespace deluge::dsp
