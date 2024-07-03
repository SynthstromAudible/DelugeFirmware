/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#pragma once
#include <stdint.h>

/*
 * ================= DEBUGGING TEXT OUTPUT =================
 *
 * To get some debugging text output from the Deluge, ideally you’ll be using a J-link,
 * and can then use its RTT utility. To tell the firmware build to include this text
 * generation and outputting, go to src/drivers/All_CPUs/uart_all_cpus/uart_all_cpus.h and
 * set ENABLE_TEXT_OUTPUT to 1. The fact that this flag is in a file labelled “uart” will seem
 * to make no sense because RTT output has nothing to do with UART. This is because before I
 * realised RTT was a thing, this flag would instead send the debugging text out of the MIDI DIN port,
 * and MIDI is UART. If you’re not using a J-link and want to see the debugging text,
 * this might be what you want to do. If so, go to src/drivers/RZA1/cpu_specific.h,
 * and set HAVE_RTT to 0. Though this configuration hasn’t been tested for a while...
 */
#ifndef ENABLE_TEXT_OUTPUT
#define ENABLE_TEXT_OUTPUT 1
#endif

struct UartItem { // Exactly 8 bytes, so can align nicely to cache line
	uint16_t txBufferWritePos;
	uint16_t txBufferReadPos;
	uint16_t txBufferReadPosAfterTransfer;
	uint8_t txSending;
	uint8_t shouldDoConsecutiveTransferAfter; // Applies to MIDI only - for PIC, always tries to do this
};

extern struct UartItem uartItems[];

extern void initUartDMA();
uint8_t uartGetChar(int32_t item, char* readData);
uint32_t* uartGetCharWithTiming(int32_t timingCaptureItem, char* readData);
void uartPutCharBack(int32_t item);
void uartInsertFakeChar(int32_t item, char data);
void uartPrintln(char const* output);
void uartPrint(char const* output);
void uartPrintNumber(int32_t number);
void uartPrintNumberSameLine(int32_t number);
void uartPrintlnFloat(float number);
void uartPrintFloat(float number);

void uartFlushIfNotSending(int32_t item);
int32_t uartGetTxBufferFullnessByItem(int32_t item);
int32_t uartGetTxBufferSpace(int32_t item);
void uartDrain(uint32_t item);

extern void tx_interrupt(int32_t item);
