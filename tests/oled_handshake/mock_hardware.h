#ifndef OLED_HANDSHAKE_TEST_MOCK_HARDWARE_H
#define OLED_HANDSHAKE_TEST_MOCK_HARDWARE_H
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "src/deluge/drivers/uart/rx_buffer.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// Match the production register macros, callbacks, and extern symbols exactly.
// NOLINTBEGIN(readability-identifier-naming)
// The production driver uses this value in #if, so it must remain a macro.
#define ALPHA_OR_BETA_VERSION 1 // NOLINT(cppcoreguidelines-macro-to-enum,modernize-macro-to-enum)

enum {
	NUM_PHYSICAL_CV_CHANNELS = 2,
	OLED_FRAME_QUEUE_SIZE = 32,
	OLED_MAIN_HEIGHT_PIXELS = 48,
	OLED_MAIN_WIDTH_PIXELS = 128,
	SPI_CHANNEL_CV = 0,
	SPI_CHANNEL_OLED_MAIN = 0,
	OLED_SPI_DMA_CHANNEL = 4,
	INTC_ID_SPRI0 = 10,
	TIMER_SYSTEM_SLOW = 4,
};
#define FREEZE_WITH_ERROR(message) assert(!(message))
enum {
	DMAC_CHCTRL_0S_CLRTC = 1,
	DMAC_CHCTRL_0S_SETEN = 2,
	OLED_MESSAGE_NONE = 256,
	OLED_MESSAGE_SELECT = 248,
	OLED_MESSAGE_DESELECT = 249,
	UART_ITEM_PIC = 0,
};
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
} dma;
#define RSPI(channel) spi
#define DMACn(channel) dma
static uint16_t timer;
static uint16_t* TCNT[] = {&timer, &timer, &timer, &timer, &timer};
volatile bool spiBusCurrentlySending;
volatile uint8_t oledFrameQueueReadPos;
uint8_t oledFrameQueueWritePos;
const uint8_t* oledFrameQueue[OLED_FRAME_QUEUE_SIZE];
static int critical_depth;
static void ENTER_CRITICAL_SECTION(void) {
	++critical_depth;
}
static void EXIT_CRITICAL_SECTION(void) {
	assert(critical_depth > 0);
	--critical_depth;
}
static void setupAndEnableInterrupt(void (*handler)(uint32_t), int irq, int priority) {
	(void)handler;
	(void)irq;
	(void)priority;
}
static void invalidate_range_all_caches(uint32_t start, uint32_t end) {
	(void)start;
	(void)end;
}
static unsigned msToSlowTimerCount(unsigned ms) {
	return ms * 33;
}
static bool dac_selected;
static void setOutputState(int port, int pin, bool value) {
	(void)port;
	(void)pin;
	dac_selected = (bool)!value;
}
// These tests observe SPI/handshake behavior, not the gate callback signature.
#define cvSent(...) ((void)0)
static unsigned pic_message;
#define bufferPICUart(message) (pic_message = (message))
enum {
	OLED_MESSAGE_ACK_CONSUMED = 255,
};
static uint8_t pic_rx[64];
static unsigned pic_read, pic_write;
static uint32_t uartGetRxBufferSpace(int item) {
	(void)item;
	return uartRingSpace(sizeof(pic_rx), pic_read, pic_write);
}
static bool uartReplacePendingByte(int item, uint8_t value, uint8_t replacement) {
	(void)item;
	return uartReplaceByteInRing(pic_rx, sizeof(pic_rx), pic_read, pic_write, value, replacement);
}
void cvSPITransferComplete(uint32_t sense);
void oledTransferComplete(uint32_t sense);

// The independent PIC-backlog fix may also be present; this suite has no backlog.
static int32_t uartGetTxBufferFullnessByItem(int item) {
	(void)item;
	return 0;
}

// NOLINTEND(readability-identifier-naming)
#endif // OLED_HANDSHAKE_TEST_MOCK_HARDWARE_H
