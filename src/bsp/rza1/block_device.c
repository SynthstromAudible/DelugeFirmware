/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

/// RZ/A1L implementation of <libdeluge/block_device.h>.
///
/// Part of bsp-rza1-legacy; depends only on the HAL's port config, so it lives
/// in the clean bsp_rza1 library. So far only the SD unit index is exposed — the
/// sector read/write/status operations declared in the header are added if/when
/// the application's storage layer moves off the FatFs diskio interface directly.

#include "libdeluge/block_device.h"

#include "RZA1/cpu_specific.h" // SD_PORT

uint8_t deluge_block_sd_unit(void) {
	return SD_PORT;
}
