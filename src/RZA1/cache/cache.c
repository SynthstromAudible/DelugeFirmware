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
/*******************************************************************************
 * File Name   : cache.c
 * $Rev: 1330 $
 * $Date:: 2015-02-17 16:07:56 +0900#$
 * Description : Cache maintenance operations
 *******************************************************************************/

/******************************************************************************
Includes   <System Includes> , "Project Includes"
******************************************************************************/
#include "RZA1/cache/cache.h"
#include "RZA1/system/iodefine.h"
#include "RZA1/system/r_typedefs.h"

#ifdef __CC_ARM
#pragma arm section code   = "CODE_CACHE_OPERATION"
#pragma arm section rodata = "CONST_CACHE_OPERATION"
#pragma arm section rwdata = "DATA_CACHE_OPERATION"
#pragma arm section zidata = "BSS_CACHE_OPERATION"
#endif

/******************************************************************************
Typedef definitions
******************************************************************************/

/******************************************************************************
Macro definitions
******************************************************************************/
#define L1CACHE_FLUSH    (0) /* L1 cache flush                */
#define L1CACHE_WB       (1) /* L1 cache write back           */
#define L1CACHE_WB_FLUSH (2) /* L1 cache write back and flush */
/* The value to set to the L2 cache maintenance operation registers */
/* (reg7_inv_way, reg7_clean_way, reg7_clean_inv_way).              */
#define L2CACHE_8WAY (0x000000FFuL) /* All entries(8way) in the L2 cache */

/******************************************************************************
Imported global variables and functions (from other files)
******************************************************************************/

/******************************************************************************
Exported global variables and functions (to be accessed by other files)
******************************************************************************/
extern void L1_I_CacheFlushAllAsm(void);
extern void L1_D_CacheOperationAsm(uint32_t ope);
extern void L1_I_CacheEnableAsm(void);
extern void L1_D_CacheEnableAsm(void);
extern void L1_I_CacheDisableAsm(void);
extern void L1_D_CacheDisableAsm(void);
extern void L1BtacEnableAsm(void);
extern void L1BtacDisableAsm(void);
extern void L1PrefetchEnableAsm(void);
extern void L1PrefetchDisableAsm(void);

/******************************************************************************
Private global variables and functions
******************************************************************************/

/******************************************************************************
 * Function Name: L1_I_CacheFlushAll
 * Description  : Flush all I cache.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L1_I_CacheFlushAll(void)
{
    /* ==== Invalidate all I cache ==== */
    L1_I_CacheFlushAllAsm();
}

/******************************************************************************
 * Function Name: L1_D_CacheFlushAll
 * Description  : Flush all D cache.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L1_D_CacheFlushAll(void)
{
    /* ==== Invalidate all D cache by set/way ==== */
    L1_D_CacheOperationAsm(L1CACHE_FLUSH);
}

/******************************************************************************
 * Function Name: L1_D_CacheWritebackAll
 * Description  : Write back all D cache.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L1_D_CacheWritebackAll(void)
{
    /* ==== Clean all D cache by set/way ==== */
    L1_D_CacheOperationAsm(L1CACHE_WB);
}

/******************************************************************************
 * Function Name: L1_D_CacheWritebackFlushAll
 * Description  : Write back and flush all D cache.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L1_D_CacheWritebackFlushAll(void)
{
    /* ==== Invalidate and clean all D cache by set/way ==== */
    L1_D_CacheOperationAsm(L1CACHE_WB_FLUSH);
}

/******************************************************************************
 * Function Name: L1_I_CacheEnable
 * Description  : Enable I cache.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L1_I_CacheEnable(void)
{
    L1_I_CacheEnableAsm();
}

/******************************************************************************
 * Function Name: L1_D_CacheEnable
 * Description  : Enable D cache.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L1_D_CacheEnable(void)
{
    L1_D_CacheEnableAsm();
}

/******************************************************************************
 * Function Name: L1_I_CacheDisable
 * Description  : Disable I cache.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L1_I_CacheDisable(void)
{
    L1_I_CacheDisableAsm();
}

/******************************************************************************
 * Function Name: L1_D_CacheDisable
 * Description  : Disable D cache.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L1_D_CacheDisable(void)
{
    L1_D_CacheDisableAsm();
}

/******************************************************************************
 * Function Name: L1BtacEnable
 * Description  : Enable branch prediction.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L1BtacEnable(void)
{
    L1BtacEnableAsm();
}

/******************************************************************************
 * Function Name: L1BtacDisable
 * Description  : Disable branch prediction.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L1BtacDisable(void)
{
    L1BtacDisableAsm();
}

/******************************************************************************
 * Function Name: L1PrefetchEnable
 * Description  : Enable Dside prefetch.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L1PrefetchEnable(void)
{
    L1PrefetchEnableAsm();
}

/******************************************************************************
 * Function Name: L1PrefetchDisable
 * Description  : Disable Dside prefetch.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L1PrefetchDisable(void)
{
    L1PrefetchDisableAsm();
}

/******************************************************************************
 * Function Name: L2CacheFlushAll
 * Description  : Flush all cache.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L2CacheFlushAll(void)
{
    /* ==== Invalidate all cache by Way ==== */
    /* Set "1" to Way bits[7:0] of the reg7_inv_way register */
    L2C.REG7_INV_WAY = L2CACHE_8WAY;
    /* Wait until Way bits[7:0] is cleard */
    while ((L2C.REG7_INV_WAY & L2CACHE_8WAY) != 0x00000000uL)
    {
        /* Wait completion */
    }
}

/******************************************************************************
 * Function Name: L2CacheEnable
 * Description  : Enable L2 cache.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L2CacheEnable(void)
{
    L2C.REG2_INT_CLEAR   = 0x000001FFuL; /* Clear the reg2_int_raw_status register */
    L2C.REG9_D_LOCKDOWN0 = 0xFFFFFFFFuL; // lock the data cache - we need a lot of work around invalidating it with DMA
                                         // before it can be used
    L2C.REG9_I_LOCKDOWN0 = 0x00000000uL; // unlock the instruction cache
    L2C.REG1_CONTROL     = 0x00000001uL; /* Enable L2 cache */
}

/******************************************************************************
 * Function Name: L2CacheDisable
 * Description  : Disable L2 cache.
 * Arguments    : none
 * Return Value : none
 ******************************************************************************/
void L2CacheDisable(void)
{
    L2C.REG1_CONTROL = 0x00000000uL; /* Disable L2 cache */
}

void L2CacheInit(void)
{
    L2CacheDisable();
    L2CacheFlushAll();
    L2CacheEnable();
}

/* End of File */
