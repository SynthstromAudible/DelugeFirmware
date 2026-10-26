#include "mock_hardware.h"
#include "src/RZA1/oled/oled_low_level.c"

static void reset(void) {
	cvPendingChannels = 0;
	cvNextChannel = 0;
	spiBusCurrentlySending = false;
	cv_sending = oled_sending = false;
	oledFrameQueueReadPos = oledFrameQueueWritePos = 0;
	memset(oledFrameQueue, 0, sizeof(oledFrameQueue));
	oledWaitingForMessage = OLED_MESSAGE_NONE;
	oledPendingMessageToSend = 0;
	oledDmaWatchdogArmed = false;
	timer = notificationCount = criticalDepth = 0;
}

int main(void) {
	// Both channels survive a busy OLED bus; replacing CV1 cannot erase CV2.
	reset();
	spiBusCurrentlySending = true;
	enqueueCVMessage(0, 0x31111100);
	enqueueCVMessage(1, 0x32222200);
	enqueueCVMessage(0, 0x31333300);
	assert(notificationCount == 0);
	oledDeselectionComplete();
	assert(spi.SPDR.LONG == 0x31333300);
	assert(notificationCount == 1 && notified[0] == 0);
	cvSPITransferComplete(0);
	assert(spi.SPDR.LONG == 0x32222200);
	assert(notificationCount == 2 && notified[1] == 1);
	cvSPITransferComplete(0);
	assert(!spiBusCurrentlySending);

	// A new update on the channel in flight must not starve the other channel.
	reset();
	enqueueCVMessage(0, 0x31111100);
	enqueueCVMessage(0, 0x31222200);
	enqueueCVMessage(1, 0x32333300);
	cvSPITransferComplete(0);
	assert(spi.SPDR.LONG == 0x32333300);
	cvSPITransferComplete(0);
	assert(spi.SPDR.LONG == 0x31222200);
	cvSPITransferComplete(0);
	assert(!spiBusCurrentlySending && criticalDepth == 0);
	puts("OLED/CV driver tests passed");
}
