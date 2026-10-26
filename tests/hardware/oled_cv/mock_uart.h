#pragma once

// Register and buffer storage for compiling the real UART driver on the host.
#define UART_ITEM_MIDI 1
#define NUM_UART_ITEMS 2
#define NUM_TIMING_CAPTURE_ITEMS 1
#define CACHE_LINE_SIZE 32
#define UNCACHED_MIRROR_OFFSET 0
#define DMA_AM_FOR_SCIF 0
#define DMAC_CHCFG_0S_REN 1
#define DMAC_CHCFG_0S_RSW 2
#define DMAC_CHCFG_0S_DEM 4
#define DMAC_CHCTRL_0S_CLREND 8
#define DMAC_CHCTRL_0S_SWRST 16
#define DMA_INTERRUPT_0 0
#define DMARS_FOR_SCIF0_TX 0
#define DMARS_FOR_SCIF0_RX 0
#define DMACnNonVolatile(channel) dma
static uint32_t dmaControl;
#define DCTRLn(channel) dmaControl
static struct {
	struct {
		uint8_t BYTE;
	} FTDR;
	uint16_t SCSCR;
} scif;
#define SCIFA(channel) scif
static void setDMARS(int channel, uint32_t value) {
}
static void initDMAWithLinkDescriptor(int channel, const uint32_t* descriptor, uint32_t resource) {
}
static void dmaChannelStart(int channel) {
}

static char picBuffer[1024], midiBuffer[1024];
const uint16_t txBufferSizes[] = {1024, 1024};
char* const txBuffers[] = {picBuffer, midiBuffer};
const uint8_t uartChannels[] = {0, 1};
const uint8_t txDmaChannels[] = {0, 1};
const uint8_t rxDmaChannels[] = {2, 3};
const uint16_t rxBufferSizes[] = {1024, 1024};
char* const rxBuffers[] = {picBuffer, midiBuffer};
char* rxBufferReadAddr[] = {picBuffer, midiBuffer};
const char timingCaptureItems[] = {UART_ITEM_MIDI};
const uint16_t timingCaptureBufferSizes[] = {1};
static uint32_t capturedTime;
uint32_t* const timingCaptureBuffers[] = {&capturedTime};
const void (*txInterruptFunctions[])(uint32_t) = {0, 0};
const uint8_t txInterruptPriorities[] = {0, 0};
const uint32_t* const uartRxLinkDescriptors[] = {0, 0};
const uint8_t timingCaptureDMAChannels[] = {0};
const uint32_t* const timingCaptureLinkDescriptors[] = {0};

static void queuePicBytes(unsigned count) {
	uartItems[UART_ITEM_PIC].txBufferWritePos =
	    (uartItems[UART_ITEM_PIC].txBufferWritePos + count) & (txBufferSizes[UART_ITEM_PIC] - 1);
}
