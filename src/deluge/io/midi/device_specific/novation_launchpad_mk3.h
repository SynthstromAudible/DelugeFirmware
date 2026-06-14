/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 */

#pragma once

#include <cstdint>

namespace novation_launchpad_mk3 {

static constexpr uint16_t kVendorId = 0x1235;

bool matchesVendorProduct(uint16_t vendorId, uint16_t productId);
bool nameIsMidiPort(char const* name);
// Launchpad X = 0x0C, Mini MK3 = 0x0D, Pro MK3 = 0x0E (Novation Programmer SysEx port byte).
uint8_t sysexDeviceId(uint16_t productId);

} // namespace novation_launchpad_mk3
