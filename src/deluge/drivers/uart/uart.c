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

#include "drivers/uart/uart.h"
#include "RZA1/system/iodefines/dmac_iodefine.h"
#include "definitions.h"
#include "drivers/dmac/dmac.h"
#include <string.h>

#include "RZA1/intc/devdrv_intc.h"
#include "RZA1/system/iodefines/scif_iodefine.h"
#include "util/cfunctions.h"
#include <math.h>

#include "RTT/SEGGER_RTT.h"
#include "timers_interrupts.h"

void uartPrintln(char const* output) {
#if ENABLE_TEXT_OUTPUT
#if HAVE_RTT
	SEGGER_RTT_WriteString(0, output);
	SEGGER_RTT_WriteString(0, "\r\n");
#else
	while (*output) {
		bufferMIDIUart(*output);
		output++;
	}
	bufferMIDIUart('\n');
	uartFlushIfNotSending(UART_ITEM_MIDI);
#endif
#endif
}

void uartPrintNumber(int32_t number) {
#if ENABLE_TEXT_OUTPUT
	char buffer[12];
	intToString(number, buffer, 1);
	uartPrintln(buffer);
#endif
}

void uartPrint(char const* output) {
#if ENABLE_TEXT_OUTPUT
#if HAVE_RTT
	SEGGER_RTT_WriteString(0, output);
#else
	while (*output) {
		bufferMIDIUart(*output);
		output++;
	}
	uartFlushIfNotSending(UART_ITEM_MIDI);
#endif
#endif
}

void uartPrintFloat(float number) {
#if ENABLE_TEXT_OUTPUT
	char buffer[12];
	intToString((int32_t)roundf(number * 100), buffer, 3);
	int32_t length = strlen(buffer);
	char temp = buffer[length - 2];
	buffer[length - 2] = 0;
	uartPrint(buffer);
	uartPrint(".");
	buffer[length - 2] = temp;
	uartPrint(&buffer[length - 2]);
#endif
}

void uartPrintlnFloat(float number) {
#if ENABLE_TEXT_OUTPUT
	uartPrintFloat(number);
#if HAVE_RTT
	SEGGER_RTT_WriteString(0, "\r\n");
#else
	bufferMIDIUart('\n');
	uartFlushIfNotSending(UART_ITEM_MIDI);
#endif
#endif
}

struct UartItem uartItems[NUM_UART_ITEMS] __attribute__((aligned(CACHE_LINE_SIZE)));

extern const uint8_t uartChannels[];
extern const uint16_t txBufferSizes[];
extern const uint8_t txDmaChannels[];
extern char* const txBuffers[];
extern char* const rxBuffers[];
extern char* rxBufferReadAddr[];
extern const uint16_t rxBufferSizes[];
extern const uint16_t txBufferSizes[];
extern const uint8_t rxDmaChannels[];
extern char const timingCaptureItems[];
extern uint16_t const timingCaptureBufferSizes[];
extern uint32_t* const timingCaptureBuffers[];
extern const void (*txInterruptFunctions[])(uint32_t);
extern const uint8_t txInterruptPriorities[];
extern const uint32_t* const uartRxLinkDescriptors[];
extern uint8_t const timingCaptureDMAChannels[];
extern const uint32_t* const timingCaptureLinkDescriptors[];
extern const bool uartItemIsScim[];

#define DMA_SCIF_TX_CONFIG (0b00000000001000000000000001101000 | DMA_AM_FOR_SCIF)
#define DMA_SCIM_TX_CONFIG                                                                                             \
	(0b00000000001000000000000000101000                                                                                \
	 | DMA_AM_FOR_SCIM) // LVL is 0 for SCIM despite the datasheet saying it should be 1 - a misprint, I believe

// Returns whether it sent anything
// Warning - this can get called from a timer ISR, or a DMA ISR, or no ISR
int32_t uartFlush(int32_t item) {

	int32_t num = uartItems[item].txBufferWritePos - uartItems[item].txBufferReadPosAfterTransfer;
	if (!num) {
		return 0;
	}

	int32_t fullNum = num & (txBufferSizes[item] - 1);

	uint32_t newConfig = DMA_SCIF_TX_CONFIG;

	// If region to send reaches the rightmost end of the circular buffer...
	if (num < 0) {
		num = txBufferSizes[item] - uartItems[item].txBufferReadPosAfterTransfer;

		int32_t numLeft = fullNum - num;

		// If there are also some further bytes starting from the left of the circular buffer that we want to send as
		// well, set that up to happen automatically
		if (numLeft) {
			DMACn(txDmaChannels[item]).N1TB_n = numLeft;
			newConfig |=
			    DMAC_CHCFG_0S_REN | DMAC_CHCFG_0S_RSW | DMAC_CHCFG_0S_DEM; // Set to switch to next "register" after
			                                                               // first transaction, and also mask interrupt
		}
	}
	DMACn(txDmaChannels[item]).CHCFG_n = newConfig | (txDmaChannels[item] & 7);

	int32_t prevReadPos = uartItems[item].txBufferReadPosAfterTransfer;
	uartItems[item].txBufferReadPosAfterTransfer =
	    (uartItems[item].txBufferReadPosAfterTransfer + fullNum) & (txBufferSizes[item] - 1);
	uartItems[item].shouldDoConsecutiveTransferAfter = false; // Only actually applies to MIDI

	DMACn(txDmaChannels[item]).N0TB_n = num;
	uint32_t dataAddress = (uint32_t)&txBuffers[item][prevReadPos];
	DMACn(txDmaChannels[item]).N0SA_n = dataAddress;
	// Paul: Removed from now as this was not present before and I am unsure if it helps with anything
	// v7_dma_flush_range(dataAddress, dataAddress + num);

	return 1;
}

