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

/// @file
/// @brief The stutter reference's records over every workload (stutter_workloads.h), which the kernel and the
/// component must equal.

#pragma once

#include "stutter_workloads.h"

#include <array>

namespace stutter_workloads {

/// @brief What the reference renders from each workload, in WorkloadId order: recorded from it, identical on the host
/// and on ARM.
inline const std::array<WorkloadRecord, kWorkloadCount> kRecordedReference{{
    // RECORD_NATIVE
    {.outputs = 0xc0f3ae0719fe2674ULL, .lifecycle = 0x5b1e15350f8c47f5ULL, .tape = 0x439ba3e453f7f060ULL},
    // RECORD_RESAMPLED
    {.outputs = 0x44c1c640047fee6dULL, .lifecycle = 0x351c07620b9dc281ULL, .tape = 0xe5e024c6fc9b03eaULL},
    // PLAY_FORWARD_NATIVE
    {.outputs = 0xb07893f360439cf5ULL, .lifecycle = 0x39a4ee7a41988313ULL, .tape = 0x439ba3e453f7f060ULL},
    // PLAY_FORWARD_RESAMPLED
    {.outputs = 0xa811ab728adece4ULL, .lifecycle = 0x6f0b4034c72fdf1dULL, .tape = 0x11e23d2f6a84317dULL},
    // PLAY_REVERSED_NATIVE
    {.outputs = 0x859e9f5c46683a64ULL, .lifecycle = 0x13591ba094dfb733ULL, .tape = 0x439ba3e453f7f060ULL},
    // PLAY_REVERSED_RESAMPLED
    {.outputs = 0x3624c7d3ac1acd09ULL, .lifecycle = 0x21d553c44bd38d9dULL, .tape = 0xfbda2dda90521003ULL},
    // PING_PONG_NATIVE
    {.outputs = 0x86d831954ba16a06ULL, .lifecycle = 0x4099c7684c7a369eULL, .tape = 0x439ba3e453f7f060ULL},
    // PING_PONG_RESAMPLED
    {.outputs = 0x13918184c15283e4ULL, .lifecycle = 0x6976644ab30ad355ULL, .tape = 0x11e23d2f6a84317dULL},
    // RATE_MOVES
    {.outputs = 0x8390d04a1980603cULL, .lifecycle = 0x8e1c6fc4739971aaULL, .tape = 0x45b450819ee24691ULL},
    // BLOCKS_1_TO_9
    {.outputs = 0x48c021f4a8f96a2bULL, .lifecycle = 0x733f7cd26f936f7eULL, .tape = 0x5ec6cd281afc7501ULL},
    // FULL_SCALE
    {.outputs = 0x12008ec54fad7803ULL, .lifecycle = 0x6b132dff6eb9a843ULL, .tape = 0x4b423bff15217d45ULL},
    // CLAMPED
    {.outputs = 0x1647eb0331204911ULL, .lifecycle = 0xad9ec3a27f65a6a9ULL, .tape = 0x56febf11a0e1ba19ULL},
}};

} // namespace stutter_workloads
