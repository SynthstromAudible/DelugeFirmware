/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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
* File Name     : GNU_e2studio-IRAM.ld
* Device(s)     : RZ/A1H (R7S721001)
* Tool-Chain    : GNUARM-NONEv14.02-EABI
* H/W Platform  : RSK+RZA1H CPU Board
* Description   : Linker file for projects that require to run from 
*               : Internal device RAM 
******************************************************************************/
/******************************************************************************
* History       : DD.MM.YYYY Version Description
*               : 21.10.2014 1.00
******************************************************************************/
OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)

/* Corresponds with code_start in start.S and referenced in bootloader with DEF_USER_CODE_START */
ENTRY(start)

/* Base Address RAM Memory Table 10-Mbyte on-chip RAM */  
/* Total RAM available 10112K */
MEMORY 
{
	/* Internal RAM address range H'2000_0000 to H'2001_FFFF is configured as data retention RAM */
	/* Write access to this address range has to be enabled by writing to registers SYSCR1 and SYSCR2 */
	SDRAM (rwx)   : ORIGIN = 0x0C000000, LENGTH = 0x04000000  /* SDRAM 32MB attached to CS3 H'0C000000 to H'0FFFFFFF */
	/* RAM0L (rwx)   : ORIGIN = 0x20020000, LENGTH = 0x000E0000 * Page 0 Lower bank ( 896KB) H'20020000 to H'200FFFFF */
	/* RAM1L (rwx)   : ORIGIN = 0x20100000, LENGTH = 0x00100000 * Page 1 Upper bank (1024KB) H'20100000 to H'201FFFFF */
	/* RAM2L (rwx)   : ORIGIN = 0x20200000, LENGTH = 0x00100000 * Page 2 Upper bank (1024KB) H'20200000 to H'202FFFFF */
	/* RAM3L (rwx)   : ORIGIN = 0x20300000, LENGTH = 0x00100000 * Page 3 Upper bank (1024KB) H'20300000 to H'203FFFFF */
	/* RAM4L (rwx)   : ORIGIN = 0x20400000, LENGTH = 0x00100000 * Page 4 Upper bank (1024KB) H'20400000 to H'204FFFFF */
	/* RAM0U (rwx)   : ORIGIN = 0x20500000, LENGTH = 0x00100000 * Page 0 Upper bank (1024KB) H'20500000 to H'205FFFFF */
	/* RAM1U (rwx)   : ORIGIN = 0x20600000, LENGTH = 0x00100000 * Page 1 Upper bank (1024KB) H'20600000 to H'206FFFFF */
	/* RAM2U (rwx)   : ORIGIN = 0x20700000, LENGTH = 0x00100000 * Page 2 Upper bank (1024KB) H'20700000 to H'207FFFFF */
	/* RAM3U (rwx)   : ORIGIN = 0x20800000, LENGTH = 0x00100000 * Page 3 Upper bank (1024KB) H'20800000 to H'208FFFFF */
	/* RAM4U (rwx)   : ORIGIN = 0x20900000, LENGTH = 0x00100000 * Page 4 Upper bank (1024KB) H'20900000 to H'209FFFFF */

	RAM012L   (rwx) : ORIGIN = 0x20000000, LENGTH =  0x00300000
	/* RAM01234L (rwx) : ORIGIN = 0x20020000, LENGTH = 0x007E0000 */
	/* RAM012U   (rwx) : ORIGIN = 0x20500000, LENGTH = 0x00300000 */

	ROM   (rw)    : ORIGIN = 0x18000000, LENGTH = 0x02000000  /* SPI ROM attached to H'18000000 to H'19FFFFFF */
	/* VRAM  (rw)    : ORIGIN = 0x60300000, LENGTH = 0x00700000   */
}

PROGRAM_STACK_SIZE    = 0x8000; 	/* Application stack                  */
IRQ_STACK_SIZE	      = 0x2000;     /* IRQ mode stack                     */
FIQ_STACK_SIZE	      = 0x2000;     /* FRQ mode stack                     */
SVC_STACK_SIZE	      = 0x2000;     /* SVC mode stack                     */
ABT_STACK_SIZE	      = 0x2000;     /* ABT mode stack                     */
TTB_SIZE              = 0x8000;     /* Level-1 Translation Table for MMU  */

INTERNAL_RAM_START = 0x20020000;
INTERNAL_RAM_END = 0x20300000;

