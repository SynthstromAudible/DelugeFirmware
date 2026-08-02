@/*******************************************************************************
@* DISCLAIMER
@* This software is supplied by Renesas Electronics Corporation and is only
@* intended for use with Renesas products. No other uses are authorized. This
@* software is owned by Renesas Electronics Corporation and is protected under
@* all applicable laws, including copyright laws.
@* THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
@* THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT
@* LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
@* AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED.
@* TO THE MAXIMUM EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS
@* ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES SHALL BE LIABLE
@* FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR
@* ANY REASON RELATED TO THIS SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE
@* BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
@* Renesas reserves the right, without notice, to make changes to this software
@* and to discontinue the availability of this software. By using this software,
@* you agree to the additional terms and conditions found by accessing the
@* following link:
@* http://www.renesas.com/disclaimer
@* Copyright (C) 2012 - 2015 Renesas Electronics Corporation. All rights reserved.
@*******************************************************************************/
@/*******************************************************************************
@* File Name   : l1_cache_operation.s
@* $Rev: 1332 $
@* $Date:: 2015-02-17 17:22:59 +0900#$
@* Description : L1 cache maintenance operations
@*******************************************************************************/


@==================================================================
@ This code provides basic global enable for Cortex-A9 cache.
@ It also enables branch prediction
@ This code must be run from a privileged mode
@==================================================================
    .section    L1_CACHE_OPERATION, "x"
    .arm

    .global  L1_I_CacheFlushAllAsm
    .global  L1_D_CacheOperationAsm
    .global  L1_I_CacheEnableAsm
    .global  L1_D_CacheEnableAsm
    .global  L1_I_CacheDisableAsm
    .global  L1_D_CacheDisableAsm
    .global  L1BtacEnableAsm
    .global  L1BtacDisableAsm
    .global  L1PrefetchEnableAsm
    .global  L1PrefetchDisableAsm


@******************************************************************************
@ Function Name : L1_I_CacheFlushAllAsm
@ Description   : Invalidate all instruction caches to PoU.
@******************************************************************************
	.func L1_I_CacheFlushAllAsm
	.type L1_I_CacheFlushAllAsm, %function
L1_I_CacheFlushAllAsm: @FUNCTION

    MOV  r0, #0
    MCR  p15, 0, r0, c7, c5, 0      @;; ICIALLU
    DSB
    ISB

    BX   lr
.endfunc

@    ENDFUNC

@******************************************************************************
@ Function Name : L1_D_CacheOperationAsm
@ Description   : r0 = 0 : DCISW. Invalidate data or unified cache line by set/way.
@               : r0 = 1 : DCCSW. Clean data or unified cache line by set/way.
@               : r0 = 2 : DCCISW. Clean and Invalidate data or unified cache line by set/way.
	.func L1_D_CacheOperationAsm
	.type L1_D_CacheOperationAsm, %function
L1_D_CacheOperationAsm: @FUNCTION

    PUSH {r4-r11}

    MRC  p15, 1, r6, c0, c0, 1      @;; Read CLIDR
    ANDS r3, r6, #0x07000000        @;; Extract coherency level
    MOV  r3, r3, LSR #23            @;; Total cache levels << 1
    BEQ  Finished                   @;; If 0, no need to clean

    MOV  r10, #0                    @;; R10 holds current cache level << 1
