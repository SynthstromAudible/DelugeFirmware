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
* File Name     : spibsc_flash_api.c
* Device(s)     : RZ/A1H (R7S721001)
* Tool-Chain    : GNUARM-NONEv14.02-EABI
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : SPI Flash Control
*******************************************************************************/
/*******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 21.10.2014 1.00
*******************************************************************************/

/******************************************************************************
Includes <System Includes> , "Project Includes"
******************************************************************************/
#include "r_typedefs.h"
#include "sflash.h"
#include "spibsc.h"
#include "r_spibsc_flash_api.h"

//#pragma arm section code   = "CODE_SPIBSC_INIT2"
//#pragma arm section rodata = "CONST_SPIBSC_INIT2"
//#pragma arm section rwdata = "DATA_SPIBSC_INIT2"
//#pragma arm section zidata = "BSS_SPIBSC_INIT2"


/******************************************************************************
Typedef definitions
******************************************************************************/


/******************************************************************************
Macro definitions
******************************************************************************/


/******************************************************************************
Imported global variables and functions (from other files)
******************************************************************************/


/******************************************************************************
Exported global variables and functions (to be accessed by other files)
******************************************************************************/
st_spibsc_spimd_reg_t g_spibsc_spimd_reg;


/******************************************************************************
Private global variables and functions
******************************************************************************/
static int32_t read_data_quad(  uint32_t addr, uint8_t * buf, int32_t unit, uint32_t ch_no, uint32_t dual, uint8_t addr_mode);
static int32_t read_data_single(uint32_t addr, uint8_t * buf, int32_t unit, uint32_t ch_no, uint32_t dual, uint8_t addr_mode);


/******************************************************************************
* Function Name: R_SFLASH_EraseSector
* Description  : Serial flash memory is sector erase
* Arguments    : uint32_t addr  : erase address(address of serial flash memory)
*                uint32_t ch_no : use channel No
*                uint32_t dual
*                uint8_t  data_width
*                uint8_t  addr_mode
* Return Value :  0 : success
*                -1 : error
******************************************************************************/
int32_t R_SFLASH_EraseSector(uint32_t addr, uint32_t ch_no, uint32_t dual, uint8_t data_width, uint8_t addr_mode)
{
    int32_t ret;
    /* sector erase in Single-SPI   */
    ret = Userdef_SFLASH_Write_Enable(ch_no);          /* WREN Command */
    if(ret != 0)
    {
        return ret;
    }
    g_spibsc_spimd_reg.cdb   = SPIBSC_1BIT;            /* Commmand bit-width = Single */
    g_spibsc_spimd_reg.adb   = SPIBSC_1BIT;            /* Address bit-width = Single */
    g_spibsc_spimd_reg.cde   = SPIBSC_OUTPUT_ENABLE;   /* Command Enable */
    g_spibsc_spimd_reg.ocde  = SPIBSC_OUTPUT_DISABLE;  /* Optional-Command Disable */
    g_spibsc_spimd_reg.ade   = addr_mode;

    g_spibsc_spimd_reg.opde  = SPIBSC_OUTPUT_DISABLE;  /* Option-Data Disable */
    g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_DISABLE;  /* Disable */
    g_spibsc_spimd_reg.sslkp = SPIBSC_SPISSL_NEGATE;   /* Negate after transfer */
    g_spibsc_spimd_reg.spire = SPIBSC_SPIDATA_DISABLE; /* Data Access (Read Disable) */
    g_spibsc_spimd_reg.spiwe = SPIBSC_SPIDATA_DISABLE; /* Data Access (Write Disable) */

    if(addr_mode == SPIBSC_OUTPUT_ADDR_32)
    {
        /* SE:Sector Erase(4-byte address) */
        g_spibsc_spimd_reg.cmd = SFLASHCMD_SECTOR_ERASE_4B;
    }
    else
    {
        /* SE:Sector Erase(3-byte address) */
        g_spibsc_spimd_reg.cmd = SFLASHCMD_SECTOR_ERASE;
    }

    if( dual == SPIBSC_CMNCR_BSZ_DUAL )
    {
        g_spibsc_spimd_reg.addr = (uint32_t)(addr >> 1);
    }
    else
    {
        g_spibsc_spimd_reg.addr = addr;
    }

    /* SDR/DDR setting */
    g_spibsc_spimd_reg.dme    = SPIBSC_DUMMY_CYC_DISABLE; /* Dummy cycle disable */
    g_spibsc_spimd_reg.addre  = SPIBSC_SDR_TRANS; /* address    :SDR transmission */
    g_spibsc_spimd_reg.opdre  = SPIBSC_SDR_TRANS; /* option data:SDR transmission */
    g_spibsc_spimd_reg.spidre = SPIBSC_SDR_TRANS; /* data       :SDR transmission */

    ret = spibsc_transfer(ch_no, &g_spibsc_spimd_reg);
    if(ret != 0)
    {
        return ret;
    }

    ret = Userdef_SFLASH_Busy_Wait(ch_no, dual, data_width);
    if(ret != 0)
    {
        return ret;
    }

    return ret;

} /* End of function R_SFLASH_EraseSector() */

