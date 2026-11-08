#ifndef OLED_HANDSHAKE_TEST_MOCK_HARDWARE_H
#define OLED_HANDSHAKE_TEST_MOCK_HARDWARE_H
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "src/deluge/drivers/uart/rx_buffer.h"
#include "src/deluge/drivers/uart/uart.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// Match the production register macros, callbacks, and extern symbols exactly.
// NOLINTBEGIN(readability-identifier-naming)
// This must remain a macro: the included production driver tests it with #if.
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
static int critical_depth;
static void ENTER_CRITICAL_SECTION(void) {
	++critical_depth;
}
static void EXIT_CRITICAL_SECTION(void) {
	assert(critical_depth > 0);
	--critical_depth;
}
#define setupAndEnableInterrupt(handler, irq, priority) ((void)0)
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

// Register and buffer storage for compiling the real UART driver on the host.
enum {
	UART_ITEM_MIDI = 1,
	NUM_UART_ITEMS = 2,
	NUM_TIMING_CAPTURE_ITEMS = 1,
	CACHE_LINE_SIZE = 32,
	UNCACHED_MIRROR_OFFSET = 0,
	DMA_AM_FOR_SCIF = 0,
	DMAC_CHCFG_0S_REN = 1,
	DMAC_CHCFG_0S_RSW = 2,
	DMAC_CHCFG_0S_DEM = 4,
	DMAC_CHCTRL_0S_CLREND = 8,
	DMAC_CHCTRL_0S_SWRST = 16,
	DMA_INTERRUPT_0 = 0,
	DMARS_FOR_SCIF0_TX = 0,
	DMARS_FOR_SCIF0_RX = 0,
};
#define DMACnNonVolatile(channel) dma
static uint32_t dma_control;
#define DCTRLn(channel) dma_control
static struct {
	struct {
		uint8_t BYTE;
	} FTDR;
	uint16_t SCSCR;
} scif;
#define SCIFA(channel) scif
static void setDMARS(int channel, uint32_t value) {
	(void)channel;
	(void)value;
}
static void initDMAWithLinkDescriptor(int channel, const uint32_t* descriptor, uint32_t resource) {
	(void)channel;
	(void)descriptor;
	(void)resource;
}
static void dmaChannelStart(int channel) {
	(void)channel;
}

static char pic_buffer[1024], midi_buffer[1024];
const uint16_t txBufferSizes[] = {1024, 1024};
char* const txBuffers[] = {pic_buffer, midi_buffer};
const uint8_t uartChannels[] = {0, 1};
const uint8_t txDmaChannels[] = {0, 1};
const uint8_t rxDmaChannels[] = {2, 3};
static char pic_rx[64], midi_rx[64];
static unsigned pic_write;
const uint16_t rxBufferSizes[] = {64, 64};
char* const rxBuffers[] = {pic_rx, midi_rx};
char* rxBufferReadAddr[] = {pic_rx, midi_rx};
const char timingCaptureItems[] = {UART_ITEM_MIDI};
const uint16_t timingCaptureBufferSizes[] = {1};
static uint32_t captured_time;
uint32_t* const timingCaptureBuffers[] = {&captured_time};
// Match the legacy UART declaration, including its qualified void return.
const void (*txInterruptFunctions[])(uint32_t) = {0, 0}; // NOLINT(clang-diagnostic-ignored-qualifiers)
const uint8_t txInterruptPriorities[] = {0, 0};
const uint32_t* const uartRxLinkDescriptors[] = {0, 0};
const uint8_t timingCaptureDMAChannels[] = {0};
const uint32_t* const timingCaptureLinkDescriptors[] = {0};

#define bufferPICUart(message) (pic_message = (message))
void cvSPITransferComplete(uint32_t sense);
void oledTransferComplete(uint32_t sense);

// Reserved marker used only by the acknowledgement scanner.
enum {
	OLED_MESSAGE_ACK_CONSUMED = 255,
};

// NOLINTEND(readability-identifier-naming)

#endif // OLED_HANDSHAKE_TEST_MOCK_HARDWARE_H