Loop1:
    ADD  r2, r10, r10, LSR #1       @;; R2 holds cache "Set" position
    MOV  r1, r6, LSR r2             @;; Bottom 3 bits are the Cache-type for this level
    AND  r1, r1, #7                 @;; Isolate those lower 3 bits
    CMP  r1, #2
    BLT  Skip                       @;; No cache or only instruction cache at this level

    MCR  p15, 2, r10, c0, c0, 0     @;; Write the Cache Size selection register (CSSELR)
    ISB                             @;; ISB to sync the change to the CacheSizeID reg
    MRC  p15, 1, r1, c0, c0, 0      @;; Reads current Cache Size ID register (CCSIDR)
    AND  r2, r1, #7                 @;; Extract the line length field
    ADD  r2, r2, #4                 @;; Add 4 for the line length offset (log2 16 bytes)
    LDR  r4, =0x3FF
    ANDS r4, r4, r1, LSR #3         @;; R4 is the max number on the way size (right aligned)
    CLZ  r5, r4                     @;; R5 is the bit position of the way size increment
    LDR  r7, =0x7FFF
    ANDS r7, r7, r1, LSR #13        @;; R7 is the max number of the index size (right aligned)
Loop2:
    MOV  r9, r4                     @;; R9 working copy of the max way size (right aligned)

Loop3:
    ORR  r11, r10, r9, LSL r5       @;; Factor in the Way number and cache number into R11
    ORR  r11, r11, r7, LSL r2       @;; Factor in the Set number
    CMP  r0, #0
    BNE  Dccsw
    MCR  p15, 0, r11, c7, c6, 2     @;; Invalidate by Set/Way (DCISW)
    B    Count
Dccsw:
    CMP  r0, #1
    BNE  Dccisw
    MCR  p15, 0, r11, c7, c10, 2    @;; Clean by set/way (DCCSW)
    B    Count
Dccisw:
    MCR  p15, 0, r11, c7, c14, 2    @;; Clean and Invalidate by set/way (DCCISW)
Count:
    SUBS r9, r9, #1                 @;; Decrement the Way number
    BGE  Loop3
    SUBS r7, r7, #1                 @;; Decrement the Set number
    BGE  Loop2
Skip:
    ADD  r10, r10, #2               @;; increment the cache number
    CMP  r3, r10
    BGT  Loop1

Finished:
    DSB
    POP  {r4-r11}
    BX   lr
.endfunc

@    ENDFUNC

@******************************************************************************
@ Function Name : L1_I_CacheEnableAsm
@ Description   : Enable instruction caches.
@******************************************************************************
	.func L1_I_CacheEnableAsm
	.type L1_I_CacheEnableAsm, %function
L1_I_CacheEnableAsm: @FUNCTION

    @;; I-cache is controlled by bit 12

    MRC  p15, 0, r0, c1, c0, 0          @;; Read CP15 register 1
    ORR  r0, r0, #(0x1 << 12)           @;; Enable I Cache
    MCR  p15, 0, r0, c1, c0, 0          @;; Write CP15 register 1

    BX   lr
.endfunc

@    ENDFUNC

@******************************************************************************
@ Function Name : L1_D_CacheEnableAsm
@ Description   : Enable data caches.
@******************************************************************************
	.func L1_D_CacheEnableAsm
	.type L1_D_CacheEnableAsm, %function
L1_D_CacheEnableAsm: @FUNCTION

    @;; D-cache is controlled by bit 2

    MRC  p15, 0, r0, c1, c0, 0          @;; Read CP15 register 1
    ORR  r0, r0, #(0x1 << 2)            @;; Enable D Cache
    MCR  p15, 0, r0, c1, c0, 0          @;; Write CP15 register 1

    BX   lr
.endfunc

@    ENDFUNC

@******************************************************************************
@ Function Name : L1_I_CacheDisableAsm
@ Description   : Disable instruction caches.
@******************************************************************************
	.func L1_I_CacheDisableAsm
	.type L1_I_CacheDisableAsm, %function
L1_I_CacheDisableAsm: @FUNCTION

    @;; I-cache is controlled by bit 12

    MRC  p15, 0, r0, c1, c0, 0          @;; Read CP15 register 1
    BIC  r0, r0, #(0x1 << 12)           @;; Disable I Cache
    MCR  p15, 0, r0, c1, c0, 0          @;; Write CP15 register 1
    ISB

    BX   lr
.endfunc

@    ENDFUNC

