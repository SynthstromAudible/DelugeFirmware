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
* File Name     : r_spibsc_ioset_api.h
* Device(s)     : RZ/A1H (R7S721001)
* Tool-Chain    : GNUARM-NONEv14.02-EABI
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : IO settings API
*******************************************************************************/
/*******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 21.10.2014 1.00
*******************************************************************************/

/* Prevention of multiple inclusion */
#ifndef _R_SPIBSC_IOSET_API_H_
#define _R_SPIBSC_IOSET_API_H_


/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/
#include "r_typedefs.h"


/******************************************************************************
Typedef definitions
******************************************************************************/
typedef struct
{
    uint32_t cdb;
    uint32_t ocdb;
    uint32_t adb;
    uint32_t opdb;
    uint32_t spidb;
    uint32_t cde;
    uint32_t ocde;
    uint32_t ade;
    uint32_t opde;
    uint32_t spide;
    uint32_t sslkp;
    uint32_t spire;
    uint32_t spiwe;

    uint32_t dme;     /* Dummy cycle enable at the time of a SPI mode */
    uint32_t addre;   /* Address DDR enable                           */
    uint32_t opdre;   /* Option data DDRenable                        */
    uint32_t spidre;  /* Transmission data DDR enable                 */

    uint8_t dmdb;     /* The dummy cycle bit width of the time of a SPI mode  */
    uint8_t dmcyc;    /* The number of dummy cycles of the time of a SPI mode */

    uint8_t  cmd;
    uint8_t  ocmd;
    uint32_t addr;
    uint8_t  opd[4];
    uint32_t smrdr[2];
    uint32_t smwdr[2];

} st_spibsc_spimd_reg_t;

typedef struct
{
    uint8_t udef_cmd;
    uint8_t udef_cmd_width;
    uint8_t udef_opd3;
    uint8_t udef_opd2;
    uint8_t udef_opd1;
    uint8_t udef_opd0;
    uint8_t udef_opd_enable;
    uint8_t udef_opd_width;
    uint8_t udef_dmycyc_num;
    uint8_t udef_dmycyc_enable;
    uint8_t udef_dmycyc_width;
    uint8_t udef_data_width;
    uint8_t udef_spbr;
    uint8_t udef_brdv;
    uint8_t udef_addr_width;
    uint8_t udef_addr_mode;

} st_spibsc_cfg_t;


/******************************************************************************
Macro definitions
******************************************************************************/
typedef enum
{
    /* request protect */
    SF_REQ_PROTECT = 0,                 

    /* release protect */
    SF_REQ_UNPROTECT,                   

    /* request Serial/Dual mode */
    SF_REQ_SERIALMODE,                  

    /* request Quad mode */
    SF_REQ_QUADMODE,                    
} en_sf_req_t;



/******************************************************************************
Functions Prototypes
******************************************************************************/
/* api function */
int32_t R_SFLASH_Exmode(uint32_t ch_no);
int32_t R_SFLASH_Spimode(uint32_t ch_no);
int32_t R_SFLASH_SpibscStop(uint32_t ch_no);
int32_t R_SFLASH_Spimode_Init(uint32_t ch_no, uint32_t dual, uint8_t data_width, uint8_t spbr, uint8_t brdv, uint8_t addr_mode);
int32_t R_SFLASH_Exmode_Init(uint32_t ch_no, uint32_t dual, st_spibsc_cfg_t *spibsccfg);
int32_t R_SFLASH_Exmode_Setting(uint32_t ch_no, uint32_t dual, st_spibsc_cfg_t *spibsccfg);
void    R_SFLASH_WaitTend(uint32_t ch_no);
int32_t R_SFLASH_Set_Config(uint32_t ch_no, st_spibsc_cfg_t *spibsccfg);


/* User defined function */
void Userdef_SPIBSC_Set_Config(uint32_t ch_no, st_spibsc_cfg_t *spibsccfg);


/* driver function */
int32_t   spibsc_bsz_set(uint32_t ch_no, uint32_t bsz, uint8_t data_width);
int32_t   spibsc_common_init(uint32_t ch_no, uint32_t bsz, uint8_t spbr, uint8_t brdv, uint8_t data_width);
void      spibsc_wait_tend(uint32_t ch_no);
int32_t   spibsc_dr_init(uint32_t ch_no, st_spibsc_cfg_t *spibsccfg);
int32_t   spibsc_exmode(uint32_t ch_no);
int32_t   spibsc_spimode(uint32_t ch_no);
int32_t   spibsc_stop(uint32_t ch_no);
int32_t   spibsc_transfer(uint32_t ch_no, st_spibsc_spimd_reg_t *regset);

/* R_SPIBSC_IOSET_API_H */
#endif 

/* End of File */
