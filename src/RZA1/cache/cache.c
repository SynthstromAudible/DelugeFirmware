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

#include "RZA1/compiler/asm/inc/asm.h"
#include "RZA1/system/iodefine.h"
#include "RZA1/system/r_typedefs.h"
#include <stdbool.h>

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
    L2C.REG9_D_LOCKDOWN0 = 0xFFFFFFFFuL; // lock the data cache - unlock late in boot sequence once everything is setup
    L2C.REG9_I_LOCKDOWN0 = 0x00000000uL; // unlock the instruction cache
    L2C.REG1_AUX_CONTROL |= 0x30000000uL;
    L2C.REG1_CONTROL = 0x00000001uL; /* Enable L2 cache */
}

extern uint32_t ttb_mmu1_base[]; /* The first level translation table - see ttb_init.S and the linker script */

/* As TTB_PARA_STRGLY in ttb_init.S - full access, domain 15 - but with B set, making it Device memory */
#define TTB_PARA_DEVICE 0x0DE6uL

/* Sets the descriptors for sections first_section to last_section inclusive, and makes sure the MMU uses them */
static void setSectionDescriptors(uint32_t first_section, uint32_t last_section, uint32_t attributes)
{
    for (uint32_t section = first_section; section <= last_section; section++)
    {
        ttb_mmu1_base[section] = (section << 20) | attributes;
    }

    /* TTBR0 marks table walks as cacheable, so make sure the walk sees the new descriptors */
    uintptr_t entries_start = (uintptr_t)&ttb_mmu1_base[first_section];
    uintptr_t entries_end   = (uintptr_t)&ttb_mmu1_base[last_section + 1];
    v7_dma_flush_range(entries_start, entries_end);
    L2CacheCleanInvalidateRange(entries_start, entries_end);

    __asm volatile("dsb" ::: "memory");
    __asm volatile("mcr p15, 0, %0, c8, c7, 0" ::"r"(0) : "memory"); /* TLBIALL */
    __asm volatile("mcr p15, 0, %0, c7, c5, 6" ::"r"(0) : "memory"); /* BPIALL */
    __asm volatile("dsb" ::: "memory");
    __asm volatile("isb" ::: "memory");
}

/******************************************************************************
 * Function Name: L2CacheMapRegistersAsDevice
 * Description  : Remaps the 1MB section holding the L2 controller's registers from strongly-ordered to Device
 *                memory. Strongly-ordered accesses each stall until the controller has answered, which made
 *                invalidating a 32kB Cluster's lines take hundreds of us. Device writes can be buffered, so a run of
 *                line operations doesn't stall on each one. They still reach the controller in order, and never
 *                speculatively.
 *
 *                Device accesses aren't ordered against Normal memory accesses though, unlike strongly-ordered ones,
 *                so code relying on that needs a DSB. This section also holds the BSC (0x3FFFC000), which configures
 *                SDRAM - so this must only be done once that's set up and the SDRAM is in use, as resetprg.c relies
 *                on BSC writes landing before it touches SDRAM.
 ******************************************************************************/
void L2CacheMapRegistersAsDevice(void)
{
    uint32_t section = (uint32_t)&L2C >> 20;
    setSectionDescriptors(section, section, TTB_PARA_DEVICE);
}

/* As TTB_PARA_NORMAL_CACHE in ttb_init.S, but with TEX=b100, C=0, B=1: Normal memory that's inner (L1) write-back
 * write-allocate, but outer (L2) non-cacheable. Relies on TEX remap being off (SCTLR.TRE=0), as it is. */
#define TTB_PARA_NORMAL_INNER_CACHE_ONLY 0x4DE6uL

/* The memory L2CacheMapRangeInnerOnly() has stopped L2 caching. Empty until then. */
static uintptr_t inner_only_start = 0;
static uintptr_t inner_only_end   = 0;

/******************************************************************************
 * Function Name: L2CacheMapRangeInnerOnly
 * Description  : Stops L2 caching the whole 1MB sections within [start, end), so buffers there that DMA writes into
 *                only need their L1 lines invalidating, which is quick. L1 caches them as before. Sections only
 *                partly within the range are left as they are.
 *
 *                Must be called before L2CacheUnlockData(), while L2 is still locked against allocating data, so it
 *                can't be holding lines from the range that the new mapping would then hide.
 ******************************************************************************/
void L2CacheMapRangeInnerOnly(uintptr_t start, uintptr_t end)
{
    uint32_t first_section = (start + 0xFFFFFuL) >> 20; /* Round up to the first whole section */
    uint32_t end_section   = end >> 20;                 /* Round down - one past the last whole section */
    if (first_section >= end_section)
    {
        return;
    }

    /* The inner attributes aren't changing, so L1's lines stay valid - but write any dirty ones out anyway, so none
     * go to memory by a different route than the one they were cached for */
    L1_D_CacheWritebackFlushAll();

    setSectionDescriptors(first_section, end_section - 1, TTB_PARA_NORMAL_INNER_CACHE_ONLY);

    inner_only_start = (uintptr_t)first_section << 20;
    inner_only_end   = (uintptr_t)end_section << 20;
}

