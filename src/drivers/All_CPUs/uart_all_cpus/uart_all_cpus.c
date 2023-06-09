/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "definitions.h"
#include "uart_all_cpus.h"
#include "dmac.h"
#include "sio_char.h"
#include <string.h>

#include "scif_iodefine.h"
#include "devdrv_intc.h"

#include "RTT/SEGGER_RTT.h"

void uartPrintln(char const* output)
{
#if ENABLE_TEXT_OUTPUT
#if HAVE_RTT
    SEGGER_RTT_WriteString(0, output);
    SEGGER_RTT_WriteString(0, "\r\n");
#else
    while (*output)
    {
        bufferMIDIUart(*output);
        output++;
    }
    bufferMIDIUart('\n');
    uartFlushIfNotSending(UART_ITEM_MIDI);
#endif
#endif
}

void uartPrintNumber(int number)
{
#if ENABLE_TEXT_OUTPUT
    char buffer[12];
    intToString(number, buffer, 1);
    uartPrintln(buffer);
#endif
}

void uartPrintNumberSameLine(int number)
{
#if ENABLE_TEXT_OUTPUT
    char buffer[12];
    intToString(number, buffer, 1);
    uartPrint(buffer);
#endif
}

void uartPrint(char const* output)
{
#if ENABLE_TEXT_OUTPUT
#if HAVE_RTT
    SEGGER_RTT_WriteString(0, output);
#else
    while (*output)
    {
        bufferMIDIUart(*output);
        output++;
    }
    uartFlushIfNotSending(UART_ITEM_MIDI);
#endif
#endif
}

void uartPrintFloat(float number)
{
#if ENABLE_TEXT_OUTPUT
    char buffer[12];
    intToString((int)roundf(number * 100), buffer, 3);
    int length         = strlen(buffer);
    char temp          = buffer[length - 2];
    buffer[length - 2] = 0;
    uartPrint(buffer);
    uartPrint(".");
    buffer[length - 2] = temp;
    uartPrint(&buffer[length - 2]);
#endif
}

void uartPrintlnFloat(float number)
{
#if ENABLE_TEXT_OUTPUT
    uartPrintFloat(number);
#if HAVE_RTT
    SEGGER_RTT_WriteString(0, "\r\n");
#else
    bufferMIDIUart('\n');
    uartFlushIfNotSending(UART_ITEM_MIDI);
#endif
#endif
}

struct UartItem uartItems[NUM_UART_ITEMS] __attribute__((aligned(CACHE_LINE_SIZE)));

extern const uint8_t uartChannels[];
extern const uint16_t txBufferSizes[];
extern const uint8_t txDmaChannels[];
extern char_t* const txBuffers[];
extern char_t* const rxBuffers[];
extern char_t* rxBufferReadAddr[];
extern const uint16_t rxBufferSizes[];
extern const uint16_t txBufferSizes[];
extern const uint8_t rxDmaChannels[];
extern char_t const timingCaptureItems[];
extern uint16_t const timingCaptureBufferSizes[];
extern uint32_t* const timingCaptureBuffers[];
extern const void (*txInterruptFunctions[])(uint32_t);
extern const uint8_t txInterruptPriorities[];
extern const uint32_t* const uartRxLinkDescriptors[];
extern uint8_t const timingCaptureDMAChannels[];
extern const uint32_t* const timingCaptureLinkDescriptors[];
extern const bool_t uartItemIsScim[];

#define DMA_SCIF_TX_CONFIG (0b00000000001000000000000001101000 | DMA_AM_FOR_SCIF)
#define DMA_SCIM_TX_CONFIG                                                                                             \
    (0b00000000001000000000000000101000                                                                                \
        | DMA_AM_FOR_SCIM) // LVL is 0 for SCIM despite the datasheet saying it should be 1 - a misprint, I believe

// Returns whether it sent anything
// Warning - this can get called from a timer ISR, or a DMA ISR, or no ISR
int uartFlush(int item)
{

    int num = uartItems[item].txBufferWritePos - uartItems[item].txBufferReadPosAfterTransfer;
    if (!num) return 0;

    int fullNum = num & (txBufferSizes[item] - 1);

    uint32_t newConfig = DMA_SCIF_TX_CONFIG;

    // If region to send reaches the rightmost end of the circular buffer...
    if (num < 0)
    {
        num = txBufferSizes[item] - uartItems[item].txBufferReadPosAfterTransfer;

        int numLeft = fullNum - num;

        // If there are also some further bytes starting from the left of the circular buffer that we want to send as well, set that up to happen automatically
        if (numLeft)
        {
            DMACn(txDmaChannels[item]).N1TB_n = numLeft;
            newConfig |=
                DMAC_CHCFG_0S_REN | DMAC_CHCFG_0S_RSW
                | DMAC_CHCFG_0S_DEM; // Set to switch to next "register" after first transaction, and also mask interrupt
        }
    }
    DMACn(txDmaChannels[item]).CHCFG_n = newConfig | (txDmaChannels[item] & 7);

    int prevReadPos = uartItems[item].txBufferReadPosAfterTransfer;
    uartItems[item].txBufferReadPosAfterTransfer =
        (uartItems[item].txBufferReadPosAfterTransfer + fullNum) & (txBufferSizes[item] - 1);
    uartItems[item].shouldDoConsecutiveTransferAfter = false; // Only actually applies to MIDI

    DMACn(txDmaChannels[item]).N0TB_n = num;
    DMACn(txDmaChannels[item]).N0SA_n = (uint32_t)&txBuffers[item][prevReadPos];

    return 1;
}

