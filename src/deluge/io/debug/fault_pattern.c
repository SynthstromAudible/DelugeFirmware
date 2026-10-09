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

// The pad-grid crash pattern. Portable: every board-specific act — getting a byte to
// the pad driver, and knowing which addresses are code or stack — goes through
// <libdeluge/fault.h>, so a crash with no probe attached still shows on the panel.
// Sharing the renderer keeps the encoding provably identical between boards, which is
// the whole point — the pattern is only useful if a photo of it decodes the same way
// whoever built the firmware.
#include "foundation/panic.h"
#include "libdeluge/fault.h"
#include <stddef.h>
#include <version.h> // kCommitShort — stamped into the crash dump

// io/debug/log.h is C++ (it pulls in <cstddef>) and this translation unit is C, so
// declare the logger's C-linkage entry point directly rather than including it. Same
// signature as log.h's; kept in step by the shared symbol.
enum DebugPrintMode { kDebugPrintModeDefault, kDebugPrintModeRaw, kDebugPrintModeNewlined };
extern void logDebug(enum DebugPrintMode mode, const char* file, int line, size_t bufsize, const char* format, ...);
#if ENABLE_TEXT_OUTPUT
#define D_PRINTLN(...) logDebug(kDebugPrintModeNewlined, __FILE__, __LINE__, 256, __VA_ARGS__)
#else
#define D_PRINTLN(...)
#endif

/// Board bounds, fetched once per report (a fault vector is not a place to re-ask).
static DelugeFaultRanges s_ranges;

[[gnu::always_inline]] static inline void sendToPIC(uint8_t msg) {
	deluge_fault_pad_write(msg);
}

[[gnu::always_inline]] static inline void sendColor(uint8_t r, uint8_t g, uint8_t b) {
	sendToPIC(r);
	sendToPIC(g);
	sendToPIC(b);
}

[[gnu::always_inline]] static inline void drawByte(uint8_t byte, uint8_t r, uint8_t g, uint8_t b) {
	for (int32_t idxBit = 7; idxBit >= 0; --idxBit) {
		if (((byte >> idxBit) & 0x01) == 0x01) {
			sendColor(r, g, b);
		}
		else {
			sendColor(0, 0, 0);
		}
	}
}

// Requires 32 pads so two double cloumns
[[gnu::always_inline]] static inline int32_t drawPointer(uint32_t idxColumnPairStart, uint32_t pointerValue, uint8_t r,
                                                         uint8_t g, uint8_t b) {
	sendToPIC(1 + idxColumnPairStart);
	++idxColumnPairStart;

	drawByte(pointerValue >> 24, r, g, b);
	drawByte(pointerValue >> 16, r, g, b);

	sendToPIC(1 + idxColumnPairStart);
	++idxColumnPairStart;

	drawByte(pointerValue >> 8, r, g, b);
	drawByte(pointerValue, r, g, b);

#if ENABLE_TEXT_OUTPUT
	D_PRINTLN("fault PTR: 0x%08x (%d, %d, %d)", pointerValue, r, g, b);
#endif

	return idxColumnPairStart;
}

// Whether `value` points into any stack the board declared. Both are checked because a fault can be
// raised on a stack other than the program one (the Rust BSP's retired storage worker fiber had its own,
// in a different region): the walk below is gated on this, so missing that stack reduced the report to a
// single address with no call chain — see DelugeFaultRanges::alt_stack_start.
[[gnu::always_inline]] static inline bool isStackPointer(uint32_t value) {
	return (s_ranges.stack_start != 0 && value >= s_ranges.stack_start && value < s_ranges.stack_end)
	       || (s_ranges.alt_stack_start != 0 && value >= s_ranges.alt_stack_start && value < s_ranges.alt_stack_end);
}

// The end of whichever declared stack `value` sits in — the limit for walking upward from it.
// Walking to the wrong stack's end would either stop immediately or run off into unrelated memory.
// `static` because a non-static C `inline` has external linkage, which makes referencing the static
// `s_ranges` ill-formed (C11 6.7.4p3) and warns. The neighbours here predate this and still warn.
[[gnu::always_inline]] static inline uint32_t stackEndFor(uint32_t value) {
	if (s_ranges.stack_start != 0 && value >= s_ranges.stack_start && value < s_ranges.stack_end) {
		return s_ranges.stack_end;
	}
	return s_ranges.alt_stack_end;
}

[[gnu::always_inline]] static inline bool isCodePointer(uint32_t value) {
	return s_ranges.code_start != 0 && value >= s_ranges.code_start && value < s_ranges.code_end;
}

