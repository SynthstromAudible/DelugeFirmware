/*
 * Copyright © 2021-2026 Synthstrom Audible Limited
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

/// `libdeluge/fault.h` for this board — the RZ/A1 half of the pad-grid crash reporter.
///
/// The pattern itself moved to the portable application
/// (`src/deluge/io/debug/fault_pattern.c`) so the Rust/Embassy BSP gets the same crash
/// display; this file is what remains board-specific, and is the transport that
/// renderer used to inline. Behaviour is unchanged: bytes go into the PIC UART's TX
/// ring through the uncached mirror, then the existing DMA flush pushes them out.
#include "libdeluge/fault.h"

#include "RZA1/cpu_specific.h"
#include "RZA1/system/iodefines/dmac_iodefine.h"
#include "RZA1/uart/sio_char.h"
#include "definitions.h"
#include "drivers/ssi/ssi.h" // clearTxBuffer()
#include "drivers/uart/uart.h"

extern uint32_t program_stack_start;
extern uint32_t program_stack_end;
extern uint32_t program_code_start;
extern uint32_t program_code_end;

void deluge_fault_ranges(DelugeFaultRanges* out) {
	if (out == NULL) {
		return;
	}
	out->code_start = (uintptr_t)&program_code_start;
	out->code_end = (uintptr_t)&program_code_end;
	out->stack_start = (uintptr_t)&program_stack_start;
	out->stack_end = (uintptr_t)&program_stack_end;
	// No second stack on this BSP: everything runs on the program stack. Zeroed, which the reporter reads as "absent".
	out->alt_stack_start = 0;
	out->alt_stack_end = 0;
}

void deluge_fault_pad_write(uint8_t byte) {
	intptr_t writePos = uartItems[UART_ITEM_PIC].txBufferWritePos;
	volatile char* uncached_tx_buf = (volatile char*)(picTxBuffer + UNCACHED_MIRROR_OFFSET);
	uncached_tx_buf[writePos] = byte;
	uartItems[UART_ITEM_PIC].txBufferWritePos += 1;
	uartItems[UART_ITEM_PIC].txBufferWritePos &= (PIC_TX_BUFFER_SIZE - 1);
}

void deluge_fault_pad_flush(void) {
	uartFlushIfNotSending(UART_ITEM_PIC);

	// Wait for the flush to finish. Bounded, per the hook's contract: a PIC that has
	// stopped consuming must not cost us the crash report entirely.
	for (uint32_t spins = 0; spins < 100000000u; ++spins) {
		if (DMACn(PIC_TX_DMA_CHANNEL).CHSTAT_n & (1 << 6)) {
			break;
		}
	}

	clearTxBuffer();
}
