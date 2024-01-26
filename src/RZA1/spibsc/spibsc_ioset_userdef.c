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
 *
 * Copyright (C) 2014 Renesas Electronics Corporation. All rights reserved.
 *******************************************************************************/
/*******************************************************************************
 * File Name     : spibsc_ioset_drv.c
 * Device(s)     : RZ/A1H (R7S721001)
 * Tool-Chain    : GNUARM-NONEv14.02-EABI
 * H/W Platform  : RSK+RZA1H CPU Board
 * Description   : User defined SPIBSC function
 *******************************************************************************/
/*******************************************************************************
 * History       : DD.MM.YYYY Version Description
 *               : 21.10.2014 1.00
 *******************************************************************************/

/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/
#include "RZA1/spibsc/r_spibsc_ioset_api.h"
#include "RZA1/spibsc/sflash.h"
#include "RZA1/spibsc/spibsc.h"
#include "RZA1/system/r_typedefs.h"

// #pragma arm section code   = "CODE_SPIBSC_INIT2"
// #pragma arm section rodata = "CONST_SPIBSC_INIT2"
// #pragma arm section rwdata = "DATA_SPIBSC_INIT2"
// #pragma arm section zidata = "BSS_SPIBSC_INIT2"

/******************************************************************************
Typedef definitions
******************************************************************************/

/******************************************************************************
Macro definitions
******************************************************************************/
// We've switched back to doing just 1-bit, since it seems the flash chips in the first Deluges manufactured don't
// support 4-bit
#define SPIBSC_BUS_WITDH (1)
// #define SPIBSC_BUS_WITDH         (4)

#define SPIBSC_OUTPUT_ADDR (SPIBSC_OUTPUT_ADDR_24)
// #define SPIBSC_OUTPUT_ADDR       (SPIBSC_OUTPUT_ADDR_32)

/******************************************************************************
Imported global variables and functions (from other files)
******************************************************************************/

/******************************************************************************
Exported global variables and functions (to be accessed by other files)
******************************************************************************/

/******************************************************************************
Private global variables and functions
******************************************************************************/

/******************************************************************************
 * Function Name: Userdef_SPIBSC_Set_Config
 * Description  : The setting function of SPIBSC.
 * Arguments    : uint32_t ch_no
 *                st_spibsc_cfg_t *spibsccfg
 * Return Value : none
 ******************************************************************************/
void Userdef_SPIBSC_Set_Config(uint32_t ch_no, st_spibsc_cfg_t* spibsccfg)
{
#if (SPIBSC_BUS_WITDH == 1)
/* command */
#if (SPIBSC_OUTPUT_ADDR == SPIBSC_OUTPUT_ADDR_32)
    spibsccfg->udef_cmd = SFLASHCMD_BYTE_READ_4B; /* 0CH Command */
#else
    spibsccfg->udef_cmd = SFLASHCMD_BYTE_READ; /* 0BH Command */
#endif

    /* address width */
    spibsccfg->udef_addr_width = SPIBSC_1BIT;

    /* optional data */
    spibsccfg->udef_opd_enable = SPIBSC_OUTPUT_DISABLE;

    /* option data bus width */
    spibsccfg->udef_opd_width = SPIBSC_1BIT;

    /* dummy cycle number */
    spibsccfg->udef_dmycyc_num    = SPIBSC_DUMMY_8CYC;
    spibsccfg->udef_dmycyc_enable = SPIBSC_DUMMY_CYC_ENABLE;

    /* data bit width */
    spibsccfg->udef_data_width = SPIBSC_1BIT;

#elif (SPIBSC_BUS_WITDH == 4)
/* command */
#if (SPIBSC_OUTPUT_ADDR == SPIBSC_OUTPUT_ADDR_32)
    spibsccfg->udef_cmd = SFLASHCMD_QUAD_IO_READ_4B; /* ECH Command */
#else
    spibsccfg->udef_cmd = SFLASHCMD_QUAD_IO_READ; /* EBH Command */
#endif

    /* address width */
    spibsccfg->udef_addr_width = SPIBSC_4BIT;

    /* optional data */
    spibsccfg->udef_opd_enable = SPIBSC_OUTPUT_OPD_3;

    /* option data bus width */
    spibsccfg->udef_opd_width = SPIBSC_4BIT;

    /* dummy cycle number */
    spibsccfg->udef_dmycyc_num    = SPIBSC_DUMMY_4CYC;
    spibsccfg->udef_dmycyc_enable = SPIBSC_DUMMY_CYC_ENABLE;

    /* data bit width */
    spibsccfg->udef_data_width = SPIBSC_4BIT;
#endif

    /* command width */
    spibsccfg->udef_cmd_width = SPIBSC_1BIT;

    /* optional data */
    spibsccfg->udef_opd3 = 0x00;
    spibsccfg->udef_opd2 = 0x00;
    spibsccfg->udef_opd1 = 0x00;
    spibsccfg->udef_opd0 = 0x00;

    /* dummy cycle width */
    spibsccfg->udef_dmycyc_width = SPIBSC_1BIT;

    /* bitrate */
    /*------------------------------------*/
    /*    udef_spbr = 1 : 66.67Mbps       */
    /*    udef_spbr = 2 : 33.33Mbps       */
    /*    udef_spbr = 3 : 22.22Mbps       */
    /*    udef_spbr = 4 : 16.67Mbps       */
    /*    udef_spbr = 5 : 13.33Mbps       */
    /*    udef_spbr = 6 : 11.11Mbps       */
    /*------------------------------------*/
    if (ch_no == 0)
    {
        spibsccfg->udef_spbr = 1;
        spibsccfg->udef_brdv = 0;
    }
    else
    {
        spibsccfg->udef_spbr = 2;
        spibsccfg->udef_brdv = 0;
    }

    spibsccfg->udef_addr_mode = SPIBSC_OUTPUT_ADDR;

} /* End of function  Userdef_SPIBSC_Set_Config() */
