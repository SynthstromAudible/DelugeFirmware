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

/// Peripheral-mode bulk-IN drain bound.
///
/// A receive is armed for one 64-byte packet, but the peripheral bulk pipe is double-buffered, so its BRDY
/// handler can find more than the armed packet already ACKed by the hardware. It drains up to this many bytes
/// in one go rather than dropping what arrived (see usb_pstd_brdy_pipe_process_rohan_midi()) -- which means it
/// writes past the armed transfer length. The BSP's receive buffer must therefore be at least this large.
#define USB_MIDI_RECEIVE_DRAIN_MAX_PACKETS 3
#define USB_MIDI_RECEIVE_DRAIN_MAX_BYTES   (USB_MIDI_RECEIVE_DRAIN_MAX_PACKETS * 64)

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
