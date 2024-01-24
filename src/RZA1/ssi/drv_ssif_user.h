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
 * File Name    : drv_ssif_user.h
 * $Rev: 676 $
 * $Date:: 2014-04-24 10:11:59 +0900#$
 * Description  : Sample Data
 ******************************************************************************/
#ifndef DRV_SSIF_USER_H
#define DRV_SSIF_USER_H

/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/******************************************************************************
Typedef definitions
******************************************************************************/

/******************************************************************************
Macro definitions
******************************************************************************/
/* ==== SSI Channel0 ==== */
/* SSI0 Control Register Setting */
#define SSI_SSICR0_CKS_VALUE  (0x00000000uL)
#define SSI_SSICR0_CHNL_VALUE (0x00000000uL) // Was 0. I experimented with 0x00400000uL
#define SSI_SSICR0_DWL_VALUE                                                                                           \
    (0x00280000uL) // Was 0x00080000uL for 16-bit data word. Changed to 0x00280000uL for 24-bit. 0x00300000uL for 32-bit
// I don't understand why, but setting SWL to 16-bit is the only way to get things working, even for 24-bit data, and it
// appears that 24 correct bits are still being sent out
#define SSI_SSICR0_SWL_VALUE                                                                                           \
    (0x00030000uL) // Was 0x00030000uL for 32-bit system word. Changed to 0x00010000uL for 16-bit. 0x00020000uL for
                   // 24-bit
#define SSI_SSICR0_SCKD_VALUE (0x00008000uL)
#define SSI_SSICR0_SWSD_VALUE (0x00004000uL)
#define SSI_SSICR0_SCKP_VALUE (0x00000000uL)
#define SSI_SSICR0_SWSP_VALUE (0x00000000uL)
#define SSI_SSICR0_SPDP_VALUE (0x00000000uL)
#define SSI_SSICR0_SDTA_VALUE (0x00000000uL)
#define SSI_SSICR0_PDTA_VALUE (0x00000000uL)
#define SSI_SSICR0_DEL_VALUE  (0x00000000uL)
#define SSI_SSICR0_CKDV_VALUE                                                                                          \
    (0x00000020uL) // Original was 0x00000030uL - /8. 0x00000040uL is /16. 0x00000090uL is /12. 0x00000020uL is /4.
/*  [30]    CKS     : B'0    : AUDIO_X1 input                                                   */
/*  [23:22] CHNL    : B'00   : 1 channel / system word                                          */
/*  [21:19] DWL     : B'001  : 16 bit / data word                                               */
/*  [18:16] SWL     : B'011  : 32 bit / system word                                             */
/*  [15]    SCKD    : B'1    : Serial Bit Clock Direction:master mode                           */
/*  [14]    SWSD    : B'1    : Serial WS Direction:master mode                                  */
/*  [13]    SCKP    : B'0    : SSIWS and SSIDATA change at the SSISCK falling edge              */
/*  [12]    SWSP    : B'0    : SSIWS is low for 1st channel, high for 2nd channel               */
/*  [11]    SPDP    : B'0    : Padding bits are low                                             */
/*  [10]    SDTA    : B'0    : Tx and Rx in the order of serial data and padding bits           */
/*  [9]     PDTA    : B'0    : The lower bits of parallel data(SSITDR, SSIRDR) are transferred  */
/*                             prior to the upper bits                                          */
/*  [8]     DEL     : B'0    : 1 clock cycle delay between SSIWS and SSIDATA                    */
/*  [7:4]   CKDV    : B'0011 : AUDIO PHY/8 (64FS,AUDIO_X1@22.5792MHz/32bit system word)         */
#define SSI_SSICR0_USER_INIT_VALUE                                                                                     \
    (SSI_SSICR0_CKS_VALUE | SSI_SSICR0_CHNL_VALUE | SSI_SSICR0_DWL_VALUE | SSI_SSICR0_SWL_VALUE                        \
        | SSI_SSICR0_SCKD_VALUE | SSI_SSICR0_SWSD_VALUE | SSI_SSICR0_SCKP_VALUE | SSI_SSICR0_SWSP_VALUE                \
        | SSI_SSICR0_SPDP_VALUE | SSI_SSICR0_SDTA_VALUE | SSI_SSICR0_PDTA_VALUE | SSI_SSICR0_DEL_VALUE                 \
        | SSI_SSICR0_CKDV_VALUE)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* DRV_SSIF_USER_H */

/* End of File */
