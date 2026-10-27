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

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Sector reads driven entirely by the SDHI interrupt, so the main thread never waits on the card for them.
///
/// The main thread hands over by calling sd_async_kick(). From then on, each time a read finishes, the interrupt asks
/// the SdAsyncNextFn for another and starts it straight away, until there are none left. So callbacks run in the SDHI
/// interrupt, or on the main thread with interrupts disabled - either way, never alongside main thread code.
///
/// Anything else that reads or writes the card must sd_async_pause() and wait for !sd_async_busy() first, then
/// sd_async_recover() in case the last read failed.

typedef struct {
	/// Must be 4-byte aligned, for DMA, and mustn't share a cache line with anything written during the read - see
	/// invalidate_range_before_dma_read()
	unsigned char* buff;
	unsigned long sector;
	long count;
	void* user; ///< Passed back to the SdAsyncDoneFn
} SdAsyncRead;

/// Fills in the next read to do. Returns false if there isn't one.
typedef bool (*SdAsyncNextFn)(SdAsyncRead* read);
/// Called once for each read that SdAsyncNextFn handed out, with SD_OK or an SD_ERR_* code.
typedef void (*SdAsyncDoneFn)(void* user, int error);

void sd_async_init(int sd_port, SdAsyncNextFn next, SdAsyncDoneFn done);

/// Starts reading, if not already, and not paused. Main thread only.
void sd_async_kick(void);

/// Whether a read is in progress, meaning the interrupt owns the SDHI and DMA channel.
bool sd_async_busy(void);

/// Stop starting reads, so something else can use the card once the current one's done. Nests. Main thread only.
void sd_async_pause(void);
void sd_async_resume(void);

/// Fails the current read if the card's stopped answering. Main thread only.
void sd_async_check_timeout(void);

/// After a read failed, puts the SDHI and card back in a usable state. Waits for the card. Does nothing if the last
/// read didn't fail. Main thread only, and not while sd_async_busy().
void sd_async_recover(void);

#ifdef __cplusplus
}
#endif
