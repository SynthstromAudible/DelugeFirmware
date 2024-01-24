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
 * File Name     : spibsc_flash_userdef.c
 * Device(s)     : RZ/A1H (R7S721001)
 * Tool-Chain    : GNUARM-NONEv14.02-EABI
 * H/W Platform  : RSK+RZA1H CPU Board
 * Description   :
 *******************************************************************************/
/*******************************************************************************
 * History       : DD.MM.YYYY Version Description
 *               : 21.10.2014 1.00
 *******************************************************************************/

/******************************************************************************
Includes <System Includes> , "Project Includes"
******************************************************************************/
#include "RZA1/spibsc/r_spibsc_flash_api.h"
#include "RZA1/spibsc/sflash.h"
#include "RZA1/spibsc/spibsc.h"
#include "RZA1/system/r_typedefs.h"

#include "deluge/deluge.h"

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

/******************************************************************************
Imported global variables and functions (from other files)
******************************************************************************/
extern st_spibsc_spimd_reg_t g_spibsc_spimd_reg;

/******************************************************************************
Exported global variables and functions (to be accessed by other files)
******************************************************************************/

/******************************************************************************
Private global variables and functions
******************************************************************************/
static int32_t set_mode(uint32_t ch_no, uint32_t dual, en_sf_req_t req, uint8_t data_width, uint8_t addr_mode);
static int32_t read_status(uint8_t* status1, uint8_t* status2, uint32_t ch_no, uint32_t dual, uint8_t data_width);
static int32_t read_config(uint8_t* config1, uint8_t* config2, uint32_t ch_no, uint32_t dual, uint8_t data_width);
static int32_t read_bank(uint8_t* bank1, uint8_t* bank2, uint32_t ch_no, uint32_t dual, uint8_t data_width);
static int32_t write_only_status(uint8_t status, uint32_t ch_no, uint32_t dual, uint8_t data_width);
static int32_t write_status(uint8_t status, uint8_t config, uint32_t ch_no, uint32_t dual, uint8_t data_width);
static int32_t write_bank(uint8_t bank, uint32_t ch_no, uint32_t dual, uint8_t data_width);
static int32_t read_autoboot(uint8_t* autoboot1, uint8_t* autoboot2, uint32_t ch_no, uint32_t dual, uint8_t data_width);
static int32_t clear_status(uint32_t ch_no, uint32_t dual, uint8_t data_width);

/******************************************************************************
 * Function Name: Userdef_SFLASH_Set_Mode
 * Description  : The setting function of serial flash memory dependence
 * Arguments    : uint32_t ch_no : use channel No
 *                uint32_t dual
 *                en_sf_req_tq req
 *                    SF_REQ_SERIALMODE -> Dual/Serial mode
 *                    SF_REQ_QUADMODE   -> Quad mode
 *                uint8_t data_width
 *                uint8_t addr_mode
 * Return Value :  0 : success
 *                -1 : error
 ******************************************************************************/
int32_t Userdef_SFLASH_Set_Mode(uint32_t ch_no, uint32_t dual, en_sf_req_t req, uint8_t data_width, uint8_t addr_mode)
{
    int32_t ret;

    ret = set_mode(ch_no, dual, req, data_width, addr_mode);

    return ret;

} /* End of function Userdef_SFLASH_Set_Mode() */

/******************************************************************************
 * Function Name: Userdef_SFLASH_Write_Enable
 * Description  : Issuing the write enable command to permit to erase/program
 *              : in the serial flash memory
 * Arguments    : uint32_t ch_no : use channel No
 * Return Value :  0 : success
 *                -1 : error
 ******************************************************************************/
int32_t Userdef_SFLASH_Write_Enable(uint32_t ch_no)
{
    int32_t ret;

    g_spibsc_spimd_reg.cdb   = SPIBSC_1BIT;            /* Single */
    g_spibsc_spimd_reg.cde   = SPIBSC_OUTPUT_ENABLE;   /* Command Enable */
    g_spibsc_spimd_reg.ocde  = SPIBSC_OUTPUT_DISABLE;  /* Optional-Command Disable */
    g_spibsc_spimd_reg.ade   = SPIBSC_OUTPUT_DISABLE;  /* Address Disable */
    g_spibsc_spimd_reg.opde  = SPIBSC_OUTPUT_DISABLE;  /* Option-Data Disable */
    g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_DISABLE;  /* Disable */
    g_spibsc_spimd_reg.sslkp = SPIBSC_SPISSL_NEGATE;   /* Negate after transfer */
    g_spibsc_spimd_reg.spire = SPIBSC_SPIDATA_DISABLE; /* Data Access (Read Disable) */
    g_spibsc_spimd_reg.spiwe = SPIBSC_SPIDATA_DISABLE; /* Data Access (Write Disable) */
    g_spibsc_spimd_reg.cmd   = SFLASHCMD_WRITE_ENABLE; /* WREN:Write Enable */

    /* SDR/DDR setting */
    g_spibsc_spimd_reg.dme    = SPIBSC_DUMMY_CYC_DISABLE; /* Dummy cycle disable */
    g_spibsc_spimd_reg.addre  = SPIBSC_SDR_TRANS;         /* address    :SDR transmission */
    g_spibsc_spimd_reg.opdre  = SPIBSC_SDR_TRANS;         /* option data:SDR transmission */
    g_spibsc_spimd_reg.spidre = SPIBSC_SDR_TRANS;         /* data       :SDR transmission */

    ret = R_SFLASH_Spibsc_Transfer(ch_no, &g_spibsc_spimd_reg);

    return ret;

} /* End of function Userdef_SFLASH_Write_Enable() */

