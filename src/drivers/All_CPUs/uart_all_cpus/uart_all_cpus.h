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

#ifndef DRIVERS_ALL_CPUS_UART_ALL_CPUS_UART_ALL_CPUS_H_
#define DRIVERS_ALL_CPUS_UART_ALL_CPUS_UART_ALL_CPUS_H_

#include "r_typedefs.h"

#define ENABLE_TEXT_OUTPUT 0


struct UartItem { // Exactly 8 bytes, so can align nicely to cache line
	uint16_t txBufferWritePos;
	uint16_t txBufferReadPos;
	uint16_t txBufferReadPosAfterTransfer;
	uint8_t txSending;
	uint8_t shouldDoConsecutiveTransferAfter; // Applies to MIDI only - for PIC, always tries to do this
};

extern struct UartItem uartItems[];


uint8_t uartGetChar(int item, char_t* readData);
uint32_t* uartGetCharWithTiming(int timingCaptureItem, char_t* readData);
void uartPutCharBack(int item);
void uartInsertFakeChar(int item, char_t data);
void uartPrintln(char const* output);
void uartPrint(char const* output);
void uartPrintNumber(int number);
void uartPrintNumberSameLine(int number);
void uartPrintlnFloat(float number);
void uartPrintFloat(float number);

void uartFlushIfNotSending(int item);
int uartGetTxBufferFullnessByItem(int item);
int uartGetTxBufferSpace(int item);


#endif /* DRIVERS_ALL_CPUS_UART_ALL_CPUS_UART_ALL_CPUS_H_ */