/* Whether L2 can't be holding any of [start, end) */
static bool isInnerOnly(uintptr_t start, uintptr_t end)
{
    return start >= inner_only_start && end <= inner_only_end;
}

void L2CacheUnlockData(void)
{
    L2CacheFlushAll();
    L2C.REG9_D_LOCKDOWN0 = 0x00000000uL; // unlock the data cache
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

/******************************************************************************
 * Function Name: L2CacheCleanRange
 * Description  : Clean (flush dirty data to RAM) L2 cache lines by PA.
 * Arguments    : start - start physical address
 *                end   - end physical address (exclusive)
 * Return Value : none
 ******************************************************************************/
void L2CacheCleanRange(uintptr_t start, uintptr_t end)
{
    uint32_t addr = start & ~31uL;
    while (addr < end)
    {
        L2C.REG7_CLEAN_PA = addr;
        while (L2C.REG7_CLEAN_PA & 1uL)
        {
        }
        addr += 32;
    }
    L2C.REG7_CACHE_SYNC = 0;
}

/******************************************************************************
 * Function Name: L2CacheInvalidateRange
 * Description  : Invalidate L2 cache lines by PA (discard, no writeback).
 * Arguments    : start - start physical address
 *                end   - end physical address (exclusive)
 * Return Value : none
 ******************************************************************************/
void L2CacheInvalidateRange(uintptr_t start, uintptr_t end)
{

    uint32_t addr = start & ~31uL;
    while (addr < end)
    {
        L2C.REG7_INV_PA = addr;
        while (L2C.REG7_INV_PA & 1uL)
        {
        }
        addr += 32;
    }
    L2C.REG7_CACHE_SYNC = 0;
}

/******************************************************************************
 * Function Name: L2CacheCleanInvalidateRange
 * Description  : Clean and invalidate L2 cache lines by PA.
 * Arguments    : start - start physical address
 *                end   - end physical address (exclusive)
 * Return Value : none
 ******************************************************************************/
void L2CacheCleanInvalidateRange(uintptr_t start, uintptr_t end)
{
    uint32_t addr = start & ~31uL;
    while (addr < end)
    {
        L2C.REG7_CLEAN_INV_PA = addr;
        while (L2C.REG7_CLEAN_INV_PA & 1uL)
        {
        }
        addr += 32;
    }
    L2C.REG7_CACHE_SYNC = 0;
}

void invalidate_range_all_caches(uintptr_t start, uintptr_t end)
{
    v7_dma_flush_range(start, end);
    L2CacheCleanInvalidateRange(start, end);
    v7_dma_inv_range(start, end);
}

/* Like L2CacheInvalidateRange(), but lines only partly inside [start, end), which might also hold something else, get
 * cleaned too rather than just discarded - as v7_dma_inv_range() does for L1.
 *
 * Register accesses dominate the time this takes - especially if the L2 controller's registers are still strongly-
 * ordered, as each access then stalls until the controller's answered (see L2CacheMapRegistersAsDevice()). So unlike
 * the functions above, this doesn't poll each operation for completion.
 * On the L2C-310, operations on a line by PA are atomic - the controller doesn't accept another register access until
 * it's finished - so there's nothing to wait for. Only background operations by way need polling. Linux's driver
 * (arch/arm/mm/cache-l2x0.c) likewise only waits on line operations for the older L2C-220. The cache sync at the end
 * makes sure they've all taken effect before DMA starts. */
static void L2CacheInvalidateRangeKeepingEdges(uintptr_t start, uintptr_t end)
{
    uintptr_t addr = start & ~31uL;
    while (addr < end)
    {
        if (addr < start || addr + 32 > end)
        {
            L2C.REG7_CLEAN_INV_PA = addr;
        }
        else
        {
            L2C.REG7_INV_PA = addr;
        }
        addr += 32;
    }
    L2C.REG7_CACHE_SYNC = 0;
    /* Device accesses reach the controller in order, so once this read's answered, everything above has finished */
    while (L2C.REG7_CACHE_SYNC & 1uL)
    {
    }
}

/* Before DMA writes into [start, end). Whatever's there now is about to be overwritten, so unlike
 * invalidate_range_all_caches() it isn't written back to memory first - which for a dirty 32kB Cluster would mean
 * writing the whole thing out. Inner then outer, so L1's partial lines are cleaned into L2 before L2 cleans them. */
void invalidate_range_before_dma_read(uintptr_t start, uintptr_t end)
{
    v7_dma_inv_range(start, end);
    if (!isInnerOnly(start, end))
    {
        L2CacheInvalidateRangeKeepingEdges(start, end);
    }
}

/* After DMA has written into [start, end), to drop anything speculatively cached meanwhile. Outer then inner, so L1
 * can't refill from a stale L2 line after being invalidated. */
void invalidate_range_after_dma_read(uintptr_t start, uintptr_t end)
{
    if (!isInnerOnly(start, end))
    {
        L2CacheInvalidateRangeKeepingEdges(start, end);
    }
    v7_dma_inv_range(start, end);
}

/* End of File */
