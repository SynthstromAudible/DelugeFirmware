#pragma once
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "src/deluge/drivers/uart/uart.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define NUM_PHYSICAL_CV_CHANNELS 2
#define OLED_FRAME_QUEUE_SIZE 32
#define OLED_MAIN_HEIGHT_PIXELS 48
#define OLED_MAIN_WIDTH_PIXELS 128
#define SPI_CHANNEL_CV 0
#define SPI_CHANNEL_OLED_MAIN 0
#define OLED_SPI_DMA_CHANNEL 4
#define INTC_ID_SPRI0 10
#define TIMER_SYSTEM_SLOW 4
#define ALPHA_OR_BETA_VERSION 1
#define FREEZE_WITH_ERROR(message) assert(!message)
#define DMAC_CHCTRL_0S_CLRTC 1
#define DMAC_CHCTRL_0S_SETEN 2
#define OLED_MESSAGE_NONE 256
#define OLED_MESSAGE_SELECT 248
#define OLED_MESSAGE_DESELECT 249
#define UART_ITEM_PIC 0
struct {
	unsigned SPDCR, SPCMD0, SPCR;
	struct {
		unsigned BYTE;
	} SPBFCR;
	struct {
		uint32_t LONG;
	} SPDR;
} spi;
struct {
	uint32_t N0TB_n, N0SA_n, CHCTRL_n;
	uint32_t N1TB_n, CHCFG_n, N0DA_n, N1SA_n, N1DA_n, CHITVL_n, CHEXT_n;
	uintptr_t CRDA_n;
} dma;
#define RSPI(channel) spi
#define DMACn(channel) dma
static uint16_t timer;
static uint16_t* TCNT[] = {&timer, &timer, &timer, &timer, &timer};
volatile bool spiBusCurrentlySending;
volatile uint8_t oledFrameQueueReadPos;
uint8_t oledFrameQueueWritePos;
const uint8_t* oledFrameQueue[OLED_FRAME_QUEUE_SIZE];
static int criticalDepth;
static void ENTER_CRITICAL_SECTION(void) {
	++criticalDepth;
}
static void EXIT_CRITICAL_SECTION(void) {
	assert(criticalDepth > 0);
	--criticalDepth;
}
#define setupAndEnableInterrupt(handler, irq, priority) ((void)0)
static void invalidate_range_all_caches(uint32_t start, uint32_t end) {
}
static unsigned msToSlowTimerCount(unsigned ms) {
	return ms * 33;
}
static bool dacSelected;
static void setOutputState(int port, int pin, bool value) {
	dacSelected = !value;
}
// These tests observe SPI/handshake behavior, not the gate callback signature.
#define cvSent(...) ((void)0)
static unsigned picMessage;
#include "mock_uart.h"
#define bufferPICUart(message)                                                                                         \
	do {                                                                                                               \
		picMessage = (message);                                                                                        \
		queuePicBytes(1);                                                                                              \
	} while (0)
void cvSPITransferComplete(uint32_t);
void oledTransferComplete(uint32_t);

// This suite drives completed handshakes explicitly, even when the independent
// acknowledgement-scanning fix is included in the build.
#define OLED_MESSAGE_ACK_CONSUMED 255
