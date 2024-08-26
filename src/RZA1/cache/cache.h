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
 * Copyright (C) 2012 - 2015 Renesas Electronics Corporation. All rights reserved.
 *******************************************************************************/
/******************************************************************************
 * File Name    : cache.h
 * $Rev: 1330 $
 * $Date:: 2015-02-17 16:07:56 +0900#$
 * Description  : Cache maintenance operations header
 ******************************************************************************/
#ifndef CACHE_H
#define CACHE_H

/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/

/******************************************************************************
Typedef definitions
******************************************************************************/

/******************************************************************************
Macro definitions
******************************************************************************/

/******************************************************************************
Variable Externs
******************************************************************************/

/******************************************************************************
Functions Prototypes
******************************************************************************/
void L1_I_CacheFlushAll(void);
void L1_D_CacheFlushAll(void);
void L1_D_CacheWritebackAll(void);
void L1_D_CacheWritebackFlushAll(void);
void L1_I_CacheEnable(void);
void L1_D_CacheEnable(void);
void L1_I_CacheDisable(void);
void L1_D_CacheDisable(void);
void L1BtacEnable(void);
void L1BtacDisable(void);
void L1PrefetchEnable(void);
void L1PrefetchDisable(void);
void L2CacheInit(void);
void L2CacheFlushAll(void);
void L2CacheEnable(void);
void L2CacheDisable(void);
void L2CacheInit(void);
extern void R_CACHE_L1Init(void);

#endif /* CACHE_H */

/* End of File */