[[gnu::always_inline]] static inline uint8_t getHexCharValue(char input) {
	uint8_t result = 0;
	if (input >= '0' && input <= '9') {
		result = input - '0';
	}
	if (input >= 'a' && input <= 'f') {
		result = input - 'a' + 10;
	}
	return result;
}

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MAX_POINTER_COUNT 4
[[gnu::always_inline]] static inline void printPointers(uint32_t addrSYSLR, uint32_t addrSYSSP, uint32_t addrUSRLR,
                                                        uint32_t addrUSRSP, bool hardFault) {
	// Search for stack pointers
	uint32_t stackPointer = 0;
	if (isStackPointer(addrUSRSP)) {
		stackPointer = addrUSRSP;
	}
	else if (isStackPointer(addrSYSSP)) {
		stackPointer = addrSYSSP;
	}

	int8_t stackPointerCount = 0;
	uint32_t stackPointers[MAX_POINTER_COUNT] = {0};

	// Search for stack pointers before any printing
	if (stackPointer != 0x00000000) {
		// Walk to the end of the stack this SP actually belongs to, not always the program stack's.
		const uint32_t stackEnd = stackEndFor(stackPointer);
		stackPointer = stackPointer - (stackPointer % 4); // Align to 4 bytes
		while (stackPointer < stackEnd) {
			uint32_t stackValue = *((uint32_t*)stackPointer);

			// Print any pointer that is pointing to code, different from the LRs and not the same as before
			if (isCodePointer(stackValue) && stackValue != stackPointers[MAX(0, stackPointerCount - 1)]
			    && stackValue != addrUSRLR && stackValue != addrSYSLR) {
				stackPointers[stackPointerCount] = stackValue;
				++stackPointerCount;

				if (stackPointerCount >= MAX_POINTER_COUNT) {
					break;
				}
				stackPointer += 12; // seems like a reasonable min size between stored lr values, avoids garbage
			}
			else {
				stackPointer += 4;
			}
		}
	}

	uint32_t currentColumnPairIndex = 0;

	// Every address drawn below is a return address: subtract 2 so it lands in the previous instruction, the call
	// itself. 4 overshoots in Thumb mode.

	// Print LR from USR mode if it is valid
	if (isCodePointer(addrUSRLR)) {
		currentColumnPairIndex = drawPointer(currentColumnPairIndex, addrUSRLR - 2, 255, 0, 255);
	}

	// Print LR from SYS mode if it is valid and different from USR mode
	if (isCodePointer(addrSYSLR) && addrSYSLR != addrUSRLR) {
		currentColumnPairIndex = drawPointer(currentColumnPairIndex, addrSYSLR - 2, 0, 0, 255);
	}

	// Print all pointers
	if (stackPointerCount > 0) {
		uint8_t currentPointerIndex = 0;
		uint8_t currentBlueValue = 0;
		while (currentPointerIndex < MAX_POINTER_COUNT) {
			currentColumnPairIndex =
			    drawPointer(currentColumnPairIndex, stackPointers[currentPointerIndex] - 2, 0, 255, currentBlueValue);

			// Stop after filling all columns
			if (currentColumnPairIndex >= 8) {
				break;
			}

			// Alternate colors
			if (currentBlueValue == 0) {
				currentBlueValue = 255;
			}
			else if (currentBlueValue == 255) {
				currentBlueValue = 0;
			}

			++currentPointerIndex;
		}
	}

	// Clear all other pads
	for (; currentColumnPairIndex < 8; ++currentColumnPairIndex) {
		sendToPIC(1 + currentColumnPairIndex);

		for (uint32_t idxColumnPairBuffer = 0; idxColumnPairBuffer < 16; ++idxColumnPairBuffer) {
			sendColor(0, 0, 0);
		}
	}

	// Print first 4 byte of commit ID
	sendToPIC(1 + currentColumnPairIndex);
	char const* commitShort = kCommitShort;

	uint8_t firstByte = (getHexCharValue(commitShort[0]) << 4) | getHexCharValue(commitShort[1]);
	drawByte(firstByte, 255, (hardFault ? 0 : 255), 0);
	uint8_t secondByte = (getHexCharValue(commitShort[2]) << 4) | getHexCharValue(commitShort[3]);
	drawByte(secondByte, 255, (hardFault ? 0 : 255), 0);

#if ENABLE_TEXT_OUTPUT
	D_PRINTLN("fault COMMIT: %s", kCommitShort);
#endif

	// Hand the transport to the board: push everything out and block until the wire is
	// idle. Bounded there, so a wedged transmitter cannot swallow the crash report.
	deluge_fault_pad_flush();
}

extern void fault_handler_print_freeze_pointers(uint32_t addrSYSLR, uint32_t addrSYSSP, uint32_t addrUSRLR,
                                                uint32_t addrUSRSP) {
	deluge_fault_ranges(&s_ranges);
	printPointers(addrSYSLR, addrSYSSP, addrUSRLR, addrUSRSP, false);
}

extern void handle_cpu_fault(uint32_t addrSYSLR, uint32_t addrSYSSP, uint32_t addrUSRLR, uint32_t addrUSRSP) {
	// Re-entry guard. Drawing the pattern touches the pad transport, so a fault raised
	// from inside that path (or a second fault while reporting the first) would recurse
	// straight back to here and never draw anything at all. First one in wins; anyone
	// after it just parks.
	static bool reporting = false;
	if (!reporting) {
		reporting = true;
		deluge_fault_ranges(&s_ranges);
		printPointers(addrSYSLR, addrSYSSP, addrUSRLR, addrUSRSP, true);
	}
	// if we start using user mode then we'd want to do this to get an accurate call stack. We don't so just don't
	//__asm__("CPS  0x10"); // Go to USR mode

	while (1) {
		__asm__("nop");
	}
}