/******************************************************************************
 * Function Name: Userdef_SFLASH_Busy_Wait
 * Description  : Loops internally when the serial flash memory is busy.
 * Arguments    : uint32_t ch_no : use channel No
 *                uint32_t dual
 *                uint8_t data_width
 * Return Value :  0 : success
 *                -1 : error
 ******************************************************************************/
int32_t Userdef_SFLASH_Busy_Wait(uint32_t ch_no, uint32_t dual, uint8_t data_width)
{
    uint8_t st_reg1;
    uint8_t st_reg2;
    int32_t ret;

    while (1)
    {
        ret = read_status(&st_reg1, &st_reg2, ch_no, dual, data_width);

        if (dual == SPIBSC_CMNCR_BSZ_DUAL)
        {
            /* s-flash x 2  */
            if (((st_reg1 & 0x01) == 0) && ((st_reg2 & 0x01) == 0))
            {
                break;
            }
        }
        else if (dual == SPIBSC_CMNCR_BSZ_SINGLE)
        {
            /* s-flash x 1  */
            if ((st_reg1 & 0x01) == 0)
            {
                break;
            }
        }
        else
        {
            ret = -1;
            break;
        }
        /* serial flash is busy */
        routineForSD();
    }
    return ret;

} /* End of function Userdef_SFLASH_Busy_Wait() */

/******************************************************************************
 * Function Name: Userdef_SFLASH_Ctrl_Protect
 * Description  : Protection of a cereal flash memory is released or set.
 * Arguments    : en_sf_req_t req :
 *                    SF_REQ_UNPROTECT -> clear all sector protection
 *                    SF_REQ_PROTECT   -> protect all sectors
 *                uint32_t ch_no : use channel No
 *                uint32_t dual
 *                uint8_t data_width
 * Return Value :  0 : success
 *                -1 : error
 ******************************************************************************/
int32_t Userdef_SFLASH_Ctrl_Protect(en_sf_req_t req, uint32_t ch_no, uint32_t dual, uint8_t data_width)
{
    uint8_t st_reg1;
    uint8_t st_reg2;
    uint8_t cf_reg1;
    uint8_t cf_reg2;
    int32_t ret;

    ret = read_status(&st_reg1, &st_reg2, ch_no, dual, data_width);
    if (ret != 0)
    {
        return ret;
    }

    ret = read_config(&cf_reg1, &cf_reg2, ch_no, dual, data_width);
    if (ret != 0)
    {
        return ret;
    }

    /* ==== Set value of Serial Flash(0) ==== */
    /* ---- clear freeze bit in configuration register  ---- */
    ret = write_status(st_reg1, (uint8_t)(cf_reg1 & (~CFREG_FREEZE_BIT)), ch_no, dual, data_width);
    if (ret != 0)
    {
        return ret;
    }

    if (req == SF_REQ_UNPROTECT)
    {
        st_reg1 &= (uint8_t)(~STREG_BPROTECT_BIT); /* un-protect in all area   */
    }
    else
    {
        st_reg1 |= STREG_BPROTECT_BIT; /* protect in all area      */
    }

    /* ---- clear or set protect bit in status register ---- */
    /* ---- with freeze bit in configuration register   ---- */
    ret = write_status(st_reg1, (uint8_t)(cf_reg1 | CFREG_FREEZE_BIT), ch_no, dual, data_width);

    return ret;

} /* End of function Userdef_SFLASH_Ctrl_Protect() */

/******************************************************************************
 * Function Name: set_mode
 * Description  : Serial flash memory mode setting
 *              : Specify the setting by the argument, req. The initial value of
 *              : mode differ to the specification of the serial
 * Arguments    : uint32_t ch_no : use channel No
 *                uint32_t dual
 *                en_sf_req_tq req
 *                    SF_REQ_SERIALMODE -> Dual/Serial mode
 *                    SF_REQ_QUADMODE   -> Quad mode
 *                uint8_t data_width
 *                uint8_t addr_mode
 * Return Value :  0 : success
 *                -1 : error
 ******************************************************************************/