// Warning - this will sometimes (not always) be called in a timer ISR
void uartFlushIfNotSending(int item)
{

    if (!uartItems[item].txSending)
    {
        // There should be no way the DMA TX-complete interrupt could occur in this bit, cos we could only be here if it'd already completed and set txSending to 0...
        int sentAny = uartFlush(item);

        if (sentAny)
        {
            uartItems[item].txSending = 1;
            DMACn(txDmaChannels[item]).CHCTRL_n |=
                DMAC_CHCTRL_0S_CLRTC | DMAC_CHCTRL_0S_SETEN; // ---- Enable DMA Transfer and clear TC bit ----
        }
    }

    // Applies to MIDI only - if sending was already happening, take note that we want to send additional stuff as soon as that's done.
    // For PIC, this always happens anyway
    else
    {
        // WARNING - it'd be a problem if the DMA TX-finished interrupt occurred right here...
        // And when not using volatiles, we also could end up here if that interrupt occurred shortly before the if statement above at the start
        uartItems[item].shouldDoConsecutiveTransferAfter = 1;
    }
}

int uartGetTxBufferFullnessByItem(int item)
{
    return (uartItems[item].txBufferWritePos - uartItems[item].txBufferReadPos) & (txBufferSizes[item] - 1);
}

int uartGetTxBufferSpace(int item)
{
    return txBufferSizes[item] - uartGetTxBufferFullnessByItem(item);
}

void uartPutCharBack(int item)
{
    int readPos            = (uint32_t)rxBufferReadAddr[item] - ((uint32_t)rxBuffers[item]);
    readPos                = (readPos - 1) & (rxBufferSizes[item] - 1);
    rxBufferReadAddr[item] = rxBuffers[item] + readPos;
}

void uartInsertFakeChar(int item, char_t data)
{
    int readPos                                        = (uint32_t)rxBufferReadAddr[item] - ((uint32_t)rxBuffers[item]);
    readPos                                            = (readPos - 1) & (rxBufferSizes[item] - 1);
    rxBufferReadAddr[item]                             = rxBuffers[item] + readPos;
    *(rxBufferReadAddr[item] + UNCACHED_MIRROR_OFFSET) = data;
}

uint8_t uartGetChar(int item, char_t* readData)
{

    char_t const* currentWritePos = (char_t*)DMACnNonVolatile(rxDmaChannels[item])
                                        .CRDA_n; // We deliberately don't go (volatile uint32_t*) here, for speed

    if (currentWritePos == rxBufferReadAddr[item]) return 0;

    *readData = *(rxBufferReadAddr[item] + UNCACHED_MIRROR_OFFSET);

    int readPos            = (uint32_t)rxBufferReadAddr[item] - ((uint32_t)rxBuffers[item]);
    readPos                = (readPos + 1) & (rxBufferSizes[item] - 1);
    rxBufferReadAddr[item] = rxBuffers[item] + readPos;

    return 1;
}

uint32_t* uartGetCharWithTiming(int timingCaptureItem, char_t* readData)
{

    int item = timingCaptureItems[timingCaptureItem];

    char_t const* currentWritePos = (char_t*)DMACnNonVolatile(rxDmaChannels[item])
                                        .CRDA_n; // We deliberately don't go (volatile uint32_t*) here, for speed

    if (currentWritePos == rxBufferReadAddr[item]) return NULL;

    *readData = *(rxBufferReadAddr[item] + UNCACHED_MIRROR_OFFSET);

    int readPos = (uint32_t)rxBufferReadAddr[item] - ((uint32_t)rxBuffers[item]);

    uint32_t* timer =
        (uint32_t*)((uint32_t)&timingCaptureBuffers[timingCaptureItem]
                                                   [readPos & (timingCaptureBufferSizes[timingCaptureItem] - 1)]
                    + UNCACHED_MIRROR_OFFSET);

    readPos                = (readPos + 1) & (rxBufferSizes[item] - 1);
    rxBufferReadAddr[item] = rxBuffers[item] + readPos;

    return timer;
}

