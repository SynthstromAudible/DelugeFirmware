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

#include "dsp-next/core/fixed_point.h"

namespace deluge::dsp {

/// @brief A linear gain ramp: a value advancing by a fixed per-sample increment, the amplitude-domain analogue of
/// @ref Phasor (which ramps phase). It carries the ramp's parameters (start value and slope); the VCA components
/// (@ref deluge::dsp::fixed::Amplitude and @ref deluge::dsp::fixed::CrudeAmplitude) are built from a Ramp and do the
/// per-sample advance during render. The headline use is a voice's per-block VCA amplitude.
///
/// The value is a Q<31> gain. Both VCAs take the high word of sample × gain (`vqdmulh` of the halved gain, or
/// `smmlar`), so their product is Q30: a voice's mix is summed at half scale. A source that pre-scales the gain
/// (@ref scale_shl_wrap) lets it wrap, as the legacy shifts do.
class Ramp {
	Q<31> value_;
	Q<31> increment_;

public:
	/// @param value the ramp's start value, a Q<31> gain.
	/// @param increment the per-sample step added to @p value, Q<31>.
	constexpr Ramp(Q<31> value, Q<31> increment) : value_{value}, increment_{increment} {}

	/// @return The ramp's start value, a Q<31> gain.
	[[nodiscard]] constexpr Q<31> value() const { return value_; }
	/// @return The per-sample step, Q<31>.
	[[nodiscard]] constexpr Q<31> increment() const { return increment_; }
};

/// @brief @p ramp with value and slope both scaled by 2^N, wrapping: the oscillator paths' per-wave amplitude
/// doublings.
/// @param ramp The ramp to scale.
/// @return The scaled ramp.
template <int N>
[[gnu::always_inline, nodiscard]] constexpr Ramp scale_shl_wrap(Ramp ramp) noexcept {
	return Ramp{scale_shl_wrap<N>(ramp.value()), scale_shl_wrap<N>(ramp.increment())};
}

} // namespace deluge::dsp