static int32_t set_mode(uint32_t ch_no, uint32_t dual, en_sf_req_t req, uint8_t data_width, uint8_t addr_mode)
{
    uint8_t st_reg1;
    uint8_t st_reg2;
    uint8_t cf_reg1;
    uint8_t cf_reg2;
    uint8_t bf_reg1;
    uint8_t bf_reg2;
    int32_t ret;

    ret = read_status(&st_reg1, &st_reg2, ch_no, dual, data_width);
    if (ret != 0)
    {
        return ret;
    }
    if (((st_reg1 & 0x60) != 0) || ((dual == SPIBSC_CMNCR_BSZ_DUAL) && ((st_reg2 & 0x60) != 0)))
    {
        clear_status(ch_no, dual, data_width);
        ret = read_status(&st_reg1, &st_reg2, ch_no, dual, data_width);
        if (ret != 0)
        {
            return ret;
        }
    }

    ret = read_config(&cf_reg1, &cf_reg2, ch_no, dual, data_width);
    if (ret != 0)
    {
        return ret;
    }

    ret = read_bank(&bf_reg1, &bf_reg2, ch_no, dual, data_width);
    if (ret != 0)
    {
        return ret;
    }

    /* set serial-flash(1) same value of serial-flash(0) */
    if (req == SF_REQ_SERIALMODE)
    {
        /* ---- set dual/serial mode ---- */
        cf_reg1 = (uint8_t)(cf_reg1 & (~CFREG_QUAD_BIT));
    }
    else if (req == SF_REQ_QUADMODE)
    {
        /* ---- set quad mode ---- */
        cf_reg1 = (cf_reg1 | CFREG_QUAD_BIT);
    }
    else
    {
        return -1;
        /* error in argument */
    }

    /* Latency Code = b'00 fixing */
    cf_reg1 = (cf_reg1 & 0x3f);

    ret = write_status(st_reg1, cf_reg1, ch_no, dual, data_width);
    if (ret != 0)
    {
        return ret;
    }

    return ret;

} /* End of function set_mode() */

/******************************************************************************
 * Function Name: read_status
 * Description  : Reads the status of serial flash memory.
 * Arguments    : uint8_t * status1 : Serial Flash(0)
 *                uint8_t * status2 : Serial Flash(1)
 *                uint32_t ch_no : use channel No
 *                uint32_t dual
 *                uint8_t data_width
 * Return Value :  0 : success
 *                -1 : error
 ******************************************************************************/
static int32_t read_status(uint8_t* status1, uint8_t* status2, uint32_t ch_no, uint32_t dual, uint8_t data_width)
{
    int32_t ret;

    /* read status in Single-SPI */
    g_spibsc_spimd_reg.cdb   = SPIBSC_1BIT;           /* Commmand bit-width = Single */
    g_spibsc_spimd_reg.spidb = SPIBSC_1BIT;           /* Data bit-width = Single */
    g_spibsc_spimd_reg.cde   = SPIBSC_OUTPUT_ENABLE;  /* Command Enable */
    g_spibsc_spimd_reg.ocde  = SPIBSC_OUTPUT_DISABLE; /* Optional-Command Disable */
    g_spibsc_spimd_reg.ade   = SPIBSC_OUTPUT_DISABLE; /* Address Disable */
    g_spibsc_spimd_reg.opde  = SPIBSC_OUTPUT_DISABLE; /* Option-Data Disable */

    if (dual == SPIBSC_CMNCR_BSZ_SINGLE)
    {
        g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_8; /* Enable(8bit) */
    }
    else
    {
        g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_8; /* Enable(8->16bit) in dual-mode */
    }

    g_spibsc_spimd_reg.sslkp    = SPIBSC_SPISSL_NEGATE;  /* Negate after transfer */
    g_spibsc_spimd_reg.spire    = SPIBSC_SPIDATA_ENABLE; /* Data Access (Read Enable) */
    g_spibsc_spimd_reg.spiwe    = SPIBSC_SPIDATA_ENABLE; /* Data Access (Write Enable) */
    g_spibsc_spimd_reg.cmd      = SFLASHCMD_READ_STATUS; /* RDSR:Read Status Register */
    g_spibsc_spimd_reg.smwdr[0] = 0x00;                  /* Output 0 in read status */
    g_spibsc_spimd_reg.smwdr[1] = 0x00;                  /* Output 0 in read status */

    /* SDR/DDR setting */
    g_spibsc_spimd_reg.dme    = SPIBSC_DUMMY_CYC_DISABLE; /* Dummy cycle disable */
    g_spibsc_spimd_reg.addre  = SPIBSC_SDR_TRANS;         /* address    :SDR transmission */
    g_spibsc_spimd_reg.opdre  = SPIBSC_SDR_TRANS;         /* option data:SDR transmission */
    g_spibsc_spimd_reg.spidre = SPIBSC_SDR_TRANS;         /* data       :SDR transmission */

    ret = R_SFLASH_Spibsc_Transfer(ch_no, &g_spibsc_spimd_reg);
    if (ret != 0)
    {
        return ret;
    }

    if (dual == SPIBSC_CMNCR_BSZ_SINGLE)
    {
        *status1 = (uint8_t)(g_spibsc_spimd_reg.smrdr[0] >> 24); /* Data[31:24]  */
        *status2 = 0;
    }
    else
    {
        *status1 = (uint8_t)(g_spibsc_spimd_reg.smrdr[0] >> 24); /* Data[63:56](31:24)   */
        *status2 = (uint8_t)(g_spibsc_spimd_reg.smrdr[0] >> 16); /* Data[55:48](23:16)   */
    }

    return ret;

} /* End of function read_status() */

