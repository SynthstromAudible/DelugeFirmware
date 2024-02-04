/*
 * Copyright © 2020	-2023 Synthstrom Audible Limited
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

#include "definitions.h"

#include "RZA1/oled/oled_low_level.h"
#include "RZA1/uart/sio_char.h"
#include "drivers/dmac/dmac.h"
#include "drivers/oled/oled.h"
#include "drivers/rspi/rspi.h"

void MainOLED_WCom(char data) {
	R_RSPI_SendBasic8(SPI_CHANNEL_OLED_MAIN, data);
}

void oledMainInit() {

	// These commands copied from the OLED manufacturer's datasheet.

	MainOLED_WCom(0xFD); // SET COMMAND LOCK
	MainOLED_WCom(0x12);
	MainOLED_WCom(0xAE); // DOT MARTIX DISPLAY OFF

	MainOLED_WCom(0x81); // CONTARST CONTROL(00H-0FFH)
	MainOLED_WCom(0xFF);

	MainOLED_WCom(0xA4); // ENTIRE DISPLAY OFF(0A4H-0A5H)

	MainOLED_WCom(0xA6); // SET NORMAL DISPLAY(0A6H-0A7H)

	MainOLED_WCom(0x00); // SET LOW COLUMN START ADDRESS
	MainOLED_WCom(0x10); // SET HIGH COLUMN START ADDRESS

	MainOLED_WCom(0x20); // SET MEMORRY ADDRESSING MODE
	MainOLED_WCom(0x00); // Horizontal

	/*
	 MainOLED_WCom(0xB0); // Set Page Start
*/

#if OLED_MAIN_HEIGHT_PIXELS != 64
	MainOLED_WCom(0x22); // Set page address start / end
	MainOLED_WCom((64 - OLED_MAIN_HEIGHT_PIXELS) >> 3);
	MainOLED_WCom(7);
#endif

	MainOLED_WCom(0x40); // SET DISPLAY START LINE (040H-07FH) // Moves entire graphics vertically

	MainOLED_WCom(0xA0); // SET SEGMENT RE-MAP(0A0H-0A1H) // Flips stuff 180deg!

	MainOLED_WCom(0xA8); // SET MULTIPLEX RATIO 64
	MainOLED_WCom(0x3F);

	MainOLED_WCom(0xC0); // COM SCAN COM1-COM64(0C8H,0C0H)

	MainOLED_WCom(0xD3); // SET DISPLAY OFFSET(OOH-3FH)
	MainOLED_WCom(0x0);

	MainOLED_WCom(0xDA); // COM PIN CONFIGURATION
	MainOLED_WCom(0x12);

	MainOLED_WCom(0xD5); // SET FRAME FREQUENCY
	MainOLED_WCom(0xF0);

	MainOLED_WCom(0xD9); // SET PRE_CHARGE PERIOD
	MainOLED_WCom(0xA2);

	MainOLED_WCom(0xDB); // SET VCOM DESELECT LEVEL
	MainOLED_WCom(0x34);

	MainOLED_WCom(0xAF); // DSPLAY ON

	R_RSPI_WaitEnd(SPI_CHANNEL_OLED_MAIN);

#ifdef OLED_MAIN_DC_PIN
	R_GPIO_PinWrite(OLED_MAIN_DC_PIN, GPIO_LEVEL_HIGH);
#else
	bufferPICUart(251); // D/C high
	uartFlushIfNotSending(UART_ITEM_PIC);
#endif

	/*
	R_INTC_RegistIntFunc(INTC_ID_RSPI_SPTI0 + SPI_CHANNEL_OLED_MAIN * 3, oledTransferComplete);
	R_INTC_SetPriority(INTC_ID_RSPI_SPTI0 + SPI_CHANNEL_OLED_MAIN * 3, 13); // Not very important
	R_INTC_Enable(INTC_ID_RSPI_SPTI0 + SPI_CHANNEL_OLED_MAIN * 3);
*/
}

struct SpiTransferQueueItem spiTransferQueue[SPI_TRANSFER_QUEUE_SIZE];
volatile bool spiTransferQueueCurrentlySending = false;
volatile uint8_t spiTransferQueueReadPos = 0;
uint8_t spiTransferQueueWritePos = 0;

void enqueueSPITransfer(int32_t destinationId, uint8_t const* image) {

	// First check there isn't already an identical transfer enqueued.
	int32_t readPosNow = spiTransferQueueReadPos;
	/*
	while (readPosNow != spiTransferQueueWritePos) {
	    if (spiTransferQueue[readPosNow].destinationId == destinationId && spiTransferQueue[readPosNow].dataAddress ==
	image) return; readPosNow = (readPosNow + 1) & (SPI_TRANSFER_QUEUE_SIZE - 1);
	}
	*/

	spiTransferQueue[spiTransferQueueWritePos].destinationId = destinationId;
	spiTransferQueue[spiTransferQueueWritePos].dataAddress = image;
	spiTransferQueueWritePos = (spiTransferQueueWritePos + 1) & (SPI_TRANSFER_QUEUE_SIZE - 1);

	// If DMA not currently sending, and our new entry is still in the queue (it didn't get sent inside an interrupt
	// just now), then send it now.
	if (!spiTransferQueueCurrentlySending && spiTransferQueueWritePos != spiTransferQueueReadPos) {
		sendSPITransferFromQueue();
	}
}

void oledDMAInit() {

	// ---- DMA Control Register Setting ----
	DCTRLn(OLED_SPI_DMA_CHANNEL) = 0;

	// ----- Transmission Side Setting ----
	DMACn(OLED_SPI_DMA_CHANNEL).CHCFG_n = 0b00000000001000000000001001101000 | (OLED_SPI_DMA_CHANNEL & 7);

	// ---- DMA Channel Interval Register Setting ----
	DMACn(OLED_SPI_DMA_CHANNEL).CHITVL_n = 0;

	// ---- DMA Channel Extension Register Setting ----
	DMACn(OLED_SPI_DMA_CHANNEL).CHEXT_n = 0;

	DMACn(OLED_SPI_DMA_CHANNEL).N0DA_n = (uint32_t)&RSPI(SPI_CHANNEL_OLED_MAIN).SPDR.BYTE.LL;

	// ---- Software Reset and clear TC bit ----
	DMACn(OLED_SPI_DMA_CHANNEL).CHCTRL_n |= DMAC_CHCTRL_0S_SWRST | DMAC_CHCTRL_0S_CLRTC;

	DMACn(OLED_SPI_DMA_CHANNEL).N0DA_n = (uint32_t)&RSPI(SPI_CHANNEL_OLED_MAIN).SPDR.BYTE.LL;

	uint32_t dmarsTX = DMARS_FOR_RSPI_TX + (SPI_CHANNEL_OLED_MAIN << 2);
	setDMARS(OLED_SPI_DMA_CHANNEL, dmarsTX);

	R_INTC_RegistIntFunc(DMA_INTERRUPT_0 + OLED_SPI_DMA_CHANNEL, oledTransferComplete);
	R_INTC_SetPriority(DMA_INTERRUPT_0 + OLED_SPI_DMA_CHANNEL, 13); // Priority is not very important
	R_INTC_Enable(DMA_INTERRUPT_0 + OLED_SPI_DMA_CHANNEL);
}
