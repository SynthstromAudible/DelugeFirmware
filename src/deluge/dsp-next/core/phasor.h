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

/// @brief A phase accumulator: the single home for an oscillator's phase state.
///
/// Phase and increment are UQ<32>: 2^32 is one cycle, so the phase wraps for free (@ref add_wrap). @ref increment
/// is the per-sample phase step (frequency · 2^32 / sampleRate). Oscillators own a Phasor rather than threading a raw
/// phase reference, which is what lets a combinator like @ref deluge::dsp::fixed::SyncedOscillator re-seed phase
/// (@ref seek) at a sync crossover without knowing the oscillator's internals.
///
/// @ref advance pre-increments: it steps the phase *then* returns it, matching the legacy render kernels
/// (`phase += increment; use(phase)` per sample), so substituting a Phasor is bit-exact.
class Phasor {
	UQ<32> phase_;
	UQ<32> increment_;

public:
	/// @brief A phasor at @p phase advancing by @p increment per sample.
	/// @param phase The starting phase.
	/// @param increment The per-sample phase step.
	constexpr Phasor(UQ<32> phase, UQ<32> increment) : phase_{phase}, increment_{increment} {}

	/// @brief Step the phase by one increment, wrapping, and return the new value (pre-increment semantics).
	/// @return The phase after the step.
	[[gnu::always_inline]] constexpr UQ<32> advance() {
		phase_ = add_wrap(phase_, increment_);
		return phase_;
	}

	/// @brief The current phase.
	/// @return The phase.
	[[nodiscard, gnu::always_inline]] constexpr UQ<32> phase() const { return phase_; }
	/// @brief The per-sample phase step.
	/// @return The increment.
	[[nodiscard, gnu::always_inline]] constexpr UQ<32> increment() const { return increment_; }

	/// @brief Jump the phase to @p phase (e.g. a hard-sync reset). Leaves the increment unchanged.
	/// @param phase The new phase.
	[[gnu::always_inline]] constexpr void seek(UQ<32> phase) { phase_ = phase; }
	/// @brief Set the per-sample phase step, keeping the phase.
	/// @param increment The new per-sample step.
	[[gnu::always_inline]] constexpr void set_increment(UQ<32> increment) { increment_ = increment; }
};

} // namespace deluge::dsp