/******************************************************************************
 * Function Name: read_config
 * Description  : Reads the serial flash memory configuration.
 * Arguments    : uint8_t * config1 : Serial Flash(0)
 *                uint8_t * config2 : Serial Flash(1)
 *                uint32_t ch_no : use channel No
 *                uint32_t dual
 *                uint8_t data_width
 *                uint32_t dual
 *                uint8_t data_width
 * Return Value :  0 : success
 *                -1 : error
 ******************************************************************************/
static int32_t read_config(uint8_t* config1, uint8_t* config2, uint32_t ch_no, uint32_t dual, uint8_t data_width)
{
    int32_t ret;

    /* read connfig in Single-SPI   */
    g_spibsc_spimd_reg.cdb   = SPIBSC_1BIT;           /* Commmand bit-width = Single */
    g_spibsc_spimd_reg.spidb = SPIBSC_1BIT;           /* Data bit-width = Single */
    g_spibsc_spimd_reg.cde   = SPIBSC_OUTPUT_ENABLE;  /* Command Enable */
    g_spibsc_spimd_reg.ocde  = SPIBSC_OUTPUT_DISABLE; /* Optional-Command Disable */
    g_spibsc_spimd_reg.ade   = SPIBSC_OUTPUT_DISABLE; /* Address Disable */
    g_spibsc_spimd_reg.opde  = SPIBSC_OUTPUT_DISABLE; /* Option-Data Disable */

    if (dual == SPIBSC_CMNCR_BSZ_SINGLE)
    {
        g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_8; /* Enable(8bit) */
    }
    else
    {
        g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_8; /* Enable(8->16bit) in dual-mode */
    }

    g_spibsc_spimd_reg.sslkp    = SPIBSC_SPISSL_NEGATE;  /* Negate after transfer */
    g_spibsc_spimd_reg.spire    = SPIBSC_SPIDATA_ENABLE; /* Data Access (Read Enable) */
    g_spibsc_spimd_reg.spiwe    = SPIBSC_SPIDATA_ENABLE; /* Data Access (Write Enable) */
    g_spibsc_spimd_reg.cmd      = SFLASHCMD_READ_CONFIG; /* RCR:Read Configuration Register */
    g_spibsc_spimd_reg.smwdr[0] = 0x00;                  /* Output 0 in read config */
    g_spibsc_spimd_reg.smwdr[1] = 0x00;                  /* Output 0 in read config */

    /* SDR/DDR setting */
    g_spibsc_spimd_reg.dme    = SPIBSC_DUMMY_CYC_DISABLE; /* Dummy cycle disable */
    g_spibsc_spimd_reg.addre  = SPIBSC_SDR_TRANS;         /* address    :SDR transmission */
    g_spibsc_spimd_reg.opdre  = SPIBSC_SDR_TRANS;         /* option data:SDR transmission */
    g_spibsc_spimd_reg.spidre = SPIBSC_SDR_TRANS;         /* data       :SDR transmission */

    ret = R_SFLASH_Spibsc_Transfer(ch_no, &g_spibsc_spimd_reg);
    if (ret != 0)
    {
        return ret;
    }

    if (dual == SPIBSC_CMNCR_BSZ_SINGLE)
    {
        *config1 = (uint8_t)(g_spibsc_spimd_reg.smrdr[0] >> 24); /* Data[31:24]  */
        *config2 = 0;
    }
    else
    {
        *config1 = (uint8_t)(g_spibsc_spimd_reg.smrdr[0] >> 24); /* Data[63:56](31:24)   */
        *config2 = (uint8_t)(g_spibsc_spimd_reg.smrdr[0] >> 16); /* Data[55:48](23:16)   */
    }

    return ret;

} /* End of function read_config() */

/******************************************************************************
 * Function Name: read_bank
 * Description  : Read the bank register of flash memory.
 * Arguments    : uint8_t * bank1 : Serial Flash(0)
 *                uint8_t * bank2 : Serial Flash(1)
 *                uint32_t ch_no : use channel No
 * Return Value :  0 : success
 *                -1 : error
 ******************************************************************************/
