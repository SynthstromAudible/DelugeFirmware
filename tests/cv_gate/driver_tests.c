#include "mock_hardware.h"
// Include the production implementation to exercise internal state with mock registers.
#include "src/RZA1/oled/oled_low_level.c" // NOLINT(bugprone-suspicious-include)

static void reset(void) {
	cv_pending_channels = 0;
	cv_next_channel = 0;
	spiBusCurrentlySending = false;
	cv_sending = oled_sending = false;
	oledFrameQueueReadPos = oledFrameQueueWritePos = 0;
	for (unsigned slot = 0; slot < OLED_FRAME_QUEUE_SIZE; ++slot) {
		oledFrameQueue[slot] = NULL;
	}
	oledWaitingForMessage = OLED_MESSAGE_NONE;
	oledPendingMessageToSend = 0;
	oledDmaWatchdogArmed = false;
	timer = notification_count = critical_depth = 0;
}

int main(void) {
	// Both channels survive a busy OLED bus; replacing CV1 cannot erase CV2.
	reset();
	spiBusCurrentlySending = true;
	enqueueCVMessage(0, 0x31111100);
	enqueueCVMessage(1, 0x32222200);
	enqueueCVMessage(0, 0x31333300);
	assert(notification_count == 0);
	oledDeselectionComplete();
	assert(spi.SPDR.LONG == 0x31333300);
	assert(notification_count == 0);
	cvSPITransferComplete(0);
	assert(spi.SPDR.LONG == 0x32222200);
	assert(notification_count == 1 && notified[0] == 0);
	cvSPITransferComplete(0);
	assert(notification_count == 2 && notified[1] == 1);
	assert(!spiBusCurrentlySending);

	// A new update on the channel in flight must not starve the other channel.
	reset();
	enqueueCVMessage(0, 0x31111100);
	enqueueCVMessage(0, 0x31222200);
	enqueueCVMessage(1, 0x32333300);
	cvSPITransferComplete(0);
	assert(spi.SPDR.LONG == 0x32333300);
	assert(notification_count == 0); // CV1's first word is obsolete; do not release its gate.
	cvSPITransferComplete(0);
	assert(spi.SPDR.LONG == 0x31222200);
	assert(notification_count == 1 && notified[0] == 1);
	cvSPITransferComplete(0);
	assert(notification_count == 2 && notified[1] == 0);
	assert(!spiBusCurrentlySending && critical_depth == 0);
	puts("OLED/CV driver tests passed");
}
