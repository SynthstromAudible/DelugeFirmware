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
*******************************************************************************/
/******************************************************************************
* File Name     : reset_handler.S
* Device(s)     : RZ/A1H (R7S721001)
* Tool-Chain    : GNUARM-NONEv14.02-EABI
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : Called by reset vector (start.S/mirrorstart.S)
*               : As such its the startpoint for this software
******************************************************************************/
/******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 21.10.2014 1.00
******************************************************************************/

    .global reset_handler
    .global undefined_handler
    .global svc_handler
    .global prefetch_handler
    .global abort_handler
    .global reserved_handler

    .text

/* Standard definitions of mode bits and interrupt flags in PSRs */
    .equ    USR_MODE , 0x10
    .equ    FIQ_MODE , 0x11
    .equ    IRQ_MODE , 0x12
    .equ    SVC_MODE , 0x13
    .equ    ABT_MODE , 0x17
    .equ    UND_MODE , 0x1b
    .equ    SYS_MODE , 0x1f
    .equ    Thum_bit , 0x20                           /* CPSR/SPSR Thumb bit */

reset_handler:
/* ========================================================================= */
/* Multi-core startup (future proofing boot code)                            */
/* Check core, if not core 0  put to sleep.                                  */
/* ========================================================================= */
        MRC     p15, 0, r0, c0, c0, 5                          /* Read MPIDR */
        ANDS    r0, r0, #3
goToSleep:
        WFINE
        BNE     goToSleep

/* ========================================================================= */
/* Disable cache and MMU in case it was left enabled from an earlier         */
/* This does not need to be done from a cold reset                           */
/*****************************************************************************/
    MRC  p15, 0, r0, c1, c0, 0 /* Read CP15 SCTLR                            */
    BIC  r0, r0, #(0x1 << 12)  /* Clear I bit 12 to disable I Cache          */
    BIC  r0, r0, #(0x1 <<  2)  /* Clear C bit  2 to disable D Cache          */
    BIC  r0, r0, #0x1           /* Clear M bit  0 to disable MMU             */
    MCR  p15, 0, r0, c1, c0, 0 /* Write value back to CP15 SCR               */

/* ========================================================================= */
/*    Reset Vbar to this programs vectors                                    */
/* ========================================================================= */
    BL   VbarInit

/* ========================================================================= */
/* To boot using the on-chip program allowing user code to run from attached */
/* SPI/eSD, or eMMC devices, the RZA1 uses the Hivecs base vector            */
/* address (0xFFFF0000).                                                     */
/* Call SetLowVectors to clear the Hivecs bit in the System Control Register */
/* to allow the processor to use the relocated vector table specified in     */
/* this program.                                                             */
/* See the Hardware Manual Boot Mode section for detailed description of the */
/* startup process when using the on-chip ROM boot program.                  */
/* ========================================================================= */
    BL  SetLowVectors

/* ========================================================================= */
/*  Setting up Stack Area hadled by .ld file                                 */
/* ========================================================================= */
/* SVC Mode(Default)                                                         */
    CPS  #SVC_MODE
    LDR  sp, =svc_stack_end

/* IRQ Mode                                                                  */
    CPS  #IRQ_MODE
    LDR  sp, =irq_stack_end

/* FRQ Mode                                                                  */
    CPS  #FIQ_MODE
    LDR  sp, =fiq_stack_end

/* ABT_MODE                                                                  */
    CPS  #ABT_MODE
    LDR  sp, =abt_stack_end

/* SYS_MODE                                                                  */
    CPS  #SYS_MODE

/* System mode Stack pointer                                                 */
    LDR  sp, =program_stack_end

/* ========================================================================= */
/*  TLB maintenance, Invalidate Data and Instruction TLBs                    */
/* ========================================================================= */
    MOV  r0,#0
    MCR  p15, 0, r0, c8, c7, 0 /* I-TLB & D-TLB invalidation (TLBIALL)       */

/* ========================================================================= */
/* Invalidate instruction cache, also flushes branch target cache            */
/* ========================================================================= */
    MOV  r0, #0                   /* SBZ                                     */
    MCR  p15, 0, r0, c7, c5, 0 /* ICIALLU                                    */

/* ========================================================================= */
/* Cache Invalidation code for Cortex-A9                                     */
/* ========================================================================= */
                                          /* Invalidate L1 Instruction Cache */
    MRC  p15, 1, r0, c0, c0, 1        /* Read Cache Level ID Register (CLIDR)*/
    TST  r0, #0x3                                         /*  Harvard Cache? */
    MOV  r0, #0
    MCRNE   p15, 0, r0, c7, c5, 0            /* Invalidate Instruction Cache */

                                           /* Invalidate Data/Unified Caches */
    MRC  p15, 1, r0, c0, c0, 1                                 /* Read CLIDR */
    ANDS r3, r0, #0x07000000                      /* Extract coherency level */
    MOV  r3, r3, LSR #23                          /* Total cache levels << 1 */
    BEQ  Finished                                  /* If 0, no need to clean */

    MOV  r10, #0                       /* R10 holds current cache level << 1 */
