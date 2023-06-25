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
 * File Name     : resetprg.c
 * Device(s)     : RZ/A1H (R7S721001)
 * Tool-Chain    : GNUARM-NONEv14.02-EABI
 * H/W Platform  : RSK+RZA1H CPU Board
 * Description   : Sample Program - C library entry point
 *               : Variants of this file must be created for each compiler
 *******************************************************************************/
/*******************************************************************************
 * History       : DD.MM.YYYY Version Description
 *               : 21.10.2014 1.00
 *******************************************************************************/

/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "r_typedefs.h"

#include "definitions.h"
#include "gpio.h"
#include "sio_char.h"
#include "stb.h"
#include "asm.h"
#include "Deluge.h"

#if defined(__thumb2__) || (defined(__thumb__) && defined(__ARM_ARCH_6M__))
#define THUMB_V7_V6M
#endif

/* Defined if this target supports the BLX Rm instruction.  */
#if !defined(__ARM_ARCH_2__) && !defined(__ARM_ARCH_3__) && !defined(__ARM_ARCH_3M__) && !defined(__ARM_ARCH_4__)      \
    && !defined(__ARM_ARCH_4T__)
#define HAVE_CALL_INDIRECT
#endif

// These are for newlib
void* __dso_handle = 0;

extern void __libc_init_array(void);

void _init(void) {
	/* Runs after preinit but before init*/
}

void _fini(void) {
	/* Runs after of fini array */
}

extern int R_CACHE_L1Init(void);

/*******************************************************************************
 * Function Name: resetprg
 * Description  :
 * Arguments    : none
 * Return Value : none
 *******************************************************************************/
void resetprg(void) {

	// Enable all modules' clocks --------------------------------------------------------------
	STB_Init();
	// SDRAM pin mux ------------------------------------------------------------------
	setPinMux(3, 0, 1);  // A1
	setPinMux(3, 1, 1);  // A1
	setPinMux(3, 2, 1);  // A1
	setPinMux(3, 3, 1);  // A1
	setPinMux(3, 4, 1);  // A1
	setPinMux(3, 5, 1);  // A1
	setPinMux(3, 6, 1);  // A1
	setPinMux(3, 7, 1);  // A1
	setPinMux(3, 8, 1);  // A1
	setPinMux(3, 9, 1);  // A1
	setPinMux(3, 10, 1); // A1
	setPinMux(3, 11, 1); // A1
	setPinMux(3, 12, 1); // A1
	setPinMux(3, 13, 1); // A1
	setPinMux(3, 14, 1); // A1

	// D pins
	setPinMux(5, 0, 1);
	setPinMux(5, 1, 1);
	setPinMux(5, 2, 1);
	setPinMux(5, 3, 1);
	setPinMux(5, 4, 1);
	setPinMux(5, 5, 1);
	setPinMux(5, 6, 1);
	setPinMux(5, 7, 1);
	setPinMux(5, 8, 1);
	setPinMux(5, 9, 1);
	setPinMux(5, 10, 1);
	setPinMux(5, 11, 1);
	setPinMux(5, 12, 1);
	setPinMux(5, 13, 1);
	setPinMux(5, 14, 1);
	setPinMux(5, 15, 1);

	//setPinMux(7, 8, 1); // CS2
	setPinMux(2, 0, 1); // CS3
	setPinMux(2, 1, 1); // RAS
	setPinMux(2, 2, 1); // CAS
	setPinMux(2, 3, 1); // CKE
	setPinMux(2, 4, 1); // WE0
	setPinMux(2, 5, 1); // WE1
	setPinMux(2, 6, 1); // RD/!WR

	R_INTC_Init(); // Set up interrupt controller

	R_CACHE_L1Init(); // Makes everything go about 1000x faster

	__enable_irq();
	__enable_fiq();
	__libc_init_array();

	main();

	/* Stops program from running off */
	while (1) {
		__asm__("nop");
	}
}
