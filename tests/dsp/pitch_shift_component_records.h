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
    {.outputs = 0x2793a2fa6d07ff2dULL, .hops = 0x8b390b7bd26f9bc4ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP7_SAW_RIGHT
    {.outputs = 0x82484995a39dc158ULL, .hops = 0x8947e525898ae284ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP7_SAW_STEREO
    {.outputs = 0x9fd3ba620c1338a9ULL, .hops = 0xf9678892f189c95ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP7_PERC_LEFT
    {.outputs = 0xc0d3b3f0db0460bcULL, .hops = 0xaba36035f5a49f05ULL, .lifecycle = 0x9368f381c1e34195ULL},
    // UP7_PERC_STEREO
    {.outputs = 0x56a3cca8bcc48498ULL, .hops = 0x70c955b1a01064a4ULL, .lifecycle = 0x9368f381c1e34195ULL},
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
    {.outputs = 0xbdeb1f0995f22286ULL, .hops = 0x4d95f4e4cc2533c4ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP5_PERC_LEFT
    {.outputs = 0x65217ea686c57728ULL, .hops = 0x68ed77d659bd9bd5ULL, .lifecycle = 0x9368f381c1e34195ULL},
    // UP12_SINE_STEREO
    {.outputs = 0xe74581b24ac19c11ULL, .hops = 0x4cc6cf6d6691e575ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP24_PERC_LEFT
    {.outputs = 0x801a3a18c5cf208eULL, .hops = 0x5c8eecfaa9c5c7b6ULL, .lifecycle = 0x9368f381c1e34195ULL},
    // UP24_SAW_STEREO
    {.outputs = 0xb1a1c9f5cf66e6f6ULL, .hops = 0x590623489afdaed5ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP7_LINEAR_PERC_LEFT
    {.outputs = 0x157859c3d7098d3bULL, .hops = 0xaba36035f5a49f05ULL, .lifecycle = 0x9368f381c1e34195ULL},
    // DOWN12_LINEAR_SINE_STEREO
    {.outputs = 0x87e7b33cab8c7b6aULL, .hops = 0xee7aea3cddb681e5ULL, .lifecycle = 0x89290f58593c8ea5ULL},
    // UP7_BLOCK1_PERC_LEFT
    {.outputs = 0xa81a7fcd95baade4ULL, .hops = 0x1023880c209eae05ULL, .lifecycle = 0xdcc0ba8d98922f25ULL},
    // DOWN12_BLOCK7_PERC_LEFT
    {.outputs = 0x302f05f233494887ULL, .hops = 0xe935b9fc476bc9f5ULL, .lifecycle = 0x2071bda8adf0c205ULL},
    // UP7_BLOCK32_SINE_STEREO
    {.outputs = 0x7adcf3ba40bba510ULL, .hops = 0xf42734f1b1ac8005ULL, .lifecycle = 0x8c97755e5defcee5ULL},
    // GLIDE_PERC_LEFT
    {.outputs = 0x6ced881a3b4bd8f9ULL, .hops = 0xc95032b67b8b0504ULL, .lifecycle = 0xf1e2b6f952cc9554ULL},
    // GLIDE_SAW_STEREO
    {.outputs = 0xba3e63c1e0857812ULL, .hops = 0xa20ab643f3b12cc7ULL, .lifecycle = 0x2e9a7b25629bdc54ULL},
    // SHARED_UP7_PERC_LEFT
    {.outputs = 0xb184d1160ff4d74cULL, .hops = 0x14103f751eea4935ULL, .lifecycle = 0x291555d30af63cf5ULL},
    // SHARED_DOWN5_PERC_STEREO
    {.outputs = 0xda7c50adafcf494bULL, .hops = 0x76426fec53b14fd4ULL, .lifecycle = 0x983f8fbeaa5804a5ULL},
    // SILENCE_UP7_LEFT
    {.outputs = 0x2950f3f0898bc80fULL, .hops = 0x9f5cb579f8d67484ULL, .lifecycle = 0x715e4354d13ef495ULL},
    // SILENCE_DOWN12_LEFT
    {.outputs = 0xb9be06ed94715767ULL, .hops = 0x3001803af585b034ULL, .lifecycle = 0xa947e50590de8025ULL},
}};

} // namespace pitch_shift_workloads
