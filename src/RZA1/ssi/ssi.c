/*******************************************************************************
 * DISCLAIMER
 * This software is supplied by Renesas Electronics Corporation and is only
 * intended for use with Renesas products. No other uses are authorized. This
 * software is owned by Renesas Electronics Corporation and is protected under
 * all applicable laws, including copyright laws.
 * THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
 * THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT
 * LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
 * AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED.
 * TO THE MAXIMUM EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS
 * ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES SHALL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR
 * ANY REASON RELATED TO THIS SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE
 * BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 * Renesas reserves the right, without notice, to make changes to this software
 * and to discontinue the availability of this software. By using this software,
 * you agree to the additional terms and conditions found by accessing the
 * following link:
 * http://www.renesas.com/disclaimer
 * Copyright (C) 2012 - 2014 Renesas Electronics Corporation. All rights reserved.
 *******************************************************************************/

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

#include "RZA1/ssi/ssi.h"
#include "RZA1/cpu_specific.h"
#include "RZA1/intc/devdrv_intc.h"
#include "RZA1/ssi/drv_ssif.h"
#include "RZA1/system/iobitmasks/dmac_iobitmask.h"
#include "RZA1/system/iodefines/cpg_iodefine.h"
#include "RZA1/system/iodefines/ssif_iodefine.h"
#include "definitions.h"
#include "deluge/drivers/dmac/dmac.h"
#include "deluge/drivers/ssi/ssi.h"

#define DMA_FIX_PRIO_MODE (0u)
#define SSI_CHANNEL_MAX   (6u)

#define SSI_CLEAR_VALUE (0)
#define SSI_SET_VALUE   (1)
#define SSI_WSET_VALUE  (3)

void ssiInit(uint8_t ssiChannel, uint8_t dmaChannel)
{
    ssiInit2(SSI_CHANNEL);

    initDMAWithLinkDescriptor(SSI_TX_DMA_CHANNEL, ssiDmaTxLinkDescriptor, DMARS_FOR_SSI0_TX + SSI_CHANNEL * 4);
    initDMAWithLinkDescriptor(SSI_RX_DMA_CHANNEL, ssiDmaRxLinkDescriptor, DMARS_FOR_SSI0_RX + SSI_CHANNEL * 4);

    dmaChannelStart(SSI_TX_DMA_CHANNEL);
    dmaChannelStart(SSI_RX_DMA_CHANNEL);

    ssiStart(SSI_CHANNEL);
}

void ssiInit2(const uint32_t ssi_channel)
{

    /* ---- SSI Software reset ---- */
    CPG.SWRSTCR1 |= (uint8_t)(1 << (6 - ssi_channel));
    CPG.SWRSTCR1; /* Dummy read */

    // And release reset
    CPG.SWRSTCR1 &= ~(uint8_t)(1 << (6 - ssi_channel));
    CPG.SWRSTCR1; /* Dummy read */

    /* ---- SSI TDM Mode Register Setting ---- */
    *ssif[ssi_channel].ssitdmr = 0;

    /* ---- SSI Control Register Setting ---- */
    // Selects AUDIO_X1 clock input. Doesn't enable interrupts - Rohan
    *ssif[ssi_channel].ssicr = SSI_SSICR0_USER_INIT_VALUE;

    /* ---- SSI FIFO Control Register Setting ---- */
    // Doesn't enable interrupts. Puts FIFOs into reset state
    *ssif[ssi_channel].ssifcr = SSI_SSIFCR_BASE_INIT_VALUE;
}

void ssiStart(const uint32_t ssi_channel)
{

    /* ==== Set SSI(Tx) ==== */
    /* ---- SSI Tx FIFO Buf reset release ---- */
    *ssif[ssi_channel].ssifcr &= ~(uint32_t)(1 << 1); // TFRST

    // ---- SSI Tx Empty Int enable ----
    *ssif[ssi_channel].ssifcr |= (uint32_t)(1 << 3); // TIE

    // ---- SSI Rx FIFO Buf reset release ----
    *ssif[ssi_channel].ssifcr &= ~(uint32_t)(1 << 0); // RFRST

    /* ---- SSI Rx Full Int enable ---- */
    *ssif[ssi_channel].ssifcr |= (uint32_t)(1 << 2); // RIE

    /* ---- SSI Tx and Rx enable ---- */
    *ssif[ssi_channel].ssicr |= (uint32_t)0b11; // TEN and REN
}
