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

/// Host-sim block device: a file-backed SD card image.
///
/// Implements both halves of the storage stack the app binds to:
///  - the <libdeluge/block_device.h> boundary (deluge_block_*), and
///  - the FatFs media-layer porting symbols (disk_initialize / disk_status /
///    disk_ioctl / disk_read_without_streaming_first / disk_write_without_
///    streaming_first) that RZA1/diskio.c provides on target.
///
/// Config (env): DELUGE_HOST_CARD_IMAGE = path to a raw FAT image (e.g. built
/// with mtools: mformat -i img -C -T <sectors>; mcopy preset into SYNTHS/).
/// Unset → no card, matching the previous always-empty-slot behaviour. Writes
/// go to the image file, so harnesses should run on a throwaway copy.
///
/// This is what lets the blank-song boot path (Song::ensureAtLeastOneSessionClip)
/// load a real preset from SYNTHS/ through the full deserialization path — the
/// substrate for the golden-master audio harness (tests/golden/).

// Large-file support: the host-sim is built -m32, where the default stdio offset type is 32-bit and fopen() of a card
// image larger than 2 GiB fails outright. Make off_t / fopen / fseeko 64-bit so a full-size SD image works.
#define _FILE_OFFSET_BITS 64

#include "fatfs/diskio.h" // FatFS DSTATUS/DRESULT/STA_*/RES_*
#include "libdeluge/block_device.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#define HOST_CARD_SECTOR_SIZE 512u

static FILE* card_file;
static uint32_t card_sector_count;
static bool card_open_attempted;
static bool card_inited; // FatFs STA_NOINIT tracking (cleared by disk_initialize)

// Lazily open the image on first query so the env var is read after main() starts.
static void card_open_if_configured(void) {
	if (card_open_attempted) {
		return;
	}
	card_open_attempted = true;

	const char* path = getenv("DELUGE_HOST_CARD_IMAGE");
	if (!path || !path[0]) {
		return;
	}
	card_file = fopen(path, "r+b");
	if (!card_file) {
		fprintf(stderr, "[host-block] could not open card image %s\n", path);
		return;
	}
	if (fseeko(card_file, 0, SEEK_END) != 0) {
		fclose(card_file);
		card_file = NULL;
		return;
	}
	off_t bytes = ftello(card_file);
	if (bytes <= 0) {
		fprintf(stderr, "[host-block] card image %s is empty\n", path);
		fclose(card_file);
		card_file = NULL;
		return;
	}
	card_sector_count = (uint32_t)((unsigned long)bytes / HOST_CARD_SECTOR_SIZE);
	fprintf(stderr, "[host-block] card image %s (%u sectors)\n", path, card_sector_count);
}

static bool card_present(void) {
	card_open_if_configured();
	return card_file != NULL;
}

static DelugeStatus card_io(uint8_t* dst, const uint8_t* src, uint32_t sector, uint32_t count) {
	if (!card_present()) {
		return DELUGE_ERR_NODEV;
	}
	if (sector + count > card_sector_count) {
		return DELUGE_ERR_IO;
	}
	if (fseeko(card_file, (off_t)sector * HOST_CARD_SECTOR_SIZE, SEEK_SET) != 0) {
		return DELUGE_ERR_IO;
	}
	size_t bytes = (size_t)count * HOST_CARD_SECTOR_SIZE;
	size_t done = dst ? fread(dst, 1, bytes, card_file) : fwrite(src, 1, bytes, card_file);
	return (done == bytes) ? DELUGE_OK : DELUGE_ERR_IO;
}

// ===========================================================================
// libdeluge/block_device.h boundary
// ===========================================================================

uint8_t deluge_block_sd_unit(void) {
	return 0;
}

DelugeStatus deluge_block_init(uint8_t unit) {
	(void)unit;
	return card_present() ? DELUGE_OK : DELUGE_ERR_NODEV;
}

bool deluge_block_ready(uint8_t unit) {
	(void)unit;
	return card_present();
}

DelugeStatus deluge_block_read(uint8_t unit, uint8_t* dst, uint32_t sector, uint32_t count) {
	(void)unit;
	return card_io(dst, NULL, sector, count);
}

DelugeStatus deluge_block_write(uint8_t unit, const uint8_t* src, uint32_t sector, uint32_t count) {
	(void)unit;
	return card_io(NULL, src, sector, count);
}

uint32_t deluge_block_sector_count(uint8_t unit) {
	(void)unit;
	return card_present() ? card_sector_count : 0;
}

uint32_t deluge_block_sector_size(uint8_t unit) {
	(void)unit;
	return HOST_CARD_SECTOR_SIZE;
}

DelugeStatus deluge_block_sync(uint8_t unit) {
	(void)unit;
	if (card_file) {
		fflush(card_file);
	}
	return DELUGE_OK;
}

DelugeCardEvent deluge_block_poll_card_event(uint8_t unit) {
	(void)unit;
	return DELUGE_CARD_EVENT_NONE; // image is present from boot; no hot-plug
}

// ===========================================================================
// FatFs media layer (RZA1/diskio.c on target)
// ===========================================================================

DSTATUS disk_initialize(BYTE pdrv) {
	(void)pdrv;
	if (!card_present()) {
		return STA_NODISK | STA_NOINIT;
	}
	card_inited = true;
	return 0;
}

DSTATUS disk_status(BYTE pdrv) {
	(void)pdrv;
	if (!card_present()) {
		return STA_NODISK | STA_NOINIT;
	}
	return card_inited ? 0 : STA_NOINIT;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
	(void)pdrv;
	if (!card_present()) {
		return RES_NOTRDY;
	}
	switch (cmd) {
	case CTRL_SYNC:
		fflush(card_file);
		return RES_OK;
	case GET_SECTOR_COUNT:
		*(LBA_t*)buff = card_sector_count;
		return RES_OK;
	case GET_SECTOR_SIZE:
		*(WORD*)buff = HOST_CARD_SECTOR_SIZE;
		return RES_OK;
	case GET_BLOCK_SIZE:
		*(DWORD*)buff = 1; // erase-block size unknown → 1 sector
		return RES_OK;
	default:
		return RES_PARERR;
	}
}

void disk_timerproc(UINT msPassed) {
	(void)msPassed;
}

// App-side cluster-streaming wrappers call down into these (the app's disk_read/
// disk_write in audio_file_manager.cpp service the streaming queue first).
DRESULT disk_read_without_streaming_first(BYTE pdrv, BYTE* buff, DWORD sector, UINT count) {
	(void)pdrv;
	return (card_io(buff, NULL, sector, count) == DELUGE_OK) ? RES_OK : RES_ERROR;
}

DRESULT disk_write_without_streaming_first(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count) {
	(void)pdrv;
	return (card_io(NULL, buff, sector, count) == DELUGE_OK) ? RES_OK : RES_ERROR;
}
