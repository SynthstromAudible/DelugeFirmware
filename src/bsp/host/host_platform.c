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

#include "board_config.h" // TRIGGER_CLOCK_INPUT_NUM_TIMES_STORED
#include "diskio.h"       // FatFS DSTATUS/DRESULT/STA_*/RES_*
#include <stdint.h>
#include <stdio.h>

// ===========================================================================
// FatFS disk backend (RZA1/diskio.c on target). The host has no SD card, so the
// media layer reports "no disk" and all access is not-ready. The app already
// handles a missing card gracefully.
// ===========================================================================

// Set while the SD routine is mid-access on target; nothing toggles it on host,
// but it is read app-wide (playback, scheduler ResourceChecker, save UI, ...).
uint8_t currentlyAccessingCard = 0;

DSTATUS disk_initialize(BYTE pdrv) {
	(void)pdrv;
	return STA_NODISK | STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv) {
	(void)pdrv;
	return STA_NODISK | STA_NOINIT;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
	(void)pdrv;
	(void)cmd;
	(void)buff;
	return RES_NOTRDY;
}

void disk_timerproc(UINT msPassed) {
	(void)msPassed;
}

// App-side cluster-streaming wrappers (declared in audio_file_manager.cpp).
DRESULT disk_read_without_streaming_first(BYTE pdrv, BYTE* buff, DWORD sector, UINT count) {
	(void)pdrv;
	(void)buff;
	(void)sector;
	(void)count;
	return RES_NOTRDY;
}

DRESULT disk_write_without_streaming_first(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count) {
	(void)pdrv;
	(void)buff;
	(void)sector;
	(void)count;
	return RES_NOTRDY;
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
