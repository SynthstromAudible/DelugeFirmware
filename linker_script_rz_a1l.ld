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

	/* Select UI methods before the internal .text wildcard, including LTO clones.
	 * Match symbols rather than object filenames: LTO combines translation units.
	 * The boot image stores these bytes at .heap; resetprg copies them to SDRAM
	 * and synchronizes caches before running any global constructors.
	 */
	.sdram_text : AT(__sdram_text_load)
	{
		. = ALIGN(4);
		PROVIDE(__sdram_text_start = .);
		*(.sdram_text .sdram_text*)
		/* Namespaced UI, keyboard, menu-item and context-menu methods. */
		*(.text._ZN6deluge3gui* .text._ZNK6deluge3gui*)
		*(.text.*._ZN6deluge3gui* .text.*._ZNK6deluge3gui*)
		*(.text._ZZN6deluge3gui* .text._ZZNK6deluge3gui*)
		*(.text.*._ZZN6deluge3gui* .text.*._ZZNK6deluge3gui*)
		/* Legacy UI classes in the global namespace. */
		*(.text._ZN12ArrangerView* .text._ZNK12ArrangerView*)
		*(.text.*._ZN12ArrangerView* .text.*._ZNK12ArrangerView*)
		*(.text._ZZN12ArrangerView* .text._ZZNK12ArrangerView*)
		*(.text.*._ZZN12ArrangerView* .text.*._ZZNK12ArrangerView*)
		*(.text._ZN13AudioClipView* .text._ZNK13AudioClipView*)
		*(.text.*._ZN13AudioClipView* .text.*._ZNK13AudioClipView*)
		*(.text._ZZN13AudioClipView* .text._ZZNK13AudioClipView*)
		*(.text.*._ZZN13AudioClipView* .text.*._ZZNK13AudioClipView*)
		*(.text._ZN13AudioRecorder* .text._ZNK13AudioRecorder*)
		*(.text.*._ZN13AudioRecorder* .text.*._ZNK13AudioRecorder*)
		*(.text._ZZN13AudioRecorder* .text._ZZNK13AudioRecorder*)
		*(.text.*._ZZN13AudioRecorder* .text.*._ZZNK13AudioRecorder*)
		*(.text._ZN14AutomationView* .text._ZNK14AutomationView*)
		*(.text.*._ZN14AutomationView* .text.*._ZNK14AutomationView*)
		*(.text._ZZN14AutomationView* .text._ZZNK14AutomationView*)
		*(.text.*._ZZN14AutomationView* .text.*._ZZNK14AutomationView*)
		*(.text._ZN7Browser* .text._ZNK7Browser*)
		*(.text.*._ZN7Browser* .text.*._ZNK7Browser*)
		*(.text._ZZN7Browser* .text._ZZNK7Browser*)
		*(.text.*._ZZN7Browser* .text.*._ZZNK7Browser*)
		*(.text._ZN10ClipMinder* .text._ZNK10ClipMinder*)
		*(.text.*._ZN10ClipMinder* .text.*._ZNK10ClipMinder*)
		*(.text._ZZN10ClipMinder* .text._ZZNK10ClipMinder*)
		*(.text.*._ZZN10ClipMinder* .text.*._ZZNK10ClipMinder*)
		*(.text._ZN26ClipNavigationTimelineView* .text._ZNK26ClipNavigationTimelineView*)
		*(.text.*._ZN26ClipNavigationTimelineView* .text.*._ZNK26ClipNavigationTimelineView*)
		*(.text._ZZN26ClipNavigationTimelineView* .text._ZZNK26ClipNavigationTimelineView*)
		*(.text.*._ZZN26ClipNavigationTimelineView* .text.*._ZZNK26ClipNavigationTimelineView*)
		*(.text._ZN8ClipView* .text._ZNK8ClipView*)
		*(.text.*._ZN8ClipView* .text.*._ZNK8ClipView*)
		*(.text._ZZN8ClipView* .text._ZZNK8ClipView*)
		*(.text.*._ZZN8ClipView* .text.*._ZZNK8ClipView*)
		*(.text._ZN12DxSyxBrowser* .text._ZNK12DxSyxBrowser*)
		*(.text.*._ZN12DxSyxBrowser* .text.*._ZNK12DxSyxBrowser*)
		*(.text._ZZN12DxSyxBrowser* .text._ZZNK12DxSyxBrowser*)
		*(.text.*._ZZN12DxSyxBrowser* .text.*._ZZNK12DxSyxBrowser*)
		*(.text._ZN20InstrumentClipMinder* .text._ZNK20InstrumentClipMinder*)
		*(.text.*._ZN20InstrumentClipMinder* .text.*._ZNK20InstrumentClipMinder*)
		*(.text._ZZN20InstrumentClipMinder* .text._ZZNK20InstrumentClipMinder*)
		*(.text.*._ZZN20InstrumentClipMinder* .text.*._ZZNK20InstrumentClipMinder*)
		*(.text._ZN18InstrumentClipView* .text._ZNK18InstrumentClipView*)
		*(.text.*._ZN18InstrumentClipView* .text.*._ZNK18InstrumentClipView*)
		*(.text._ZZN18InstrumentClipView* .text._ZZNK18InstrumentClipView*)
		*(.text.*._ZZN18InstrumentClipView* .text.*._ZZNK18InstrumentClipView*)
		*(.text._ZN22LoadInstrumentPresetUI* .text._ZNK22LoadInstrumentPresetUI*)
		*(.text.*._ZN22LoadInstrumentPresetUI* .text.*._ZNK22LoadInstrumentPresetUI*)
		*(.text._ZZN22LoadInstrumentPresetUI* .text._ZZNK22LoadInstrumentPresetUI*)
		*(.text.*._ZZN22LoadInstrumentPresetUI* .text.*._ZZNK22LoadInstrumentPresetUI*)
		*(.text._ZN26LoadMidiDeviceDefinitionUI* .text._ZNK26LoadMidiDeviceDefinitionUI*)
		*(.text.*._ZN26LoadMidiDeviceDefinitionUI* .text.*._ZNK26LoadMidiDeviceDefinitionUI*)
		*(.text._ZZN26LoadMidiDeviceDefinitionUI* .text._ZZNK26LoadMidiDeviceDefinitionUI*)
		*(.text.*._ZZN26LoadMidiDeviceDefinitionUI* .text.*._ZZNK26LoadMidiDeviceDefinitionUI*)
		*(.text._ZN13LoadPatternUI* .text._ZNK13LoadPatternUI*)
		*(.text.*._ZN13LoadPatternUI* .text.*._ZNK13LoadPatternUI*)
		*(.text._ZZN13LoadPatternUI* .text._ZZNK13LoadPatternUI*)
		*(.text.*._ZZN13LoadPatternUI* .text.*._ZZNK13LoadPatternUI*)
		*(.text._ZN10LoadSongUI* .text._ZNK10LoadSongUI*)
		*(.text.*._ZN10LoadSongUI* .text.*._ZNK10LoadSongUI*)
		*(.text._ZZN10LoadSongUI* .text._ZZNK10LoadSongUI*)
		*(.text.*._ZZN10LoadSongUI* .text.*._ZZNK10LoadSongUI*)
		*(.text._ZN6LoadUI* .text._ZNK6LoadUI*)
		*(.text.*._ZN6LoadUI* .text.*._ZNK6LoadUI*)
		*(.text._ZZN6LoadUI* .text._ZZNK6LoadUI*)
		*(.text.*._ZZN6LoadUI* .text.*._ZZNK6LoadUI*)
		*(.text._ZN15PerformanceView* .text._ZNK15PerformanceView*)
		*(.text.*._ZN15PerformanceView* .text.*._ZNK15PerformanceView*)
		*(.text._ZZN15PerformanceView* .text._ZZNK15PerformanceView*)
		*(.text.*._ZZN15PerformanceView* .text.*._ZZNK15PerformanceView*)
		*(.text._ZN8QwertyUI* .text._ZNK8QwertyUI*)
		*(.text.*._ZN8QwertyUI* .text.*._ZNK8QwertyUI*)
		*(.text._ZZN8QwertyUI* .text._ZZNK8QwertyUI*)
		*(.text.*._ZZN8QwertyUI* .text.*._ZZNK8QwertyUI*)
		*(.text._ZN12RenameClipUI* .text._ZNK12RenameClipUI*)
		*(.text.*._ZN12RenameClipUI* .text.*._ZNK12RenameClipUI*)
		*(.text._ZZN12RenameClipUI* .text._ZZNK12RenameClipUI*)
		*(.text.*._ZZN12RenameClipUI* .text.*._ZZNK12RenameClipUI*)
		*(.text._ZN12RenameDrumUI* .text._ZNK12RenameDrumUI*)
		*(.text.*._ZN12RenameDrumUI* .text.*._ZNK12RenameDrumUI*)
		*(.text._ZZN12RenameDrumUI* .text._ZZNK12RenameDrumUI*)
		*(.text.*._ZZN12RenameDrumUI* .text.*._ZZNK12RenameDrumUI*)
		*(.text._ZN14RenameMidiCCUI* .text._ZNK14RenameMidiCCUI*)
		*(.text.*._ZN14RenameMidiCCUI* .text.*._ZNK14RenameMidiCCUI*)
		*(.text._ZZN14RenameMidiCCUI* .text._ZZNK14RenameMidiCCUI*)
		*(.text.*._ZZN14RenameMidiCCUI* .text.*._ZZNK14RenameMidiCCUI*)
		*(.text._ZN14RenameOutputUI* .text._ZNK14RenameOutputUI*)
		*(.text.*._ZN14RenameOutputUI* .text.*._ZNK14RenameOutputUI*)
		*(.text._ZZN14RenameOutputUI* .text._ZZNK14RenameOutputUI*)
		*(.text.*._ZZN14RenameOutputUI* .text.*._ZZNK14RenameOutputUI*)
		*(.text._ZN8RenameUI* .text._ZNK8RenameUI*)
		*(.text.*._ZN8RenameUI* .text.*._ZNK8RenameUI*)
		*(.text._ZZN8RenameUI* .text._ZZNK8RenameUI*)
		*(.text.*._ZZN8RenameUI* .text.*._ZZNK8RenameUI*)
		*(.text._ZN6RootUI* .text._ZNK6RootUI*)
		*(.text.*._ZN6RootUI* .text.*._ZNK6RootUI*)
		*(.text._ZZN6RootUI* .text._ZZNK6RootUI*)
		*(.text.*._ZZN6RootUI* .text.*._ZZNK6RootUI*)
		*(.text._ZN13SampleBrowser* .text._ZNK13SampleBrowser*)
		*(.text.*._ZN13SampleBrowser* .text.*._ZNK13SampleBrowser*)
		*(.text._ZZN13SampleBrowser* .text._ZZNK13SampleBrowser*)
		*(.text.*._ZZN13SampleBrowser* .text.*._ZZNK13SampleBrowser*)
		*(.text._ZN18SampleMarkerEditor* .text._ZNK18SampleMarkerEditor*)
		*(.text.*._ZN18SampleMarkerEditor* .text.*._ZNK18SampleMarkerEditor*)
		*(.text._ZZN18SampleMarkerEditor* .text._ZZNK18SampleMarkerEditor*)
		*(.text.*._ZZN18SampleMarkerEditor* .text.*._ZZNK18SampleMarkerEditor*)
		*(.text._ZN22SaveInstrumentPresetUI* .text._ZNK22SaveInstrumentPresetUI*)
		*(.text.*._ZN22SaveInstrumentPresetUI* .text.*._ZNK22SaveInstrumentPresetUI*)
		*(.text._ZZN22SaveInstrumentPresetUI* .text._ZZNK22SaveInstrumentPresetUI*)
		*(.text.*._ZZN22SaveInstrumentPresetUI* .text.*._ZZNK22SaveInstrumentPresetUI*)
		*(.text._ZN12SaveKitRowUI* .text._ZNK12SaveKitRowUI*)
		*(.text.*._ZN12SaveKitRowUI* .text.*._ZNK12SaveKitRowUI*)
		*(.text._ZZN12SaveKitRowUI* .text._ZZNK12SaveKitRowUI*)
		*(.text.*._ZZN12SaveKitRowUI* .text.*._ZZNK12SaveKitRowUI*)
		*(.text._ZN26SaveMidiDeviceDefinitionUI* .text._ZNK26SaveMidiDeviceDefinitionUI*)
		*(.text.*._ZN26SaveMidiDeviceDefinitionUI* .text.*._ZNK26SaveMidiDeviceDefinitionUI*)
		*(.text._ZZN26SaveMidiDeviceDefinitionUI* .text._ZZNK26SaveMidiDeviceDefinitionUI*)
		*(.text.*._ZZN26SaveMidiDeviceDefinitionUI* .text.*._ZZNK26SaveMidiDeviceDefinitionUI*)
		*(.text._ZN13SavePatternUI* .text._ZNK13SavePatternUI*)
		*(.text.*._ZN13SavePatternUI* .text.*._ZNK13SavePatternUI*)
		*(.text._ZZN13SavePatternUI* .text._ZZNK13SavePatternUI*)
		*(.text.*._ZZN13SavePatternUI* .text.*._ZZNK13SavePatternUI*)
		*(.text._ZN10SaveSongUI* .text._ZNK10SaveSongUI*)
		*(.text.*._ZN10SaveSongUI* .text.*._ZNK10SaveSongUI*)
		*(.text._ZZN10SaveSongUI* .text._ZZNK10SaveSongUI*)
		*(.text.*._ZZN10SaveSongUI* .text.*._ZZNK10SaveSongUI*)
		*(.text._ZN6SaveUI* .text._ZNK6SaveUI*)
		*(.text.*._ZN6SaveUI* .text.*._ZNK6SaveUI*)
		*(.text._ZZN6SaveUI* .text._ZZNK6SaveUI*)
		*(.text.*._ZZN6SaveUI* .text.*._ZZNK6SaveUI*)
		*(.text._ZN11SessionView* .text._ZNK11SessionView*)
		*(.text.*._ZN11SessionView* .text.*._ZNK11SessionView*)
		*(.text._ZZN11SessionView* .text._ZZNK11SessionView*)
		*(.text.*._ZZN11SessionView* .text.*._ZZNK11SessionView*)
		*(.text._ZN6Slicer* .text._ZNK6Slicer*)
		*(.text.*._ZN6Slicer* .text.*._ZNK6Slicer*)
		*(.text._ZZN6Slicer* .text._ZZNK6Slicer*)
		*(.text.*._ZZN6Slicer* .text.*._ZZNK6Slicer*)
		*(.text._ZN11SlotBrowser* .text._ZNK11SlotBrowser*)
		*(.text.*._ZN11SlotBrowser* .text.*._ZNK11SlotBrowser*)
		*(.text._ZZN11SlotBrowser* .text._ZZNK11SlotBrowser*)
		*(.text.*._ZZN11SlotBrowser* .text.*._ZZNK11SlotBrowser*)
		*(.text._ZN11SoundEditor* .text._ZNK11SoundEditor*)
		*(.text.*._ZN11SoundEditor* .text.*._ZNK11SoundEditor*)
		*(.text._ZZN11SoundEditor* .text._ZZNK11SoundEditor*)
		*(.text.*._ZZN11SoundEditor* .text.*._ZZNK11SoundEditor*)
		*(.text._ZN12TimelineView* .text._ZNK12TimelineView*)
		*(.text.*._ZN12TimelineView* .text.*._ZNK12TimelineView*)
		*(.text._ZZN12TimelineView* .text._ZZNK12TimelineView*)
		*(.text.*._ZZN12TimelineView* .text.*._ZZNK12TimelineView*)
		*(.text._ZN2UI* .text._ZNK2UI*)
		*(.text.*._ZN2UI* .text.*._ZNK2UI*)
		*(.text._ZZN2UI* .text._ZZNK2UI*)
		*(.text.*._ZZN2UI* .text.*._ZZNK2UI*)
		*(.text._ZN4View* .text._ZNK4View*)
		*(.text.*._ZN4View* .text.*._ZNK4View*)
		*(.text._ZZN4View* .text._ZZNK4View*)
		*(.text.*._ZZN4View* .text.*._ZZNK4View*)
		. = ALIGN(4);
		PROVIDE(__sdram_text_end = .);
	} > SDRAM

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

	/* Resolve the forward load address after internal sections are laid out. */
	__sdram_text_load = ADDR(.heap);

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

	__sdram_data_load = LOADADDR(.sdram_data);
	__sdram_rodata_load = LOADADDR(.sdram_rodata);
	__reloc_sections_load = LOADADDR(.exceptions);

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
