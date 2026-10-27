#include "mock_hardware.h"
// Include the production implementation to exercise internal state with mock registers.
#include "src/RZA1/oled/oled_low_level.c" // NOLINT(bugprone-suspicious-include)
#include "src/deluge/drivers/uart/uart.c" // NOLINT(bugprone-suspicious-include)

static unsigned picReadPosition(void) {
	return (unsigned)(rxBufferReadAddr[UART_ITEM_PIC] - pic_rx);
}

static void receivePIC(uint8_t value) {
	assert(uartRingSpace(sizeof(pic_rx), picReadPosition(), pic_write) > 0);
	pic_rx[pic_write] = (char)value;
	pic_write = (pic_write + 1) % sizeof(pic_rx);
	dma.CRDA_n = (uintptr_t)(pic_rx + pic_write);
}

static void settleOLED(void) {
	oledRoutine();
	timer += msToSlowTimerCount(3);
	oledRoutine();
}

static void expectInput(uint8_t expected) {
	char value = 0;
	assert(uartGetChar(UART_ITEM_PIC, &value));
	assert((uint8_t)value == expected);
}

static void testHandshakeTiming(void) {
	// A committed OLED transaction holds CV until deselection completes.
	enqueueCVMessage(0, 0x31123400);
	cvSPITransferComplete(0);
	static uint8_t frame[768];
	oledFrameQueue[0] = frame;
	oledFrameQueueWritePos = 1;
	sendSPITransferFromQueue();
	receivePIC(OLED_MESSAGE_SELECT); // A stale ack cannot satisfy a request not yet sent.
	oledRoutine();
	assert(pic_message == OLED_MESSAGE_SELECT);
	assert(picReadPosition() == pic_write && pic_rx_queue.count == 0 && !oled_settling);
	enqueueCVMessage(1, 0x32567800);
	assert(spi.SPDR.LONG == 0x31123400); // Cannot interrupt a SELECT already sent.

	// Ack processing bypasses deferred UI input without disturbing release ordering.
	receivePIC(252); // NEXT_PAD_OFF
	receivePIC(42);
	receivePIC(OLED_MESSAGE_SELECT);
	oledRoutine();
	assert(picReadPosition() == pic_write && pic_rx_queue.count == 2);
	timer = msToSlowTimerCount(3) - 1;
	oledRoutine();
	assert(oledFrameQueueReadPos == 0);
	timer++;
	oledRoutine();
	assert(oledFrameQueueReadPos == 1 && dma.N0TB_n == sizeof(frame));
	oledTransferComplete(0);
	oledRoutine();
	assert(pic_message == OLED_MESSAGE_DESELECT); // No separate 10 ms dispatch wait.

	// Keep the settling requirement across timer wrap; duplicate acks must not extend it.
	timer = 65500;
	receivePIC(OLED_MESSAGE_DESELECT);
	oledRoutine();
	timer = (uint16_t)(65500 + msToSlowTimerCount(3) - 1);
	receivePIC(OLED_MESSAGE_DESELECT);
	oledRoutine();
	assert(spi.SPDR.LONG == 0x31123400);
	timer++;
	oledRoutine();
	assert(spi.SPDR.LONG == 0x32567800);
	cvSPITransferComplete(0);
	assert(!spiBusCurrentlySending);

	expectInput(252);
	expectInput(42);
	assert(pic_rx_queue.count == 0);
}

static void queueFrame(void) {
	static uint8_t frame[768];
	oledFrameQueue[oledFrameQueueReadPos] = frame;
	oledFrameQueueWritePos = (oledFrameQueueReadPos + 1) & (OLED_FRAME_QUEUE_SIZE - 1);
	sendSPITransferFromQueue();
}

static void testRecoveryDuringSettling(void) {
	queueFrame();
	oledRoutine();
	receivePIC(OLED_MESSAGE_SELECT);
	oledRoutine();
	assert(oled_settling);

	// freezeWithError handles its own transfer and resets these public fields
	// on resume, without completing the interrupted settling interval.
	oledWaitingForMessage = OLED_MESSAGE_NONE;
	spiBusCurrentlySending = false;
	timer = (uint16_t)(oled_settle_time + 1);
	unsigned frame_read = oledFrameQueueReadPos;
	queueFrame();
	oledRoutine(); // Commit a fresh SELECT after recovery.
	oledRoutine(); // The expired old deadline must not start this frame's DMA.
	assert(oledWaitingForMessage == OLED_MESSAGE_SELECT);
	assert(oledFrameQueueReadPos == frame_read);

	receivePIC(OLED_MESSAGE_SELECT);
	oledRoutine();
	timer += msToSlowTimerCount(3) - 1;
	oledRoutine();
	assert(oledFrameQueueReadPos == frame_read);
	++timer;
	oledRoutine();
	assert(oledFrameQueueReadPos == ((frame_read + 1) & (OLED_FRAME_QUEUE_SIZE - 1)));
	oledTransferComplete(0);
	oledRoutine();
	receivePIC(OLED_MESSAGE_DESELECT);
	settleOLED();
}