static int32_t read_bank(uint8_t* bank1, uint8_t* bank2, uint32_t ch_no, uint32_t dual, uint8_t data_width)
{
    int32_t ret;

    /* read the bank register   */

    g_spibsc_spimd_reg.cdb   = SPIBSC_1BIT;           /* Commmand bit-width = Single */
    g_spibsc_spimd_reg.spidb = SPIBSC_1BIT;           /* Data bit-width = Single */
    g_spibsc_spimd_reg.cde   = SPIBSC_OUTPUT_ENABLE;  /* Command Enable */
    g_spibsc_spimd_reg.ocde  = SPIBSC_OUTPUT_DISABLE; /* Optional-Command Disable */
    g_spibsc_spimd_reg.ade   = SPIBSC_OUTPUT_DISABLE; /* Address Disable */
    g_spibsc_spimd_reg.opde  = SPIBSC_OUTPUT_DISABLE; /* Option-Data Disable */

    g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_8; /* Enable(8bit) */

    g_spibsc_spimd_reg.sslkp    = SPIBSC_SPISSL_NEGATE;  /* Negate after transfer */
    g_spibsc_spimd_reg.spire    = SPIBSC_SPIDATA_ENABLE; /* Data Access (Read Enable) */
    g_spibsc_spimd_reg.spiwe    = SPIBSC_SPIDATA_ENABLE; /* Data Access (Write Enable) */
    g_spibsc_spimd_reg.cmd      = SFLASHCMD_READ_BANK;   /* Bank Register Read */
    g_spibsc_spimd_reg.smwdr[0] = 0x00;                  /* Output 0 in read config */
    g_spibsc_spimd_reg.smwdr[1] = 0x00;                  /* Output 0 in read config */

    /* SDR/DDR setting */
    g_spibsc_spimd_reg.dme    = SPIBSC_DUMMY_CYC_DISABLE; /* Dummy cycle disable */
    g_spibsc_spimd_reg.addre  = SPIBSC_SDR_TRANS;         /* address    :SDR transmission */
    g_spibsc_spimd_reg.opdre  = SPIBSC_SDR_TRANS;         /* option data:SDR transmission */
    g_spibsc_spimd_reg.spidre = SPIBSC_SDR_TRANS;         /* data       :SDR transmission */

    ret = R_SFLASH_Spibsc_Transfer(ch_no, &g_spibsc_spimd_reg);

    if (dual == SPIBSC_CMNCR_BSZ_SINGLE)
    {
        *bank1 = (uint8_t)(g_spibsc_spimd_reg.smrdr[0] >> 24); /* Data[31:24]  */
        *bank2 = 0;
    }
    else
    {
        *bank1 = (uint8_t)(g_spibsc_spimd_reg.smrdr[0] >> 24); /* Data[31:24]  */
        *bank2 = (uint8_t)(g_spibsc_spimd_reg.smrdr[0] >> 16); /* Data[55:48](23:16)   */
    }

    return ret;

} /* End of function read_bank() */

/******************************************************************************
 * Function Name: write_only_status
 * Description  : Programs the status and configuration of the serial flash memory.
 * Arguments    : uint8_t status : status register value
 *                uint32_t ch_no : use channel No
 *                uint32_t dual
 *                uint8_t data_width
 * Return Value :  0 : success
 *                -1 : error
 ******************************************************************************/
static int32_t write_only_status(uint8_t status, uint32_t ch_no, uint32_t dual, uint8_t data_width)
{
    int32_t ret;

    /* write status in Single-SPI   */
    ret = Userdef_SFLASH_Write_Enable(ch_no); /* WREN Command */
    if (ret != 0)
    {
        return ret;
    }

    g_spibsc_spimd_reg.cdb    = SPIBSC_1BIT;            /* Commmand bit-width = Single */
    g_spibsc_spimd_reg.opdb   = SPIBSC_1BIT;            /* Optional-Data bit-width = Single */
    g_spibsc_spimd_reg.cde    = SPIBSC_OUTPUT_ENABLE;   /* Command Enable */
    g_spibsc_spimd_reg.ocde   = SPIBSC_OUTPUT_DISABLE;  /* Optional-Command Disable */
    g_spibsc_spimd_reg.ade    = SPIBSC_OUTPUT_DISABLE;  /* Address Disable */
    g_spibsc_spimd_reg.opde   = SPIBSC_OUTPUT_OPD_3;    /* Option-Data OPD3 */
    g_spibsc_spimd_reg.spide  = SPIBSC_OUTPUT_DISABLE;  /* Disable */
    g_spibsc_spimd_reg.sslkp  = SPIBSC_SPISSL_NEGATE;   /* Negate after transfer */
    g_spibsc_spimd_reg.spire  = SPIBSC_SPIDATA_DISABLE; /* Data Access (Read Disable) */
    g_spibsc_spimd_reg.spiwe  = SPIBSC_SPIDATA_DISABLE; /* Data Access (Write Disable) */
    g_spibsc_spimd_reg.cmd    = SFLASHCMD_WRITE_STATUS; /* WRR:Write Register */
    g_spibsc_spimd_reg.opd[0] = status;
    g_spibsc_spimd_reg.opd[1] = 0;
    g_spibsc_spimd_reg.opd[2] = 0;
    g_spibsc_spimd_reg.opd[3] = 0;

    /* SDR/DDR setting */
    g_spibsc_spimd_reg.dme    = SPIBSC_DUMMY_CYC_DISABLE; /* Dummy cycle disable */
    g_spibsc_spimd_reg.addre  = SPIBSC_SDR_TRANS;         /* address    :SDR transmission */
    g_spibsc_spimd_reg.opdre  = SPIBSC_SDR_TRANS;         /* option data:SDR transmission */
    g_spibsc_spimd_reg.spidre = SPIBSC_SDR_TRANS;         /* data       :SDR transmission */

    ret = R_SFLASH_Spibsc_Transfer(ch_no, &g_spibsc_spimd_reg);
    if (ret != 0)
    {
        return ret;
    }

    ret = Userdef_SFLASH_Busy_Wait(ch_no, dual, data_width);

    return ret;

} /* End of function write_only_status() */

