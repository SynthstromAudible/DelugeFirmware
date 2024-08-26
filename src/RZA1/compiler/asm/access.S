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
/*******************************************************************************
* File Name     : access.S
* Device(s)     : RZ/A1H (R7S721001)
* Tool-Chain    : GNUARM-NONEv14.02-EABI
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : This is the code provides access to the co-processors
*******************************************************************************/
/*******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 21.10.2014 1.00
*******************************************************************************/

    .text
    .code 32

        .global enable_neon_vfp_access_priv
        .global enable_neon_vfp_access_full
        .global disable_neon_vfp_access
        .global neon_vfp_on
        .global neon_vfp_off
        .global enable_mmu
        .global disable_mmu
        .global VbarInit
        .global SetLowVectors
        .global SetHighVectors

/* ========================================================================== */
/* Enable access to NEON/VFP by enabling access to Coprocessors 10 and 11.    */
/* Enables Privileged Access i.e. in both privileged and non privileged modes */
/* ========================================================================== */
    .func   enable_neon_vfp_access_priv
    .type enable_neon_vfp_access_priv, %function
enable_neon_vfp_access_priv:
    MRC  p15, 0, r0, c1, c0, 2 /* Read Coprocessor Access Register (CPACR)    */
    ORR  r0, r0, #(0x5 << 20)  /* Enable access to CP 10 & 11                 */
    BIC  r0, r0, #(3 << 30)    /* Clear ASEDIS/D32DIS if set                  */
    MCR  p15, 0, r0, c1, c0, 2 /* Write Coprocessor Access Register (CPACR)   */
    ISB
    BX        lr
    .endfunc
    .align 4
    
/* ========================================================================== */
/* Enable access to NEON/VFP by enabling access to Coprocessors 10 and 11.    */
/* Enables Full Access i.e. in both privileged and non privileged modes       */
/* ========================================================================== */
    .func   enable_neon_vfp_access_full
    .type enable_neon_vfp_access_full, %function
enable_neon_vfp_access_full:
    MRC  p15, 0, r0, c1, c0, 2 /* Read Coprocessor Access Register (CPACR)    */
    ORR  r0, r0, #(0xF << 22)  /* Enable access to CP 10 & 11                 */
    BIC  r0, r0, #(3 << 30)    /* Clear ASEDIS/D32DIS if set                  */
    MCR  p15, 0, r0, c1, c0, 2 /* Write Coprocessor Access Register (CPACR)   */
    ISB
    BX        lr
    .endfunc
    .align 4

    
/* ========================================================================== */
/* Disable access to NEON/VFP by enabling access to Coprocessors 10 and 11.   */
/* ========================================================================== */
    .func   disable_neon_vfp_access
    .type disable_neon_vfp_access, %function
disable_neon_vfp_access:
    MRC  p15, 0, r0, c1, c0, 2 /* Read Coprocessor Access Register (CPACR)    */
    BIC  r0, r0, #(0xF << 20)  /* Disable access to CP 10 & 11                */
    MCR  p15, 0, r0, c1, c0, 2 /* Write Coprocessor Access Register (CPACR)   */
    ISB
    BX        lr
    .align 4
    .endfunc
    
/* ========================================================================== */
/*  Switch on the VFP and NEON hardware                                       */
/* ========================================================================== */
    .func neon_vfp_on
    .type neon_vfp_on, %function
neon_vfp_on:
    MOV  r0, #0x40000000
    //VMSR FPEXC, r0                        /* Write FPEXC register, EN bit set */
    ISB
    BX        lr
    .endfunc
    .align 4

/* ========================================================================== */
/*  Switch off the VFP and NEON hardware                                      */
/* ========================================================================== */
    .func neon_vfp_off
    .type neon_vfp_off, %function
neon_vfp_off:
    MOV  r0, #0x00000000
    //VMSR FPEXC, r0                      /* Write FPEXC register, EN bit clear */
    ISB
    BX        lr
    .endfunc
    .align 4

/* ========================================================================== */
/* Enable MMU                                                                 */
/* Leaving the caches disabled.                                               */
/* ========================================================================== */
    .func enable_mmu
    .type enable_mmu, %function
enable_mmu:
    MRC  p15, 0, r0, c1, c0, 0     /* Read CP15 System Control register (SCTLR) */
    BIC  r0, r0, #(0x1 << 12)             /* Clear I bit 12 to disable I Cache */
    BIC  r0, r0, #(0x1 <<  2)             /* Clear C bit  2 to disable D Cache */
    BIC  r0, r0, #(0x1 <<  1)          /* Clear C bit  1 to disable btac Cache */
    BIC  r0, r0, #0x2       /* Clear A bit 1 to disable alignment fault check */
    ORR  r0, r0, #0x1      /* Set M bit 1 to enable MMU before scatter loading */
    MCR  p15, 0, r0, c1, c0, 0            /* Write CP15 System Control register */
    ISB
    BX        lr
    .endfunc
    .align 4

/* ========================================================================== */
/* Disable MMU                                                                */
/* Leaving the caches disabled.                                               */
/* ========================================================================== */
        .func disable_mmu
        .type disable_mmu, %function

disable_mmu:
        MRC  p15, 0, r0, c1, c0, 0       /* Read CP15 System Control register */
        BIC  r0, r0, #(0x1 << 12)        /* Clear I bit 12 to disable I Cache */
        BIC  r0, r0, #(0x1 <<  2)        /* Clear C bit  2 to disable D Cache */
        BIC  r0, r0, #0x2   /* Clear A bit 1 to disable alignment fault check */
        BIC  r0, r0, #0x1 /* Set M bit 0 to enable MMU before scatter loading */
        MCR  p15, 0, r0, c1, c0, 0      /* Write CP15 System Control register */
        BX   lr
        .endfunc
        .align 4

/* ========================================================================== */
/* VbarInit                                                                   */
/* ========================================================================== */
	.func VbarInit
    .type VbarInit, %function
VbarInit:
/* ========================================================================== */
/* Set Vector Base Address Register to point to application's vector table    */
/* ========================================================================== */
	LDR r0, = start
	MCR p15, 0, r0, c12, c0, 0

	BX		lr
	.endfunc

/* ========================================================================== */
/* Set SCTLR Low Vectors                                                      */
/*      0 = Normal (Low address) Vectors                                      */
/*      1 = High (High address) Vectors                                       */
/* ========================================================================== */
    .func SetLowVectors
    .type SetLowVectors, %function
SetLowVectors:
    MRC  p15, 0, r0, c1, c0, 0   /* Read CP15 System Control register (SCTLR) */
    BIC  r0, r0, #(0x1 << 13)           /* Clear V bit 13 to set Low Vectors  */
    MCR  p15, 0, r0, c1, c0, 0          /* Write CP15 System Control register */
    ISB
    BX        lr
    .endfunc
    .align 4

/* ========================================================================== */
/* Set SCTLR High Vectors                                                     */
/*      0 = Normal (Low address) Vectors                                      */
/*      1 = High (High address) Vectors                                       */
/* ========================================================================== */
    .func SetHighVectors
    .type SetHighVectors, %function
SetHighVectors:
    MRC  p15, 0, r0, c1, c0, 0   /* Read CP15 System Control register (SCTLR) */
    ORR  r0, r0, #(0x1 << 13)           /* Set V bit 13 to set High Vectors   */
    MCR  p15, 0, r0, c1, c0, 0          /* Write CP15 System Control register */
    ISB
    BX        lr
    .endfunc
	.align 4
	.end