SECTIONS
{
	.frunk_bss (NOLOAD) :
	{
        . = ALIGN(4);
        PROVIDE(__frunk_bss_start = .);
        *(.frunk_bss .frunk_bss*)
        . = ALIGN(4);
		KEEP(*(.frunk_bss))
        PROVIDE(__frunk_bss_end = .);
	} > RAM012L

	/* L1 translation table must be aligned to 16KB Boundary!           */
	/* Please refer to Cortex-A Series Version: 4.0 Programmer’s Guide, */
	/* section 9.4 First level address translation                      */
	.ttb_mmu1 INTERNAL_RAM_START : ALIGN(0x4000)
	{
	    PROVIDE(__frunk_slack_end = .);
		ttb_mmu1_base = .;
		.  += TTB_SIZE;
		.	= ALIGN(0x4);
		ttb_mmu1_end = .;
	} >RAM012L
	
	.irq_stack :
	{
		irq_stack_start = .;
		.  += IRQ_STACK_SIZE;
		.	= ALIGN(0x4);
		irq_stack_end = .;
		fiq_stack_start = .;
		.  += FIQ_STACK_SIZE;
		.	= ALIGN(0x4);
		fiq_stack_end = .;
		svc_stack_start = .;
		.  += SVC_STACK_SIZE;
		.	= ALIGN(0x4);
		svc_stack_end = .;
		abt_stack_start = .;
		.  += ABT_STACK_SIZE;
		.	= ALIGN(0x4);
		abt_stack_end = .;
	} >RAM012L
	
	.bss (NOLOAD) :
	{
		 _bss = .;
		 PROVIDE(__bss_start__ = .);
		 *(.bss)
		 *(.bss.**)
		 *(COMMON)
		 . = ALIGN(0x4);
		 PROVIDE(__bss_end__ = .);
		 _ebss = .;
	} >RAM012L
	
	/* ALIGN(0x20) is only necessary for v0 bootloaders */
  	.reset : ALIGN(0x20) /* 0x10 isn't enough. Not sure why any of this is necessary though. */
	{
        PROVIDE(program_code_start = .);

		execute = .;
		*start.S.obj (.text)
		*start.S.obj (.rodata)
		*start.S.obj (.data)
		
		*access.S.obj (.text)
		*access.S.obj (.rodata)
		*access.S.obj (.data)

		*initsct.S.obj (.text)
		*initsct.S.obj (.rodata)
		*initsct.S.obj (.data)

		*reset_handler.S.obj (.text)
		*reset_handler.S.obj (.rodata)
		*reset_handler.S.obj (.data)

		*ttb_init.S.obj (.text)
		*ttb_init.S.obj (.rodata)
		*ttb_init.S.obj (.data)
		address_end_reset = .;

    } > RAM012L

    .text : ALIGN(0x4) {
		_stext = .;
		
		*(.text)
		*(.text*)
		*(.text.startup)

		_etext = .;
        PROVIDE(program_code_end = .);

	} > RAM012L

	.rodata :
	{
		. = ALIGN(0x4);

		*(.rodata)
		INPUT_SECTION_FLAGS(!SHF_STRINGS) *(.rodata.*) /* this should be all strings?  */
		*(.glue_7) /* stubs generated by gcc to glue ARM7 code  calling Thumb code */
		*(.glue_7t) /* stubs generated by gcc to glue Thumb code calling ARM7  code */
		
		KEEP(*(.init))
		KEEP(*(.fini))
		
		. = ALIGN(0x4);
	} > RAM012L
	
	.data : 
	{
		. = ALIGN(0x8);
		address_start_data_ROM = .;
		_data = .;
		_sdata = .;
		*(.data)
		*(.data*)
		*(.igot.plt)
		*(.got.plt) 
		*(.got)
		.	= ALIGN(0x8);
		address_end_data_ROM = .;
		_edata = .;
	} > RAM012L
	
	.preinit_array :
	{
		PROVIDE_HIDDEN(__preinit_array_start = .);
		KEEP(*(.preinit_array*))
		PROVIDE_HIDDEN(__preinit_array_end = .);
	} > RAM012L

	.init_array :
	{
		PROVIDE_HIDDEN(__init_array_start = .);
		KEEP(*(SORT(.init_array*)))
		KEEP(*(.init_array*))
		PROVIDE_HIDDEN(__init_array_end = .);
	} > RAM012L

	.fini_array :
	{
		PROVIDE_HIDDEN(__fini_array_start = .);
		KEEP(*(.fini_array*))
		KEEP(*(SORT(.fini_array*)))
		PROVIDE_HIDDEN(__fini_array_end = .);
	} > RAM012L

	.tors :
	{
		__CTOR_LIST__ = .;
		. = ALIGN(2);
		__ctors = .;
		*(.ctors)
		__ctors_end = .;
		__CTOR_END__ = .;
		__DTOR_LIST__ = .;
		___dtors = .;
		*(.dtors)
		___dtors_end = .;
		__DTOR_END__ = .;
		. = ALIGN(2);
		_mdata = .;
	} > RAM012L

	.heap (NOLOAD) :
	{
		. = ALIGN(4);
		PROVIDE(__heap_start__ = .);
		PROVIDE(__heap_start = .);
		PROVIDE(_nl_heap_start = .);
		KEEP(*(.heap))
		. = ALIGN(0x4);
		PROVIDE(__heap_end__ = .);
		PROVIDE(__heap_end = .);
		PROVIDE(_nl_heap_end = .);
	} > RAM012L

	/* This worked in isolated testing, for example with init_crc_table but I had problems with bigger stuff, maybe timing? */
	.sdram_text : AT(ADDR(.heap))
	{
        . = ALIGN(4);
        PROVIDE(__sdram_text_start = .);
        *(.sdram_text .sdram_text*)
        . = ALIGN(4);
		KEEP(*(.sdram_text))
        PROVIDE(__sdram_text_end = .);
	} > SDRAM

	.sdram_data : AT(ADDR(.heap) + SIZEOF(.sdram_text))
	{
        . = ALIGN(4);
        PROVIDE(__sdram_data_start = .);
        *(.sdram_data .sdram_data*)
        . = ALIGN(4);
		KEEP(*(.sdram_data))
        PROVIDE(__sdram_data_end = .);
	} > SDRAM

	.sdram_rodata :AT(ADDR(.heap) + SIZEOF(.sdram_text) + SIZEOF(.sdram_data))
    {
        . = ALIGN(0x4);
        PROVIDE(__sdram_rodata_start = .);
        INPUT_SECTION_FLAGS(SHF_STRINGS) *(.rodata*) /* there's a bunch of different patterns for string constant sections */
        *(.sdram_rodata)
        *(.sdram_rodata*)
        PROVIDE(__sdram_rodata_end = .);
        . = ALIGN(0x4);
    } > SDRAM

    .exceptions : AT(ADDR(.heap) + SIZEOF(.sdram_text) + SIZEOF(.sdram_data) + SIZEOF(.sdram_rodata)) {
        PROVIDE(__reloc_sections_start__ = .);
        *(.ARM.extab)
        *(.ARM.extab.*)
        PROVIDE_HIDDEN(__exidx_start = .);
         *(.ARM.exidx*)
         *(.gnu.linkonce.armexidx.*)
        PROVIDE_HIDDEN(__exidx_end = .);
        PROVIDE(__reloc_sections_end__ = .);
    } > SDRAM

	/* Reset location counter to RAM012L. Warning: Changing relocation sections before that will require change here */
	. = ADDR(.heap) + SIZEOF(.sdram_text) + SIZEOF(.sdram_data) + SIZEOF(.sdram_rodata) + SIZEOF(.exceptions);
	. = ALIGN(4);
    . = ALIGN(0xFFFF);
	/* Definition of code_end declared in start.S and referenced in bootloader with DEF_USER_CODE_END */
	/* end is also used by glibc as start of heap and might be reinitialized */
	/* For more information see: rdimon's libgloss/arm/syscall.c @ _sbrk */
	/* See also: https://ww1.microchip.com/downloads/en/DeviceDoc/Frequently-Asked-Questions-4.9.3.26.txt point 12 */	
	end = .;
	_end = .;

	/* EVERYTHING PLACED AFTER END SECTION WILL NOT BE COPIED TO RAM!!!!! */
	
    .sdram_bss (NOLOAD) :
    {
        . = ALIGN(4);
        PROVIDE(__sdram_bss_start = .);
        *(.sdram_bss .sdram_bss*)
        . = ALIGN(4);
		KEEP(*(.sdram_bss))
        PROVIDE(__sdram_bss_end = .);
    } > SDRAM

	.program_stack (INTERNAL_RAM_END - PROGRAM_STACK_SIZE) (NOLOAD) :
	{
		program_stack_start = .;
		.  += PROGRAM_STACK_SIZE;
		.	= ALIGN(0x4);
		program_stack_end = .;
	} >RAM012L

/* Remove information from the compiler libraries 
   mainly a sanity check since linker gc should
   do this too*/
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
    libstdc++.a ( * )
  }

}


/*
	// In order to attempt to move init functions in the SDRAM you need to move sdram_text before the .text sections
	// so _Z41__static_initialization_and_destruction_0ii is not yet consumed by the wildcard, that works with the following block
	// Problem was that seemingly the functions were copied to SDRAM but not executed as part of the init routine
	.sdram_text : AT(__heap_start)
	{
        . = ALIGN(4);
        PROVIDE(__sdram_text_start = .);
		*(.text._Z41__static_initialization_and_destruction_0ii)
        *(.sdram_text .sdram_text*)
        . = ALIGN(4);
        PROVIDE(__sdram_text_end = .);
	} > SDRAM

*/