static void testRepeatedHandshakes(void) {
	receivePIC(252);
	receivePIC(42);
	// More acknowledgements than the DMA ring can hold, with no panel reads.
	for (unsigned attempt = 0; attempt < 200; ++attempt) {
		queueFrame();
		oledRoutine();
		assert(pic_message == OLED_MESSAGE_SELECT);
		receivePIC(OLED_MESSAGE_SELECT);
		settleOLED();
		oledTransferComplete(0);
		oledRoutine();
		assert(pic_message == OLED_MESSAGE_DESELECT);
		receivePIC(OLED_MESSAGE_DESELECT);
		settleOLED();
		assert(picReadPosition() == pic_write && pic_rx_queue.count == 2);
	}
	expectInput(252);
	expectInput(42);
}

static void testCommittedHandshake(bool lose_ack) {
	// The reported boundary: 55 deferred bytes leave eight free DMA slots;
	// SELECT's ack plus six more input bytes would leave only one free slot.
	for (unsigned index = 0; index < 55; ++index) {
		receivePIC(index);
	}
	queueFrame();
	oledRoutine();
	assert(pic_message == OLED_MESSAGE_SELECT);
	enqueueCVMessage(0, 0x319abc00);
	receivePIC(OLED_MESSAGE_SELECT);
	for (unsigned index = 55; index < 61; ++index) {
		receivePIC(index);
	}
	settleOLED();
	if (lose_ack) {
		// More input arrives after SELECT, exhausting the admission reserve.
		for (unsigned index = 0; index < 144; ++index) {
			receivePIC(70);
			oledRoutine();
		}
		assert(uartGetRxBufferSpace(UART_ITEM_PIC) == 0);
	}
	oledTransferComplete(0);
	pic_message = 0;
	oledRoutine();
	assert(pic_message == OLED_MESSAGE_DESELECT);
	if (lose_ack) {
		pic_message = 0;
		timer += msToSlowTimerCount(50);
		oledRoutine();
		assert(pic_message == OLED_MESSAGE_DESELECT);
	}
	receivePIC(OLED_MESSAGE_DESELECT);
	settleOLED();
	assert(spi.SPDR.LONG == 0x319abc00 && dac_selected);
	cvSPITransferComplete(0);
	assert(!spiBusCurrentlySending);
	for (unsigned index = 0; index < 61; ++index) {
		expectInput(index);
	}
	if (lose_ack) {
		for (unsigned index = 0; index < 144; ++index) {
			expectInput(70);
		}
	}
	assert(pic_rx_queue.count == 0 && picReadPosition() == pic_write);
}

static void testStagingBackpressure(void) {
	// Preserve a full DMA ring of staging headroom before committing SELECT.
	for (unsigned batch = 0; batch < 4; ++batch) {
		for (unsigned index = 0; index < 48; ++index) {
			receivePIC(42);
		}
		oledRoutine();
	}
	queueFrame();
	pic_message = 0;
	oledRoutine();
	assert(pic_message == 0 && oledPendingMessageToSend == OLED_MESSAGE_SELECT);
	enqueueCVMessage(0, 0x31123400);
	oledRoutine();
	assert(spi.SPDR.LONG == 0x31123400 && dac_selected);
	cvSPITransferComplete(0);
	while (pic_rx_queue.count) {
		expectInput(42);
	}
	oledRoutine();
	assert(pic_message == OLED_MESSAGE_SELECT);
	receivePIC(OLED_MESSAGE_SELECT);
	settleOLED();
	oledTransferComplete(0);
	oledRoutine();
	receivePIC(OLED_MESSAGE_DESELECT);
	settleOLED();
}

static void testInputReplay(void) {
	receivePIC(7);
	expectInput(7); // Direct DMA read before any OLED service.
	uartPutCharBack(UART_ITEM_PIC);
	receivePIC(42);
	receivePIC(OLED_MESSAGE_SELECT);
	assert(uartReplacePendingByte(UART_ITEM_PIC, OLED_MESSAGE_SELECT, OLED_MESSAGE_ACK_CONSUMED));
	expectInput(7);
	uartPutCharBack(UART_ITEM_PIC); // Replay a staged event after UI deferral.
	receivePIC(43);
	oledRoutine();
	expectInput(7);
	expectInput(42);
	expectInput(43);
}

static void testFullStaging(void) {
	for (unsigned batch = 0; batch < 5; ++batch) {
		for (unsigned index = 0; index < 51; ++index) {
			receivePIC(42);
		}
		oledRoutine();
	}
	assert(pic_rx_queue.count == 255);
	// Keep one reserved slot for insertion, including read/put-back replay.
	uartInsertFakeChar(UART_ITEM_PIC, 99);
	expectInput(99);
	uartPutCharBack(UART_ITEM_PIC);
	expectInput(99);
	receivePIC(252);
	receivePIC(43);
	receivePIC(OLED_MESSAGE_DESELECT);
	assert(uartReplacePendingByte(UART_ITEM_PIC, OLED_MESSAGE_DESELECT, OLED_MESSAGE_ACK_CONSUMED));
	assert(pic_rx_queue.count == 255); // No overwrite when staging is full.
	for (unsigned index = 0; index < 255; ++index) {
		expectInput(42);
	}
	oledRoutine();
	expectInput(252);
	expectInput(43);
	assert(picReadPosition() == pic_write && pic_rx_queue.count == 0);
}

int main(void) {
	dma.CRDA_n = (uintptr_t)pic_rx;
	testHandshakeTiming();
	testRepeatedHandshakes();
	testCommittedHandshake(false);
	testCommittedHandshake(true);
	testStagingBackpressure();
	testInputReplay();
	testFullStaging();
	testRecoveryDuringSettling();
	puts("OLED handshake driver tests passed");
}
