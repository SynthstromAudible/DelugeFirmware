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
    0xc5dd199c14418989ULL, // UP7_SAW_LEFT
    0xef3b8b73ec2cd4c0ULL, // UP7_SAW_RIGHT
    0xcca776721cdfcaa1ULL, // UP7_SAW_STEREO
    0x34bb15d30f478fe9ULL, // UP7_PERC_LEFT
    0x55735d3d10f5f9a3ULL, // UP7_PERC_STEREO
    0xdc2f0306c1a8f280ULL, // DOWN12_PERC_LEFT
    0x604336f3ced8e0a5ULL, // DOWN12_PERC_STEREO
    0xbf4145afce87ea21ULL, // DOWN24_SAW_LEFT
    0x3b7d35d1f91e11a3ULL, // DOWN5_SINE_LEFT
    0xb01aa8b2e179a481ULL, // DOWN1_SINE_LEFT
    0x48f13c13dbdfcd15ULL, // UP_HALF_SINE_LEFT
    0xe817ce307fd8ef5cULL, // UP1_SINE_LEFT
    0x5ece8ff83f914c1aULL, // UP5_PERC_LEFT
    0x8d8420ec54425328ULL, // UP12_SINE_STEREO
    0x758784c23cdcaa33ULL, // UP24_PERC_LEFT
    0x4f3e7fd7309ae28fULL, // UP24_SAW_STEREO
    0x764a9b1bb92cbcf2ULL, // UP7_LINEAR_PERC_LEFT
    0x92eea86408317aacULL, // DOWN12_LINEAR_SINE_STEREO
    0xbc5d09b14db5bbcbULL, // UP7_BLOCK1_PERC_LEFT
    0x2a0d384d77d96012ULL, // DOWN12_BLOCK7_PERC_LEFT
    0x2c94a166b82dd67ULL,  // UP7_BLOCK32_SINE_STEREO
    0x4fe396159017ba7fULL, // GLIDE_PERC_LEFT
    0xb5d7182fb7faa1efULL, // GLIDE_SAW_STEREO
    0x0ULL,                // SHARED_UP7_PERC_LEFT: skipped (shared)
    0x0ULL,                // SHARED_DOWN5_PERC_STEREO: skipped (shared)
    0xae20fdf24f4d7f7ULL,  // SILENCE_UP7_LEFT
    0x85c13b6ef5bbc132ULL, // SILENCE_DOWN12_LEFT
}};