/******************************************************************************
 * Function Name: write_status
 * Description  : Programs the status and configuration of the serial flash memory.
 * Arguments    : uint8_t status : status register value
 *                uint8_t config : configuration register value
 *                uint32_t ch_no : use channel No
 *                uint32_t dual
 *                uint8_t data_width
 * Return Value :  0 : success
 *                -1 : error
 ******************************************************************************/
static int32_t write_status(uint8_t status, uint8_t config, uint32_t ch_no, uint32_t dual, uint8_t data_width)
{
    int32_t ret;

    if ((status & STREG_SRWD_BIT) == STREG_SRWD_BIT)
    {
        status = (uint8_t)((status & ~STREG_SRWD_BIT));
        ret    = write_only_status(status, ch_no, dual, data_width);
        if (ret != 0)
        {
            return ret;
        }
    }

    /* write status in Single-SPI   */
    ret = Userdef_SFLASH_Write_Enable(ch_no); /* WREN Command */
    if (ret != 0)
    {
        return ret;
    }

    g_spibsc_spimd_reg.cdb    = SPIBSC_1BIT;            /* Commmand bit-width = Single */
    g_spibsc_spimd_reg.opdb   = SPIBSC_1BIT;            /* Optional-Data bit-width = Single */
    g_spibsc_spimd_reg.cde    = SPIBSC_OUTPUT_ENABLE;   /* Command Enable */
    g_spibsc_spimd_reg.ocde   = SPIBSC_OUTPUT_DISABLE;  /* Optional-Command Disable */
    g_spibsc_spimd_reg.ade    = SPIBSC_OUTPUT_DISABLE;  /* Address Disable */
    g_spibsc_spimd_reg.opde   = SPIBSC_OUTPUT_OPD_32;   /* Option-Data OPD3, OPD2 */
    g_spibsc_spimd_reg.spide  = SPIBSC_OUTPUT_DISABLE;  /* Disable */
    g_spibsc_spimd_reg.sslkp  = SPIBSC_SPISSL_NEGATE;   /* Negate after transfer */
    g_spibsc_spimd_reg.spire  = SPIBSC_SPIDATA_DISABLE; /* Data Access (Read Disable) */
    g_spibsc_spimd_reg.spiwe  = SPIBSC_SPIDATA_DISABLE; /* Data Access (Write Disable) */
    g_spibsc_spimd_reg.cmd    = SFLASHCMD_WRITE_STATUS; /* WRR:Write Register */
    g_spibsc_spimd_reg.opd[0] = status;
    g_spibsc_spimd_reg.opd[1] = config;
    g_spibsc_spimd_reg.opd[2] = 0;
    g_spibsc_spimd_reg.opd[3] = 0;

    /* SDR/DDR setting */
    g_spibsc_spimd_reg.dme    = SPIBSC_DUMMY_CYC_DISABLE; /* Dummy cycle disable */
    g_spibsc_spimd_reg.addre  = SPIBSC_SDR_TRANS;         /* address    :SDR transmission */
    g_spibsc_spimd_reg.opdre  = SPIBSC_SDR_TRANS;         /* option data:SDR transmission */
    g_spibsc_spimd_reg.spidre = SPIBSC_SDR_TRANS;         /* data       :SDR transmission */

    ret = R_SFLASH_Spibsc_Transfer(ch_no, &g_spibsc_spimd_reg);
    if (ret != 0)
    {
        return ret;
    }

    ret = Userdef_SFLASH_Busy_Wait(ch_no, dual, data_width);

    return ret;

} /* End of function write_status() */

/******************************************************************************
 * Function Name: write_bank
 * Description  : Set Bank Address Register of the serial flash memory.
 * Arguments    : uint8_t bank : bank write register value
 *                uint32_t ch_no : use channel No
 *                uint32_t dual
 *                uint8_t data_width
 * Return Value :  0 : success
 *                -1 : error
 ******************************************************************************/