// Warning - this will sometimes (not always) be called in a timer ISR
void uartFlushIfNotSending(int32_t item) {

	if (!uartItems[item].txSending) {
		// There should be no way the DMA TX-complete interrupt could occur in this bit, cos we could only be here if
		// it'd already completed and set txSending to 0...
		int32_t sentAny = uartFlush(item);

		if (sentAny) {
			uartItems[item].txSending = 1;
			DMACn(txDmaChannels[item]).CHCTRL_n |=
			    DMAC_CHCTRL_0S_CLRTC | DMAC_CHCTRL_0S_SETEN; // ---- Enable DMA Transfer and clear TC bit ----
		}
	}

	// Applies to MIDI only - if sending was already happening, take note that we want to send additional stuff as soon
	// as that's done. For PIC, this always happens anyway
	else {
		// WARNING - it'd be a problem if the DMA TX-finished interrupt occurred right here...
		// And when not using volatiles, we also could end up here if that interrupt occurred shortly before the if
		// statement above at the start
		uartItems[item].shouldDoConsecutiveTransferAfter = 1;
	}
}

int32_t uartGetTxBufferFullnessByItem(int32_t item) {
	return (uartItems[item].txBufferWritePos - uartItems[item].txBufferReadPos) & (txBufferSizes[item] - 1);
}

int32_t uartGetTxBufferSpace(int32_t item) {
	return txBufferSizes[item] - uartGetTxBufferFullnessByItem(item);
}

void uartDrain(uint32_t item) {
	char value;
	bool charReceived = true;
	while (charReceived) {
		charReceived = uartGetChar(item, (char*)&value);
	}
}

void uartPutCharBack(int32_t item) {
	int32_t readPos = (uint32_t)rxBufferReadAddr[item] - ((uint32_t)rxBuffers[item]);
	readPos = (readPos - 1) & (rxBufferSizes[item] - 1);
	rxBufferReadAddr[item] = rxBuffers[item] + readPos;
}

void uartInsertFakeChar(int32_t item, char data) {
	int32_t readPos = (uint32_t)rxBufferReadAddr[item] - ((uint32_t)rxBuffers[item]);
	readPos = (readPos - 1) & (rxBufferSizes[item] - 1);
	rxBufferReadAddr[item] = rxBuffers[item] + readPos;
	*(rxBufferReadAddr[item] + UNCACHED_MIRROR_OFFSET) = data;
}

uint8_t uartGetChar(int32_t item, char* readData) {

	char const* currentWritePos = (char*)DMACnNonVolatile(rxDmaChannels[item])
	                                  .CRDA_n; // We deliberately don't go (volatile uint32_t*) here, for speed

	if (currentWritePos == rxBufferReadAddr[item]) {
		return 0;
	}

	*readData = *(rxBufferReadAddr[item] + UNCACHED_MIRROR_OFFSET);

	int32_t readPos = (uint32_t)rxBufferReadAddr[item] - ((uint32_t)rxBuffers[item]);
	readPos = (readPos + 1) & (rxBufferSizes[item] - 1);
	rxBufferReadAddr[item] = rxBuffers[item] + readPos;

	return 1;
}

uint32_t* uartGetCharWithTiming(int32_t timingCaptureItem, char* readData) {

	int32_t item = timingCaptureItems[timingCaptureItem];

	char const* currentWritePos = (char*)DMACnNonVolatile(rxDmaChannels[item])
	                                  .CRDA_n; // We deliberately don't go (volatile uint32_t*) here, for speed

	if (currentWritePos == rxBufferReadAddr[item]) {
		return NULL;
	}

	*readData = *(rxBufferReadAddr[item] + UNCACHED_MIRROR_OFFSET);

	int32_t readPos = (uint32_t)rxBufferReadAddr[item] - ((uint32_t)rxBuffers[item]);

	uint32_t* timer =
	    (uint32_t*)((uint32_t)&timingCaptureBuffers[timingCaptureItem]
	                                               [readPos & (timingCaptureBufferSizes[timingCaptureItem] - 1)]
	                + UNCACHED_MIRROR_OFFSET);

	readPos = (readPos + 1) & (rxBufferSizes[item] - 1);
	rxBufferReadAddr[item] = rxBuffers[item] + readPos;

	return timer;
}

