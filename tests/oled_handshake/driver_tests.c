#include "mock_hardware.h"
#include "src/RZA1/oled/oled_low_level.c"

int main(void) {
	// A committed OLED transaction holds CV until deselection completes.
	enqueueCVMessage(0, 0x31123400);
	cvSPITransferComplete(0);
	static uint8_t frame[768];
	oledFrameQueue[0] = frame;
	oledFrameQueueWritePos = 1;
	sendSPITransferFromQueue();
	picRx[0] = OLED_MESSAGE_SELECT; // A stale ack cannot satisfy a request not yet sent.
	picWrite = 1;
	oledRoutine();
	assert(picMessage == OLED_MESSAGE_SELECT);
	assert(picRx[0] == OLED_MESSAGE_ACK_CONSUMED && !oledSettling);
	enqueueCVMessage(1, 0x32567800);
	assert(spi.SPDR.LONG == 0x31123400); // Cannot interrupt a SELECT already sent.

	// Ack processing bypasses deferred UI input without disturbing release ordering.
	picRx[0] = 252; // NEXT_PAD_OFF
	picRx[1] = 42;  // Its corresponding pad event
	picRx[2] = OLED_MESSAGE_SELECT;
	picWrite = 3;
	oledRoutine();
	assert(picRead == 0 && picRx[0] == 252 && picRx[1] == 42);
	assert(picRx[2] == OLED_MESSAGE_ACK_CONSUMED);
	timer = msToSlowTimerCount(3) - 1;
	oledRoutine();
	assert(oledFrameQueueReadPos == 0);
	timer++;
	oledRoutine();
	assert(oledFrameQueueReadPos == 1 && dma.N0TB_n == sizeof(frame));
	oledTransferComplete(0);
	oledRoutine();
	assert(picMessage == OLED_MESSAGE_DESELECT); // No separate 10 ms dispatch wait.

	// Keep the settling requirement across timer wrap; duplicate acks must not extend it.
	timer = 65500;
	picRx[3] = OLED_MESSAGE_DESELECT;
	picWrite = 4;
	oledRoutine();
	timer = (uint16_t)(65500 + msToSlowTimerCount(3) - 1);
	picRx[4] = OLED_MESSAGE_DESELECT;
	picWrite = 5;
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
	puts("OLED handshake driver tests passed");
}