/******************************************************************************
* Function Name: R_SFLASH_ByteProgram
* Description  : Data of an argument is written on a serial flash memory.
*                When 1 was designated by SPI_QUAD macro, the page program command
*                corresponding to Quad is used.
*                When 0 was designated by SPI_QUAD macro, The page program command
*                corresponding to Single is used.
* Arguments    : uint32_t addr : write address(address of serial flash memory)
*                uint8_t *buf  : write data(Start address in a buffer)
*                int32_t size  : The number of data byte
*                uint32_t ch_no : use channel No
*                uint32_t dual
*                uint32_t data_width
*                uint8_t  addr_mode
* Return Value :  0 : success
*                -1 : error
******************************************************************************/
int32_t R_SFLASH_ByteProgram(uint32_t addr, uint8_t * buf, int32_t size, uint32_t ch_no,
                             uint32_t dual, uint8_t data_width, uint8_t addr_mode)
{
    int32_t unit;
    int32_t ret;
    uint8_t  *buf_b;
    uint16_t *buf_s;
    uint32_t *buf_l;

    ret = Userdef_SFLASH_Write_Enable(ch_no);          /* WREN Command */
    if(ret != 0)
    {
        return ret;
    }

    /* ---- Command,Address ---- */
    g_spibsc_spimd_reg.cdb   = SPIBSC_1BIT;            /* Commmand bit-width = Single */
    g_spibsc_spimd_reg.adb   = SPIBSC_1BIT;            /* Address bit-width = Single */
    g_spibsc_spimd_reg.cde   = SPIBSC_OUTPUT_ENABLE;   /* Command Enable */
    g_spibsc_spimd_reg.ocde  = SPIBSC_OUTPUT_DISABLE;  /* Optional-Command Disable */
    g_spibsc_spimd_reg.ade   = addr_mode;
    g_spibsc_spimd_reg.opde  = SPIBSC_OUTPUT_DISABLE;  /* Option-Data Disable */
    g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_DISABLE;  /* Disable */
    g_spibsc_spimd_reg.sslkp = SPIBSC_SPISSL_KEEP;     /* Keep after transfer */
    g_spibsc_spimd_reg.spire = SPIBSC_SPIDATA_DISABLE; /* Data Access (Read Disable) */
    g_spibsc_spimd_reg.spiwe = SPIBSC_SPIDATA_DISABLE; /* Data Access (Write Disable) */

    /* SDR/DDR setting */
    g_spibsc_spimd_reg.dme    = SPIBSC_DUMMY_CYC_DISABLE; /* Dummy cycle disable */
    g_spibsc_spimd_reg.addre  = SPIBSC_SDR_TRANS; /* address    :SDR transmission */
    g_spibsc_spimd_reg.opdre  = SPIBSC_SDR_TRANS; /* option data:SDR transmission */
    g_spibsc_spimd_reg.spidre = SPIBSC_SDR_TRANS; /* data       :SDR transmission */

    if (data_width == SPIBSC_1BIT)
    {
        if(addr_mode == SPIBSC_OUTPUT_ADDR_32)
        {
            /* PP: Page Program(4-byte address) */
            g_spibsc_spimd_reg.cmd = SFLASHCMD_BYTE_PROGRAM_4B;
        }
        else
        {
            /* PP: Page Program(3-byte address) */
            g_spibsc_spimd_reg.cmd = SFLASHCMD_BYTE_PROGRAM;
        }
    }
    else
    {
        if(addr_mode == SPIBSC_OUTPUT_ADDR_32)
        {
             /* QPP: Quad Page Program(4-byte address) */
            g_spibsc_spimd_reg.cmd = SFLASHCMD_QUAD_PROGRAM_4B;
        }
        else
        {
            /* QPP: Quad Page Program(3-byte address) */
            g_spibsc_spimd_reg.cmd = SFLASHCMD_QUAD_PROGRAM;
        }
    }

    if (dual == SPIBSC_CMNCR_BSZ_DUAL)
    {
        g_spibsc_spimd_reg.addr = (uint32_t)(addr >> 1);
    }
    else
    {
        g_spibsc_spimd_reg.addr = addr;
    }

    ret = spibsc_transfer(ch_no, &g_spibsc_spimd_reg);     /* Command,Address */
    if(ret != 0)
    {
        return ret;
    }

    /* ---- Data ---- */
    g_spibsc_spimd_reg.spidb = data_width;

    g_spibsc_spimd_reg.cde   = SPIBSC_OUTPUT_DISABLE;  /* Command Disable */
    g_spibsc_spimd_reg.ocde  = SPIBSC_OUTPUT_DISABLE;  /* Optional-Command Disable */
    g_spibsc_spimd_reg.ade   = SPIBSC_OUTPUT_DISABLE;  /* Disable Adr */
    g_spibsc_spimd_reg.opde  = SPIBSC_OUTPUT_DISABLE;  /* Option-Data Disable */
    g_spibsc_spimd_reg.spire = SPIBSC_SPIDATA_DISABLE; /* Data Access (Read Disable) */
    g_spibsc_spimd_reg.spiwe = SPIBSC_SPIDATA_ENABLE;  /* Data Access (Write Enable) */

    if (dual == SPIBSC_CMNCR_BSZ_DUAL)
    {
        if ((size % 8) == 0)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_32;  /* Enable(64bit) */
            unit = 8;
        }
        else if ((size % 4) == 0)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_16;  /* Enable(32bit) */
            unit = 4;
        }
        else if ((size % 2) == 0)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_8;   /* Enable(16bit) */
            unit = 2;
        }
        else
        {
            return -1;
        }
    }
    else
    {
        if ((size % 4) == 0)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_32;  /* Enable(32bit) */
            unit = 4;
        }
        else if ((size % 2) == 0)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_16;  /* Enable(16bit) */
            unit = 2;
        }
        else
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_8;   /* Enable(8bit) */
            unit = 1;
        }
    }

    while (size > 0)
    {
        if( unit == 1 )
        {
            buf_b = (uint8_t *)buf;
            g_spibsc_spimd_reg.smwdr[0] = (uint32_t)(((uint32_t)(*buf_b) << 24) & 0xff000000ul);
        }
        else if( unit == 2 )
        {
            buf_s = (uint16_t *)buf;
            g_spibsc_spimd_reg.smwdr[0] = (uint32_t)(((uint32_t)(*buf_s) << 16) & 0xffff0000ul);
        }
        else if( unit == 4 )
        {
            buf_l = (uint32_t *)buf;
            g_spibsc_spimd_reg.smwdr[0] = (uint32_t)(((uint32_t)(*buf_l)      ) & 0xfffffffful);
        }
        else if( unit == 8 )
        {
            buf_l = (uint32_t *)buf;
            g_spibsc_spimd_reg.smwdr[1] = (uint32_t)(((uint32_t)(*buf_l)      ) & 0xfffffffful);
            buf_l++;
            g_spibsc_spimd_reg.smwdr[0] = (uint32_t)(((uint32_t)(*buf_l)      ) & 0xfffffffful);
        }
        else
        {
            /* Do Nothing */
        }

        buf  += unit;

        size -= unit;

        if (size <= 0)
        {
            g_spibsc_spimd_reg.sslkp = SPIBSC_SPISSL_NEGATE;
        }

        ret = spibsc_transfer(ch_no, &g_spibsc_spimd_reg);    /* Data */
        if(ret != 0)
        {
            return ret;
        }
    }

    ret = Userdef_SFLASH_Busy_Wait(ch_no, dual, data_width);

    return ret;

} /* End of function R_SFLASH_ByteProgram() */

