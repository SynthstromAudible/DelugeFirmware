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

namespace deluge::dsp::fixed {

/// @brief The reciprocal of a hard-sync resetter's phase increment, used to turn the resetter's
/// overshoot past a cycle boundary into the fractional sample position of the reset.
///
/// Read as Q<16>, it is 1 / the increment read as Q31 (2^47 / increment in raw terms): `mul` a Q31 overshoot by it
/// for the overshoot's fraction of a sample at Q15, which `as_wrap<Q<31>>` lines up at Q31.
/// Both sync components (@ref SyncedPhasor, @ref SyncedOscillator) derive this from the resetter
/// increment they already hold, rather than having the caller precompute it.
///
/// The divisor is the increment's top 16 bits rounded up, 1 to 2^16, so the reciprocal comes out a touch small and
/// a reset lands at or before its exact phase, never past the next sample's. An increment up to 2^16 (a master
/// slower than about 0.67 Hz) has a divisor of 1, whose 2^31 doesn't fit: it clamps to 2^31 − 1, which places its
/// resets earlier still but within the sample.
///
/// @p resetter_increment of 0 means the resetter never crosses a boundary (@ref SyncWindower renders
/// the block unsynced); the reciprocal is then unused, but the division must still not run: on the
/// Embassy BSP the division intrinsic traps (UNDEF) on a zero divisor. Return 0: a non-crossing
/// resetter has no fractional reset position, so the value this feeds is correctly nothing.
[[nodiscard]] inline int32_t resetter_reciprocal(uint32_t resetter_increment) {
	if (resetter_increment == 0) {
		return 0;
	}
	// Rounded up without forming increment + 65535, which wraps for an increment above 0xFFFF0000 (Voice::adjustPitch
	// can produce one: it only rejects results at or above 2^32)
	const uint32_t rounded_up = (resetter_increment >> 16) + ((resetter_increment & 65535) != 0 ? 1 : 0);
	if (rounded_up == 1) {
		return INT32_MAX;
	}
	// In double, 2^31 / rounded_up is exact when it is a whole number and otherwise at least 2^-16 from one, far more
	// than its rounding, so truncating it gives the integer quotient; and the A9 divides doubles in hardware but not
	// integers, so it is much quicker than the library's integer divide
	return static_cast<int32_t>(2147483648.0 / rounded_up);
}

} // namespace deluge::dsp::fixed
