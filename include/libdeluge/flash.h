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

/// libdeluge/flash.h — small on-board persistent settings flash.
///
/// A reserved region of the board's SPI boot flash that survives power-off,
/// separate from the SD card, used for device settings. Offsets are relative to
/// that region; the BSP owns the base address, channel, and erase granularity,
/// so no SPIBSC parameters cross the boundary. Replaces direct `R_SFLASH_*`
/// (RZ/A1L SPIBSC) use.
#ifndef LIBDELUGE_FLASH_H
#define LIBDELUGE_FLASH_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Read `len` bytes from `offset` within the settings flash region. [task]
void deluge_flash_read(uint32_t offset, void* dst, uint32_t len);

/// Erase the flash sector containing `offset`. [task]
void deluge_flash_erase(uint32_t offset);

/// Program `len` bytes at `offset`. The containing sector must have been erased
/// first (NOR flash can only clear bits on erase). [task]
void deluge_flash_program(uint32_t offset, const void* src, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_FLASH_H