/******************************************************************************
* Function Name: R_SFLASH_ByteRead
* Description  : The range of the serial flash memory designated by an argument
*                is read and it's stocked in a buffer.
*                When 1 was designated by SPI_QUAD macro, read command corresponding to Quad is used.
*                When 0 was designated by SPI_QUAD macro, read command corresponding to Single is used.
* Arguments    : uint32_t addr  : read address(address of serial flash memory)
*                uint8_t *buf   : Start address in a buffer
*                int32_t  size  : The number of data byte
*                uint32_t ch_no : use channel No
*                uint32_t dual  : dual mode
*                uint32_t data_width
*                uint8_t  addr_mode
* Return Value :  0 : success
*                -1 : error
******************************************************************************/
int32_t R_SFLASH_ByteRead(uint32_t addr, uint8_t * buf, int32_t size, uint32_t ch_no, uint32_t dual,
                          uint8_t data_width, uint8_t addr_mode)
{
    int32_t ret = 0;
    int32_t unit;

    if (dual == SPIBSC_CMNCR_BSZ_DUAL)
    {
        if ((size % 8) == 0)
        {
            unit = 8;
        }
        else if ((size % 4) == 0)
        {
            unit = 4;
        }
        else if ((size % 2) == 0)
        {
            unit = 2;
        }
        else
        {
            return -1;
        }
    }
    else
    {
        if ((size % 4) == 0)
        {
            unit = 4;
        }
        else if ((size % 2) == 0)
        {
            unit = 2;
        }
        else
        {
            unit = 1;
        }
    }

    while (size > 0)
    {
        if(data_width == SPIBSC_1BIT)
        {
            ret = read_data_single(addr, buf, unit, ch_no, dual, addr_mode);
        }
        else
        {
            ret = read_data_quad(  addr, buf, unit, ch_no, dual, addr_mode);
        }
        if(ret != 0)
        {
            return ret;
        }

        /* increment address and buf */
        addr += (uint32_t)unit;
        buf  += unit;
        size -= unit;
    }
    return ret;

} /* End of function R_SFLASH_ByteRead() */

