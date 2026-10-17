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

// The same sequence as sd_read_sect(), but each step is started by the interrupt that ends the one before, rather than
// by waiting for it:
//   CMD13 (check the card's ready) -> CMD18, or CMD17 for 1 or 2 sectors -> DMA til All end -> CMD13 (check it went ok)
// Recovering from errors needs waiting, so that's left to sd_async_recover() on the main thread.

#include "../../../inc/sd_async.h"
#include "../../../inc/sdif.h"
#include "../inc/access/sd.h"

#include "OSLikeStuff/scheduler_api.h"
#include "OSLikeStuff/timers_interrupts/clock_type.h"
#include "OSLikeStuff/timers_interrupts/timers_interrupts.h"
#include "RZA1/cache/cache.h"
#include "deluge/deluge.h"

#include <stdint.h>

extern uint8_t currentlyAccessingCard;

typedef enum {
	ASYNC_IDLE,
	ASYNC_CHECK_STATUS,       // Waiting for the response to CMD13, before reading
	ASYNC_READ_COMMAND,       // Waiting for the response to CMD17 / CMD18
	ASYNC_DATA,               // DMA is moving the data. Waiting for All end
	ASYNC_CHECK_STATUS_AFTER, // Waiting for the response to CMD13, after the data
	ASYNC_FAILED,             // Waiting for sd_async_recover()
} AsyncState;

// Longest we'll wait for any one step. Matches sddev_wait_dma_end(), as some cards occasionally take over a second.
#define ASYNC_TIMEOUT_TICKS (2 * DELUGE_CLOCKS_PER)

// All end should only come once DMA has emptied the SDHI buffer, but its last writes might still be on their way
#define DMA_END_SPINS 100000

static struct {
	volatile AsyncState state;
	volatile int pause_count;
	volatile uint32_t step_started; // getSystemTicks() when the current step began

	int sd_port;
	SdAsyncNextFn next;
	SdAsyncDoneFn done;

	SdAsyncRead read;
	unsigned char* buff;      // Where the next sector's data goes
	unsigned long psn;        // Next sector to read
	long sectors_left;        // Only used for single block reads
	bool single;              // Reading a sector at a time with CMD17, rather than all at once with CMD18
	int dma_64;               // SD_MODE_DMA_64 or SD_MODE_DMA
	unsigned short cd_mask;   // Card detect interrupts, which are masked during DMA
} async = {.state = ASYNC_IDLE};

static bool is_active(AsyncState state) {
	return state != ASYNC_IDLE && state != ASYNC_FAILED;
}

static void set_state(AsyncState state) {
	async.state = state;
	async.step_started = getSystemTicks();
}

static int issue_cmd(SDHNDL* hndl, unsigned short cmd, unsigned long arg) {
	int i;
	for (i = 0; i < SCLKDIVEN_LOOP_COUNT; i++) {
		if (sd_inp(hndl, SD_INFO2) & SD_INFO2_MASK_SCLKDIVEN) {
			break;
		}
	}
	if (i == SCLKDIVEN_LOOP_COUNT) {
		_sd_set_err(hndl, SD_ERR_CBSY_ERROR);
		return hndl->error;
	}

	// Clear any stale flags, so they're not mistaken for this command's
	sd_outp(hndl, SD_INFO1, (unsigned short)~SD_INFO1_MASK_TRNS_RESP);
	sd_outp(hndl, SD_INFO2, (unsigned short)~SD_INFO2_MASK_ERR);
	_sd_clear_info(hndl, SD_INFO1_MASK_TRNS_RESP, SD_INFO2_MASK_ERR);

	_sd_set_int_mask(hndl, SD_INFO1_MASK_RESP, SD_INFO2_MASK_ERR);
	_sd_set_arg(hndl, (unsigned short)(arg >> 16), (unsigned short)arg);
	sd_outp(hndl, SD_CMD, cmd);
	return SD_OK;
}

static void stop_dma(SDHNDL* hndl) {
	sddev_disable_dma(async.sd_port);
	sd_outp(hndl, CC_EXT_MODE, (unsigned short)(sd_inp(hndl, CC_EXT_MODE) & ~CC_EXT_MODE_DMASDRW));
#if (TARGET_RZ_A1 == 1)
	sd_outp(hndl, EXT_SWAP, 0x0000); // Clear DMASEL for 64byte transfer
#endif
}

static void start_next(SDHNDL* hndl);

static void fail(SDHNDL* hndl, int error) {
	if (async.state == ASYNC_DATA) {
		_sd_set_int_mask(hndl, async.cd_mask, 0);
	}
	stop_dma(hndl);
	_sd_clear_int_mask(hndl, SD_INFO1_MASK_TRNS_RESP, 0x837f);
	_sd_clear_info(hndl, SD_INFO1_MASK_TRNS_RESP, 0x837f);

	_sd_set_err(hndl, error);
	if (hndl->error == SD_OK) {
		hndl->error = SD_ERR;
	}

	set_state(ASYNC_FAILED);
	async.done(async.read.user, hndl->error);
}

