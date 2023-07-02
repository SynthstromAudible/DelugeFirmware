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
* Copyright (C) 2013,2014 Renesas Electronics Corporation. All rights reserved.
*******************************************************************************/
/*******************************************************************************
*  System Name : SD Driver Sample Program
*  File Name   : sd_cfg.h
*  Abstract    : SD Memory card driver configuration
*  Version     : 4.00.01
*  Device      : RZ/A1 Group
*  Tool-Chain  : 
*  OS          : None
*
*  Note        : 
*              
*
*  History     : May.30.2013 ver.4.00.00 Initial release
*              : Jun.30.2014 ver.4.00.01 Modified file comment
*******************************************************************************/
#ifndef _SD_CFG_H_
#define _SD_CFG_H_

/* ------------------------------------------------------
  Set SDHI Base Address
--------------------------------------------------------*/
#define SDCFG_IP0_BASE         0xE804E000
#define SDCFG_IP1_BASE         0xE804E800

/* ------------------------------------------------------
  Set the method of check SD Status
--------------------------------------------------------*/
#define SDCFG_HWINT
//#define SDCFG_POLL

/* ------------------------------------------------------
  Set the method of data transfer
--------------------------------------------------------*/
#define SDCFG_TRNS_DMA
//#define SDCFG_TRNS_SW

    #ifdef SDCFG_TRNS_DMA
#define SDCFG_TRANS_DMA_64
    #endif

#define SD0_DMA_CHANNEL 3 // Not actually used
#define SD1_DMA_CHANNEL 2 // Deluge uses this SD port

/* ------------------------------------------------------
  Set the card type to support
--------------------------------------------------------*/
//#define SDCFG_MEM
#define SDCFG_IO

/* ------------------------------------------------------
  Set the speed to support
--------------------------------------------------------*/
//#define SDCFG_DS
#define SDCFG_HS

/* ------------------------------------------------------
  Set the version to support
--------------------------------------------------------*/
//#define SDCFG_VER1X            /* Version 1.1 */
#define SDCFG_VER2X            /* Version 2.x */

/* ------------------------------------------------------
  Set the method to detect card
--------------------------------------------------------*/
#define SDCFG_CD_INT

#ifdef SDCFG_CD_INT
    #ifndef SDCFG_HWINT
        #error    please define SDCFG_HWINT
    #endif
#endif

/* ------------------------------------------------------
  Set the SD bus width
--------------------------------------------------------*/
//#define SDCFG_SDMODE_1BIT




/* ==== end of the setting ==== */

                                            #if    defined(SDCFG_SDMODE_1BIT)
#if    defined(SDCFG_HWINT)
    #if    defined(SDCFG_TRNS_DMA)
        #if    defined(SDCFG_IO)
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #endif
        #else
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #endif
        #endif
    #else
        #if    defined(SDCFG_IO)
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #endif
        #else
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #endif
        #endif
    #endif
#else
    #if    defined(SDCFG_TRNS_DMA)
        #if    defined(SDCFG_IO)
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #endif
        #else
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #endif
        #endif
    #else
        #if    defined(SDCFG_IO)
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #endif
        #else
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER2X|SD_MODE_1BIT)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER1X|SD_MODE_1BIT)
                #endif
            #endif
        #endif
    #endif
#endif    
                                            #else
#if    defined(SDCFG_HWINT)
    #if    defined(SDCFG_TRNS_DMA)
        #if    defined(SDCFG_IO)
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER1X)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER1X)
                #endif
            #endif
        #else
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER1X)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER1X)
                #endif
            #endif
        #endif
    #else
        #if    defined(SDCFG_IO)
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER1X)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER1X)
                #endif
            #endif
        #else
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER1X)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_HWINT|SD_MODE_SW|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER1X)
                #endif
            #endif
        #endif
    #endif
#else
    #if    defined(SDCFG_TRNS_DMA)
        #if    defined(SDCFG_IO)
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER1X)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER1X)
                #endif
            #endif
        #else
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER1X)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_DMA|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER1X)
                #endif
            #endif
        #endif
    #else
        #if    defined(SDCFG_IO)
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_IO|SD_MODE_HS|SD_MODE_VER1X)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_IO|SD_MODE_DS|SD_MODE_VER1X)
                #endif
            #endif
        #else
            #if    defined(SDCFG_HS)
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_MEM|SD_MODE_HS|SD_MODE_VER1X)
                #endif
            #else
                #if    defined(SDCFG_VER2X)
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER2X)
                #else
                    #define SDCFG_DRIVER_MODE2     (SD_MODE_POLL|SD_MODE_SW|SD_MODE_MEM|SD_MODE_DS|SD_MODE_VER1X)
                #endif
            #endif
        #endif
    #endif
#endif    
                                                #endif

    #ifdef SDCFG_TRANS_DMA_64
#define SDCFG_DRIVER_MODE    (SDCFG_DRIVER_MODE2 | SD_MODE_DMA_64)
    #else
#define SDCFG_DRIVER_MODE    SDCFG_DRIVER_MODE2
    #endif


#endif /* _SD_CFG_H_    */

/* End of File */
