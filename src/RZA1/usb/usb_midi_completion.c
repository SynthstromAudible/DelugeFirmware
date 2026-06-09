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

#include "RZA1/usb/usb_midi_completion.h"

// Power of two. The handlers record into this queue inside usb_cstd_usb_task() and
// the BSP drains it immediately after in the same task, so it only ever holds the
// completions from a single poll - a handful at most.
#define USB_MIDI_COMPLETION_QUEUE_LEN  32
#define USB_MIDI_COMPLETION_QUEUE_MASK (USB_MIDI_COMPLETION_QUEUE_LEN - 1)

static UsbMidiCompletion g_completions[USB_MIDI_COMPLETION_QUEUE_LEN];
static uint32_t g_write_idx;
static uint32_t g_read_idx;

void usb_midi_completion_record(UsbMidiCompletionKind kind, uint8_t deviceNum, uint16_t bytesReceived)
{
    if ((g_write_idx - g_read_idx) >= USB_MIDI_COMPLETION_QUEUE_LEN)
    {
        return; // Full - should not happen (drained every poll); drop rather than wrap.
    }
    UsbMidiCompletion* e = &g_completions[g_write_idx & USB_MIDI_COMPLETION_QUEUE_MASK];
    e->kind              = (uint8_t)kind;
    e->deviceNum         = deviceNum;
    e->bytesReceived     = bytesReceived;
    g_write_idx++;
}

bool usb_midi_completion_take(UsbMidiCompletion* out)
{
    if (g_write_idx == g_read_idx)
    {
        return false;
    }
    *out = g_completions[g_read_idx & USB_MIDI_COMPLETION_QUEUE_MASK];
    g_read_idx++;
    return true;
}