static int32_t write_bank(uint8_t bank, uint32_t ch_no, uint32_t dual, uint8_t data_width)
{
    int32_t ret;

    /* write status in Single-SPI */
    ret = Userdef_SFLASH_Write_Enable(ch_no); /* WREN Command */
    if (ret != 0)
    {
        return ret;
    }

    g_spibsc_spimd_reg.cdb    = SPIBSC_1BIT;            /* Commmand bit-width = Single */
    g_spibsc_spimd_reg.opdb   = SPIBSC_1BIT;            /* Optional-Data bit-width = Single */
    g_spibsc_spimd_reg.cde    = SPIBSC_OUTPUT_ENABLE;   /* Command Enable */
    g_spibsc_spimd_reg.ocde   = SPIBSC_OUTPUT_DISABLE;  /* Optional-Command Disable */
    g_spibsc_spimd_reg.ade    = SPIBSC_OUTPUT_DISABLE;  /* Address Disable */
    g_spibsc_spimd_reg.opde   = SPIBSC_OUTPUT_OPD_3;    /* Option-Data OPD3 */
    g_spibsc_spimd_reg.spide  = SPIBSC_OUTPUT_DISABLE;  /* Disable */
    g_spibsc_spimd_reg.sslkp  = SPIBSC_SPISSL_NEGATE;   /* Negate after transfer */
    g_spibsc_spimd_reg.spire  = SPIBSC_SPIDATA_DISABLE; /* Data Access (Read Disable) */
    g_spibsc_spimd_reg.spiwe  = SPIBSC_SPIDATA_DISABLE; /* Data Access (Write Disable) */
    g_spibsc_spimd_reg.cmd    = SFLASHCMD_WRITE_BANK;   /* Bank Register Write */
    g_spibsc_spimd_reg.opd[0] = bank;
    g_spibsc_spimd_reg.opd[1] = 0;
    g_spibsc_spimd_reg.opd[2] = 0;
    g_spibsc_spimd_reg.opd[3] = 0;

    /* SDR/DDR setting */
    g_spibsc_spimd_reg.dme    = SPIBSC_DUMMY_CYC_DISABLE; /* Dummy cycle disable */
    g_spibsc_spimd_reg.addre  = SPIBSC_SDR_TRANS;         /* address    :SDR transmission */
    g_spibsc_spimd_reg.opdre  = SPIBSC_SDR_TRANS;         /* option data:SDR transmission */
    g_spibsc_spimd_reg.spidre = SPIBSC_SDR_TRANS;         /* data       :SDR transmission */

    ret = R_SFLASH_Spibsc_Transfer(ch_no, &g_spibsc_spimd_reg);
    if (ret != 0)
    {
        return ret;
    }

    ret = Userdef_SFLASH_Busy_Wait(ch_no, dual, data_width);

    return ret;

} /* End of function write_bank() */

/******************************************************************************
 * Function Name: read_autoboot
 * Description  : Reads the autoboot register of the serial flash memory.
 * Arguments    : uint8_t * autoboot1 : Serial Flash(0)
 *                uint8_t * autoboot2 : Serial Flash(1)
 *                uint32_t ch_no : use channel No
 *                uint32_t dual
 *                uint8_t data_width
 * Return Value :  0 : success
 *                -1 : error
 ******************************************************************************/
static int32_t read_autoboot(uint8_t* autoboot1, uint8_t* autoboot2, uint32_t ch_no, uint32_t dual, uint8_t data_width)
{
    int32_t ret;

    /* read status in Single-SPI */
    g_spibsc_spimd_reg.cdb   = SPIBSC_1BIT;           /* Commmand bit-width = Single */
    g_spibsc_spimd_reg.spidb = SPIBSC_1BIT;           /* Data bit-width = Single */
    g_spibsc_spimd_reg.cde   = SPIBSC_OUTPUT_ENABLE;  /* Command Enable */
    g_spibsc_spimd_reg.ocde  = SPIBSC_OUTPUT_DISABLE; /* Optional-Command Disable */
    g_spibsc_spimd_reg.ade   = SPIBSC_OUTPUT_DISABLE; /* Address Disable */
    g_spibsc_spimd_reg.opde  = SPIBSC_OUTPUT_DISABLE; /* Option-Data Disable */

    if (dual == SPIBSC_CMNCR_BSZ_SINGLE)
    {
        g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_8; /* Enable(8bit) */
    }
    else
    {
        g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_8; /* Enable(8->16bit) in dual-mode */
    }

    g_spibsc_spimd_reg.sslkp    = SPIBSC_SPISSL_NEGATE;    /* Negate after transfer */
    g_spibsc_spimd_reg.spire    = SPIBSC_SPIDATA_ENABLE;   /* Data Access (Read Enable) */
    g_spibsc_spimd_reg.spiwe    = SPIBSC_SPIDATA_ENABLE;   /* Data Access (Write Enable) */
    g_spibsc_spimd_reg.cmd      = SFLASHCMD_READ_AUTOBOOT; /* aUTOBOOT REG */
    g_spibsc_spimd_reg.smwdr[1] = 0x00;                    /* Output 0 in read status */

    /* SDR/DDR setting */
    g_spibsc_spimd_reg.dme    = SPIBSC_DUMMY_CYC_DISABLE; /* Dummy cycle disable */
    g_spibsc_spimd_reg.addre  = SPIBSC_SDR_TRANS;         /* address    :SDR transmission */
    g_spibsc_spimd_reg.opdre  = SPIBSC_SDR_TRANS;         /* option data:SDR transmission */
    g_spibsc_spimd_reg.spidre = SPIBSC_SDR_TRANS;         /* data       :SDR transmission */

    ret = R_SFLASH_Spibsc_Transfer(ch_no, &g_spibsc_spimd_reg);
    if (ret != 0)
    {
        return ret;
    }

    if (dual == SPIBSC_CMNCR_BSZ_SINGLE)
    {
        *autoboot1 = (uint8_t)(g_spibsc_spimd_reg.smrdr[0] >> 24); /* Data[31:24]  */
        *autoboot2 = 0;
    }
    else
    {
        *autoboot1 = (uint8_t)(g_spibsc_spimd_reg.smrdr[0] >> 24); /* Data[63:56](31:24)   */
        *autoboot2 = (uint8_t)(g_spibsc_spimd_reg.smrdr[0] >> 16); /* Data[55:48](23:16)   */
    }

    return ret;

} /* End of function read_autoboot() */

