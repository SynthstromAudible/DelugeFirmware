/*
 * Copyright © 2026 Synthstrom Audible Limited
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

// USB audio (UAC1) as a peripheral: one stereo stream each way, alongside USB MIDI.
// "Out" and "in" are from the host's point of view, as in the USB spec - so the out stream is audio coming into the
// Deluge from the computer, and the in stream is audio the Deluge sends to the computer.

// Only pipes 1 and 2 can do isochronous transfers
#define USB_CFG_PAUDIO_ISO_OUT (USB_PIPE1)
#define USB_CFG_PAUDIO_ISO_IN (USB_PIPE2)

#define USB_PAUDIO_EP_OUT 3 // Endpoint numbers. MIDI has 1 (in) and 2 (out)
#define USB_PAUDIO_EP_IN 4

#define USB_PAUDIO_INTERFACE_CONTROL 0
#define USB_PAUDIO_INTERFACE_STREAM_OUT 1
#define USB_PAUDIO_INTERFACE_STREAM_IN 2
#define USB_PMIDI_INTERFACE 3
#define USB_PAUDIO_NUM_INTERFACES 4

#define USB_PAUDIO_SAMPLE_RATE 44100
#define USB_PAUDIO_NUM_CHANNELS 2
// 24-bit samples, each in a 4-byte subframe and MSB-aligned - which makes them the same as the Deluge's q31 samples,
// and keeps every packet a whole number of 32-bit words for the FIFO DMA
#define USB_PAUDIO_SUBFRAME_BYTES 4
#define USB_PAUDIO_BIT_RESOLUTION 24
#define USB_PAUDIO_BYTES_PER_FRAME (USB_PAUDIO_NUM_CHANNELS * USB_PAUDIO_SUBFRAME_BYTES)
// 44.1 frames per 1ms frame on average. We never send more than 46 (see framesForNextPacket()), and allow the host up
// to 48
#define USB_PAUDIO_MAX_FRAMES_PER_PACKET 48
#define USB_PAUDIO_MAX_PACKET_BYTES (USB_PAUDIO_MAX_FRAMES_PER_PACKET * USB_PAUDIO_BYTES_PER_FRAME)
// The out stream's adaptive alternate setting (for hosts without implicit feedback, like Windows) gets a slightly
// smaller max packet size - still plenty for a host sending 44 or 45 frames at its own rate. Hosts choosing between
// alternate settings of the same format by bandwidth then prefer it, if they can't use the asynchronous one
#define USB_PAUDIO_ADAPTIVE_MAX_FRAMES_PER_PACKET 46
#define USB_PAUDIO_ADAPTIVE_MAX_PACKET_BYTES (USB_PAUDIO_ADAPTIVE_MAX_FRAMES_PER_PACKET * USB_PAUDIO_BYTES_PER_FRAME)
