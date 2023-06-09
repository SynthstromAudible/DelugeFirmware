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
******************************************************************************/
/******************************************************************************
* File Name     : sflash.h
* Device(s)     : RZ/A1H (R7S721001)
* Tool-Chain    : GNUARM-NONEv14.02-EABI
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : Flash commands
*******************************************************************************/
/*******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 21.10.2014 1.00
*******************************************************************************/

/* Prevention of multiple inclusion */
#ifndef _R_SFLASH_H_
#define _R_SFLASH_H_


/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/


/******************************************************************************
Typedef definitions
******************************************************************************/


/******************************************************************************
Macro definitions
******************************************************************************/
/* ---- serial flash command[S25FL512 Spansion)(uniform=256KB)] ---- */
#define SFLASHCMD_CHIP_ERASE    (0xC7)
#define SFLASHCMD_SECTOR_ERASE  (0xD8)
#define SFLASHCMD_BYTE_PROGRAM  (0x02)
#define SFLASHCMD_BYTE_READ     (0x0B)
#define SFLASHCMD_DUAL_READ     (0x3B)
#define SFLASHCMD_QUAD_READ     (0x6B)
#define SFLASHCMD_DUAL_IO_READ  (0xBB)
#define SFLASHCMD_QUAD_IO_READ  (0xEB)
#define SFLASHCMD_WRITE_ENABLE  (0x06)
#define SFLASHCMD_READ_STATUS   (0x05)
#define SFLASHCMD_READ_CONFIG   (0x35)
#define SFLASHCMD_WRITE_STATUS  (0x01)
#define SFLASHCMD_QUAD_PROGRAM  (0x32)
#define SFLASHCMD_READ_BANK     (0x16)
#define SFLASHCMD_WRITE_BANK    (0x17)
#define SFLASHCMD_READ_AUTOBOOT (0x14)
#define SFLASHCMD_CLEAR_STATUS  (0x30)

/* 4-byte address command*/
#define SFLASHCMD_BYTE_READ_4B     (0x0C)
#define SFLASHCMD_QUAD_IO_READ_4B  (0xEC)
#define SFLASHCMD_QUAD_READ_4B     (0x6C)
#define SFLASHCMD_BYTE_PROGRAM_4B  (0x12)
#define SFLASHCMD_QUAD_PROGRAM_4B  (0x34)
#define SFLASHCMD_SECTOR_ERASE_4B  (0xDC)


/* ---- serial flash register definitions ---- */
#define CFREG_QUAD_BIT          (0x02)          /* Quad mode bit(Configuration Register) */
#define CFREG_FREEZE_BIT        (0x01)          /* freeze bit(Configuration Register) */
#define STREG_BPROTECT_BIT      (0x1c)          /* protect bit(Status Register) */
#define STREG_SRWD_BIT          (0x80)          /* Status Register Write Disable(Status Register) */

#define CFREG_LC_BIT            (0xc0)          /* Latency Code bit(Configuration Register) */

#define SET_EXTADD_4BYTE        (0x80)          /* Extended Address Enable bit(Bank Address Register) */

/* Page and Sector size */
#define SF_PAGE_SIZE            (512)           /* Page size of serial flash memory */
#define SF_SECTOR_SIZE          (256 * 1024)    /* Sector size = 256 KB   */
#define SF_NUM_OF_SECTOR        (256)           /* Sector Count : 256 */


/******************************************************************************
Functions Prototypes
******************************************************************************/


/* R_SFLASH_H */
#endif 

/* End of File */
