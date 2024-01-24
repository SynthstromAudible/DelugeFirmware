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

#include "RZA1/cpu_specific.h"
#include "RZA1/spibsc/r_spibsc_flash_api.h"
#include "RZA1/spibsc/r_spibsc_ioset_api.h"
#include "RZA1/spibsc/spibsc.h"
#include "RZA1/system/iobitmasks/cpg_iobitmask.h"
#include "RZA1/system/iobitmasks/spibsc_iobitmask.h"
#include "RZA1/system/iodefine.h"
#include "RZA1/system/rza_io_regrw.h"

void initSPIBSC()
{

    // We've switched back to doing just 1-bit.
    // setPinMux(4, 2, 2);
    // setPinMux(4, 3, 2);

    // For some odd reason, all of this code being here makes it work. This was copied from the bootloader. I'm not sure
    // why it's all relevant

    /* Temporary (unused) variable for synchronisation read. */
    volatile uint16_t temp = 0;

    /* Used to suppress the 'variable declared but not used' warning */
    UNUSED_VARIABLE(temp);

    /* Release pin function for memory control without changing pin state. */
    if (RZA_IO_RegRead_16((uint16_t*)&CPG.DSFR, CPG_DSFR_IOKEEP_SHIFT, CPG_DSFR_IOKEEP) == 1)
    {
        RZA_IO_RegWrite_16((uint16_t*)&CPG.DSFR, 0, CPG_DSFR_IOKEEP_SHIFT, CPG_DSFR_IOKEEP);

        /* Perform a read after the write for synchronisation. */
        temp = RZA_IO_RegRead_16((uint16_t*)&CPG.DSFR, CPG_DSFR_IOKEEP_SHIFT, CPG_DSFR_IOKEEP);
    }

    /* SPIBSC stop accessing the SPI in bus mode */
    RZA_IO_RegWrite_32(&SPIBSC0.DRCR, 1, SPIBSC_DRCR_SSLN_SHIFT, SPIBSC_DRCR_SSLN);

    /* Wait for the setting to be accepted. */
    while (1)
    {
        if (RZA_IO_RegRead_32(&SPIBSC0.CMNSR, SPIBSC_CMNSR_SSLF_SHIFT, SPIBSC_CMNSR_SSLF) == SPIBSC_SSL_NEGATE)
        {
            /* This is intentional */
            break;
        }
    }

    /* Wait for the final transfers to end (TEND=1) for setting change in SPIBSC. */
    while (RZA_IO_RegRead_32(&SPIBSC0.CMNSR, SPIBSC_CMNSR_TEND_SHIFT, SPIBSC_CMNSR_TEND) != SPIBSC_TRANS_END)
    {
        /* wait for transfer-end */
    }

    /* The next access delayed setting : 1SPBCLK   */
    /* SPBSSL negate delayed setting   : 1.5SPBCLK */
    /* Clock delayed setting           : 1SPBCLK   */
    SPIBSC0.SSLDR = 0;

    /* bitrate setting register(SPBCR)  33.33[MHz] */
    RZA_IO_RegWrite_32(&SPIBSC0.SPBCR, 0, SPIBSC_SPBCR_BRDV_SHIFT, SPIBSC_SPBCR_BRDV);
    RZA_IO_RegWrite_32(&SPIBSC0.SPBCR, 2, SPIBSC_SPBCR_SPBR_SHIFT, SPIBSC_SPBCR_SPBR);

    /* Data read control register(DRCR) set to enable the Read Cache */
    RZA_IO_RegWrite_32(&SPIBSC0.DRCR, 1, SPIBSC_DRCR_RBE_SHIFT, SPIBSC_DRCR_RBE);

    R_SFLASH_WaitTend(SPIBSC_CH);

    st_spibsc_cfg_t g_spibsc_cfg;
    R_SFLASH_Set_Config(SPIBSC_CH,
        &g_spibsc_cfg); // Collates a bunch of preset options into the variable-holder g_spibsc_cfg, for us to actually
                        // enact in the next step

    if (R_SFLASH_Exmode_Setting(SPIBSC_CH, SPIBSC_CMNCR_BSZ_SINGLE, &g_spibsc_cfg)
        != 0) // This is the line I had to change from the default RSK config - it would only have worked in "DUAL" mode
              // - when 2x flash chips connected
    {
        // Uart::println("flash init didn't work");
    }
}
