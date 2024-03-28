/*
 * Copyright © 2021-2023 Synthstrom Audible Limited
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

#include "RZA1/oled/oled_low_level.h"
#include "definitions.h"
#include "deluge/drivers/oled/oled.h"

#include "RZA1/compiler/asm/inc/asm.h"
#include "RZA1/gpio/gpio.h"
#include "RZA1/mtu/mtu.h"
#include "RZA1/uart/sio_char.h"
#include "deluge/drivers/dmac/dmac.h"
#include "deluge/drivers/rspi/rspi.h"
#include "deluge/util/cfunctions.h"
#include "timers_interrupts.h"

#define OLED_CODE_FOR_CV 1

void setupSPIInterrupts()
{
    setupAndEnableInterrupt(cvSPITransferComplete, INTC_ID_SPRI0 + SPI_CHANNEL_CV * 3, 5);
}

void enqueueCVMessage(int channel, uint32_t message)
{
    enqueueSPITransfer(OLED_CODE_FOR_CV, (uint8_t const*)message);
}

int oledWaitingForMessage    = 256; // 256 means none.
int oledPendingMessageToSend = 0;   // 0 means none. The purpose of this variable is to ensure thread safety.

uint16_t oledMessageTimeoutTime;

// Call this before you routinely call uartFlushIfNotSending().
void oledRoutine()
{
    if (oledPendingMessageToSend)
    {
        oledWaitingForMessage    = oledPendingMessageToSend; // We'll wait to hear back from the PIC.
        oledPendingMessageToSend = 0;
sendMessageToPIC:
        oledMessageTimeoutTime = *TCNT[TIMER_SYSTEM_SLOW] + msToSlowTimerCount(50);
        bufferPICUart(oledWaitingForMessage);
    }

    else if (oledWaitingForMessage != 256)
    {
        int16_t howLate = *TCNT[TIMER_SYSTEM_SLOW] - oledMessageTimeoutTime;
        if (howLate >= 0)
        {
            // oledLowLevelTimerCallback(); // No need to actually set the delay timer - we've already waited for ages -
            // any further delay would be pointless.
            goto sendMessageToPIC;
        }
    }
}

void oledSelectingComplete()
{
    oledWaitingForMessage                   = 256;
    RSPI(SPI_CHANNEL_OLED_MAIN).SPDCR       = 0x20u;              // 8-bit
    RSPI(SPI_CHANNEL_OLED_MAIN).SPCMD0      = 0b0000011100000010; // 8-bit
    RSPI(SPI_CHANNEL_OLED_MAIN).SPBFCR.BYTE = 0b01100000;         // 0b00100000;
    // DMACn(OLED_SPI_DMA_CHANNEL).CHCFG_n = 0b00000000001000000000001001101000 | (OLED_SPI_DMA_CHANNEL & 7);

    int transferSize                   = (OLED_MAIN_HEIGHT_PIXELS >> 3) * OLED_MAIN_WIDTH_PIXELS;
    DMACn(OLED_SPI_DMA_CHANNEL).N0TB_n = transferSize; // TODO: only do this once?
    uint32_t dataAddress               = (uint32_t)spiTransferQueue[spiTransferQueueReadPos].dataAddress;
    DMACn(OLED_SPI_DMA_CHANNEL).N0SA_n = dataAddress;
    spiTransferQueueReadPos            = (spiTransferQueueReadPos + 1) & (SPI_TRANSFER_QUEUE_SIZE - 1);
    v7_dma_flush_range(dataAddress, dataAddress + transferSize);
    DMACn(OLED_SPI_DMA_CHANNEL).CHCTRL_n |=
        DMAC_CHCTRL_0S_CLRTC | DMAC_CHCTRL_0S_SETEN; // ---- Enable DMA Transfer and clear TC bit ----
}

int spiDestinationSendingTo;

void sendCVTransfer()
{
    setOutputState(6, 1, false); // Select CV DAC

    RSPI(SPI_CHANNEL_OLED_MAIN).SPDCR  = 0x60u;              // 32-bit
    RSPI(SPI_CHANNEL_OLED_MAIN).SPCMD0 = 0b0000001100000010; // 32-bit
    RSPI(SPI_CHANNEL_OLED_MAIN).SPBFCR.BYTE =
        0b00110010; // 0b11110010;//0b01100010;//0b00100010; // Now we do *not* reset the RX buffer.

    RSPI(SPI_CHANNEL_OLED_MAIN).SPCR |= 1 << 7; // Receive interrupt enable

    uint32_t message = (uint32_t)spiTransferQueue[spiTransferQueueReadPos].dataAddress;

    spiTransferQueueReadPos =
        (spiTransferQueueReadPos + 1)
        & (SPI_TRANSFER_QUEUE_SIZE - 1); // Must do this before actually sending SPI, cos the interrupt could come
                                         // before we do this otherwise. True story.

    RSPI(SPI_CHANNEL_CV).SPDR.LONG = message;
}

void initiateSelectingOled()
{
    oledPendingMessageToSend = 248;

    // Actual queue position gets moved along in oledSelectingComplete() when that gets called.
}

void sendSPITransferFromQueue()
{

    spiTransferQueueCurrentlySending = true;

    spiDestinationSendingTo = spiTransferQueue[spiTransferQueueReadPos].destinationId;

    // If it's OLED data...
    if (spiDestinationSendingTo == 0)
    {
        initiateSelectingOled();
    }

    // Or if it's CV...
    else
    {
        sendCVTransfer();
    }
}

void oledTransferComplete(uint32_t int_sense)
{

    // If anything else to send, and it's to the OLED again, then just go ahead.
    if (spiTransferQueueWritePos != spiTransferQueueReadPos
        && spiTransferQueue[spiTransferQueueReadPos].destinationId == 0)
    {
        oledSelectingComplete();
    }

    // Otherwise, deselect OLED. And when that's finished, we might send some more if there is more.
    else
    {
        oledPendingMessageToSend = 249;
    }
}

void cvSPITransferComplete(uint32_t sense)
{

    RSPI(SPI_CHANNEL_OLED_MAIN).SPCR &= ~(1 << 7); // Receive interrupt disable

    setOutputState(6, 1,
        true); // Deselect CV DAC. We do it here, nice and early, since we might be re-selecting it very soon in
               // sendCVTransfer(),
    // and a real pulse does need to be sent. This should be safe - I even tried only deselecting it in the instruction
    // preceding the re-selection, and it did work.

    RSPI(SPI_CHANNEL_OLED_MAIN).SPBFCR.BYTE |=
        1 << 6; // Reset RX buffer. Slightly weird that we have to do it here, after the transfer? Or not weird?

    // If anything else to send, and it's to the CV DAC again, then just go ahead.
    if (spiTransferQueueWritePos != spiTransferQueueReadPos
        && spiTransferQueue[spiTransferQueueReadPos].destinationId == OLED_CODE_FOR_CV)
    {
        sendCVTransfer();
    }

    // Otherwise...
    else
    {
        // And, if there's some OLED data waiting to send, do that.
        if (spiTransferQueueWritePos != spiTransferQueueReadPos)
        {
            spiDestinationSendingTo = 0;
            initiateSelectingOled();
        }

        // Otherwise, we're all done.
        else
        {
            spiTransferQueueCurrentlySending = false;
        }
    }
}

void oledDeselectionComplete()
{
    oledWaitingForMessage            = 256;
    spiTransferQueueCurrentlySending = false;

    // There might be something more to send now...
    if (spiTransferQueueWritePos != spiTransferQueueReadPos)
    {
        sendSPITransferFromQueue();
    }
}

void oledLowLevelTimerCallback()
{
    if (oledWaitingForMessage == 248)
        oledSelectingComplete();
    else
        oledDeselectionComplete();
}
