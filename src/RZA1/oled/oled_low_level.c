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

#include "OSLikeStuff/timers_interrupts/timers_interrupts.h"
#include "RZA1/cache/cache.h"
#include "RZA1/compiler/asm/inc/asm.h"
#include "RZA1/gpio/gpio.h"
#include "RZA1/mtu/mtu.h"
#include "RZA1/uart/sio_char.h"
#include "deluge/drivers/dmac/dmac.h"
#include "deluge/drivers/rspi/rspi.h"
#include "deluge/processing/engines/cv_engine_c_interface.h"
#include "deluge/util/cfunctions.h"

void setupSPIInterrupts()
{
    setupAndEnableInterrupt(cvSPITransferComplete, INTC_ID_SPRI0 + SPI_CHANNEL_CV * 3, 5);
}

// Priority CV channel: CV words bypass the OLED frame queue so gate timing isn't held
// hostage by OLED refreshes on the shared SPI bus. A single latest-wins slot matches the queue's
// previous coalescing of unsent CV words.
volatile bool cvPriorityPending  = false;
volatile uint32_t cvPriorityData = 0;

void sendCVWord(uint32_t message);
bool sendPriorityCVIfPending();

void enqueueCVMessage(int channel, uint32_t message)
{
    ENTER_CRITICAL_SECTION();
    // Latest value wins (matches the queue's old coalescing of unsent CV words).
    cvPriorityData    = message;
    cvPriorityPending = true;
    // If the bus is idle, kick it off now; otherwise it gets drained at the next inter-transfer
    // boundary, ahead of any queued OLED frames.
    if (!spiBusCurrentlySending)
    {
        spiBusCurrentlySending = true;
        sendPriorityCVIfPending();
    }
    EXIT_CRITICAL_SECTION();
}

int oledWaitingForMessage    = OLED_MESSAGE_NONE; // 256 means none.
int oledPendingMessageToSend = 0; // 0 means none. The purpose of this variable is to ensure thread safety.
int last_message_sent        = 0;
uint16_t oledMessageTimeoutTime;
// Watchdog for a lost OLED DMA transfer-complete interrupt (see oledRoutine).
uint16_t oledDmaWatchdogTime;
bool oledDmaWatchdogArmed = false;
// these get optimized out, set em to volatile if you need to keep them for debugging
bool oled_sending = false;
bool cv_sending   = false;

static void armOledDmaWatchdog()
{
    oledDmaWatchdogArmed = true;
    oledDmaWatchdogTime  = *TCNT[TIMER_SYSTEM_SLOW] + msToSlowTimerCount(50);
}

// Call this before you routinely call uartFlushIfNotSending().
void oledRoutine()
{
    if (oledPendingMessageToSend)
    {
        oledWaitingForMessage    = oledPendingMessageToSend; // We'll wait to hear back from the PIC.
        oledPendingMessageToSend = 0;
sendMessageToPIC:
        last_message_sent      = oledWaitingForMessage;
        oledMessageTimeoutTime = *TCNT[TIMER_SYSTEM_SLOW] + msToSlowTimerCount(50);
        bufferPICUart(oledWaitingForMessage);
    }

    else if (oledWaitingForMessage != OLED_MESSAGE_NONE)
    {
        int16_t howLate = *TCNT[TIMER_SYSTEM_SLOW] - oledMessageTimeoutTime;
        if (howLate >= 0)
        {
            // oledLowLevelTimerCallback(); // No need to actually set the delay timer - we've already waited for ages -
            // any further delay would be pointless.
            oledMessageTimeoutTime = *TCNT[TIMER_SYSTEM_SLOW] + msToSlowTimerCount(50);
            last_message_sent      = oledWaitingForMessage;
            bufferPICUart(oledWaitingForMessage);
        }
    }

    // The retries above only re-send the PIC UART SELECT/DESELECT handshake. Nothing re-drives a
    // lost OLED DMA transfer-complete interrupt (oledTransferComplete, a priority-13 DMA IRQ).
    // Under heavy CV-DAC interrupt traffic (priority 5) on the shared SPI bus that completion can
    // be missed, leaving spiBusCurrentlySending stuck true forever: enqueueOLEDFrame
    // then dedups every identical frame and the physical OLED freezes while rendering/sysex keep
    // running (issue #3670).
    //
    // oled_sending with waiting==NONE && pending==0 means an OLED DMA was kicked but hasn't
    // completed (cv_sending is excluded so we never disturb an in-flight CV word). If that state
    // persists far longer than any real transfer (sub-millisecond), treat the completion interrupt
    // as lost and re-drive it.
    else if (oled_sending && spiBusCurrentlySending && oledPendingMessageToSend == 0)
    {
        if (!oledDmaWatchdogArmed)
        {
            armOledDmaWatchdog();
        }
        else if ((int16_t)(*TCNT[TIMER_SYSTEM_SLOW] - oledDmaWatchdogTime) >= 0)
        {
            oledDmaWatchdogArmed = false;
            ENTER_CRITICAL_SECTION();
            // Re-check under lock - the real interrupt may have landed in the meantime.
            if (oled_sending && spiBusCurrentlySending && oledWaitingForMessage == OLED_MESSAGE_NONE
                && oledPendingMessageToSend == 0)
            {
                // Clear the transfer-complete flag so a late/duplicate IRQ can't double-advance, then
                // run the completion path the lost interrupt should have run.
                DMACn(OLED_SPI_DMA_CHANNEL).CHCTRL_n |= DMAC_CHCTRL_0S_CLRTC;
                oledTransferComplete(0);
            }
            EXIT_CRITICAL_SECTION();
        }
    }
    else
    {
        oledDmaWatchdogArmed = false;
    }
}

