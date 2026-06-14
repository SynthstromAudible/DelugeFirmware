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
/// Apply it with `multiply_32x32_rshift32(overshoot, reciprocal) << 17` (an effective >> 47).
/// Both sync components (@ref SyncedPhasor, @ref SyncedOscillator) derive this from the resetter
/// increment they already hold, rather than having the caller precompute it. The increment is
/// rounded up first so the reciprocal comes out a touch small — products of it can't overflow.
///
/// @p resetter_increment of 0 means there is no resetter (the caller isn't syncing); the reciprocal
/// is then unused, but the division must still not run — a 0 increment makes the divisor 0, and on
/// ARM `__aeabi_idiv0` raises SIGFPE (x86 -m32 libgcc quietly returns garbage, which is why it only
/// bites on hardware/QEMU). Return 0: a non-crossing resetter has no fractional reset position, so
/// the value this feeds is correctly nothing.
[[nodiscard]] inline int32_t resetter_reciprocal(uint32_t resetter_increment) {
	if (resetter_increment == 0) {
		return 0;
	}
	return static_cast<int32_t>(2147483648u / static_cast<uint16_t>((resetter_increment + 65535) >> 16));
}

} // namespace deluge::dsp::fixed
