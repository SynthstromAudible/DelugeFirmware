#include "mock_hardware.h"
#include "src/RZA1/oled/oled_low_level.c"
#include "src/deluge/drivers/uart/uart.c"

static void testSustainedPadTraffic(void) {
	static uint8_t frame[768];
	// Start near the ring boundary and leave a second batch behind an active DMA.
	// Without backpressure, this producer replenishes the queue before every poll.
	memset(uartItems, 0, sizeof(uartItems));
	uartItems[UART_ITEM_PIC].txBufferReadPos = 900;
	uartItems[UART_ITEM_PIC].txBufferReadPosAfterTransfer = 900;
	uartItems[UART_ITEM_PIC].txBufferWritePos = 900;
	queuePicBytes(392);
	uartFlushIfNotSending(UART_ITEM_PIC);
	queuePicBytes(50);
	oledFrameQueue[oledFrameQueueWritePos] = frame;
	oledFrameQueueWritePos = (oledFrameQueueWritePos + 1) & (OLED_FRAME_QUEUE_SIZE - 1);
	sendSPITransferFromQueue();
	for (unsigned poll = 0; poll < 8 && oledWaitingForMessage == OLED_MESSAGE_NONE; ++poll) {
		if (uartGetTxBufferSpace(UART_ITEM_PIC) > 392) {
			queuePicBytes(392);
		}
		oledRoutine();
		if (uartItems[UART_ITEM_PIC].txSending) {
			tx_interrupt(UART_ITEM_PIC);
		}
	}
	assert(oledWaitingForMessage == OLED_MESSAGE_SELECT);
	assert(!oledIsWaitingForPicTxDrain());
	assert(uartGetTxBufferSpace(UART_ITEM_PIC) > 392);
	oledSelectingComplete();
	oledTransferComplete(0);
	oledRoutine();
	oledDeselectionComplete();
	assert(!spiBusCurrentlySending && criticalDepth == 0);
}

int main(void) {
	static uint8_t frame[768];
	dma.CRDA_n = (uintptr_t)rxBufferReadAddr[UART_ITEM_PIC]; // No incoming UART bytes.
	const uint32_t firstCV = 0x31123400;
	const uint32_t secondCV = 0x31567800;
	queuePicBytes(392);
	oledFrameQueue[0] = frame;
	oledFrameQueueWritePos = 1;
	sendSPITransferFromQueue();

	// A grid redraw on the PIC UART must leave SELECT cancellable by CV.
	assert(uartGetTxBufferSpace(UART_ITEM_PIC) == 0);
	assert(uartGetTxBufferSpace(UART_ITEM_MIDI) == 1024);
	oledRoutine();
	assert(picMessage == 0 && oledWaitingForMessage == OLED_MESSAGE_NONE);
	assert(oledPendingMessageToSend == OLED_MESSAGE_SELECT);
	enqueueCVMessage(0, firstCV);
	assert(spi.SPDR.LONG == firstCV && dacSelected);
	assert(oledPendingMessageToSend == 0 && !oled_sending);
	assert(oledFrameQueueReadPos == 0 && oledFrameQueue[0] == frame);
	assert(uartGetTxBufferSpace(UART_ITEM_PIC) == 0); // Reservation survives CV preemption.

	// A second request must wait for the CV transfer already in flight.
	enqueueCVMessage(0, secondCV);
	assert(spi.SPDR.LONG == firstCV);
	cvSPITransferComplete(0);
	assert(spi.SPDR.LONG == secondCV);
	cvSPITransferComplete(0);
	assert(!dacSelected && oledPendingMessageToSend == OLED_MESSAGE_SELECT);
	oledRoutine();
	assert(picMessage == 0 && oledWaitingForMessage == OLED_MESSAGE_NONE);

	// Once the pad backlog drains, resume the original frame.
	uartFlushIfNotSending(UART_ITEM_PIC);
	tx_interrupt(UART_ITEM_PIC);
	assert(uartGetTxBufferFullnessByItem(UART_ITEM_PIC) == 0);
	oledRoutine();
	assert(picMessage == OLED_MESSAGE_SELECT && oledWaitingForMessage == OLED_MESSAGE_SELECT);
	assert(uartGetTxBufferSpace(UART_ITEM_PIC) == 1023); // Pad producers resume after SELECT.
	enqueueCVMessage(0, firstCV);
	assert(spi.SPDR.LONG == secondCV && !dacSelected); // SELECT is now committed.
	oledSelectingComplete();                           // Simulate completed SELECT acknowledgement and settling.
	assert(oledFrameQueueReadPos == 1 && dma.N0TB_n == sizeof(frame));
	oledTransferComplete(0);

	// Never defer DESELECT behind this guard: the bus is already held.
	queuePicBytes(392);
	oledRoutine();
	assert(picMessage == OLED_MESSAGE_DESELECT && oledWaitingForMessage == OLED_MESSAGE_DESELECT);
	assert(spi.SPDR.LONG == secondCV);
	oledDeselectionComplete();
	assert(spi.SPDR.LONG == firstCV && dacSelected);
	cvSPITransferComplete(0);
	assert(!spiBusCurrentlySending && !dacSelected && criticalDepth == 0);
	testSustainedPadTraffic();
	puts("OLED SELECT/CV preemption tests passed");
}