void oledSelectingComplete()
{
    oledWaitingForMessage = OLED_MESSAGE_NONE;
#if ALPHA_OR_BETA_VERSION
    if (oledFrameQueue[oledFrameQueueReadPos] == NULL)
    {
        FREEZE_WITH_ERROR("OLED frame queue slot is empty");
    }
#endif
    RSPI(SPI_CHANNEL_OLED_MAIN).SPDCR       = 0x20u;              // 8-bit
    RSPI(SPI_CHANNEL_OLED_MAIN).SPCMD0      = 0b0000011100000010; // 8-bit
    RSPI(SPI_CHANNEL_OLED_MAIN).SPBFCR.BYTE = 0b01100000;         // 0b00100000;
    // DMACn(OLED_SPI_DMA_CHANNEL).CHCFG_n = 0b00000000001000000000001001101000 | (OLED_SPI_DMA_CHANNEL & 7);

    int transferSize                      = (OLED_MAIN_HEIGHT_PIXELS >> 3) * OLED_MAIN_WIDTH_PIXELS;
    DMACn(OLED_SPI_DMA_CHANNEL).N0TB_n    = transferSize; // TODO: only do this once?
    uint32_t dataAddress                  = (uint32_t)oledFrameQueue[oledFrameQueueReadPos];
    DMACn(OLED_SPI_DMA_CHANNEL).N0SA_n    = dataAddress;
    oledFrameQueue[oledFrameQueueReadPos] = NULL;
    oledFrameQueueReadPos                 = (oledFrameQueueReadPos + 1) & (OLED_FRAME_QUEUE_SIZE - 1);
    // todo - should only need a flush
    invalidate_range_all_caches(dataAddress, dataAddress + transferSize);
    armOledDmaWatchdog();
    DMACn(OLED_SPI_DMA_CHANNEL).CHCTRL_n |=
        DMAC_CHCTRL_0S_CLRTC | DMAC_CHCTRL_0S_SETEN; // ---- Enable DMA Transfer and clear TC bit ----
}

// Physically pushes one CV word onto the shared SPI peripheral: 32-bit mode, select the DAC, enable the
// RX-complete interrupt, then write the word (which kicks off the transfer). Used by the CV priority path.
void sendCVWord(uint32_t message)
{
    setOutputState(6, 1, false); // Select CV DAC

    RSPI(SPI_CHANNEL_OLED_MAIN).SPDCR  = 0x60u;              // 32-bit
    RSPI(SPI_CHANNEL_OLED_MAIN).SPCMD0 = 0b0000001100000010; // 32-bit
    RSPI(SPI_CHANNEL_OLED_MAIN).SPBFCR.BYTE =
        0b00110010; // 0b11110010;//0b01100010;//0b00100010; // Now we do *not* reset the RX buffer.

    RSPI(SPI_CHANNEL_OLED_MAIN).SPCR |= 1 << 7; // Receive interrupt enable

    RSPI(SPI_CHANNEL_CV).SPDR.LONG = message;
}