@******************************************************************************
@ Function Name : L1_D_CacheDisableAsm
@ Description   : Disable data caches.
@******************************************************************************
	.func L1_D_CacheDisableAsm
	.type L1_D_CacheDisableAsm, %function
L1_D_CacheDisableAsm: @FUNCTION

    @;; D-cache is controlled by bit 2

    MRC  p15, 0, r0, c1, c0, 0          @;; Read CP15 register 1
    BIC  r0, r0, #(0x1 << 2)            @;; Disable D Cache
    MCR  p15, 0, r0, c1, c0, 0          @;; Write CP15 register 1
    ISB

    BX   lr
.endfunc

@    ENDFUNC

@******************************************************************************
@ Function Name : L1BtacEnableAsm
@ Description   : Enable program flow prediction.
@******************************************************************************
	.func L1BtacEnableAsm
	.type L1BtacEnableAsm, %function
L1BtacEnableAsm: @FUNCTION

    @;; Turning on branch prediction requires a general enable
    @;; CP15, c1. Control Register

    @;; Bit 11 [Z] bit Program flow prediction:
    @;; 0 = Program flow prediction disabled
    @;; 1 = Program flow prediction enabled.

    MRC  p15, 0, r0, c1, c0, 0          @;; Read System Control Register
    ORR  r0, r0, #(0x1 << 11)
    MCR  p15, 0, r0, c1, c0, 0          @;; Write System Control Register
    ISB

    BX   lr
.endfunc

@    ENDFUNC

@******************************************************************************
@ Function Name : L1BtacDisableAsm
@ Description   : Disable program flow prediction.
@******************************************************************************
	.func L1BtacDisableAsm
	.type L1BtacDisableAsm, %function
L1BtacDisableAsm: @FUNCTION

    @;; Turning off branch prediction requires a general enable
    @;; CP15, c1. Control Register

    @;; Bit 11 [Z] bit Program flow prediction:
    @;; 0 = Program flow prediction disabled
    @;; 1 = Program flow prediction enabled.

    MRC  p15, 0, r0, c1, c0, 0          @;; Read System Control Register
    BIC  r0, r0, #(0x1 << 11)
    MCR  p15, 0, r0, c1, c0, 0          @;; Write System Control Register

    BX   lr
.endfunc

@    ENDFUNC

@******************************************************************************
@ Function Name : L1PrefetchEnableAsm
@ Description   : Enable Dside prefetch.
@******************************************************************************
	.func L1PrefetchEnableAsm
	.type L1PrefetchEnableAsm, %function
L1PrefetchEnableAsm: @FUNCTION

    @;; Bit 2 [DP] Dside prefetch:
    @;; 0 = Dside prefetch disabled
    @;; 1 = Dside prefetch enabled.

    MRC  p15, 0, r0, c1, c0, 1          @;; Read Auxiliary Control Register
    ORR  r0, r0, #(0x1 << 2)            @;; Enable Dside prefetch
    MCR  p15, 0, r0, c1, c0, 1          @;; Write Auxiliary Control Register
    ISB

    BX   lr
.endfunc

@    ENDFUNC

@******************************************************************************
@ Function Name : L1PrefetchDisableAsm
@ Description   : Disable Dside prefetch.
@******************************************************************************
	.func L1PrefetchDisableAsm
	.type L1PrefetchDisableAsm, %function
L1PrefetchDisableAsm: @FUNCTION

    @;; Bit 2 [DP] Dside prefetch:
    @;; 0 = Dside prefetch disabled
    @;; 1 = Dside prefetch enabled.

    MRC  p15, 0, r0, c1, c0, 1          @;; Read Auxiliary Control Register
    BIC  r0, r0, #(0x1 << 2)            @;; Disable Dside prefetch
    MCR  p15, 0, r0, c1, c0, 1          @;; Write Auxiliary Control Register

    BX   lr
.endfunc

@    ENDFUNC

    .END