Loop1:
    ADD  r2, r10, r10, LSR #1               /* R2 holds cache "Set" position */
    MOV  r1, r0, LSR r2   /* Bottom 3 bits are the Cache-type for this level */
    AND  r1, r1, #7                            /* Isolate those lower 3 bits */
    CMP  r1, #2
    BLT  Skip            /* No cache or only instruction cache at this level */

    MCR  p15, 2, r10, c0, c0, 0  /* Write the Cache Size selection  (CSSELR) */
    ISB                     /* ISB to sync the change to the CacheSizeID reg */
    MRC  p15, 1, r1, c0, c0, 0       /* Reads current Cache Size ID (CCSIDR) */
    AND  r2, r1, #7                         /* Extract the line length field */
    ADD  r2, r2, #4      /* Add 4 for the line length offset (log2 16 bytes) */
    LDR  r4, =0x3FF
    ANDS r4, r4, r1, LSR #3          /* R4 is the max number on the way size */
    CLZ  r5, r4          /* R5 is the bit position of the way size increment */
    LDR  r7, =0x7FFF
    ANDS r7, r7, r1, LSR #13       /* R7 is the max number of the index size */
Loop2:
    MOV  r9, r4                      /* R9 working copy of the max way size  */

Loop3:
    ORR  r11, r10, r9, LSL r5 /* Factor in the Way and cache number into R11 */
    ORR  r11, r11, r7, LSL r2                    /* Factor in the Set number */
    MCR  p15, 0, r11, c7, c6, 2             /* Invalidate by Set/Way (DCISW) */
    SUBS r9, r9, #1                              /* Decrement the Way number */
    BGE  Loop3
    SUBS r7, r7, #1                              /* Decrement the Set number */
    BGE  Loop2
Skip:
    ADD  r10, r10, #2                          /* increment the cache number */
    CMP  r3, r10
    BGT  Loop1

Finished:
/* ========================================================================= */
/* TTB initialize                                                            */
/* ========================================================================= */
    BL    init_TTB                                         /* Initialize TTB */

/* ========================================================================= */
/* Setup domain control register - Enable all domains to client mode         */
/* ========================================================================= */
	MRC  p15, 0, r0, c3, c0, 0     /* Read Domain Access Control Register    */
	LDR  r0, =0x55555555    /* Initialize every domain entry to b01 (client) */
	MCR  p15, 0, r0, c3, c0, 0       /* Write Domain Access Control Register */

/* ========================================================================= */
/* Enable access to NEON/VFP by enabling access to Coprocessors 10 and 11.   */
/* Enables Full Access i.e. in both privileged and non privileged modes      */
/* ========================================================================= */
    MRC  p15, 0, r0, c1, c0, 2   /* Read Coprocessor Access Control Register */
    ORR  r0, r0, #(0xF << 20)                 /* Enable access to CP 10 & 11 */
    MCR  p15, 0, r0, c1, c0, 2  /* Write Coprocessor Access Control Register */
    ISB

/* ========================================================================= */
/* Switch on the VFP and NEON hardware                                       */
/* ========================================================================= */
    MOV  r0, #0x40000000
    VMSR FPEXC, r0                       /* Write FPEXC register, EN bit set */

/* ========================================================================= */
/* Enable MMU                                                                */
/* ========================================================================= */
    BL   enable_mmu

/* ========================================================================= */
/* Hardware initialize                                                       */
/* Initialize CPG, BSC for CS0 and CS1, enable On-Chip Data-Retention RAM    */
/* ========================================================================= */
    LDR  r12,=Peripheral_Basic_Init    /* Use register for possible long jump */
    BLX  r12                                          /* Hardware Initialize */

/* ========================================================================= */
/* initsct                                                                   */
/* Initialize ZI in Stack Area                                               */
/* ========================================================================= */
    LDR  r12,=initsct                 /* Use register for possible long jump */
    BLX  r12                                             /* Stack Initialize */
    
/* ========================================================================= */
/* Branch to C library entry point                                           */
/* ========================================================================= */
    LDR  r12,=resetprg
    BX   r12                              /* Branch to C library entry point */

/* ========================================================================= */
/* Other Handler                                                             */
/* ========================================================================= */
.global abort
abort:
undefined_handler:
svc_handler:
abort_handler:
prefetch_handler:
reserved_handler:
    CPSID   i                   /* Disable interrupt handling to preserve stack */
    MOV     r0, LR              /* Store SYS LR value as first function parameter */
    MOV     r1, SP              /* Store SYS SP value as second function parameter */
    MOV     r2, #0              /* USR LR - 0 since we don't currently use user mode */
    MOV     r3, #0              /* USR SP - 0 since we don't use user mode */
    LDR  r12,=handle_cpu_fault  /* Load address of handle_cpu_fault */
    BX   r12                    /* Jump to handle_cpu_fault without link */
.end
