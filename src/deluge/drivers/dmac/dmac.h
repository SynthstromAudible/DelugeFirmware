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

#ifndef DRIVERS_ALL_CPUS_DMAC_DMAC_H_
#define DRIVERS_ALL_CPUS_DMAC_DMAC_H_

#include "r_typedefs.h"
#include "dmac_iodefine.h"
#include "dmac_iobitmask.h"
#include "cpu_specific.h"

#define DMA_INTERRUPT_0 INTC_ID_DMAINT0

void setDMARS(int dmaChannel, uint32_t dmarsValue);
void initDMAWithLinkDescriptor(int dma_channel, const uint32_t* linkDescriptor, uint32_t dmarsValue);
void dmaChannelStart(const uint32_t dma_channel);

#endif /* DRIVERS_ALL_CPUS_DMAC_DMAC_H_ */
