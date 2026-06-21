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

/// Host-sim platform backend: the non-boundary symbols the portable app expects
/// from its environment but which are NOT part of the <libdeluge/...> contract —
/// on the SoC they come from RZA1/diskio.c, RZA1/usb/, the firmware src/main.c,
/// and the linker. Here they are inert host stubs sufficient to link and to run
/// headless (no SD card, no USB). The memory-map boundary symbols live in
/// host_bsp.c alongside the rest of memory.h.

// 64-bit file offsets even under -m32, so disk images >2GB work (pread64/pwrite64).
// Must precede the first system header include.
#define _FILE_OFFSET_BITS 64

#include "board_config.h" // TRIGGER_CLOCK_INPUT_NUM_TIMES_STORED
#include "diskio.h"       // FatFS DSTATUS/DRESULT/STA_*/RES_*
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

// ===========================================================================
// FatFS disk backend (RZA1/diskio.c on target). On host the "SD card" is a FAT
// disk-IMAGE file named by env DELUGE_SD_IMAGE: the five porting callbacks read/
// write 512-byte sectors against it, so the app sees a normal FAT volume (song
// load, on-demand sample-cluster streaming, stem writes — all unchanged). When
// the env is unset we keep the historical no-disk behaviour, so a plain run
// still boots the default no-SD patch. The app provides disk_read/disk_write
// (the LBA_t-facing wrappers, audio_file_manager.cpp); they call the
// _without_streaming_first variants below, which do the real I/O.
// ===========================================================================

#define HOST_SECTOR_SIZE 512u

static int host_img_fd = -1;
static uint32_t host_img_sectors = 0;
static int host_img_writable = 0;
static int host_img_tried = 0; // open attempted (success or failure) — don't retry

// Lazily open the image on the first disk_initialize/disk_status. Returns the
// DSTATUS bits to report.
static DSTATUS host_sd_open(void) {
	if (host_img_fd >= 0) {
		return host_img_writable ? 0 : STA_PROTECT;
	}
	if (host_img_tried) {
		return STA_NODISK | STA_NOINIT; // no image configured, or open failed
	}
	host_img_tried = 1;

	const char* path = getenv("DELUGE_SD_IMAGE");
	if (path == NULL || path[0] == '\0') {
		return STA_NODISK | STA_NOINIT;
	}

	int writable = 1;
	int fd = open(path, O_RDWR);
	if (fd < 0) {
		fd = open(path, O_RDONLY);
		writable = 0;
	}
	if (fd < 0) {
		fprintf(stderr, "[host-sd] cannot open DELUGE_SD_IMAGE '%s'\n", path);
		return STA_NODISK | STA_NOINIT;
	}

	struct stat st;
	if (fstat(fd, &st) != 0 || st.st_size < (off_t)HOST_SECTOR_SIZE) {
		fprintf(stderr, "[host-sd] not a usable image: '%s'\n", path);
		close(fd);
		return STA_NODISK | STA_NOINIT;
	}

	host_img_fd = fd;
	host_img_writable = writable;
	host_img_sectors = (uint32_t)(st.st_size / HOST_SECTOR_SIZE);
	fprintf(stderr, "[host-sd] mounted '%s' (%u sectors, %s)\n", path, host_img_sectors, writable ? "rw" : "ro");
	return host_img_writable ? 0 : STA_PROTECT;
}

// Set while the SD routine is mid-access on target; nothing toggles it on host
// (synchronous I/O, no reentrancy), but it is read app-wide.
uint8_t currentlyAccessingCard = 0;

DSTATUS disk_initialize(BYTE pdrv) {
	if (pdrv != 0) {
		return STA_NOINIT;
	}
	return host_sd_open();
}

DSTATUS disk_status(BYTE pdrv) {
	if (pdrv != 0) {
		return STA_NOINIT;
	}
	if (host_img_fd >= 0) {
		return host_img_writable ? 0 : STA_PROTECT;
	}
	return host_sd_open();
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
	(void)pdrv;
	if (host_img_fd < 0) {
		return RES_NOTRDY;
	}
	switch (cmd) {
	case CTRL_SYNC:
		fsync(host_img_fd);
		return RES_OK;
	case GET_SECTOR_COUNT:
		*(LBA_t*)buff = host_img_sectors;
		return RES_OK;
	case GET_SECTOR_SIZE:
		*(WORD*)buff = (WORD)HOST_SECTOR_SIZE;
		return RES_OK;
	case GET_BLOCK_SIZE:
		*(DWORD*)buff = 1; // erase block size unknown / irrelevant for an image
		return RES_OK;
	default:
		return RES_PARERR;
	}
}