static void start_read_command(SDHNDL* hndl) {
	unsigned short cmd;
	if (async.single) {
		sd_outp(hndl, SD_STOP, 0x0000); // Disable SD_SECCNT
		cmd = CMD17;
	}
	else {
		sd_outp(hndl, SD_STOP, 0x0100); // Enable SD_SECCNT, so CMD12 gets issued automatically at the end
		sd_outp(hndl, SD_SECCNT, (unsigned short)async.read.count);
		cmd = CMD18;
	}

#if (TARGET_RZ_A1 == 1)
	if (async.dma_64 == SD_MODE_DMA_64) {
		sd_outp(hndl, EXT_SWAP, 0x0100); // Set DMASEL for 64byte transfer
	}
#endif
	sd_outp(hndl, CC_EXT_MODE, (unsigned short)(sd_inp(hndl, CC_EXT_MODE) | CC_EXT_MODE_DMASDRW));

	unsigned long psn = async.psn; // For SET_ACC_ADDR
	if (issue_cmd(hndl, cmd, SET_ACC_ADDR) != SD_OK) {
		fail(hndl, hndl->error);
		return;
	}
	set_state(ASYNC_READ_COMMAND);
}

static void start_data(SDHNDL* hndl) {
	long count = async.single ? 1 : async.read.count;

	_sd_clear_int_mask(hndl, SD_INFO1_MASK_RESP, SD_INFO2_MASK_ILA);

	// As in doActualReadRohan(), card insert and remove interrupts are off during DMA
	async.cd_mask = (unsigned short)(hndl->int_info1_mask & SD_INFO1_MASK_DET_CD);
	_sd_clear_int_mask(hndl, SD_INFO1_MASK_DET_CD, 0);
	set_state(ASYNC_DATA);

	// See doActualReadRohan() for why this is needed before as well as after
	invalidate_range_before_dma_read((uintptr_t)async.buff, (uintptr_t)(async.buff + count * 512));

	unsigned long reg = hndl->reg_base;
	if (TARGET_RZ_A1 != 1 || async.dma_64 != SD_MODE_DMA_64) {
		reg += SD_BUF0;
	}
	if (sddev_init_dma(async.sd_port, (unsigned long)async.buff, reg, count * 512, SD_TRANS_READ) != SD_OK) {
		fail(hndl, SD_ERR_CPU_IF);
		return;
	}

	_sd_set_int_mask(hndl, SD_INFO1_MASK_DATA_TRNS, SD_INFO2_MASK_ERR);
}

static void end_data(SDHNDL* hndl) {
	long count = async.single ? 1 : async.read.count;

	for (int i = 0; !sddev_dma_ended(async.sd_port); i++) {
		if (i >= DMA_END_SPINS) {
			fail(hndl, SD_ERR_CPU_IF);
			return;
		}
	}

	stop_dma(hndl);
	_sd_set_int_mask(hndl, async.cd_mask, 0);
	_sd_clear_int_mask(hndl, SD_INFO1_MASK_DATA_TRNS, SD_INFO2_MASK_BRE);

	invalidate_range_after_dma_read((uintptr_t)async.buff, (uintptr_t)(async.buff + count * 512));

	async.buff += count * 512;
	async.psn += count;
	async.sectors_left -= count;

	if (issue_cmd(hndl, CMD13, (unsigned long)hndl->rca[0] << 16) != SD_OK) {
		fail(hndl, hndl->error);
		return;
	}
	set_state(ASYNC_CHECK_STATUS_AFTER);
}

// Returns whether the card responded to the last command, and is ready for another
static bool check_resp(SDHNDL* hndl, bool check_tran_state) {
	if (_sd_get_resp(hndl, SD_RESP_R1) != SD_OK) {
		fail(hndl, hndl->error);
		return false;
	}
	if (check_tran_state && (hndl->resp_status & RES_STATE) != STATE_TRAN) {
		fail(hndl, SD_ERR);
		return false;
	}
	return true;
}

static void finish(SDHNDL* hndl) {
	_sd_clear_int_mask(hndl, SD_INFO1_MASK_TRNS_RESP, SD_INFO2_MASK_ERR);
	_sd_clear_info(hndl, SD_INFO1_MASK_TRNS_RESP, SD_INFO2_MASK_ERR);
	async.done(async.read.user, SD_OK);
	start_next(hndl);
}