// Warning - obviously this gets called in a DMA ISR
// This is the function which seems to cause a crash if called via interrupt during the above one
void tx_interrupt(int item)
{

    uartItems[item].txBufferReadPos = uartItems[item].txBufferReadPosAfterTransfer;

    // May want to try sending a consecutive transfer
    if (item != UART_ITEM_MIDI || uartItems[item].shouldDoConsecutiveTransferAfter)
    {

        uartItems[item].shouldDoConsecutiveTransferAfter = 0;

        int sentAny = uartFlush(item);

        if (sentAny)
        {
            DMACn(txDmaChannels[item]).CHCTRL_n =
                DMAC_CHCTRL_0S_CLRTC | DMAC_CHCTRL_0S_SETEN
                | DMAC_CHCTRL_0S_CLREND; // ---- Enable DMA Transfer and clear TC bit ----
            return;
        }
    }

    // If nothing sent...
    uartItems[item].txSending = 0;

    // Clear Transfer End Bit Status - but hey we actually don't need to do this to clear the interrupt - provided we're not going to be reading these flags later
    //*g_dma_reg_tbl[txDmaChannels[item]].chctrl |= (DMAC0_CHCTRL_n_CLREND | DMAC0_CHCTRL_n_CLRTC);
}

// (I think) this has to be called after Uart initialized, otherwise problem when booting from flash
void initUartDMA()
{

    // For each Uart item...
    int item;
    for (item = 0; item < NUM_UART_ITEMS; item++)
    {

        bool_t isScim = 0;

        uartItems[item].txBufferWritePos                 = 0;
        uartItems[item].txBufferReadPos                  = 0;
        uartItems[item].txBufferReadPosAfterTransfer     = 0;
        uartItems[item].txSending                        = 0;
        uartItems[item].shouldDoConsecutiveTransferAfter = 0;

        int sciChannel = uartChannels[item];

        // Set up TX DMA channel -----------------------------------------------------------------------
        int txDmaChannel = txDmaChannels[item];

        // ---- DMA Control Register Setting ----
        DCTRLn(txDmaChannel) = 0;

        uint32_t destinationRegister = (uint32_t)&SCIFA(sciChannel).FTDR.BYTE;

        // ---- DMA Next0 Address Setting ----
        DMACn(txDmaChannel).N0DA_n = destinationRegister;

        // ---- DMA Next1 Address Setting ----
        DMACn(txDmaChannel).N1SA_n = (uint32_t)txBuffers[item];
        DMACn(txDmaChannel).N1DA_n = destinationRegister;

        // ----- Transmission Side Setting ----
        DMACn(txDmaChannel).CHCFG_n = (DMA_SCIF_TX_CONFIG) | (txDmaChannel & 7);

        // ---- DMA Expansion Resource Selector Setting ----
        unsigned int dmarsTX = (DMARS_FOR_SCIF0_TX) + (sciChannel << 2);
        setDMARS(txDmaChannel, dmarsTX);

        // ---- DMA Channel Interval Register Setting ----
        DMACn(txDmaChannel).CHITVL_n = 0;

        // ---- DMA Channel Extension Register Setting ----
        DMACn(txDmaChannel).CHEXT_n = 0;

        // ---- Software Reset and clear TC bit ----
        DMACn(txDmaChannel).CHCTRL_n |= DMAC_CHCTRL_0S_SWRST | DMAC_CHCTRL_0S_CLRTC;

        // TX DMA-finished interrupt
        R_INTC_RegistIntFunc(DMA_INTERRUPT_0 + txDmaChannel, txInterruptFunctions[item]);
        R_INTC_SetPriority(DMA_INTERRUPT_0 + txDmaChannel, txInterruptPriorities[item]);
        R_INTC_Enable(DMA_INTERRUPT_0 + txDmaChannel);

        // Set up RX DMA channel -----------------------------------------------------------------------
        int rxDmaChannel     = rxDmaChannels[item];
        unsigned int dmarsRX = (DMARS_FOR_SCIF0_RX) + (sciChannel << 2);

        initDMAWithLinkDescriptor(rxDmaChannel, uartRxLinkDescriptors[item], dmarsRX);
        dmaChannelStart(rxDmaChannel);

        SCIFA(sciChannel).SCSCR = 0x00F0u; // Enable "interrupt" (which actually triggers DMA)
    }

    // Set up MIDI RX timing-capture DMA channel -----------------------------------------------------------------------
    int i;
    for (i = 0; i < NUM_TIMING_CAPTURE_ITEMS; i++)
    {
        int uartItem   = timingCaptureItems[i];
        int dmaChannel = timingCaptureDMAChannels[i];

        unsigned int dmarsRX = DMARS_FOR_SCIF0_RX + (uartChannels[uartItem] << 2);
        initDMAWithLinkDescriptor(dmaChannel, timingCaptureLinkDescriptors[i], dmarsRX);
        dmaChannelStart(dmaChannel);
    }
}
