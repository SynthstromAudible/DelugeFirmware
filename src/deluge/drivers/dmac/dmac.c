/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include "drivers/dmac/dmac.h"
#include "RZA1/system/iodefines/dmac_iodefine.h"
void setDMARS(int32_t dmaChannel, uint32_t dmarsValue) {

	uint32_t mask = 0xFFFF0000;

	if (dmaChannel & 1) {
		dmarsValue <<= 16;
		mask >>= 16;
	}

	volatile uint32_t* dmarsAddress = DMARSnAddress(dmaChannel);

	*dmarsAddress = (*dmarsAddress & mask) | dmarsValue;
}

void initDMAWithLinkDescriptor(int32_t dma_channel, const uint32_t* linkDescriptor, uint32_t dmarsValue) {
	DCTRLn(dma_channel) = 0;                              // DMA Control Register Setting
	DMACn(dma_channel).CHCFG_n = linkDescriptor[4];       // Config
	setDMARS(dma_channel, dmarsValue);                    // DMA Expansion Resource Selector Setting
	DMACn(dma_channel).NXLA_n = (uint32_t)linkDescriptor; // Link descriptor address
}

void dmaChannelStart(const uint32_t dma_channel) {
	DMACn(dma_channel).CHCTRL_n |= DMAC_CHCTRL_0S_SWRST; // Status clear
	DMACn(dma_channel).CHCTRL_n |= DMAC_CHCTRL_0S_SETEN; // Enable DMA transfer
}

#define NUM_DMA_CHANNELS 16
// How many status reads to wait for each channel to stop, so a wedged channel can't hang the load
#define DMA_STOP_POLL_LIMIT 100000

void dmaStopAllChannels(void) {
	const uint32_t active = DMAC0_CHSTAT_n_EN | DMAC0_CHSTAT_n_TACT;
	// CLREN: no new transfer and no further descriptor load. A transfer unit in flight still completes.
	for (int32_t ch = 0; ch < NUM_DMA_CHANNELS; ch++) {
		DMACn(ch).CHCTRL_n = DMAC0_CHCTRL_n_CLREN;
	}
	for (int32_t ch = 0; ch < NUM_DMA_CHANNELS; ch++) {
		for (int32_t i = 0; i < DMA_STOP_POLL_LIMIT; i++) {
			if (!(DMACn(ch).CHSTAT_n & active)) {
				DMACn(ch).CHCTRL_n = DMAC0_CHCTRL_n_SWRST; // Clear END, ER etc., only once the channel has stopped
				break;
			}
		}
	}
}
