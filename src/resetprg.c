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

#include "RZA1/system/r_typedefs.h"

#include "RZA1/bsc/bsc_userdef.h" //sdram init
#include "RZA1/cache/cache.h"
#include "RZA1/compiler/asm/inc/asm.h"
#include "RZA1/gpio/gpio.h"
#include "RZA1/stb/stb.h"
#include "RZA1/uart/sio_char.h" // IWYU pragma: keep false positive, needed for R_INTC_Init
#include "definitions.h"        // IWYU pragma: keep false positive, needed for memory limits
#include "deluge/deluge.h"
#include <string.h> //memset

#if defined(__thumb2__) || (defined(__thumb__) && defined(__ARM_ARCH_6M__))
#define THUMB_V7_V6M
#endif

/* Defined if this target supports the BLX Rm instruction.  */
#if !defined(__ARM_ARCH_2__) && !defined(__ARM_ARCH_3__) && !defined(__ARM_ARCH_3M__) && !defined(__ARM_ARCH_4__)      \
    && !defined(__ARM_ARCH_4T__)
#define HAVE_CALL_INDIRECT
#endif

extern void __libc_init_array(void);

#define PLACEMENT_SDRAM_START (0x0C000000)
#define PLACEMENT_INTRAM_START (0x20020000)
#define PLACEMENT_FLASH_START (0x18080000) // Copied from bootloader, start address of firmware image in flash

extern uint32_t __reloc_sections_start__;
extern uint32_t __reloc_sections_end__;
extern uint32_t __heap_start;
extern uint32_t __frunk_bss_start;
extern uint32_t __frunk_bss_end;
extern uint32_t __sdram_bss_start;
extern uint32_t __sdram_bss_end;
extern uint32_t __sdram_text_start;
extern uint32_t __sdram_text_end;
extern uint32_t __sdram_data_start;
extern uint32_t __sdram_data_end;
extern uint32_t __sdram_rodata_start;
extern uint32_t __sdram_rodata_end;

void* __dso_handle = NULL;

void _init(void) {
	// empty
}

void _fini(void) {
	// empty
}

static void emptySection(uint32_t* start, uint32_t* end) {
	uint32_t* dst = start;
	while (dst < end) {
		*dst = 0;
		++dst;
	}
}

static void relocateSDRAMSection(uint32_t* start, uint32_t* end) {
	uint32_t* src = (uint32_t*)((uint32_t)&__heap_start + ((uint32_t)start - PLACEMENT_SDRAM_START));
	uint32_t* dst = start;
	while (dst < end) {
		*dst = *src; // Copy to SDRAM
		*src = 0;    // Clear in internal ram
		++src;
		++dst;
	}
}

/*******************************************************************************
 * Function Name: resetprg
 * Description  :
 * Arguments    : none
 * Return Value : none
 *******************************************************************************/
void resetprg(void) {
	emptySection(&__frunk_bss_start, &__frunk_bss_end);
	emptySection(&__sdram_bss_start, &__sdram_bss_end);

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

	// setPinMux(7, 8, 1); // CS2
	setPinMux(2, 0, 1); // CS3
	setPinMux(2, 1, 1); // RAS
	setPinMux(2, 2, 1); // CAS
	setPinMux(2, 3, 1); // CKE
	setPinMux(2, 4, 1); // WE0
	setPinMux(2, 5, 1); // WE1
	setPinMux(2, 6, 1); // RD/!WR

	R_INTC_Init(); // Set up interrupt controller

	// branch prediction, data cache, instruction cache
	R_CACHE_L1Init();

	// must be second or l1 will flush into it. Note this is currently instruction caching only, DMA has
	// bad interactions with data caching in L2 since it's physically after the DMA controllers
	L2CacheInit();
	__enable_irq();
	__enable_fiq();
	// Setup SDRAM. Have to do this before we init global objects
	userdef_bsc_cs2_init(0); // 64MB, hardcoded

	const uint32_t SDRAM_SIZE = EXTERNAL_MEMORY_END - EXTERNAL_MEMORY_BEGIN;
	memset((void*)EXTERNAL_MEMORY_BEGIN, 0, SDRAM_SIZE);

	relocateSDRAMSection(&__reloc_sections_start__, &__reloc_sections_end__);
	relocateSDRAMSection(&__sdram_text_start, &__sdram_text_end);
	relocateSDRAMSection(&__sdram_data_start, &__sdram_data_end);
	relocateSDRAMSection(&__sdram_rodata_start, &__sdram_rodata_end);

	__libc_init_array();
	// located in OSLikeStuff/main.c
	main();

	/* Stops program from running off */
	while (1) {
		__asm__("nop");
	}
}