/******************************************************************************
 * Function Name: clear_status
 * Description  : After programming or eraseure, the status should be checked
 *                for programme or erase error. If the error is set,
 *                clear_status() MUST be called, otherwise the device will be
 *                locked out.It is good practice to check for the errors evertime
 *                and specially the first time the status is checked.
 *
 * Arguments    : uint32_t ch_no : use channel No
 *                uint32_t dual
 *                uint8_t data_width
 * Return Value :  0 : success
 *                -1 : error
 ******************************************************************************/
static int32_t clear_status(uint32_t ch_no, uint32_t dual, uint8_t data_width)
{
    int32_t ret;

    /* write status in Single-SPI   */
    ret = Userdef_SFLASH_Write_Enable(ch_no); /* WREN Command */
    if (ret != 0)
    {
        return ret;
    }

    g_spibsc_spimd_reg.cdb    = SPIBSC_1BIT;            /* Commmand bit-width = Single */
    g_spibsc_spimd_reg.opdb   = SPIBSC_1BIT;            /* Optional-Data bit-width = Single */
    g_spibsc_spimd_reg.cde    = SPIBSC_OUTPUT_ENABLE;   /* Command Enable */
    g_spibsc_spimd_reg.ocde   = SPIBSC_OUTPUT_DISABLE;  /* Optional-Command Disable */
    g_spibsc_spimd_reg.ade    = SPIBSC_OUTPUT_DISABLE;  /* Address Disable */
    g_spibsc_spimd_reg.opde   = SPIBSC_OUTPUT_DISABLE;  /* No data */
    g_spibsc_spimd_reg.spide  = SPIBSC_OUTPUT_DISABLE;  /* Disable */
    g_spibsc_spimd_reg.sslkp  = SPIBSC_SPISSL_NEGATE;   /* Negate after transfer */
    g_spibsc_spimd_reg.spire  = SPIBSC_SPIDATA_DISABLE; /* Data Access (Read Disable) */
    g_spibsc_spimd_reg.spiwe  = SPIBSC_SPIDATA_DISABLE; /* Data Access (Write Disable) */
    g_spibsc_spimd_reg.cmd    = SFLASHCMD_CLEAR_STATUS; /* WRR:Write Register */
    g_spibsc_spimd_reg.opd[0] = 0;
    g_spibsc_spimd_reg.opd[1] = 0;
    g_spibsc_spimd_reg.opd[2] = 0;
    g_spibsc_spimd_reg.opd[3] = 0;

    /* SDR/DDR setting */
    g_spibsc_spimd_reg.dme    = SPIBSC_DUMMY_CYC_DISABLE; /* Dummy cycle disable */
    g_spibsc_spimd_reg.addre  = SPIBSC_SDR_TRANS;         /* address    :SDR transmission */
    g_spibsc_spimd_reg.opdre  = SPIBSC_SDR_TRANS;         /* option data:SDR transmission */
    g_spibsc_spimd_reg.spidre = SPIBSC_SDR_TRANS;         /* data       :SDR transmission */

    ret = R_SFLASH_Spibsc_Transfer(ch_no, &g_spibsc_spimd_reg);
    if (ret != 0)
    {
        return ret;
    }

    ret = Userdef_SFLASH_Busy_Wait(ch_no, dual, data_width);

    return ret;

} /* End of function clear_status() */

/* End of File */
