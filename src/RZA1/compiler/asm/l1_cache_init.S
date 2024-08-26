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
* File Name     : l1_cache_init.S
* Device(s)     : RZ/A1H (R7S721001)
* Tool-Chain    : GNUARM-NONEv14.02-EABI
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : This code provides basic global enable for Cortex-A9 cache.
*               : It also enables branch prediction.
*               : This code must be run from a privileged mode.
*******************************************************************************/
/*******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 21.10.2014 1.00
*******************************************************************************/

	.text
	.code 32

	.global R_CACHE_L1Init
	.func R_CACHE_L1Init
	.type R_CACHE_L1Init, %function
R_CACHE_L1Init:
	
/******************************************************************************/
/* Enable caches                                                              */
/* Caches are controlled by the System Control Register:                      */
/******************************************************************************/
	MRC  p15, 0, r0, c1, c0, 0			              /* Read CP15 register 1 */
	ORR  r0, r0, #(0x1 << 12)			                    /* Enable I Cache */
	ORR  r0, r0, #(0x1 << 2)			                    /* Enable D Cache */
	ORR  r0, r0, #(0x1 << 11)                            /* branch prediction */
	MCR  p15, 0, r0, c1, c0, 0			             /* Write CP15 register 1 */


/******************************************************************************/
/* Enable Program Flow Prediction                                             */
/*                                                                            */
/* Branch prediction is controlled by the System Control Register:            */
/* Set Bit 11 to enable branch prediction and return                          */
/******************************************************************************/
	MRC  p15, 0, r0, c1, c0, 0		  	      /* Read System Control Register */
	ORR  r0, r0, #(0x1 << 11)
	MCR  p15, 0, r0, c1, c0, 0			     /* Write System Control Register */

/******************************************************************************/
/* Enable D-side prefetch                                                     */
/******************************************************************************/
	MRC  p15, 0, r0, c1, c0, 1			   /* Read Auxiliary Control Register */
	ORR  r0, r0, #(0x1 << 2)			     /* Enable Dside prefetch */
	MCR  p15, 0, r0, c1, c0, 1			/* Write Auxiliary Control Register */

	BX   lr

	.endfunc
	.align 4
	.end
