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

#include <argon.hpp>
#include <cstdint>

/// @file
/// @brief Stateless q31 sine-table evaluator — the leaf oscillator math kernel.

namespace deluge::dsp::fixed {

/// @brief Evaluate the sine table at four phases at once, with linear interpolation.
///
/// This is a pure function of @p phase, **not** an oscillator: it owns no phase or
/// frequency. Phase generation — advancing by an increment, unison spread, hard sync —
/// belongs to the caller (today @c Voice, later the source engine), not here.
///
/// Frequency modulation needs no separate entry point: FM is simply evaluating the
/// sine at a modulated phase, so the caller adds the modulation to @p phase, e.g.
/// @code sine(phase + (modulation << 8)) @endcode
///
/// @param phase Four phase positions, each a full-circle q32 value (2^32 == one cycle).
/// @return Four interpolated sine samples, each left-shifted into the high half of its
///         32-bit lane (Q1.31 scale, not reduced to a clean q31); the caller applies
///         amplitude.
[[nodiscard]] Argon<int32_t> sine(Argon<uint32_t> phase);

/// @brief One sample of an FM operator's sine, in the operator's 24-bit phase format.
///
/// The scalar step for the serial FM **feedback** path, which has a sample-to-sample
/// data dependency and so cannot vectorise. It is deliberately not the scalar
/// counterpart of sine(): it evaluates in a 24-bit phase (the carrier shifted down by
/// 8) and omits the interpolation doubling, so its output is bit-distinct. This is an
/// FM-operator concern that merely shares the sine table; it will move to the FM engine
/// in Phase 3.
///
/// @param carrier_phase The operator's carrier phase, a full-circle q32 value.
/// @param phase_shift The phase modulation to add, in 24-bit phase units.
/// @return The interpolated sine sample, in the high half of the result (Q1.31 scale).
[[nodiscard]] int32_t sine_fm_operator(uint32_t carrier_phase, uint32_t phase_shift);

} // namespace deluge::dsp::fixed
