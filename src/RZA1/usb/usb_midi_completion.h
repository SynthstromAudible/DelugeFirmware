/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

#pragma once

/// HAL-owned USB-MIDI pipe-completion queue.
///
/// The Renesas bulk-IN/-OUT (BRDY/BEMP) handlers record a completion event here
/// rather than calling up into the BSP transport. The BSP polls/drains the queue
/// from its service routine and does the buffer/state work. This keeps the
/// dependency one-directional (BSP -> HAL): the HAL never names BSP or application
/// structures; it only reports that a pipe finished.
///
/// Single-context: the handlers run inside usb_cstd_usb_task() and the BSP drains
/// the queue immediately afterwards in the same task, so a plain ring (no locking)
/// is sufficient.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Size of a device's bulk-IN receive buffer. Receive transfers are armed for one 64-byte packet at a time, but the
/// peripheral-mode bulk pipe is double-buffered, so its BRDY handler can have to drain up to two packets (plus margin)
/// in one go rather than dropping what the hardware already ACKed - see usb_pstd_brdy_pipe_process_rohan_midi().
#define USB_MIDI_RECEIVE_BUFFER_BYTES 192

typedef enum UsbMidiCompletionKind {
    USB_MIDI_COMPLETION_RECEIVE = 0,    ///< bulk-IN done: deviceNum + bytesReceived are set
    USB_MIDI_COMPLETION_SEND_HOST,      ///< bulk-OUT done as host
    USB_MIDI_COMPLETION_SEND_PERIPHERAL ///< bulk-OUT done as peripheral
} UsbMidiCompletionKind;

typedef struct UsbMidiCompletion
{
    uint8_t kind;           ///< UsbMidiCompletionKind
    uint8_t deviceNum;      ///< RECEIVE only: which hosted device
    uint16_t bytesReceived; ///< RECEIVE only: bytes now in that device's buffer
} UsbMidiCompletion;

/// Record a completed transfer. Called by the HAL pipe-completion handlers.
void usb_midi_completion_record(UsbMidiCompletionKind kind, uint8_t deviceNum, uint16_t bytesReceived);

/// Pop the next pending completion into `out`; returns false if the queue is empty.
/// Drained by the BSP transport's service routine.
bool usb_midi_completion_take(UsbMidiCompletion* out);

#ifdef __cplusplus
}
#endif
