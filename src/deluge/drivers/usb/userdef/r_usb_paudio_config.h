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

// USB audio (UAC1) as a peripheral: one 4-channel stream each way, alongside USB MIDI.
// "Out" and "in" are from the host's point of view, as in the USB spec - so the out stream is audio coming into the
// Deluge from the computer, and the in stream is audio the Deluge sends to the computer.
//
// Channels from the host: 1/2 go straight to the Deluge's outputs, 3/4 are an input for audio tracks and the sampler.
// Channels to the host: 1/2 are the Deluge's line/mic input, 3/4 its main mix.

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
#define USB_PAUDIO_NUM_CHANNELS 4
// Sizes are set by full speed's bandwidth. The host reserves time for each isochronous endpoint at its max packet
// size, including an allowance for worst-case bit stuffing (USB 2.0 5.11.3), and only 90% of each 1ms frame - 900us -
// can be reserved like this. Both streams have to fit:
// - Samples are 24-bit, packed into 3 bytes. 4-byte subframes, as the Deluge's own q31 samples are, wouldn't fit
// - Packets are at most 45 frames. 44.1kHz needs packets of 44 or 45, so that's the smallest it can be. At 12 bytes a
//   frame, 540 bytes takes 428.6us, so 857us for both streams - also within the 1157 bytes a frame a hub's transaction
//   translator allows. 48 frames, 456.6us each, didn't fit: hosts couldn't open the second stream
// framesForNextPacket() never sends more than 45, and a host following our clock through implicit feedback sends us no
// more than we send it. A 12-byte frame also keeps every packet a whole number of 32-bit words for the FIFO DMA
#define USB_PAUDIO_SUBFRAME_BYTES 3
#define USB_PAUDIO_BIT_RESOLUTION 24
#define USB_PAUDIO_BYTES_PER_FRAME (USB_PAUDIO_NUM_CHANNELS * USB_PAUDIO_SUBFRAME_BYTES)
#define USB_PAUDIO_MAX_FRAMES_PER_PACKET 45
#define USB_PAUDIO_MAX_PACKET_BYTES (USB_PAUDIO_MAX_FRAMES_PER_PACKET * USB_PAUDIO_BYTES_PER_FRAME)
// Each audio pipe's buffer in the USB controller, a whole number of its 64-byte blocks: 9, double buffered. The pipe
// table (r_usb_peptable.c) puts the from-host pipe's at block 8 and the to-host pipe's at 26, then MIDI's bulk in at 44
#define USB_PAUDIO_PIPE_BUF_BYTES ((USB_PAUDIO_MAX_PACKET_BYTES + 63) / 64 * 64)
