#include "mock_hardware.h"
#include "src/RZA1/oled/oled_low_level.c"

int main(void) {
	static uint8_t frame[768];
	const uint32_t firstCV = 0x31123400;
	const uint32_t secondCV = 0x31567800;
	oledFrameQueue[0] = frame;
	oledFrameQueueWritePos = 1;
	sendSPITransferFromQueue();

	// A grid redraw on the PIC UART must leave SELECT cancellable by CV.
	picTxFullness = 392;
	oledRoutine();
	assert(picMessage == 0 && oledWaitingForMessage == OLED_MESSAGE_NONE);
	assert(oledPendingMessageToSend == OLED_MESSAGE_SELECT);
	enqueueCVMessage(0, firstCV);
	assert(spi.SPDR.LONG == firstCV && dacSelected);
	assert(oledPendingMessageToSend == 0 && !oled_sending);
	assert(oledFrameQueueReadPos == 0 && oledFrameQueue[0] == frame);

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
	picTxFullness = 0;
	oledRoutine();
	assert(picMessage == OLED_MESSAGE_SELECT && oledWaitingForMessage == OLED_MESSAGE_SELECT);
	enqueueCVMessage(0, firstCV);
	assert(spi.SPDR.LONG == secondCV && !dacSelected); // SELECT is now committed.
	oledSelectingComplete();                           // Simulate completed SELECT acknowledgement and settling.
	assert(oledFrameQueueReadPos == 1 && dma.N0TB_n == sizeof(frame));
	oledTransferComplete(0);

	// Never defer DESELECT behind this guard: the bus is already held.
	picTxFullness = 392;
	oledRoutine();
	assert(picMessage == OLED_MESSAGE_DESELECT && oledWaitingForMessage == OLED_MESSAGE_DESELECT);
	assert(spi.SPDR.LONG == secondCV);
	oledDeselectionComplete();
	assert(spi.SPDR.LONG == firstCV && dacSelected);
	cvSPITransferComplete(0);
	assert(!spiBusCurrentlySending && !dacSelected && criticalDepth == 0);
	puts("OLED SELECT/CV preemption tests passed");
}
