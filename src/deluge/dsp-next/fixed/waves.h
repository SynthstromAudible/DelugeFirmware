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
#include <limits>

namespace deluge::dsp::fixed {

/// @file
/// @brief Stateless analytic (non-band-limited) wave kernels — the leaf math for the crude
/// oscillators. Pure functions of phase, owning no phase/frequency (the generator owns those).
///
/// These are dsp-next's own copies, snake_case, so the oscillator library is self-contained
/// rather than reaching up into the app's @c util/waves.h — which keeps its camelCase
/// `getSquare`/`getTriangleSmall` for its many other consumers (LFOs, envelopes, GUI
/// animation). The bodies are identical, so substituting them is bit-exact. Mirrors how the
/// sine table evaluator moved to @ref sine.h while @c getSine stayed in util for the rest.

/// @brief Square wave: full-scale, switching at the pulse-width threshold.
/// @param phase Full-circle q32 phase (2^32 == one cycle).
/// @param pulse_width Threshold phase; @p phase at or above it gives the low half. A threshold
///        of 2^31 is a 50% square; other values make a (non-band-limited) pulse wave.
/// @return @c INT32_MIN (low) or @c INT32_MAX (high).
[[nodiscard, gnu::always_inline]] inline int32_t square(uint32_t phase, uint32_t pulse_width) {
	return (phase >= pulse_width) ? std::numeric_limits<int32_t>::min() : std::numeric_limits<int32_t>::max();
}

/// @brief Triangle wave at half scale (±2^30): folds the phase about its midpoint.
///
/// Half scale (not full like @ref square) is deliberate and load-bearing: the crude triangle's
/// VCA multiplies this by a *doubled* amplitude, and `value * (amp<<1)` differs from
/// `(value<<1) * amp` exactly when `amp<<1` overflows int32 — so full-scaling the kernel would
/// be an (unprovable) behavioural change. TODO(audible): unify wave scales + amplitude
/// convention in the re-baselined audible-improvement phase.
/// @param phase Full-circle q32 phase (2^32 == one cycle).
/// @return The triangle sample, in [-2^30, 2^30).
[[nodiscard, gnu::always_inline]] inline int32_t triangle(uint32_t phase) {
	if (phase >= 2147483648u) {
		phase = -phase;
	}
	return static_cast<int32_t>(phase - 1073741824u);
}

} // namespace deluge::dsp::fixed
