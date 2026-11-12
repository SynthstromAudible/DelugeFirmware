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

// USB audio (UAC1) as a peripheral: one stream each way, alongside USB MIDI - stereo both ways, or multitrack (below).
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

/* Multitrack: an optional configuration (SETTINGS > USB AUDIO > MODE), in place of the stereo one. The Deluge sends the
 * host 8 channels - its main mix, and three stereo buses that tracks can be routed to - and the host sends it 4: two
 * stereo inputs, the first working just as the stereo configuration's does. Same endpoints, pipes and interfaces, so
 * all the code is shared.
 *
 * Its size is set by full speed's isochronous bandwidth. The host reserves time for each isochronous endpoint at its
 * max packet size, including worst-case bit stuffing (USB 2.0 5.11.3: 7268ns + 83.54ns x floor(3.167 + 7 x 8 x bytes /
 * 6), plus about 1us of host delay), and only 900us of each 1ms frame can be reserved like that. Nor can a full speed
 * isochronous packet be over 1023 bytes. 8 channels of 45 frames in 4-byte subframes, like the stereo configuration's,
 * would be 1440 bytes, and packed 24-bit 1080 - neither's possible. So samples are 16-bit:
 * - To the host, 8 channels x 2 bytes x at most 45 frames: 720 bytes, 570us. 44.1kHz needs packets of 44 or 45, so
 *   framesForNextPacket() never sends more than 45, only speeding up on what would have been 44-frame packets
 * - From the host, 4 channels x 2 bytes x 46 frames: 368 bytes, 294us (360 bytes, 288us, for the adaptive setting).
 *   A host following our clock through implicit feedback sends what we sent it, so 45 at most, and one sending at its
 *   own rate 44 or 45 - the 46th frame is only slack
 * - 864us together, inside the 900us but with only enough left over for something small, like a keyboard's interrupt
 *   endpoint, sharing the bus (or a hub's transaction translator). 6 channels to the host would leave 165us
 * 8 channels is also the most Windows' audio stack handles in shared mode. Every frame is still a whole number of
 * 32-bit words (16 and 8 bytes), as the FIFO DMA needs */
#define USB_PAUDIO_MT_CHANNELS_TO_HOST 8
#define USB_PAUDIO_MT_CHANNELS_FROM_HOST 4
#define USB_PAUDIO_MT_SUBFRAME_BYTES 2
#define USB_PAUDIO_MT_BIT_RESOLUTION 16
#define USB_PAUDIO_MT_BYTES_PER_FRAME_TO_HOST (USB_PAUDIO_MT_CHANNELS_TO_HOST * USB_PAUDIO_MT_SUBFRAME_BYTES)
#define USB_PAUDIO_MT_BYTES_PER_FRAME_FROM_HOST (USB_PAUDIO_MT_CHANNELS_FROM_HOST * USB_PAUDIO_MT_SUBFRAME_BYTES)
#define USB_PAUDIO_MT_MAX_FRAMES_TO_HOST 45
#define USB_PAUDIO_MT_MAX_FRAMES_FROM_HOST 46
// Smaller, so that hosts choosing between alternate settings of the same format by bandwidth prefer it if they can't
// use the asynchronous one - as with the stereo configuration's
#define USB_PAUDIO_MT_ADAPTIVE_MAX_FRAMES_FROM_HOST 45
#define USB_PAUDIO_MT_MAX_PACKET_BYTES_TO_HOST                                                                         \
	(USB_PAUDIO_MT_MAX_FRAMES_TO_HOST * USB_PAUDIO_MT_BYTES_PER_FRAME_TO_HOST)
#define USB_PAUDIO_MT_MAX_PACKET_BYTES_FROM_HOST                                                                       \
	(USB_PAUDIO_MT_MAX_FRAMES_FROM_HOST * USB_PAUDIO_MT_BYTES_PER_FRAME_FROM_HOST)
#define USB_PAUDIO_MT_ADAPTIVE_MAX_PACKET_BYTES_FROM_HOST                                                              \
	(USB_PAUDIO_MT_ADAPTIVE_MAX_FRAMES_FROM_HOST * USB_PAUDIO_MT_BYTES_PER_FRAME_FROM_HOST)

/* The pipes' buffers in the USB controller (r_usb_peptable.c) are sized for whichever configuration needs more, in
 * whole 64-byte blocks, each double buffered */
#define USB_PAUDIO_MAX_OF(a, b) ((a) > (b) ? (a) : (b))
#define USB_PAUDIO_PIPE_BUF_BYTES(bytes) (((bytes) + 63) / 64 * 64)
#define USB_PAUDIO_FROM_HOST_PIPE_BUF_BYTES                                                                            \
	USB_PAUDIO_PIPE_BUF_BYTES(USB_PAUDIO_MAX_OF(USB_PAUDIO_MAX_PACKET_BYTES, USB_PAUDIO_MT_MAX_PACKET_BYTES_FROM_HOST))
#define USB_PAUDIO_TO_HOST_PIPE_BUF_BYTES                                                                              \
	USB_PAUDIO_PIPE_BUF_BYTES(USB_PAUDIO_MAX_OF(USB_PAUDIO_MAX_PACKET_BYTES, USB_PAUDIO_MT_MAX_PACKET_BYTES_TO_HOST))

/* Which configuration descriptor the host gets - usbAudioStartPeripheral() picks it */
#define USB_PAUDIO_CONFIG_MIDI_ONLY 0
#define USB_PAUDIO_CONFIG_STEREO 1
#define USB_PAUDIO_CONFIG_MULTITRACK 2
