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
/******************************************************************************
 * File Name    : drv_ssif.h
 * $Rev: 676 $
 * $Date:: 2014-04-24 10:11:59 +0900#$
 * Description  : This module is header for sample of SSIF driver.
 ******************************************************************************/
#ifndef DRV_SSIF_H
#define DRV_SSIF_H

/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/
// #include "sample_audio_dma.h"

#include "RZA1/intc/devdrv_intc.h" /* INTC Driver header */
#include "RZA1/ssi/drv_ssif.h"
#include "RZA1/ssi/drv_ssif_user.h"
#include "RZA1/system/dev_drv.h" /* Device Driver common header */
#include "RZA1/system/iodefine.h"
#include "RZA1/system/r_typedefs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/******************************************************************************
Typedef definitions
******************************************************************************/

#define SSI_CHANNEL_MAX (6u)

/* ==== SSI Transmission and reception ==== */
#define SSI_NONE       (0)
#define SSI_RX         (1u)
#define SSI_TX         (2u)
#define SSI_FULLDUPLEX (SSI_RX | SSI_TX)

#define SSI_SSIFCR_TIE_INIT_VALUE   (0x00000000uL)
#define SSI_SSIFCR_RIE_INIT_VALUE   (0x00000000uL)
#define SSI_SSIFCR_TFRST_INIT_VALUE (0x00000002uL)
#define SSI_SSIFCR_RFRST_INIT_VALUE (0x00000001uL)
#define SSI_SSIFCR_TTRG_INIT_VALUE  (0x000000A0uL) // By Rohan - I made it 0x000000C0uL, but have now given up on that
#define SSI_SSIFCR_BASE_INIT_VALUE                                                                                     \
    (SSI_SSIFCR_TIE_INIT_VALUE | SSI_SSIFCR_RIE_INIT_VALUE | SSI_SSIFCR_TFRST_INIT_VALUE | SSI_SSIFCR_TTRG_INIT_VALUE  \
        | SSI_SSIFCR_RFRST_INIT_VALUE)
/*  [3]   TIE   : B'0  : Transmit data empty interrupt (TXI) request is disabled    */
/*  [2]   RIE   : B'0  : Receive data full interrupt (RXI) request is disabled      */
/*  [1]   TFRST : B'1  : Reset is enabled                                           */
/*  [0]   RFRST : B'1  : Reset is enabled                                           */

/* ==== SSI Register Table ==== */
typedef struct
{
    volatile uint32_t* ssicr;   /* control register(SSICR) */
    volatile uint32_t* ssifcr;  /* FIFO control register(SSIFCR) */
    volatile uint32_t* ssisr;   /* status register(SSISR) */
    volatile uint32_t* ssifsr;  /* FIFO status register(SSIFSR) */
    volatile uint32_t* ssiftdr; /* Tx FIFO data register(SSIFTDR) */
    volatile uint32_t* ssifrdr; /* Rx FIFO data register(SSIFRDR) */
    volatile uint32_t* ssitdmr; /* TDM mode register(SSITDMR) */
} ssif_reg_t;

static const ssif_reg_t ssif[SSI_CHANNEL_MAX] = {
    {
        /* ch0 */
        &SSIF0.SSICR,                       /* control register(SSICR) */
        &SSIF0.SSIFCR,                      /* FIFO control register(SSIFCR) */
        &SSIF0.SSISR,                       /* status register(SSISR) */
        &SSIF0.SSIFSR,                      /* FIFO status register(SSIFSR) */
        (volatile uint32_t*)&SSIF0.SSIFTDR, /* Tx FIFO data register(SSIFTDR) */
        (volatile uint32_t*)&SSIF0.SSIFRDR, /* Rx FIFO data register(SSIFRDR) */
        &SSIF0.SSITDMR                      /* TDM mode register(SSITDMR) */
    },
    {
        /* ch1 */
        &SSIF1.SSICR,                       /* control register(SSICR) */
        &SSIF1.SSIFCR,                      /* FIFO control register(SSIFCR) */
        &SSIF1.SSISR,                       /* status register(SSISR) */
        &SSIF1.SSIFSR,                      /* FIFO status register(SSIFSR) */
        (volatile uint32_t*)&SSIF1.SSIFTDR, /* Tx FIFO data register(SSIFTDR) */
        (volatile uint32_t*)&SSIF1.SSIFRDR, /* Rx FIFO data register(SSIFRDR) */
        &SSIF1.SSITDMR                      /* TDM mode register(SSITDMR) */
    },
    {
        /* ch2 */
        &SSIF2.SSICR,                       /* control register(SSICR) */
        &SSIF2.SSIFCR,                      /* FIFO control register(SSIFCR) */
        &SSIF2.SSISR,                       /* status register(SSISR) */
        &SSIF2.SSIFSR,                      /* FIFO status register(SSIFSR) */
        (volatile uint32_t*)&SSIF2.SSIFTDR, /* Tx FIFO data register(SSIFTDR) */
        (volatile uint32_t*)&SSIF2.SSIFRDR, /* Rx FIFO data register(SSIFRDR) */
        &SSIF2.SSITDMR                      /* TDM mode register(SSITDMR) */
    },
    {
        /* ch3 */
        &SSIF3.SSICR,                       /* control register(SSICR) */
        &SSIF3.SSIFCR,                      /* FIFO control register(SSIFCR) */
        &SSIF3.SSISR,                       /* status register(SSISR) */
        &SSIF3.SSIFSR,                      /* FIFO status register(SSIFSR) */
        (volatile uint32_t*)&SSIF3.SSIFTDR, /* Tx FIFO data register(SSIFTDR) */
        (volatile uint32_t*)&SSIF3.SSIFRDR, /* Rx FIFO data register(SSIFRDR) */
        &SSIF3.SSITDMR                      /* TDM mode register(SSITDMR) */
    },
    {
        /* ch4 */
        &SSIF4.SSICR,                       /* control register(SSICR) */
        &SSIF4.SSIFCR,                      /* FIFO control register(SSIFCR) */
        &SSIF4.SSISR,                       /* status register(SSISR) */
        &SSIF4.SSIFSR,                      /* FIFO status register(SSIFSR) */
        (volatile uint32_t*)&SSIF4.SSIFTDR, /* Tx FIFO data register(SSIFTDR) */
        (volatile uint32_t*)&SSIF4.SSIFRDR, /* Rx FIFO data register(SSIFRDR) */
        &SSIF4.SSITDMR                      /* TDM mode register(SSITDMR) */
    },
    {
        /* ch5 */
        &SSIF5.SSICR,                       /* control register(SSICR) */
        &SSIF5.SSIFCR,                      /* FIFO control register(SSIFCR) */
        &SSIF5.SSISR,                       /* status register(SSISR) */
        &SSIF5.SSIFSR,                      /* FIFO status register(SSIFSR) */
        (volatile uint32_t*)&SSIF5.SSIFTDR, /* Tx FIFO data register(SSIFTDR) */
        (volatile uint32_t*)&SSIF5.SSIFRDR, /* Rx FIFO data register(SSIFRDR) */
        &SSIF5.SSITDMR                      /* TDM mode register(SSITDMR) */
    }};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* DRV_SSIF_H */

/* End of File */
