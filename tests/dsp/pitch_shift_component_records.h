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
/// @brief The live pitch shifter's own records over every workload (pitch_shift_workloads.h): the legacy classes'
/// records (pitch_shift_workload_records.h) but where a quirk's fix changed what the shifter renders
/// (docs/dev/live_pitch_shifter_quirks.md). live_pitch_shifter_spec names the workloads each fix reaches.

#pragma once

#include "pitch_shift_workloads.h"

#include <array>

namespace pitch_shift_workloads {

/// @brief What the live pitch shifter renders from each workload, in WorkloadId order, identical on the host and on
/// ARM.
inline constexpr std::array<WorkloadRecord, kWorkloadCount> kShifterRecords{{
    // UP7_SAW_LEFT
    {.outputs = 0x1c548255f0b402cdULL, .hops = 0x82939e542dd13f25ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP7_SAW_RIGHT
    {.outputs = 0x5a0f537da6e93d4fULL, .hops = 0xec82f3ee74ec3b04ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP7_SAW_STEREO
    {.outputs = 0x4a39a4de848aa257ULL, .hops = 0x267da5ec2d76b745ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP7_PERC_LEFT
    {.outputs = 0xab6707606a16ebaeULL, .hops = 0x34a5ec1a11971a35ULL, .lifecycle = 0x9368f381c1e34195ULL},
    // UP7_PERC_STEREO
    {.outputs = 0x35bb3e55bcb80c22ULL, .hops = 0x281c42d42ffd7034ULL, .lifecycle = 0x9368f381c1e34195ULL},
    // DOWN12_PERC_LEFT
    {.outputs = 0x41eb0d2a9f4595b4ULL, .hops = 0xfe945b4b9e32b525ULL, .lifecycle = 0x13f631ef6a6fdd25ULL},
    // DOWN12_PERC_STEREO
    {.outputs = 0x876aa5da92f3bfb9ULL, .hops = 0x7bb57b12c3e35a64ULL, .lifecycle = 0x13f631ef6a6fdd25ULL},
    // DOWN24_SAW_LEFT
    {.outputs = 0xefea2bb3570cf65fULL, .hops = 0xdf0982665c4abf05ULL, .lifecycle = 0x13f631ef6a6fdd25ULL},
    // DOWN5_SINE_LEFT
    {.outputs = 0x71c29cce1894857fULL, .hops = 0x74a1659afc830240ULL, .lifecycle = 0x89290f58593c8ea5ULL},
    // DOWN1_SINE_LEFT
    {.outputs = 0xba873b27f45cb94cULL, .hops = 0x7da421b827bb3797ULL, .lifecycle = 0x89290f58593c8ea5ULL},
    // UP_HALF_SINE_LEFT
    {.outputs = 0x9911d3f711b0260dULL, .hops = 0x3cf5f1abe3082225ULL, .lifecycle = 0x5616e352c838ea25ULL},
    // UP1_SINE_LEFT
    {.outputs = 0x9ea6cabbcf52621aULL, .hops = 0xcd4a2627c65533c4ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP5_PERC_LEFT
    {.outputs = 0xe72d193bcc9c9373ULL, .hops = 0x7a8334fcf9ae9574ULL, .lifecycle = 0x9368f381c1e34195ULL},
    // UP12_SINE_STEREO
    {.outputs = 0xba3e2ba11e1ec749ULL, .hops = 0x258b06f511c67464ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP24_PERC_LEFT
    {.outputs = 0x205aa1d0970919f1ULL, .hops = 0x2b76d584b149a7f5ULL, .lifecycle = 0x9368f381c1e34195ULL},
    // UP24_SAW_STEREO
    {.outputs = 0x72d01680e5f7349dULL, .hops = 0x1a9b362b6bff12e6ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP7_LINEAR_PERC_LEFT
    {.outputs = 0x42be4978228a9f8ULL, .hops = 0x34a5ec1a11971a35ULL, .lifecycle = 0x9368f381c1e34195ULL},
    // DOWN12_LINEAR_SINE_STEREO
    {.outputs = 0x87e7b33cab8c7b6aULL, .hops = 0xee7aea3cddb681e5ULL, .lifecycle = 0x89290f58593c8ea5ULL},
    // UP7_BLOCK1_PERC_LEFT
    {.outputs = 0x398ff67e0c7b4901ULL, .hops = 0x30285b3976e39f54ULL, .lifecycle = 0xdcc0ba8d98922f25ULL},
    // DOWN12_BLOCK7_PERC_LEFT
    {.outputs = 0x302f05f233494887ULL, .hops = 0xe935b9fc476bc9f5ULL, .lifecycle = 0x2071bda8adf0c205ULL},
    // UP7_BLOCK32_SINE_STEREO
    {.outputs = 0x50b28c46c0f9bd8aULL, .hops = 0xf767c6813700e424ULL, .lifecycle = 0x8c97755e5defcee5ULL},
    // GLIDE_PERC_LEFT
    {.outputs = 0xd10d2149f796b906ULL, .hops = 0xe839d0986189c717ULL, .lifecycle = 0xf60d6b0daf5f4a05ULL},
    // GLIDE_SAW_STEREO
    {.outputs = 0x4a0f81d5dcebd09bULL, .hops = 0x1276e9ee51717e54ULL, .lifecycle = 0x7f87e203e1369d17ULL},
    // SHARED_UP7_PERC_LEFT
    {.outputs = 0x9e7319c9797fb297ULL, .hops = 0x6ad0cbb49002b424ULL, .lifecycle = 0x291555d30af63cf5ULL},
    // SHARED_DOWN5_PERC_STEREO
    {.outputs = 0xda7c50adafcf494bULL, .hops = 0x76426fec53b14fd4ULL, .lifecycle = 0x983f8fbeaa5804a5ULL},
    // SILENCE_UP7_LEFT
    {.outputs = 0xf39ad02ee046d7caULL, .hops = 0x1a61b42ad699cf94ULL, .lifecycle = 0x715e4354d13ef495ULL},
    // SILENCE_DOWN12_LEFT
    {.outputs = 0xb9be06ed94715767ULL, .hops = 0x3001803af585b034ULL, .lifecycle = 0xa947e50590de8025ULL},
}};

} // namespace pitch_shift_workloads
