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

#include "fault_handler.h"
#include "RTT/SEGGER_RTT.h"
#include "RZA1/compiler/asm/inc/asm.h"
#include "RZA1/system/iodefines/dmac_iodefine.h"
#include "RZA1/uart/sio_char.h"
#include "definitions.h"
#include "drivers/ssi/ssi.h"
#include "drivers/uart/uart.h"
#include <version.h>

extern uint32_t program_stack_start;
extern uint32_t program_stack_end;
extern uint32_t program_code_start;
extern uint32_t program_code_end;

[[gnu::always_inline]] inline void sendToPIC(uint8_t msg) {
	intptr_t writePos = uartItems[UART_ITEM_PIC].txBufferWritePos;
	volatile char* uncached_tx_buf = (volatile char*)(picTxBuffer + UNCACHED_MIRROR_OFFSET);
	uncached_tx_buf[writePos] = msg;
	uartItems[UART_ITEM_PIC].txBufferWritePos += 1;
	uartItems[UART_ITEM_PIC].txBufferWritePos &= (PIC_TX_BUFFER_SIZE - 1);
}

[[gnu::always_inline]] inline void sendColor(uint8_t r, uint8_t g, uint8_t b) {
	sendToPIC(r);
	sendToPIC(g);
	sendToPIC(b);
}

[[gnu::always_inline]] inline void drawByte(uint8_t byte, uint8_t r, uint8_t g, uint8_t b) {
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
[[gnu::always_inline]] inline int32_t drawPointer(uint32_t idxColumnPairStart, uint32_t pointerValue, uint8_t r,
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
	SEGGER_RTT_printf(0, "PTR: 0x%8X (%d, %d, %d)\n", pointerValue, r, g, b);
#endif

	return idxColumnPairStart;
}

[[gnu::always_inline]] inline bool isStackPointer(uint32_t value) {
	if (value >= (uint32_t)&program_stack_start && value < (uint32_t)&program_stack_end) {
		return true;
	}

	return false;
}

[[gnu::always_inline]] inline bool isCodePointer(uint32_t value) {
	if (value >= (uint32_t)&program_code_start && value < (uint32_t)&program_code_end) {
		return true;
	}

	return false;
}

[[gnu::always_inline]] inline uint8_t getHexCharValue(char input) {
	uint8_t result = 0;
	if (input >= '0' && input <= '9') {
		result = input - '0';
	}
	if (input >= 'a' && input <= 'f') {
		result = input - 'a' + 10;
	}
	return result;
}

#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX_POINTER_COUNT 4
[[gnu::always_inline]] inline void printPointers(uint32_t addrSYSLR, uint32_t addrSYSSP, uint32_t addrUSRLR,
                                                 uint32_t addrUSRSP, bool hardFault) {
	// Search for stack pointers
	uint32_t stackPointer = 0;
	if (isStackPointer(addrUSRSP)) {
		stackPointer = addrUSRSP;
	}
	else if (isStackPointer(addrSYSSP)) {
		stackPointer = addrSYSSP;
	}

	uint8_t stackPointerCount = 0;
	uint32_t stackPointers[MAX_POINTER_COUNT] = {0};

	// Search for stack pointers before any printing
	if (stackPointer != 0x00000000) {
		stackPointer = stackPointer - (stackPointer % 4); // Align to 4 bytes
		while (stackPointer <= (uint32_t)&program_stack_end) {
			uint32_t stackValue = *((uint32_t*)stackPointer);

			// Print any pointer that is pointing to code, different from the LRs and not the same as before
			if (isCodePointer(stackValue) && stackValue != stackPointers[MIN(0, stackPointerCount - 1)]
			    && stackValue != addrUSRLR && stackValue != addrSYSLR) {
				stackPointers[stackPointerCount] = stackValue;
				++stackPointerCount;

				if (stackPointerCount >= MAX_POINTER_COUNT) {
					break;
				}
			}

			stackPointer += 4;
		}
	}

	uint32_t currentColumnPairIndex = 0;

	// Print LR from USR mode if it is valid
	if (isCodePointer(addrUSRLR)) {
		currentColumnPairIndex = drawPointer(currentColumnPairIndex, addrUSRLR, 255, 0, 255);
	}

	// Print LR from SYS mode if it is valid and different from USR mode
	if (isCodePointer(addrSYSLR) && addrSYSLR != addrUSRLR) {
		currentColumnPairIndex = drawPointer(currentColumnPairIndex, addrSYSLR, 0, 0, 255);
	}

	// Print all pointers
	if (stackPointerCount > 0) {
		uint8_t currentPointerIndex = 0;
		uint8_t currentBlueValue = 0;
		while (currentPointerIndex < MAX_POINTER_COUNT) {
			currentColumnPairIndex =
			    drawPointer(currentColumnPairIndex, stackPointers[currentPointerIndex], 0, 255, currentBlueValue);

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
	SEGGER_RTT_printf(0, "COMMIT: %s\n", kCommitShort);
#endif

	uartFlushIfNotSending(UART_ITEM_PIC);

	// Wait for flush to finish
	while (!(DMACn(PIC_TX_DMA_CHANNEL).CHSTAT_n & (1 << 6))) {}
}

//@TODO: Pointers seem to be wrong right now and we will need to filter out the SP call to
// fault_handler_print_freeze_pointers (we can't inline, otherwise that would be huge)
extern void fault_handler_print_freeze_pointers(uint32_t addrSYSLR, uint32_t addrSYSSP, uint32_t addrUSRLR,
                                                uint32_t addrUSRSP) {
	__disable_irq();
	printPointers(addrSYSLR, addrSYSSP, addrUSRLR, addrUSRSP, false);
	clearTxBuffer();
	__enable_irq();
}

extern void handle_cpu_fault(uint32_t addrSYSLR, uint32_t addrSYSSP, uint32_t addrUSRLR, uint32_t addrUSRSP) {
	printPointers(addrSYSLR, addrSYSSP, addrUSRLR, addrUSRSP, true);
	clearTxBuffer();
	// if we start using user mode then we'd want to do this to get an accurate call stack. We don't so just don't
	//__asm__("CPS  0x10"); // Go to USR mode

	while (1) {
		__asm__("nop");
	}
}
