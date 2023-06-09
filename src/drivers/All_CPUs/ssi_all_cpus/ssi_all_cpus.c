/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "definitions.h"
#include "ssif_iodefine.h"
#include "ssi_all_cpus.h"
#include "dmac_iodefine.h"

int32_t ssiTxBuffer[SSI_TX_BUFFER_NUM_SAMPLES * NUM_MONO_OUTPUT_CHANNELS] __attribute__((aligned(CACHE_LINE_SIZE)));

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
int32_t ssiRxBuffer[SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS] __attribute__((aligned(CACHE_LINE_SIZE)));
#else
int32_t* ssiRxBuffer = 0;
#endif

const uint32_t ssiDmaTxLinkDescriptor[] __attribute__((aligned(CACHE_LINE_SIZE))) = {
    0b1101,                                                                          // Header
    (uint32_t)ssiTxBuffer,                                                           // Source address
    (uint32_t)&SSIF(SSI_CHANNEL).SSIFTDR.LONG,                                       // Destination address
    sizeof(ssiTxBuffer),                                                             // Transaction size
    0b10000001001000100010001000101000 | DMA_LVL_FOR_SSI | (SSI_TX_DMA_CHANNEL & 7), // Config
    0,                                                                               // Interval
    0,                                                                               // Extension
    (uint32_t)ssiDmaTxLinkDescriptor // Next link address (this one again)
};

#if DELUGE_MODEL != DELUGE_MODEL_40_PAD
const uint32_t ssiDmaRxLinkDescriptor[] __attribute__((aligned(CACHE_LINE_SIZE))) = {
    0b1101,                                                                          // Header
    (uint32_t)&SSIF(SSI_CHANNEL).SSIFRDR.LONG,                                       // Source address
    (uint32_t)ssiRxBuffer,                                                           // Destination address
    sizeof(ssiRxBuffer),                                                             // Transaction size
    0b10000001000100100010001000100000 | DMA_LVL_FOR_SSI | (SSI_RX_DMA_CHANNEL & 7), // Config
    0,                                                                               // Interval
    0,                                                                               // Extension
    (uint32_t)ssiDmaRxLinkDescriptor // Next link address (this one again)
};
#endif

void* getTxBufferCurrentPlace()
{
    return (void*)((DMACn(SSI_TX_DMA_CHANNEL).CRSA_n & ~((NUM_MONO_OUTPUT_CHANNELS * 4) - 1)) + UNCACHED_MIRROR_OFFSET);
}

void* getRxBufferCurrentPlace()
{
    return (void*)((DMACn(SSI_RX_DMA_CHANNEL).CRDA_n & ~((NUM_MONO_INPUT_CHANNELS * 4) - 1)) + UNCACHED_MIRROR_OFFSET);
}

int32_t* getTxBufferStart()
{
    return (int32_t*)((uint32_t)&ssiTxBuffer[0] + UNCACHED_MIRROR_OFFSET);
}

int32_t* getTxBufferEnd()
{
    return (int32_t*)((uint32_t)&ssiTxBuffer[SSI_TX_BUFFER_NUM_SAMPLES * NUM_MONO_OUTPUT_CHANNELS]
                      + UNCACHED_MIRROR_OFFSET);
}

int32_t* getRxBufferStart()
{
    return (int32_t*)((uint32_t)&ssiRxBuffer[0] + UNCACHED_MIRROR_OFFSET);
}

int32_t* getRxBufferEnd()
{
    return (
        int32_t*)((uint32_t)&ssiRxBuffer[SSI_RX_BUFFER_NUM_SAMPLES * NUM_MONO_INPUT_CHANNELS] + UNCACHED_MIRROR_OFFSET);
}
