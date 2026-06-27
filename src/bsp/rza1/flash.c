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

/// RZ/A1L implementation of <libdeluge/flash.h> over the SPIBSC SPI flash.
///
/// Part of bsp-rza1-legacy; depends only on the SPIBSC HAL, so it lives in the
/// clean bsp_rza1 library. The settings region is the top 4 KiB sector of the
/// boot flash (0x80000 - 0x1000); the application addresses it by offset.

#include "libdeluge/flash.h"

#include "RZA1/cpu_specific.h"              // SPIBSC_CH
#include "RZA1/spibsc/r_spibsc_flash_api.h" // R_SFLASH_*
#include "RZA1/spibsc/spibsc.h"             // SPIBSC_CMNCR_BSZ_SINGLE, SPIBSC_1BIT, SPIBSC_OUTPUT_ADDR_24

/// Base address of the reserved settings sector within the SPI boot flash.
#define DELUGE_SETTINGS_FLASH_BASE (0x80000 - 0x1000)

void deluge_flash_read(uint32_t offset, void* dst, uint32_t len) {
	R_SFLASH_ByteRead(DELUGE_SETTINGS_FLASH_BASE + offset, (uint8_t*)dst, (int32_t)len, SPIBSC_CH,
	                  SPIBSC_CMNCR_BSZ_SINGLE, SPIBSC_1BIT, SPIBSC_OUTPUT_ADDR_24);
}

void deluge_flash_erase(uint32_t offset) {
	R_SFLASH_EraseSector(DELUGE_SETTINGS_FLASH_BASE + offset, SPIBSC_CH, SPIBSC_CMNCR_BSZ_SINGLE, 1,
	                     SPIBSC_OUTPUT_ADDR_24);
}

void deluge_flash_program(uint32_t offset, const void* src, uint32_t len) {
	R_SFLASH_ByteProgram(DELUGE_SETTINGS_FLASH_BASE + offset, (uint8_t*)src, (int32_t)len, SPIBSC_CH,
	                     SPIBSC_CMNCR_BSZ_SINGLE, SPIBSC_1BIT, SPIBSC_OUTPUT_ADDR_24);
}
