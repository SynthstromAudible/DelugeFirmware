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

#pragma once

#include "RZA1/system/iobitmasks/dmac_iobitmask.h"
#include "stdint.h"

#define DMA_INTERRUPT_0 INTC_ID_DMAINT0

void setDMARS(int32_t dmaChannel, uint32_t dmarsValue);
void initDMAWithLinkDescriptor(int32_t dma_channel, const uint32_t* linkDescriptor, uint32_t dmarsValue);
void dmaChannelStart(const uint32_t dma_channel);
