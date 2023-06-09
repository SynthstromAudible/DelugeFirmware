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
* File Name     : spibsc.h
* Device(s)     : RZ/A1H (R7S721001)
* Tool-Chain    : GNUARM-NONEv14.02-EABI
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : Macro bit settings
*******************************************************************************/
/*******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 21.10.2014 1.00
*******************************************************************************/

/* Prevention of multiple inclusion */
#ifndef _SPIBSC_H_
#define _SPIBSC_H_


/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/
#include "r_typedefs.h"


/******************************************************************************
Macro definitions
******************************************************************************/
#define SPIBSC_CMNCR_MD_EXTRD       (0)
#define SPIBSC_CMNCR_MD_SPI         (1)

#define SPIBSC_OUTPUT_LOW           (0)
#define SPIBSC_OUTPUT_HIGH          (1)
#define SPIBSC_OUTPUT_LAST          (2)
#define SPIBSC_OUTPUT_HiZ           (3)

#define SPIBSC_CMNCR_CPHAT_EVEN     (0)
#define SPIBSC_CMNCR_CPHAT_ODD      (1)

#define SPIBSC_CMNCR_CPHAR_ODD      (0)
#define SPIBSC_CMNCR_CPHAR_EVEN     (1)

#define SPIBSC_CMNCR_SSLP_LOW       (0)
#define SPIBSC_CMNCR_SSLP_HIGH      (1)

#define SPIBSC_CMNCR_CPOL_LOW       (0)
#define SPIBSC_CMNCR_CPOL_HIGH      (1)

#define SPIBSC_CMNCR_BSZ_SINGLE     (0)
#define SPIBSC_CMNCR_BSZ_DUAL       (1)

#define SPIBSC_DELAY_1SPBCLK        (0)
#define SPIBSC_DELAY_2SPBCLK        (1)
#define SPIBSC_DELAY_3SPBCLK        (2)
#define SPIBSC_DELAY_4SPBCLK        (3)
#define SPIBSC_DELAY_5SPBCLK        (4)
#define SPIBSC_DELAY_6SPBCLK        (5)
#define SPIBSC_DELAY_7SPBCLK        (6)
#define SPIBSC_DELAY_8SPBCLK        (7)


#define SPIBSC_BURST_1              (0x00)
#define SPIBSC_BURST_2              (0x01)
#define SPIBSC_BURST_3              (0x02)
#define SPIBSC_BURST_4              (0x03)
#define SPIBSC_BURST_5              (0x04)
#define SPIBSC_BURST_6              (0x05)
#define SPIBSC_BURST_7              (0x06)
#define SPIBSC_BURST_8              (0x07)
#define SPIBSC_BURST_9              (0x08)
#define SPIBSC_BURST_10             (0x09)
#define SPIBSC_BURST_11             (0x0a)
#define SPIBSC_BURST_12             (0x0b)
#define SPIBSC_BURST_13             (0x0c)
#define SPIBSC_BURST_14             (0x0d)
#define SPIBSC_BURST_15             (0x0e)
#define SPIBSC_BURST_16             (0x0f)

#define SPIBSC_BURST_DISABLE        (0)
#define SPIBSC_BURST_ENABLE         (1)

#define SPIBSC_DRCR_RCF_EXE         (1)

#define SPIBSC_SSL_NEGATE           (0)
#define SPIBSC_TRANS_END            (1)

#define SPIBSC_1BIT                 (0)
#define SPIBSC_2BIT                 (1)
#define SPIBSC_4BIT                 (2)

#define SPIBSC_OUTPUT_DISABLE       (0)
#define SPIBSC_OUTPUT_ENABLE        (1)
#define SPIBSC_OUTPUT_ADDR_24       (0x07)
#define SPIBSC_OUTPUT_ADDR_32       (0x0f)
#define SPIBSC_OUTPUT_OPD_3         (0x08)
#define SPIBSC_OUTPUT_OPD_32        (0x0c)
#define SPIBSC_OUTPUT_OPD_321       (0x0e)
#define SPIBSC_OUTPUT_OPD_3210      (0x0f)

#define SPIBSC_OUTPUT_SPID_8        (0x08)
#define SPIBSC_OUTPUT_SPID_16       (0x0c)
#define SPIBSC_OUTPUT_SPID_32       (0x0f)

#define SPIBSC_SPISSL_NEGATE        (0)
#define SPIBSC_SPISSL_KEEP          (1)

#define SPIBSC_SPIDATA_DISABLE      (0)
#define SPIBSC_SPIDATA_ENABLE       (1)

#define SPIBSC_SPI_DISABLE          (0)
#define SPIBSC_SPI_ENABLE           (1)


/* Use for setting of the DME bit of "data read enable register"(DRENR) */
#define SPIBSC_DUMMY_CYC_DISABLE    (0)
#define SPIBSC_DUMMY_CYC_ENABLE     (1)

/* Use for setting of the DMCYC [2:0] bit of "data read dummy cycle register"(DRDMCR) */
#define SPIBSC_DUMMY_1CYC           (0)
#define SPIBSC_DUMMY_2CYC           (1)
#define SPIBSC_DUMMY_3CYC           (2)
#define SPIBSC_DUMMY_4CYC           (3)
#define SPIBSC_DUMMY_5CYC           (4)
#define SPIBSC_DUMMY_6CYC           (5)
#define SPIBSC_DUMMY_7CYC           (6)
#define SPIBSC_DUMMY_8CYC           (7)

/* Use for setting of "data read DDR enable register"(DRDRENR) */
#define SPIBSC_SDR_TRANS            (0)
#define SPIBSC_DDR_TRANS            (1)


/******************************************************************************
Functions Prototypes
******************************************************************************/


/* SPIBSC_H */
#endif 

/* End of File */
