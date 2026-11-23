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
/// @brief The live pitch shifter's records over every workload (pitch_shift_workloads.h), recorded from the legacy
/// classes in the app before their rewrite; the oracle and the dsp-next component must equal them.

#pragma once

#include "pitch_shift_workloads.h"

#include <array>

namespace pitch_shift_workloads {

/// @brief What the legacy classes rendered from each workload, in WorkloadId order, identical on the host and on ARM.
inline constexpr std::array<WorkloadRecord, kWorkloadCount> kRecordedReference{{
    // UP7_SAW_LEFT
    {.outputs = 0xc165bf528f48c2f5ULL, .hops = 0x8b390b7bd26f9bc4ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP7_SAW_RIGHT
    {.outputs = 0xc325f78f0d8ea3d8ULL, .hops = 0x8947e525898ae284ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP7_SAW_STEREO
    {.outputs = 0x6786649d9fc8ede1ULL, .hops = 0xf9678892f189c95ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP7_PERC_LEFT
    {.outputs = 0xb0a101506de2dcfULL, .hops = 0xaba36035f5a49f05ULL, .lifecycle = 0x9368f381c1e34195ULL},
    // UP7_PERC_STEREO
    {.outputs = 0x9ede3f214d5c4ce5ULL, .hops = 0x2613b2bd22caf74ULL, .lifecycle = 0x9368f381c1e34195ULL},
    // DOWN12_PERC_LEFT
    {.outputs = 0xd653d00f950f6469ULL, .hops = 0xebc62cf759e5fe35ULL, .lifecycle = 0x13f631ef6a6fdd25ULL},
    // DOWN12_PERC_STEREO
    {.outputs = 0x795fe464ce5b78d6ULL, .hops = 0x2e781adf45e5cd85ULL, .lifecycle = 0x13f631ef6a6fdd25ULL},
    // DOWN24_SAW_LEFT
    {.outputs = 0x9231730a59a60353ULL, .hops = 0xc18597bd8c657db4ULL, .lifecycle = 0x13f631ef6a6fdd25ULL},
    // DOWN5_SINE_LEFT
    {.outputs = 0x62b5db3a0daca3c9ULL, .hops = 0xb762a241bd884585ULL, .lifecycle = 0x89290f58593c8ea5ULL},
    // DOWN1_SINE_LEFT
    {.outputs = 0x3447756eccc4d57dULL, .hops = 0xae041b3b3b34ddc4ULL, .lifecycle = 0x89290f58593c8ea5ULL},
    // UP_HALF_SINE_LEFT
    {.outputs = 0x9911d3f711b0260dULL, .hops = 0x3cf5f1abe3082225ULL, .lifecycle = 0x5616e352c838ea25ULL},
    // UP1_SINE_LEFT
    {.outputs = 0x9f35c273e13b8282ULL, .hops = 0x4bba1ad05ad44bc4ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP5_PERC_LEFT
    {.outputs = 0xb3c72e6917594072ULL, .hops = 0x68ed77d659bd9bd5ULL, .lifecycle = 0x9368f381c1e34195ULL},
    // UP12_SINE_STEREO
    {.outputs = 0xe74581b24ac19c11ULL, .hops = 0x4cc6cf6d6691e575ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP24_PERC_LEFT
    {.outputs = 0xd57ee3d75eec82caULL, .hops = 0xd65dcb2b2792cd65ULL, .lifecycle = 0x9368f381c1e34195ULL},
    // UP24_SAW_STEREO
    {.outputs = 0x81ebab5818c6badULL, .hops = 0x176ae05ab5c51d26ULL, .lifecycle = 0x32d1e61076787b15ULL},
    // UP7_LINEAR_PERC_LEFT
    {.outputs = 0x138e1d40d2957f10ULL, .hops = 0xaba36035f5a49f05ULL, .lifecycle = 0x9368f381c1e34195ULL},
    // DOWN12_LINEAR_SINE_STEREO
    {.outputs = 0x53135e67d7e483c3ULL, .hops = 0x6a5881be459b2254ULL, .lifecycle = 0x89290f58593c8ea5ULL},
    // UP7_BLOCK1_PERC_LEFT
    {.outputs = 0xa81a7fcd95baade4ULL, .hops = 0x1023880c209eae05ULL, .lifecycle = 0xdcc0ba8d98922f25ULL},
    // DOWN12_BLOCK7_PERC_LEFT
    {.outputs = 0x302f05f233494887ULL, .hops = 0xe935b9fc476bc9f5ULL, .lifecycle = 0x2071bda8adf0c205ULL},
    // UP7_BLOCK32_SINE_STEREO
    {.outputs = 0x7adcf3ba40bba510ULL, .hops = 0xf42734f1b1ac8005ULL, .lifecycle = 0x8c97755e5defcee5ULL},
    // GLIDE_PERC_LEFT
    {.outputs = 0x4e60fbcaefe26f7aULL, .hops = 0x5e3eb3ca9d5242f7ULL, .lifecycle = 0xf1e2b6f952cc9554ULL},
    // GLIDE_SAW_STEREO
    {.outputs = 0xea23f4fa913ca97ULL, .hops = 0x1a5a982c69af3535ULL, .lifecycle = 0x2e9a7b25629bdc54ULL},
    // SHARED_UP7_PERC_LEFT
    {.outputs = 0x2ac2ad032dd241faULL, .hops = 0xd4c9be12aa1f6794ULL, .lifecycle = 0x291555d30af63cf5ULL},
    // SHARED_DOWN5_PERC_STEREO
    {.outputs = 0xf0b33d79da01dd95ULL, .hops = 0x4f2108f4cf8f6795ULL, .lifecycle = 0x983f8fbeaa5804a5ULL},
    // SILENCE_UP7_LEFT
    {.outputs = 0x2d8cd6f364969b51ULL, .hops = 0x9f5cb579f8d67484ULL, .lifecycle = 0x715e4354d13ef495ULL},
    // SILENCE_DOWN12_LEFT
    {.outputs = 0xb9be06ed94715767ULL, .hops = 0x3001803af585b034ULL, .lifecycle = 0xa947e50590de8025ULL},
}};

} // namespace pitch_shift_workloads