/******************************************************************************
* Function Name: R_SFLASH_Spibsc_Transfer
* Description  : Transmission setting of a SPI multi-I/O bus controller.
* Arguments    : uint32_t ch_no : use channel No
*                st_spibsc_spimd_reg_t *regset :
*                The pointer to a structure for the transfer
* Return Value :  0 : success
*                -1 : error
******************************************************************************/
int32_t R_SFLASH_Spibsc_Transfer(uint32_t ch_no, st_spibsc_spimd_reg_t * regset)
{
    return spibsc_transfer(ch_no, regset);

} /* End of function R_SFLASH_Spibsc_Transfer() */

/******************************************************************************
* Function Name: R_SFLASH_Ctrl_Protect
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
int32_t R_SFLASH_Ctrl_Protect(en_sf_req_t req, uint32_t ch_no, uint32_t dual, uint8_t data_width)
{
    return Userdef_SFLASH_Ctrl_Protect(req, ch_no, dual, data_width);

} /* End of function R_SFLASH_Ctrl_Protect() */

/******************************************************************************
* Function Name: read_data_quad
* Description  : The range of the serial flash memory designated by an argument
*                is read and it's stocked in a buffer.
*                An operation mode is performed by a Quad mode.
* Arguments    : uint32_t addr  : read address(address of serial flash memory)
*                uint8_t *buf   : Start address in a buffer
*                int32_t  unit  : data size
*                uint32_t ch_no : use channel No
*                uint32_t dual  : dual mode
*                uint8_t  addr_mode
* Return Value :  0 : success
*                -1 : error
******************************************************************************/
static int32_t read_data_quad(uint32_t addr, uint8_t * buf, int32_t unit, uint32_t ch_no, uint32_t dual, uint8_t addr_mode)
{
    int32_t ret;
    uint8_t  *buf_b;
    uint16_t *buf_s;
    uint32_t *buf_l;

   /* ---- Command,Address,Dummy ---- */
    g_spibsc_spimd_reg.cdb   = SPIBSC_1BIT;            /* Commmand bit-width = Single */
    g_spibsc_spimd_reg.adb   = SPIBSC_1BIT;            /* Address bit-width = Single */
    g_spibsc_spimd_reg.cde   = SPIBSC_OUTPUT_ENABLE;   /* Command Enable */
    g_spibsc_spimd_reg.ocde  = SPIBSC_OUTPUT_DISABLE;  /* Optional-Command Disable */
    g_spibsc_spimd_reg.ade   = addr_mode;

    g_spibsc_spimd_reg.opde  = SPIBSC_OUTPUT_DISABLE;  /* Option-Data Disable */

    g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_DISABLE;  /* Disable */
    g_spibsc_spimd_reg.sslkp = SPIBSC_SPISSL_KEEP;     /* Keep after transfer */
    g_spibsc_spimd_reg.spire = SPIBSC_SPIDATA_DISABLE; /* Data Access (Read Disable) */
    g_spibsc_spimd_reg.spiwe = SPIBSC_SPIDATA_DISABLE; /* Data Access (Write Disable) */

    if(addr_mode == SPIBSC_OUTPUT_ADDR_32)
    {
        /* QOR: Quad Output Read(4-byte address) */
        g_spibsc_spimd_reg.cmd = SFLASHCMD_QUAD_READ_4B;
    }
    else
    {
        /* QOR: Quad Output Read(4-byte address) */
        g_spibsc_spimd_reg.cmd = SFLASHCMD_QUAD_READ;
    }

    /* dummy cycle setting */
    g_spibsc_spimd_reg.dme   = SPIBSC_DUMMY_CYC_ENABLE;  /* Dummy cycle insertion enable */
    g_spibsc_spimd_reg.dmdb  = SPIBSC_1BIT;
    g_spibsc_spimd_reg.dmcyc = SPIBSC_DUMMY_8CYC;        /* Latency Code of configuration register is b'00 */
                                                         /* Mode bit Cycle:0, Dummy Cycle:8 */
    /* SDR/DDR setting */
    g_spibsc_spimd_reg.addre  = SPIBSC_SDR_TRANS; /* address    :SDR transmission */
    g_spibsc_spimd_reg.opdre  = SPIBSC_SDR_TRANS; /* option data:SDR transmission */
    g_spibsc_spimd_reg.spidre = SPIBSC_SDR_TRANS; /* data       :SDR transmission */

    if (dual == SPIBSC_CMNCR_BSZ_DUAL)
    {
        g_spibsc_spimd_reg.addr = (uint32_t)(addr >> 1);
    }
    else
    {
        g_spibsc_spimd_reg.addr = addr;
    }

    ret = spibsc_transfer(ch_no, &g_spibsc_spimd_reg); /* Command,Address */
    if(ret != 0)
    {
        return ret;
    }

    /* ---- Data ---- */
    g_spibsc_spimd_reg.spidb = SPIBSC_4BIT;            /* Quad */
    g_spibsc_spimd_reg.cde   = SPIBSC_OUTPUT_DISABLE;  /* Command Disable */
    g_spibsc_spimd_reg.ocde  = SPIBSC_OUTPUT_DISABLE;  /* Optional-Command Disable */
    g_spibsc_spimd_reg.ade   = SPIBSC_OUTPUT_DISABLE;  /* Disable Adr */
    g_spibsc_spimd_reg.opde  = SPIBSC_OUTPUT_DISABLE;  /* Option-Data Disable */
    g_spibsc_spimd_reg.spire = SPIBSC_SPIDATA_ENABLE;  /* Data Access (Read Enable) */
    g_spibsc_spimd_reg.spiwe = SPIBSC_SPIDATA_DISABLE; /* Data Access (Write Disable) */

    /* DATA input part is dummy cycle disable */
    g_spibsc_spimd_reg.dme     = SPIBSC_DUMMY_CYC_DISABLE;     /* Dummy cycle insertion disable */

    if (dual == SPIBSC_CMNCR_BSZ_DUAL)
    {
        if (unit == 8)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_32;  /* Enable(64bit) */
        }
        else if (unit == 4)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_16;  /* Enable(32bit) */
        }
        else if (unit == 2)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_8;   /* Enable(16bit) */
        }
        else
        {
            return -1;
        }
    }
    else
    {
        if (unit == 4)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_32;  /* Enable(32bit) */
        }
        else if (unit == 2)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_16;  /* Enable(16bit) */
        }
        else
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_8;   /* Enable(8bit) */
        }
    }

    g_spibsc_spimd_reg.sslkp = SPIBSC_SPISSL_NEGATE;
    ret = spibsc_transfer(ch_no, &g_spibsc_spimd_reg);         /* Data input */
    if(ret != 0)
    {
        return ret;
    }


    if( unit == 1 )
    {
        buf_b = (uint8_t *)buf;
        *buf_b = (uint8_t)((g_spibsc_spimd_reg.smrdr[0] >> 24) & 0x000000fful);
    }
    else if( unit == 2 )
    {
        buf_s = (uint16_t *)buf;
        *buf_s = (uint16_t)((g_spibsc_spimd_reg.smrdr[0] >> 16) & 0x0000fffful);
    }
    else if( unit == 4 )
    {
        buf_l = (uint32_t *)buf;
        *buf_l = (uint32_t)((g_spibsc_spimd_reg.smrdr[0]      ) & 0xfffffffful);
    }
    else if( unit == 8 )
    {
        buf_l = (uint32_t *)buf;
        *buf_l = (uint32_t)((g_spibsc_spimd_reg.smrdr[0]      ) & 0xfffffffful);
        buf_l++;
        *buf_l = (uint32_t)((g_spibsc_spimd_reg.smrdr[1]      ) & 0xfffffffful);
    }
    else
    {
        /* Do Nothing */
    }

    return 0;

} /* End of function read_data_quad() */

