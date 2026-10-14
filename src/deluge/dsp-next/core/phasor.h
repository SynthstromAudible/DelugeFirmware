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

/// @brief A q32 phase accumulator: the single home for an oscillator's phase state.
///
/// Phase is unsigned 0.32 fixed-point — the full uint32 range is one cycle, so it wraps
/// for free. @ref increment is the per-sample phase step (frequency · 2^32 / sampleRate).
/// Oscillators own a Phasor rather than threading a raw `uint32_t&`, which is what lets a
/// combinator like @ref deluge::dsp::fixed::SyncedOscillator re-seed phase (@ref seek) at a
/// sync crossover without knowing the oscillator's internals.
///
/// @ref advance pre-increments: it steps the phase *then* returns it, matching the legacy
/// render kernels (`phase += increment; use(phase)` per sample), so substituting a Phasor is
/// bit-exact.
class Phasor {
	uint32_t phase_;
	uint32_t increment_;

public:
	/// @brief A phasor at @p phase advancing by @p increment per sample.
	/// @param phase The starting phase, 0.32 unsigned: the full range is one cycle.
	/// @param increment The per-sample phase step, 0.32 unsigned.
	constexpr Phasor(uint32_t phase, uint32_t increment) : phase_{phase}, increment_{increment} {}

	/// @brief Step the phase by one increment and return the new value (pre-increment semantics).
	/// @return The phase after the step, 0.32 unsigned.
	[[gnu::always_inline]] constexpr uint32_t advance() {
		phase_ += increment_;
		return phase_;
	}

	/// @brief The current phase.
	/// @return The phase, 0.32 unsigned.
	[[nodiscard, gnu::always_inline]] constexpr uint32_t phase() const { return phase_; }
	/// @brief The per-sample phase step.
	/// @return The increment, 0.32 unsigned.
	[[nodiscard, gnu::always_inline]] constexpr uint32_t increment() const { return increment_; }

	/// @brief Jump the phase to @p phase (e.g. a hard-sync reset). Leaves the increment unchanged.
	/// @param phase The new phase, 0.32 unsigned.
	[[gnu::always_inline]] constexpr void seek(uint32_t phase) { phase_ = phase; }
	/// @brief Set the per-sample phase step, keeping the phase.
	/// @param increment The new per-sample step, 0.32 unsigned.
	[[gnu::always_inline]] constexpr void setIncrement(uint32_t increment) { increment_ = increment; }
};

} // namespace deluge::dsp
