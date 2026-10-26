#include "mock_hardware.h"
// Include the production implementation to exercise internal state with mock registers.
#include "src/RZA1/oled/oled_low_level.c" // NOLINT(bugprone-suspicious-include)

static void testHandshakeTiming(void) {
	// A committed OLED transaction holds CV until deselection completes.
	enqueueCVMessage(0, 0x31123400);
	cvSPITransferComplete(0);
	static uint8_t frame[768];
	oledFrameQueue[0] = frame;
	oledFrameQueueWritePos = 1;
	sendSPITransferFromQueue();
	pic_rx[0] = OLED_MESSAGE_SELECT; // A stale ack cannot satisfy a request not yet sent.
	pic_write = 1;
	oledRoutine();
	assert(pic_message == OLED_MESSAGE_SELECT);
	assert(pic_rx[0] == OLED_MESSAGE_ACK_CONSUMED && !oled_settling);
	enqueueCVMessage(1, 0x32567800);
	assert(spi.SPDR.LONG == 0x31123400); // Cannot interrupt a SELECT already sent.

	// Ack processing bypasses deferred UI input without disturbing release ordering.
	pic_rx[0] = 252; // NEXT_PAD_OFF
	pic_rx[1] = 42;  // Its corresponding pad event
	pic_rx[2] = OLED_MESSAGE_SELECT;
	pic_write = 3;
	oledRoutine();
	assert(pic_read == 0 && pic_rx[0] == 252 && pic_rx[1] == 42);
	assert(pic_rx[2] == OLED_MESSAGE_ACK_CONSUMED);
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
	pic_rx[3] = OLED_MESSAGE_DESELECT;
	pic_write = 4;
	oledRoutine();
	timer = (uint16_t)(65500 + msToSlowTimerCount(3) - 1);
	pic_rx[4] = OLED_MESSAGE_DESELECT;
	pic_write = 5;
	oledRoutine();
	assert(spi.SPDR.LONG == 0x31123400);
	timer++;
	oledRoutine();
	assert(spi.SPDR.LONG == 0x32567800);
	cvSPITransferComplete(0);
	assert(!spiBusCurrentlySending);

	// A wrapped receive window must leave already-consumed bytes alone.
	uint8_t ring[] = {248, 42, 248, 7, 8, 9, 252, 248};
	assert(uartReplaceByteInRing(ring, 8, 6, 2, 248, 255));
	assert(ring[7] == 255 && ring[0] == 255 && ring[2] == 248);
	assert(ring[6] == 252 && ring[1] == 42);
	assert(!uartReplaceByteInRing(ring, 8, 2, 2, 248, 255));
}

static void testDeferredInputBackpressure(void) {
	// Keep the panel consumer deferred over far more handshakes than fit in
	// the actual 64-byte PIC ring. Backpressure must retain the release pair.
	static uint8_t frame[768];
	pic_rx[0] = 252;
	pic_rx[1] = 42;
	pic_read = 0;
	pic_write = 2;
	unsigned completed = 0;
	for (unsigned attempt = 0; attempt < 200; ++attempt) {
		oledPendingMessageToSend = OLED_MESSAGE_SELECT;
		pic_message = 0;
		oledRoutine();
		if (pic_message == 0) {
			assert(uartGetRxBufferSpace(UART_ITEM_PIC) < 8);
			continue;
		}
		assert(pic_message == OLED_MESSAGE_SELECT);
		oledFrameQueue[oledFrameQueueReadPos] = frame;
		oledFrameQueueWritePos = (oledFrameQueueReadPos + 1) & (OLED_FRAME_QUEUE_SIZE - 1);
		pic_rx[pic_write++] = OLED_MESSAGE_SELECT;
		oledRoutine();
		timer += msToSlowTimerCount(3);
		oledRoutine();
		oledTransferComplete(0);
		oledRoutine();
		assert(pic_message == OLED_MESSAGE_DESELECT);
		pic_rx[pic_write++] = OLED_MESSAGE_DESELECT;
		oledRoutine();
		timer += msToSlowTimerCount(3);
		oledRoutine();
		++completed;
		assert(pic_write < sizeof(pic_rx));
		assert(pic_read == 0 && pic_rx[0] == 252 && pic_rx[1] == 42);
	}
	assert(completed > 0 && completed < 32);
	// Receive backpressure must not make an unsent SELECT block CV output.
	oledFrameQueue[oledFrameQueueReadPos] = frame;
	oledFrameQueueWritePos = (oledFrameQueueReadPos + 1) & (OLED_FRAME_QUEUE_SIZE - 1);
	spiBusCurrentlySending = true;
	enqueueCVMessage(0, 0x319abc00);
	oledRoutine();
	assert(spi.SPDR.LONG == 0x319abc00);
	assert(oledPendingMessageToSend == 0 && !oled_sending);
	cvSPITransferComplete(0);
	assert(oledPendingMessageToSend == OLED_MESSAGE_SELECT);
	oledFrameQueueWritePos = oledFrameQueueReadPos;
}

static void testRetryBackpressure(void) {
	// Retrying a lost ack also respects backpressure, but ack processing and
	// settling continue even when no more requests can safely be sent.
	oledPendingMessageToSend = 0;
	oledWaitingForMessage = OLED_MESSAGE_SELECT;
	pic_write = sizeof(pic_rx) - 7;
	pic_message = 0;
	timer += msToSlowTimerCount(50);
	oledRoutine();
	assert(pic_message == 0); // SELECT retries retain the DESELECT reserve too.
	oledWaitingForMessage = OLED_MESSAGE_DESELECT;
	pic_write = sizeof(pic_rx) - 2;
	pic_message = 0;
	timer += msToSlowTimerCount(50);
	oledRoutine();
	assert(pic_message == 0);
	pic_rx[pic_write++] = OLED_MESSAGE_DESELECT;
	oledRoutine();
	timer += msToSlowTimerCount(3);
	oledRoutine();
	assert(oledWaitingForMessage == OLED_MESSAGE_NONE);
	assert(pic_rx[0] == 252 && pic_rx[1] == 42);

	// Once the input consumer catches up, OLED requests resume, including
	// when the DMA producer has wrapped around the end of the ring.
	pic_read = pic_write;
	assert(uartRingSpace(64, 62, 1) == 60);
	oledPendingMessageToSend = OLED_MESSAGE_SELECT;
	// No acknowledgement is needed to verify that the pending request resumes.
	oledRoutine();
	assert(pic_message == OLED_MESSAGE_SELECT);
}

int main(void) {
	testHandshakeTiming();
	testDeferredInputBackpressure();
	testRetryBackpressure();
	puts("OLED handshake driver tests passed");
}
