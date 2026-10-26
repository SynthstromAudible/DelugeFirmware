#include "mock_hardware.h"
// Include the production implementations to exercise their internal state with mock registers.
#include "src/RZA1/oled/oled_low_level.c" // NOLINT(bugprone-suspicious-include)
#include "src/deluge/drivers/uart/uart.c" // NOLINT(bugprone-suspicious-include)

static void testSustainedPadTraffic(void) {
	static uint8_t frame[768];
	// Start near the ring boundary and leave a second batch behind an active DMA.
	// Without backpressure, this producer replenishes the queue before every poll.
	for (unsigned item = 0; item < NUM_UART_ITEMS; ++item) {
		uartItems[item] = (struct UartItem){0};
	}
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
	assert(!spiBusCurrentlySending && critical_depth == 0);
}

static void finishCommittedFrame(uint32_t first_cv, uint32_t second_cv, size_t frame_size) {
	enqueueCVMessage(0, first_cv);
	assert(spi.SPDR.LONG == second_cv && !dac_selected); // SELECT is now committed.
	oledSelectingComplete();                             // Simulate completed SELECT acknowledgement and settling.
	assert(oledFrameQueueReadPos == 1 && dma.N0TB_n == frame_size);
	oledTransferComplete(0);

	// Never defer DESELECT behind this guard: the bus is already held.
	queuePicBytes(392);
	oledRoutine();
	assert(pic_message == OLED_MESSAGE_DESELECT && oledWaitingForMessage == OLED_MESSAGE_DESELECT);
	assert(spi.SPDR.LONG == second_cv);
	oledDeselectionComplete();
	assert(spi.SPDR.LONG == first_cv && dac_selected);
	cvSPITransferComplete(0);
	assert(!spiBusCurrentlySending && !dac_selected && critical_depth == 0);
}

static void testCVPreemption(void) {
	static uint8_t frame[768];
	dma.CRDA_n = (uintptr_t)rxBufferReadAddr[UART_ITEM_PIC]; // No incoming UART bytes.
	const uint32_t first_cv = 0x31123400;
	const uint32_t second_cv = 0x31567800;
	queuePicBytes(392);
	oledFrameQueue[0] = frame;
	oledFrameQueueWritePos = 1;
	sendSPITransferFromQueue();

	// A grid redraw on the PIC UART must leave SELECT cancellable by CV.
	assert(uartGetTxBufferSpace(UART_ITEM_PIC) == 0);
	assert(uartGetTxBufferSpace(UART_ITEM_MIDI) == 1024);
	oledRoutine();
	assert(pic_message == 0 && oledWaitingForMessage == OLED_MESSAGE_NONE);
	assert(oledPendingMessageToSend == OLED_MESSAGE_SELECT);
	enqueueCVMessage(0, first_cv);
	assert(spi.SPDR.LONG == first_cv && dac_selected);
	assert(oledPendingMessageToSend == 0 && !oled_sending);
	assert(oledFrameQueueReadPos == 0 && oledFrameQueue[0] == frame);
	assert(uartGetTxBufferSpace(UART_ITEM_PIC) == 0); // Reservation survives CV preemption.

	// A second request must wait for the CV transfer already in flight.
	enqueueCVMessage(0, second_cv);
	assert(spi.SPDR.LONG == first_cv);
	cvSPITransferComplete(0);
	assert(spi.SPDR.LONG == second_cv);
	cvSPITransferComplete(0);
	assert(!dac_selected && oledPendingMessageToSend == OLED_MESSAGE_SELECT);
	oledRoutine();
	assert(pic_message == 0 && oledWaitingForMessage == OLED_MESSAGE_NONE);

	// Once the pad backlog drains, resume the original frame.
	uartFlushIfNotSending(UART_ITEM_PIC);
	tx_interrupt(UART_ITEM_PIC);
	assert(uartGetTxBufferFullnessByItem(UART_ITEM_PIC) == 0);
	oledRoutine();
	assert(pic_message == OLED_MESSAGE_SELECT && oledWaitingForMessage == OLED_MESSAGE_SELECT);
	assert(uartGetTxBufferSpace(UART_ITEM_PIC) == 1023); // Pad producers resume after SELECT.
	finishCommittedFrame(first_cv, second_cv, sizeof(frame));
}

int main(void) {
	testCVPreemption();
	testSustainedPadTraffic();
	puts("OLED SELECT/CV preemption tests passed");
}
