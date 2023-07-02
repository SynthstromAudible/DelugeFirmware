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
* http://www.renesas.com/disclaimer*
* Copyright (C) 2013-2014 Renesas Electronics Corporation. All rights reserved.
*******************************************************************************/
/*******************************************************************************
* File Name : lvds_iodefine.h
* $Rev: 2444 $
* $Date:: 2014-10-14 21:15:39 +0100#$
* Description : Definition of I/O Register (V1.01a)
******************************************************************************/
#ifndef LVDS_IODEFINE_H
#define LVDS_IODEFINE_H
/* ->SEC M1.10.1 : Not magic number */

struct st_lvds
{                                                          /* LVDS             */
    volatile uint32_t  LVDS_UPDATE;                            /*  LVDS_UPDATE     */
    volatile uint32_t  LVDSFCL;                                /*  LVDSFCL         */
    volatile uint8_t   dummy608[24];                           /*                  */
    volatile uint32_t  LCLKSELR;                               /*  LCLKSELR        */
    volatile uint32_t  LPLLSETR;                               /*  LPLLSETR        */
    volatile uint8_t   dummy609[4];                            /*                  */
    volatile uint32_t  LPHYACC;                                /*  LPHYACC         */
};


#define LVDS    (*(struct st_lvds    *)0xFCFF7A30uL) /* LVDS */


#define LVDSLVDS_UPDATE LVDS.LVDS_UPDATE
#define LVDSLVDSFCL LVDS.LVDSFCL
#define LVDSLCLKSELR LVDS.LCLKSELR
#define LVDSLPLLSETR LVDS.LPLLSETR
#define LVDSLPHYACC LVDS.LPHYACC
/* <-SEC M1.10.1 */
#endif