/******************************************************************************
* Function Name: read_data_single
* Description  : The range of the serial flash memory designated by an argument
*                is read and it's stocked in a buffer.
*                An operation mode is performed by a Single mode.
* Arguments    : uint32_t addr  : read address(address of serial flash memory)
*                uint8_t *buf   : Start address in a buffer
*                int32_t  unit  : data size
*                uint32_t ch_no : use channel No
*                uint32_t dual  : dual mode
*                uint8_t  addr_mode
* Return Value :  0 : success
*                -1 : error
******************************************************************************/
static int32_t read_data_single(uint32_t addr, uint8_t * buf, int32_t unit, uint32_t ch_no, uint32_t dual, uint8_t addr_mode)
{
    int32_t   ret;
    uint8_t  *buf_b;
    uint16_t *buf_s;
    uint32_t *buf_l;

    /* ---- Command,Address,Dummy ---- */
    g_spibsc_spimd_reg.cdb   = SPIBSC_1BIT;            /* Commmand bit-width = Single */
    g_spibsc_spimd_reg.adb   = SPIBSC_1BIT;            /* Address bit-width = Single  */
    g_spibsc_spimd_reg.cde   = SPIBSC_OUTPUT_ENABLE;   /* Command Enable              */
    g_spibsc_spimd_reg.ocde  = SPIBSC_OUTPUT_DISABLE;  /* Optional-Command Disable    */
    g_spibsc_spimd_reg.ade   = addr_mode;

    g_spibsc_spimd_reg.opde  = SPIBSC_OUTPUT_DISABLE;  /* Option-Data Disable */

    g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_DISABLE;  /* Disable                     */
    g_spibsc_spimd_reg.sslkp = SPIBSC_SPISSL_KEEP;     /* Keep after transfer         */
    g_spibsc_spimd_reg.spire = SPIBSC_SPIDATA_DISABLE; /* Data Access (Read Disable)  */
    g_spibsc_spimd_reg.spiwe = SPIBSC_SPIDATA_DISABLE; /* Data Access (Write Disable) */

    if(addr_mode == SPIBSC_OUTPUT_ADDR_32)
    {
        /* Fast READ(4-byte address) */
        g_spibsc_spimd_reg.cmd = SFLASHCMD_BYTE_READ_4B;
    }
    else
    {
        /* Fast READ(4-byte address) */
        g_spibsc_spimd_reg.cmd = SFLASHCMD_BYTE_READ;
    }

    /* dummy cycle setting */
    g_spibsc_spimd_reg.dme   = SPIBSC_DUMMY_CYC_ENABLE;   /* Dummy cycle insertion enable */
    g_spibsc_spimd_reg.dmdb  = SPIBSC_1BIT;
    g_spibsc_spimd_reg.dmcyc = SPIBSC_DUMMY_8CYC;         /* Latency Code of configuration register is b'00 */
                                                          /* Mode bit Cycle:0, Dummy Cycle:8 */

    /* SDR/DDR setting */
    g_spibsc_spimd_reg.addre  = SPIBSC_SDR_TRANS; /* address    :SDR transmission */
    g_spibsc_spimd_reg.opdre  = SPIBSC_SDR_TRANS; /* option data:SDR transmission */
    g_spibsc_spimd_reg.spidre = SPIBSC_SDR_TRANS; /* data       :SDR transmission */

    if (dual == SPIBSC_CMNCR_BSZ_DUAL)
    {
        g_spibsc_spimd_reg.addr = (uint32_t)(addr >> 1);
    }
    else
    {
        g_spibsc_spimd_reg.addr = addr;
    }

    ret = spibsc_transfer(ch_no, &g_spibsc_spimd_reg); /* Command,Address */
    if(ret != 0)
    {
        return ret;
    }

    /* ---- Data ---- */
    g_spibsc_spimd_reg.spidb = SPIBSC_1BIT;            /* Single                     */
    g_spibsc_spimd_reg.cde   = SPIBSC_OUTPUT_DISABLE;  /* Command Disable            */
    g_spibsc_spimd_reg.ocde  = SPIBSC_OUTPUT_DISABLE;  /* Optional-Command Disable   */
    g_spibsc_spimd_reg.ade   = SPIBSC_OUTPUT_DISABLE;  /* Disable Adr                */
    g_spibsc_spimd_reg.opde  = SPIBSC_OUTPUT_DISABLE;  /* Option-Data Disable        */
    g_spibsc_spimd_reg.spire = SPIBSC_SPIDATA_ENABLE;  /* Data Access (Read Enable)  */
    g_spibsc_spimd_reg.spiwe = SPIBSC_SPIDATA_DISABLE; /* Data Access (Write Enable) */

    /* DATA input part is dummy cycle disable */
    g_spibsc_spimd_reg.dme     = SPIBSC_DUMMY_CYC_DISABLE;    /* Dummy cycle insertion disable */

    if (dual == SPIBSC_CMNCR_BSZ_DUAL)
    {
        if (unit == 8)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_32;  /* Enable(64bit) */
        }
        else if (unit == 4)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_16;  /* Enable(32bit) */
        }
        else if (unit == 2)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_8;   /* Enable(16bit) */
        }
        else
        {
            return -1;
        }
    }
    else
    {
        if (unit == 4)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_32;  /* Enable(32bit) */
        }
        else if (unit == 2)
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_16;  /* Enable(16bit) */
        }
        else
        {
            g_spibsc_spimd_reg.spide = SPIBSC_OUTPUT_SPID_8;   /* Enable(8bit)  */
        }
    }

    g_spibsc_spimd_reg.sslkp = SPIBSC_SPISSL_NEGATE;
    ret = spibsc_transfer(ch_no, &g_spibsc_spimd_reg);         /* Data input */
    if(ret != 0)
    {
        return ret;
    }

    if( unit == 1 )
    {
         buf_b = (uint8_t *)buf;
        *buf_b = (uint8_t)((g_spibsc_spimd_reg.smrdr[0] >> 24) & 0x000000fful);
    }
    else if( unit == 2 )
    {
         buf_s = (uint16_t *)buf;
        *buf_s = (uint16_t)((g_spibsc_spimd_reg.smrdr[0] >> 16) & 0x0000fffful);
    }
    else if( unit == 4 )
    {
         buf_l = (uint32_t *)buf;
        *buf_l = (uint32_t)((g_spibsc_spimd_reg.smrdr[0]      ) & 0xfffffffful);
    }
    else if( unit == 8 )
        {
         buf_l = (uint32_t *)buf;
        *buf_l = (uint32_t)((g_spibsc_spimd_reg.smrdr[0]      ) & 0xfffffffful);
         buf_l++;
        *buf_l = (uint32_t)((g_spibsc_spimd_reg.smrdr[1]      ) & 0xfffffffful);
    }
    else
    {
        /* Do Nothing */
    }

    return 0;

} /* End of function read_data_single() */

/* End of File */
