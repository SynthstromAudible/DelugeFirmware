/*
 * Copyright (c) 2026 Chris Griggs
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 */

#include "io/midi/device_specific/novation_launchpad_mk3.h"

#include <cstring>

namespace novation_launchpad_mk3 {

static bool pidInRange(uint16_t productId, uint16_t base, uint16_t alt) {
	return (productId >= base && productId <= (base + 0x0F)) || productId == alt;
}

bool matchesVendorProduct(uint16_t vendorId, uint16_t productId) {
	if (vendorId != kVendorId) {
		return false;
	}
	if (pidInRange(productId, 0x0103, 0x1103)) {
		return true; // Launchpad X
	}
	if (pidInRange(productId, 0x0113, 0x1113)) {
		return true; // Launchpad Mini MK3
	}
	if (pidInRange(productId, 0x0123, 0x1123)) {
		return true; // Launchpad Pro MK3
	}
	return false;
}

uint8_t sysexDeviceId(uint16_t productId) {
	if (pidInRange(productId, 0x0123, 0x1123)) {
		return 0x0E; // Pro MK3
	}
	if (pidInRange(productId, 0x0113, 0x1113)) {
		return 0x0D; // Mini MK3
	}
	return 0x0C; // Launchpad X (and fallback)
}

bool nameIsMidiPort(char const* name) {
	if (name == nullptr) {
		return false;
	}
	size_t len = strlen(name);
	if (len < 7) {
		return false;
	}
	return strcmp(name + len - 7, " port 2") == 0;
}

} // namespace novation_launchpad_mk3