// Warning - obviously this gets called in a DMA ISR
// This is the function which seems to cause a crash if called via interrupt during the above one
void tx_interrupt(int32_t item) {

	uartItems[item].txBufferReadPos = uartItems[item].txBufferReadPosAfterTransfer;

	// May want to try sending a consecutive transfer
	if (item != UART_ITEM_MIDI || uartItems[item].shouldDoConsecutiveTransferAfter) {

		uartItems[item].shouldDoConsecutiveTransferAfter = 0;

		int32_t sentAny = uartFlush(item);

		if (sentAny) {
			DMACn(txDmaChannels[item]).CHCTRL_n =
			    DMAC_CHCTRL_0S_CLRTC | DMAC_CHCTRL_0S_SETEN
			    | DMAC_CHCTRL_0S_CLREND; // ---- Enable DMA Transfer and clear TC bit ----
			return;
		}
	}

	// If nothing sent...
	uartItems[item].txSending = 0;

	// Clear Transfer End Bit Status - but hey we actually don't need to do this to clear the interrupt - provided we're
	// not going to be reading these flags later
	//*g_dma_reg_tbl[txDmaChannels[item]].chctrl |= (DMAC0_CHCTRL_n_CLREND | DMAC0_CHCTRL_n_CLRTC);
}

// (I think) this has to be called after Uart initialized, otherwise problem when booting from flash
void initUartDMA() {

	// For each Uart item...
	int32_t item;
	for (item = 0; item < NUM_UART_ITEMS; item++) {

		bool isScim = 0;

		uartItems[item].txBufferWritePos = 0;
		uartItems[item].txBufferReadPos = 0;
		uartItems[item].txBufferReadPosAfterTransfer = 0;
		uartItems[item].txSending = 0;
		uartItems[item].shouldDoConsecutiveTransferAfter = 0;

		int32_t sciChannel = uartChannels[item];

		// Set up TX DMA channel -----------------------------------------------------------------------
		int32_t txDmaChannel = txDmaChannels[item];

		// ---- DMA Control Register Setting ----
		DCTRLn(txDmaChannel) = 0;

		uint32_t destinationRegister = (uint32_t)&SCIFA(sciChannel).FTDR.BYTE;

		// ---- DMA Next0 Address Setting ----
		DMACn(txDmaChannel).N0DA_n = destinationRegister;

		// ---- DMA Next1 Address Setting ----
		DMACn(txDmaChannel).N1SA_n = (uint32_t)txBuffers[item];
		DMACn(txDmaChannel).N1DA_n = destinationRegister;

		// ----- Transmission Side Setting ----
		DMACn(txDmaChannel).CHCFG_n = (DMA_SCIF_TX_CONFIG) | (txDmaChannel & 7);

		// ---- DMA Expansion Resource Selector Setting ----
		uint32_t dmarsTX = (DMARS_FOR_SCIF0_TX) + (sciChannel << 2);
		setDMARS(txDmaChannel, dmarsTX);

		// ---- DMA Channel Interval Register Setting ----
		DMACn(txDmaChannel).CHITVL_n = 0;

		// ---- DMA Channel Extension Register Setting ----
		DMACn(txDmaChannel).CHEXT_n = 0;

		// ---- Software Reset and clear TC bit ----
		DMACn(txDmaChannel).CHCTRL_n |= DMAC_CHCTRL_0S_SWRST | DMAC_CHCTRL_0S_CLRTC;
		
		setupAndEnableInterrupt(txInterruptFunctions[item], DMA_INTERRUPT_0 + txDmaChannel,
		                        txInterruptPriorities[item]);
		// Set up RX DMA channel -----------------------------------------------------------------------
		int32_t rxDmaChannel = rxDmaChannels[item];
		uint32_t dmarsRX = (DMARS_FOR_SCIF0_RX) + (sciChannel << 2);

		initDMAWithLinkDescriptor(rxDmaChannel, uartRxLinkDescriptors[item], dmarsRX);
		dmaChannelStart(rxDmaChannel);

		SCIFA(sciChannel).SCSCR = 0x00F0u; // Enable "interrupt" (which actually triggers DMA)
	}

	// Set up MIDI RX timing-capture DMA channel -----------------------------------------------------------------------
	int32_t i;
	for (i = 0; i < NUM_TIMING_CAPTURE_ITEMS; i++) {
		int32_t uartItem = timingCaptureItems[i];
		int32_t dmaChannel = timingCaptureDMAChannels[i];

		uint32_t dmarsRX = DMARS_FOR_SCIF0_RX + (uartChannels[uartItem] << 2);
		initDMAWithLinkDescriptor(dmaChannel, timingCaptureLinkDescriptors[i], dmarsRX);
		dmaChannelStart(dmaChannel);
	}
}
