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

/// libdeluge/block_device.h — sector-addressed storage for FatFs.
///
/// Mirrors the FatFs `diskio` contract so the existing FAT layer binds with a
/// thin shim. Backed by `drivers/sd` + `RZA1/sdhi` today; `deluge-bsp::sd` /
/// `::fat` (embedded-sdmmc) on the Rust side. A "unit" is a logical drive index.
#ifndef LIBDELUGE_BLOCK_DEVICE_H
#define LIBDELUGE_BLOCK_DEVICE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// The block-device unit (FatFs physical drive) of the board's SD card. The
/// board owns which drive the SD is on, so the application does not hard-code
/// it. [task]
uint8_t deluge_block_sd_unit(void);

/// Bring up storage media (e.g. SD card init). [task]
DelugeStatus deluge_block_init(uint8_t unit);

/// True if media is present and initialised. [task]
bool deluge_block_ready(uint8_t unit);

/// Read `count` sectors starting at `sector` into `dst`. [task]
DelugeStatus deluge_block_read(uint8_t unit, uint8_t* dst, uint32_t sector, uint32_t count);

/// Write `count` sectors starting at `sector` from `src`. [task]
DelugeStatus deluge_block_write(uint8_t unit, const uint8_t* src, uint32_t sector, uint32_t count);

/// Total number of addressable sectors. [task]
uint32_t deluge_block_sector_count(uint8_t unit);

/// Bytes per sector (typically 512). [task]
uint32_t deluge_block_sector_size(uint8_t unit);

/// Flush any pending writes / complete in-flight operations. [task]
DelugeStatus deluge_block_sync(uint8_t unit);

/// A card-detect edge observed since the last poll.
typedef enum DelugeCardEvent {
	DELUGE_CARD_EVENT_NONE = 0,
	DELUGE_CARD_EVENT_INSERTED,
	DELUGE_CARD_EVENT_EJECTED,
} DelugeCardEvent;

/// Drain the next pending card-detect event for `unit` (NONE if none pending).
///
/// Pull-based, not a callback: the card-detect interrupt records the edge in the
/// BSP/HAL and the application polls for it from its own loop. The lower layers
/// never call up into the app. [task]
DelugeCardEvent deluge_block_poll_card_event(uint8_t unit);

// NOTE: loadAnyEnqueuedClustersRoutine() is still an app upcall used by the
// storage busy-wait path; it is removed when storage waits are converted to
// scheduler yields (it belongs to that concurrency work, not card-detect).
void loadAnyEnqueuedClustersRoutine(void);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_BLOCK_DEVICE_H
