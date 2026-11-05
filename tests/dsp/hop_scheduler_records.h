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
/// @brief The hop scheduler's records over every workload (pitch_shift_workloads.h): each block's hop count, the hop's
/// progress after it, the noise and the output, hashed, in WorkloadId order (0 for the shared workloads, which the
/// scheduler spec skips).

#pragma once

#include "pitch_shift_workloads.h"

#include <array>
#include <cstdint>

inline constexpr std::array<uint64_t, pitch_shift_workloads::kWorkloadCount> kSchedulerRecords{{
    0xdcd24539e9c693fbULL, // UP7_SAW_LEFT
    0x54a9b16830f0c4cbULL, // UP7_SAW_RIGHT
    0x6a0dddaa946d4686ULL, // UP7_SAW_STEREO
    0xa2efc00053292d68ULL, // UP7_PERC_LEFT
    0x180486501bb422dULL,  // UP7_PERC_STEREO
    0x5143ec6cfa4562edULL, // DOWN12_PERC_LEFT
    0xd3abcdfd3e7925aaULL, // DOWN12_PERC_STEREO
    0x2fa82a1be08f87c6ULL, // DOWN24_SAW_LEFT
    0xb1795cad00ce49c4ULL, // DOWN5_SINE_LEFT
    0xc068e019627c27bdULL, // DOWN1_SINE_LEFT
    0x48f13c13dbdfcd15ULL, // UP_HALF_SINE_LEFT
    0x2b422ec276c688b2ULL, // UP1_SINE_LEFT
    0x62b3a013edbe3563ULL, // UP5_PERC_LEFT
    0x8d8420ec54425328ULL, // UP12_SINE_STEREO
    0x37b8bd4183f945c3ULL, // UP24_PERC_LEFT
    0x3bd72ddca72cdc8cULL, // UP24_SAW_STEREO
    0x115495c73fd20467ULL, // UP7_LINEAR_PERC_LEFT
    0x4ebbb54cf8b8182bULL, // DOWN12_LINEAR_SINE_STEREO
    0xbc5d09b14db5bbcbULL, // UP7_BLOCK1_PERC_LEFT
    0x2a0d384d77d96012ULL, // DOWN12_BLOCK7_PERC_LEFT
    0x2c94a166b82dd67ULL,  // UP7_BLOCK32_SINE_STEREO
    0x90616c07b03553feULL, // GLIDE_PERC_LEFT
    0x7e8f8b092a0d8d20ULL, // GLIDE_SAW_STEREO
    0x0ULL,                // SHARED_UP7_PERC_LEFT: skipped (shared)
    0x0ULL,                // SHARED_DOWN5_PERC_STEREO: skipped (shared)
    0x8771c6c5fda975f7ULL, // SILENCE_UP7_LEFT
    0x85c13b6ef5bbc132ULL, // SILENCE_DOWN12_LEFT
}};