// Drains the priority CV slot if one is waiting. Must only be called at an inter-transfer boundary
// (nothing currently on the bus). Returns true if a CV word was sent. Sends the word then calls
// cvSent() so any gate waiting on this CV is released on schedule.
bool sendPriorityCVIfPending()
{
    if (!cvPriorityPending)
    {
        return false;
    }
    cvPriorityPending = false;
    cv_sending        = true;
    sendCVWord(cvPriorityData);
    cvSent();
    return true;
}

void initiateSelectingOled()
{
#if ALPHA_OR_BETA_VERSION

    if (oledWaitingForMessage != OLED_MESSAGE_NONE)
    {
        FREEZE_WITH_ERROR("OLED message already pending");
    }
#endif
    oledPendingMessageToSend = OLED_MESSAGE_SELECT;

    // Actual queue position gets moved along in oledSelectingComplete() when that gets called.
}

void sendSPITransferFromQueue()
{
    spiBusCurrentlySending = true;

    // A waiting CV word jumps ahead of any queued OLED frames.
    if (sendPriorityCVIfPending())
    {
        return;
    }
#if ALPHA_OR_BETA_VERSION
    if (oledFrameQueue[oledFrameQueueReadPos] == NULL)
    {
        FREEZE_WITH_ERROR("OLED frame queue slot is empty");
    }
#endif

    // The queue only ever holds OLED frames now.
    oled_sending = true;
    initiateSelectingOled();
}

void oledTransferComplete(uint32_t int_sense)
{
    oledDmaWatchdogArmed = false;

    // If there's another frame queued, just go ahead - unless a CV word is waiting, in which case we
    // deselect so the CV can slip in ahead of the next frame.
    if (!cvPriorityPending && oledFrameQueueWritePos != oledFrameQueueReadPos
        && last_message_sent == OLED_MESSAGE_SELECT)
    {
        oledSelectingComplete();
    }

    // Otherwise, deselect OLED. And when that's finished, we might send some more if there is more.
    else
    {
        oledPendingMessageToSend = OLED_MESSAGE_DESELECT;
    }
}

void cvSPITransferComplete(uint32_t sense)
{

    RSPI(SPI_CHANNEL_OLED_MAIN).SPCR &= ~(1 << 7); // Receive interrupt disable

    setOutputState(6, 1,
        true); // Deselect CV DAC. We do it here, nice and early, since we might be re-selecting it very soon in
               // sendPriorityCVIfPending(),
    // and a real pulse does need to be sent. This should be safe - I even tried only deselecting it in the instruction
    // preceding the re-selection, and it did work.

    RSPI(SPI_CHANNEL_OLED_MAIN).SPBFCR.BYTE |=
        1 << 6; // Reset RX buffer. Slightly weird that we have to do it here, after the transfer? Or not weird?

    // Another CV word turned up while this one was sending? Send it before returning to OLED frames
    //.
    if (sendPriorityCVIfPending())
    {
        return;
    }

    // If there's a queued OLED frame, resume sending it; otherwise the bus goes idle.
    if (oledFrameQueueWritePos != oledFrameQueueReadPos)
    {
        oled_sending = true;
        cv_sending   = false;
        initiateSelectingOled();
    }
    else
    {
        spiBusCurrentlySending = false;
        cv_sending             = false;
    }
}

void oledDeselectionComplete()
{
    oledWaitingForMessage  = OLED_MESSAGE_NONE;
    spiBusCurrentlySending = false;
    oled_sending           = false;
    // A waiting CV word goes out before any more OLED frames.
    if (cvPriorityPending)
    {
        spiBusCurrentlySending = true;
        sendPriorityCVIfPending();
        return;
    }
    // There might be something more to send now...
    if (oledFrameQueueWritePos != oledFrameQueueReadPos)
    {
        sendSPITransferFromQueue();
    }
}

void oledLowLevelTimerCallback()
{
    if (oledWaitingForMessage == OLED_MESSAGE_SELECT)
        oledSelectingComplete();
    else
        oledDeselectionComplete();
}