static int begin(SDHNDL* hndl) {
	unsigned long psn = async.read.sector;
	long count = async.read.count;

	hndl->error = SD_OK;

	if (hndl->mount != SD_MOUNT_UNLOCKED_CARD) {
		return SD_ERR;
	}
	if (_sd_check_media(hndl) != SD_OK) {
		return SD_ERR_NO_CARD;
	}
	if (count <= 0 || count > TRANS_SECTORS || psn >= hndl->card_sector_size
	    || psn + count > hndl->card_sector_size) {
		return SD_ERR;
	}
	// sd_read_sect() falls back to PIO for these, which would mean an interrupt per sector
	if (!(hndl->trans_mode & SD_MODE_DMA) || ((unsigned long)async.read.buff & 0x03u) != 0) {
		return SD_ERR;
	}
	// MMC needs CMD12 issuing by hand after reading the last sector. Not worth doing here.
	if (hndl->media_type == SD_MEDIA_MMC && psn + count == hndl->card_sector_size) {
		return SD_ERR;
	}

	async.buff = async.read.buff;
	async.psn = psn;
	async.sectors_left = count;
	async.single = (count <= 2); // As in sd_read_sect()
	async.dma_64 = (hndl->trans_mode & SD_MODE_DMA_64) ? SD_MODE_DMA_64 : SD_MODE_DMA;

	sd_outp(hndl, SD_SIZE, 512);
	if (_sd_set_clock(hndl, (int)hndl->csd_tran_speed, SD_CLOCK_ENABLE) != SD_OK) {
		return hndl->error;
	}

	if (issue_cmd(hndl, CMD13, (unsigned long)hndl->rca[0] << 16) != SD_OK) {
		return hndl->error;
	}
	set_state(ASYNC_CHECK_STATUS);
	return SD_OK;
}

static void start_next(SDHNDL* hndl) {
	if (async.pause_count == 0 && async.next(&async.read)) {
		int error = begin(hndl);
		if (error != SD_OK) {
			fail(hndl, error);
		}
		return;
	}

	_sd_set_clock(hndl, 0, SD_CLOCK_DISABLE);
	set_state(ASYNC_IDLE);
}

/// Called from sd_int_handler(). Returns whether the interrupt was ours.
int sd_async_handle_int(SDHNDL* hndl) {
	if (!is_active(async.state) || hndl->sd_port != async.sd_port) {
		return 0;
	}

	unsigned short info1 = hndl->int_info1;
	if (hndl->int_info2 & SD_INFO2_MASK_ERR) {
		_sd_check_info2_err(hndl);
		fail(hndl, hndl->error);
		return 1;
	}
	hndl->int_info1 &= (unsigned short)~SD_INFO1_MASK_TRNS_RESP;

	switch (async.state) {
	case ASYNC_CHECK_STATUS:
		if ((info1 & SD_INFO1_MASK_RESP) && check_resp(hndl, true)) {
			start_read_command(hndl);
		}
		break;

	case ASYNC_READ_COMMAND:
		if ((info1 & SD_INFO1_MASK_RESP) && check_resp(hndl, false)) {
			start_data(hndl);
		}
		break;

	case ASYNC_DATA:
		if (info1 & SD_INFO1_MASK_DATA_TRNS) {
			end_data(hndl);
		}
		break;

	case ASYNC_CHECK_STATUS_AFTER:
		if ((info1 & SD_INFO1_MASK_RESP) && check_resp(hndl, true)) {
			if (async.single && async.sectors_left > 0) {
				start_read_command(hndl);
			}
			else {
				finish(hndl);
			}
		}
		break;

	default:
		break;
	}
	return 1;
}

void sd_async_init(int sd_port, SdAsyncNextFn next, SdAsyncDoneFn done) {
	async.sd_port = sd_port;
	async.next = next;
	async.done = done;
}

void sd_async_kick(void) {
	if (async.next == 0) {
		return;
	}
	ENTER_CRITICAL_SECTION();
	if (async.state == ASYNC_IDLE) {
		SDHNDL* hndl = _sd_get_hndls(async.sd_port);
		if (hndl != 0) {
			start_next(hndl);
		}
	}
	EXIT_CRITICAL_SECTION();
}

bool sd_async_busy(void) {
	return is_active(async.state);
}

void sd_async_pause(void) {
	async.pause_count++;
}

void sd_async_resume(void) {
	if (async.pause_count > 0) {
		async.pause_count--;
	}
}

void sd_async_check_timeout(void) {
	ENTER_CRITICAL_SECTION();
	if (is_active(async.state) && (int32_t)(getSystemTicks() - async.step_started) > (int32_t)ASYNC_TIMEOUT_TICKS) {
		SDHNDL* hndl = _sd_get_hndls(async.sd_port);
		logAudioAction("sd_async timeout");
		fail(hndl, SD_ERR_HOST_TOE);
	}
	EXIT_CRITICAL_SECTION();
}

void sd_async_recover(void) {
	if (async.state != ASYNC_FAILED) {
		return;
	}
	logAudioAction("sd recover");
	SDHNDL* hndl = _sd_get_hndls(async.sd_port);
	if (hndl != 0) {
		uint8_t wasAccessingCard = currentlyAccessingCard;
		currentlyAccessingCard = 1;
		_sd_read_abort(hndl, SD_MODE_DMA);
		currentlyAccessingCard = wasAccessingCard;
	}
	set_state(ASYNC_IDLE);
}
