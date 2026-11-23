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
/// @brief The reverb reference's records over every workload (reverb_workloads.h), which the kernels and the component
/// must equal.

#pragma once

#include "reverb_workloads.h"

#include <array>

namespace reverb_workloads {

/// @brief What the reference renders from each workload, in WorkloadId order: recorded from it, identical on the host
/// and on ARM.
inline const std::array<WorkloadRecord, kWorkloadCount> kRecordedReference{{
    // FREEVERB_BOOT
    {.outputs = 0x29f3d0b572121cf7ULL, .state = 0xa132a6772d4b5924ULL, .send = 0x55ca4862105a986bULL},
    // FREEVERB_TURNED
    {.outputs = 0x2f90678be2d385c0ULL, .state = 0x34b03710c3506d79ULL, .send = 0xe0fe2a4b3d69cacfULL},
    // FREEVERB_BLOCKS_1_TO_9
    {.outputs = 0x66315637d067dbceULL, .state = 0xcb5ba822f9141ea4ULL, .send = 0x500852109609f425ULL},
    // FREEVERB_FULL_SCALE
    {.outputs = 0x7697dcccedc6b526ULL, .state = 0x24cb2c8e6b4af0cdULL, .send = 0x52f09b4abc2f52a0ULL},
    // FREEVERB_DC
    {.outputs = 0x889e19b8e24ebcb5ULL, .state = 0x8196b4bbe001f0d6ULL, .send = 0x1338c924637eef2aULL},
    // FREEVERB_KNOB_STEPS
    {.outputs = 0x7cc3ba80103cdf78ULL, .state = 0xb14e347bc92b62a8ULL, .send = 0xa672fe371969c955ULL},
    // MUTABLE_BOOT
    {.outputs = 0xb718ceeb41c8e66dULL, .state = 0xb8f782630a8b3f2cULL, .send = 0x66106852d2e2c924ULL},
    // MUTABLE_TURNED
    {.outputs = 0x7a1737257df51498ULL, .state = 0x6db86b0c039c5c7aULL, .send = 0x9a861deed90599e5ULL},
    // MUTABLE_BLOCKS_1_TO_9
    {.outputs = 0x474c06a8dd522fa8ULL, .state = 0x6bf6b2235168f664ULL, .send = 0x210cd744af9d1692ULL},
    // MUTABLE_FULL_SCALE
    {.outputs = 0xcbd1fe6ff03f3b25ULL, .state = 0xe717d078f5e6e301ULL, .send = 0x167e0cbcd4c0af25ULL},
    // MUTABLE_LONG
    {.outputs = 0x37ff5071f5ac11eULL, .state = 0x22c92e99493885c2ULL, .send = 0x679d4977836d2f65ULL},
    // MUTABLE_KNOB_STEPS
    {.outputs = 0x7bac8c20e7be97b9ULL, .state = 0x134fdcf05c057821ULL, .send = 0xf461061928a225ceULL},
    // DIGITAL_BOOT
    {.outputs = 0xe66d9f79274723eULL, .state = 0xab5df3233aa77a60ULL, .send = 0x66106852d2e2c924ULL},
    // DIGITAL_TURNED
    {.outputs = 0xaf1f5794f06f8e8bULL, .state = 0xff4c36e5b104e5b3ULL, .send = 0x9a861deed90599e5ULL},
    // DIGITAL_BLOCKS_1_TO_9
    {.outputs = 0x38b2b3de695ae8eaULL, .state = 0xdd8ef70ff1bcee34ULL, .send = 0x210cd744af9d1692ULL},
    // DIGITAL_FULL_SCALE
    {.outputs = 0xcbd1fe6ff03f3b25ULL, .state = 0x90e5c298d4d222f3ULL, .send = 0x167e0cbcd4c0af25ULL},
    // DIGITAL_LONG
    {.outputs = 0x3fc9a557b17ea7aULL, .state = 0x48249788ae0d0fb7ULL, .send = 0x679d4977836d2f65ULL},
    // DIGITAL_KNOB_STEPS
    {.outputs = 0x487e14b4867e1bc5ULL, .state = 0x56c4ef71e0a3cccdULL, .send = 0xf461061928a225ceULL},
    // MODEL_CHANGES
    {.outputs = 0xc278cfae898d2f31ULL, .state = 0x56ccba4b54b7e90fULL, .send = 0x3d400bbb6f28bbfeULL},
    // MUTABLE_FULL_SCALE_OPEN
    {.outputs = 0x438baccd17f0f580ULL, .state = 0xa32effb049366afbULL, .send = 0x167e0cbcd4c0af25ULL},
    // DIGITAL_FULL_SCALE_OPEN
    {.outputs = 0x42ad306788d7612bULL, .state = 0x62847f4b87281956ULL, .send = 0x167e0cbcd4c0af25ULL},
}};

} // namespace reverb_workloads