void disk_timerproc(UINT msPassed) {
	(void)msPassed;
}

// App-side cluster-streaming wrappers (declared in audio_file_manager.cpp). These
// do the real sector I/O against the image; FatFS's disk_read/disk_write and the
// sample streamer both funnel here.
DRESULT disk_read_without_streaming_first(BYTE pdrv, BYTE* buff, DWORD sector, UINT count) {
	(void)pdrv;
	if (host_img_fd < 0) {
		return RES_NOTRDY;
	}
	if ((uint64_t)sector + count > host_img_sectors) {
		return RES_PARERR;
	}
	size_t total = (size_t)count * HOST_SECTOR_SIZE;
	off_t base = (off_t)sector * HOST_SECTOR_SIZE;
	size_t done = 0;
	while (done < total) {
		ssize_t n = pread(host_img_fd, buff + done, total - done, base + (off_t)done);
		if (n <= 0) {
			return RES_ERROR;
		}
		done += (size_t)n;
	}
	return RES_OK;
}

DRESULT disk_write_without_streaming_first(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count) {
	(void)pdrv;
	if (host_img_fd < 0) {
		return RES_NOTRDY;
	}
	if (!host_img_writable) {
		return RES_WRPRT;
	}
	if ((uint64_t)sector + count > host_img_sectors) {
		return RES_PARERR;
	}
	size_t total = (size_t)count * HOST_SECTOR_SIZE;
	off_t base = (off_t)sector * HOST_SECTOR_SIZE;
	size_t done = 0;
	while (done < total) {
		ssize_t n = pwrite(host_img_fd, buff + done, total - done, base + (off_t)done);
		if (n <= 0) {
			return RES_ERROR;
		}
		done += (size_t)n;
	}
	return RES_OK;
}

// Fixed timestamp (2024-01-01 00:00:00) in FatFS packed form. No RTC on host.
DWORD get_fattime(void) {
	return ((DWORD)(2024 - 1980) << 25) | ((DWORD)1 << 21) | ((DWORD)1 << 16);
}

// ===========================================================================
// USB host/peripheral control (RZA1/usb/ on target). Headless host → inert.
// ===========================================================================

uint8_t anythingInitiallyAttachedAsUSBHost = 0;

void openUSBHost(void) {
}
void closeUSBHost(void) {
}
void openUSBPeripheral(void) {
}

// ===========================================================================
// Trigger-clock input edge buffer (defined in firmware src/main.c, filled from
// the GPIO ISR on target). No external clock input on host → stays empty.
// ===========================================================================

uint32_t triggerClockRisingEdgeTimes[TRIGGER_CLOCK_INPUT_NUM_TIMES_STORED];
uint32_t triggerClockRisingEdgesReceived = 0;
uint32_t triggerClockRisingEdgesProcessed = 0;

// ===========================================================================
// eyalroz printf sink. Route to stdout so D_PRINTLN / debug output is visible.
// ===========================================================================

void putchar_(char c) {
	putchar((unsigned char)c);
}

// ===========================================================================
// arm-linux reference build only (sim/arm-linux-toolchain.cmake). On __arm__ the app takes code
// paths that reference symbols normally provided by bsp/rza1 or hand-asm: the fault handler (its
// FREEZE_WITH_ERROR macro reads LR/SP and calls fault_handler_print_freeze_pointers) and the FM
// synth's neon_fm_kernel. Neither is on the audio path the WAV-diff harness exercises (error path;
// default patch is subtractive, not FM), so stub them. On x86 (__arm__ undefined) these paths are
// not taken and the symbols are not referenced.
// ===========================================================================
#if defined(__arm__)
void fault_handler_print_freeze_pointers(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
	(void)a;
	(void)b;
	(void)c;
	(void)d;
}
void neon_fm_kernel(const int32_t* in, const int32_t* busin, int32_t* out, int count, int32_t phase0, int32_t freq,
                    int32_t gain1, int32_t dgain) {
	(void)in;
	(void)busin;
	(void)out;
	(void)count;
	(void)phase0;
	(void)freq;
	(void)gain1;
	(void)dgain;
}
#endif
