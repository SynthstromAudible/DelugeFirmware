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
* File Name     : irqfiq_handler.s
* Device(s)     : RZ/A1H (R7S721001)
* Tool-Chain    : GNUARM-NONEv14.02-EABI
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : Sample Program - IRQ, FIQ handler
*******************************************************************************/
/*******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 21.10.2014 1.00
/*******************************************************************************/

/* Standard definitions of mode bits and interrupt (I & F) flags in PSRs */
    .equ    SYS_MODE          , 0x1F
    .equ    INTC_ICCIAR_ADDR  , 0xE820200C
    .equ    INTC_ICCEOIR_ADDR , 0xE8202010


/* ================================================================== */
/* Entry point for the FIQ handler */
/* ================================================================== */
    .global  irq_handler
    .global  fiq_handler

fiq_handler:
    SUB        lr, lr, #4
    SRSDB    sp!, #SYS_MODE
    CPS        #SYS_MODE
    PUSH    {r0-r3, r12}
    LDR     r2, =INTC_ICCIAR_ADDR
    LDR        r0, [r2]
    PUSH    {r0}
    MOV        r1, sp
    AND        r1, r1, #4
    SUB        sp, sp, r1
    PUSH    {r1, lr}
    BL    fiq_handler_interrupt
    POP        {r1, lr}
    ADD        sp, sp, r1
    POP        {r0}
    LDR     r2, =INTC_ICCEOIR_ADDR
    STR        r0, [r2]
    POP        {r0-r3, r12}

fiq_handler_end:
    RFEIA    sp!

/* ================================================================== */
/* Entry point for the IRQ handler */
/* ================================================================== */
    .func irq_handler
irq_handler:
    SUB        lr, lr, #4
    SRSDB    sp!, #SYS_MODE
    CPS        #SYS_MODE
    PUSH    {r0-r3, r12}
    LDR     r2, =INTC_ICCIAR_ADDR
    LDR        r0, [r2]
    PUSH    {r0}
    MOV        r1, sp
    AND        r1, r1, #4
    SUB        sp, sp, r1
    PUSH    {r1, lr}
    BL        INTC_Handler_Interrupt
    POP        {r1, lr}
    ADD        sp, sp, r1
    POP        {r0}
    LDR     r2, =INTC_ICCEOIR_ADDR
    STR        r0, [r2]
    POP        {r0-r3, r12}
    RFEIA    sp!

Literals3:

    .endfunc
    .end
