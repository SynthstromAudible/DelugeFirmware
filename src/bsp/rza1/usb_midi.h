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

/// BSP-internal USB-MIDI transport.
///
/// This module will own (across phases 3–5 of the USB-MIDI transport relocation,
/// see docs/dev/usb_midi_transport_relocation.md) the per-device RX/TX buffers,
/// the send ring buffer, the Renesas USB transfer descriptors and the send/receive
/// state machine — everything that today is smeared across `midi_engine.cpp` and
/// the HAL `rohan_midi` pipe handlers. It bridges the Renesas USB pipes to the
/// `<libdeluge/midi_io.h>` byte-stream boundary; the application sees only that
/// boundary and deals in MIDI bytes (parsed/produced via the deluge_midi library).
///
/// The module now owns the whole transport: the per-device RX/TX buffers, the
/// send ring and completion counters (`BspUsbMidiDevice`), the Renesas USB
/// transfer descriptors (`g_usb_midi_*_utr`, defined in the .c), the send/receive
/// state machine *and* per-slot connection tracking (maintained from the HAL
/// enumeration paths via the attach/detach notifications below). The application
/// no longer includes this header: it reads and writes USB-MIDI as a byte stream
/// of 4-byte event packets through `<libdeluge/midi_io.h>` (one port per device
/// slot, mapped by the boundary implementation in midi_io.c), and the receive
/// transfers are (re)armed down here as part of the port read.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// USB-MIDI send buffering sizes (relocated from midi_device_manager.h; these are
// transport, not protocol).
//
// size in 32-bit messages. NOTE: increasing this even more doesn't work.
// Looks like a hardware limitation (maybe we more in FS mode)?
#define MIDI_SEND_BUFFER_LEN_INNER 32
// Seems to be the max for a hydrasynth on a usb hub? We should figure out how to find this from the device config but I
// haven't seen anything below this yet. Widi bud's can do 3, both do fine at 16 without a hub involved
#define MIDI_SEND_BUFFER_LEN_INNER_HOST 2

// MUST be an exact power of two
#define MIDI_SEND_BUFFER_LEN_RING 1024
#define MIDI_SEND_RING_MASK (MIDI_SEND_BUFFER_LEN_RING - 1)

/// Per-device USB-MIDI transport state. POD, no application dependencies. The
/// receive DMA writes `receiveData` directly, so an instance must stay in a
/// DMA-capable region (the storage array is placed in SDRAM, as before).
typedef struct BspUsbMidiDevice {
	uint8_t currentlyWaitingToReceive;
	uint8_t sq; // Only for connections as HOST
	uint8_t canHaveMIDISent;
	uint8_t connected; // Non-zero while a device is attached on this slot (the
	                   // send path's "is this an active sink", was cable[0]!=NULL)
	uint16_t numBytesReceived;
	__attribute__((aligned(8))) uint8_t receiveData[64];

	// This buffer is passed directly to the USB driver, and is limited to what the hardware allows
	uint8_t dataSendingNow[MIDI_SEND_BUFFER_LEN_INNER * 4];
	// This will show a value after the general flush function is called, throughout other Devices being sent to before
	// this one, and until we've completed our send
	uint8_t numBytesSendingNow;

	// This is a ring buffer for data waiting to be sent which doesn't fit the smaller buffer above.
	uint32_t sendDataRingBuf[MIDI_SEND_BUFFER_LEN_RING];
	uint32_t ringBufWriteIdx;
	uint32_t ringBufReadIdx;
} BspUsbMidiDevice;

/// Zero the USB-MIDI transport state and initialise the send transfer
/// descriptors. Backs `deluge_midi_init()`; runs before the USB stack is opened.
void bsp_usb_midi_init(void);

// --- Send path (BSP-owned: the send ring, the Renesas bulk-OUT pipe driving and
// the send-completion state machine). The boundary write queues pre-packed
// USB-MIDI event words on a slot's ring; the flush moves the bytes.

/// Flush buffered USB-MIDI output toward the wire if the pipe is idle (host: all
/// connected devices; peripheral: the upstream device). Backs
/// `deluge_midi_flush()` for USB ports (any USB port flushes the whole
/// controller). Safe to call from the MIDI routine or the gate-output interrupt.
void bsp_usb_midi_flush_output(void);

/// Send-completion entry points, called by the HAL bulk-OUT (BEMP) handlers when a
/// transfer finishes: chain the next queued transfer for the device(s). Host and
/// peripheral variants.
void bsp_usb_midi_send_complete_as_host(int32_t ip);
void bsp_usb_midi_send_complete_as_peripheral(int32_t ip);

// --- Receive path (BSP-owned: the bulk-IN transfer setup + the receive DMA
// buffers). The HAL bulk-IN (BRDY) handlers write the received bytes straight into
// the device's receiveData and record the completion; the boundary read drains the
// buffer and (re)arms the next transfer.

/// Record a completed bulk-IN transfer for a device: `bytesReceived` bytes are now
/// in its receiveData buffer, and it is no longer waiting to receive. Dispatched
/// from the HAL pipe-completion queue by bsp_usb_midi_service().
void bsp_usb_midi_receive_complete(uint8_t ip, uint8_t deviceNum, uint16_t bytesReceived);

/// Pump the USB-MIDI send/receive state machine and drain HAL pipe-completion
/// events. Backs `deluge_midi_service()`.
void bsp_usb_midi_service(void);

// --- Connection tracking (maintained from the HAL enumeration paths). The HAL
// calls these alongside the application's hosted/peripheral callbacks; they reset
// the slot's transport counters + sequence bit and keep its `connected` flag —
// state the application used to maintain through its ConnectedUSBMIDIDevice.

/// A hosted device was configured on slot (ip, d): reset its transport state
/// (sq, counters) and mark it a live send sink.
void bsp_usb_midi_device_attached(uint8_t ip, uint8_t d);

/// A hosted device on slot (ip, d) detached: it is no longer a send sink.
void bsp_usb_midi_device_detached(uint8_t ip, uint8_t d);

/// We were configured as a peripheral: slot (ip, 0) is the upstream connection.
/// Resets its transport state and the controller's send-in-progress flag.
void bsp_usb_midi_peripheral_configured(uint8_t ip);

/// The upstream host detached us.
void bsp_usb_midi_peripheral_detached(uint8_t ip);

/// Whether device slot (ip, d) currently has a live (enumerated) endpoint.
/// Backs `deluge_midi_port_connected()` for USB ports.
bool bsp_usb_midi_device_connected(uint8_t ip, uint8_t d);

/// USB role queries. On this board the USB controller is the MIDI-device
/// transport, so its host/peripheral role surfaces through the MIDI boundary.
/// `bsp_usb_midi_is_host()` is true when the controller hosts devices (they plug
/// into the Deluge); `bsp_usb_midi_peripheral_connected()` is true when, as a
/// peripheral, a host has enumerated us. These replace the app reading the USB
/// mode globals (g_usb_usbmode / g_usb_peri_connected) directly.
bool bsp_usb_midi_is_host(void);
bool bsp_usb_midi_peripheral_connected(void);

// --- The byte-stream surface backing <libdeluge/midi_io.h> for USB ports. The
// boundary implementation (midi_io.c) maps a DelugeMidiPort to (ip, d) and calls
// these; the bytes are whole 4-byte USB-MIDI event packets in little-endian word
// order (the deluge_midi protocol library's wire layout).

/// Drain the bytes of a completed bulk-IN transfer for slot (ip, d) into `dst`
/// (up to `max` bytes; 0 while a transfer is still in flight or nothing arrived),
/// then (re)arm the slot's next receive transfer — host: only when the send chain
/// is idle; peripheral: slot 0 — skipping the re-arm when the USB lock is already
/// held (the next read retries). When it returns > 0 and `arrival_ticks` is
/// non-NULL, fills it with the batch's bulk-IN completion timestamp (board
/// foundation ticks, the HAL's timeLastBRDY) for MIDI-clock-in timing.
uint32_t bsp_usb_midi_read_timed(uint8_t ip, uint8_t d, uint8_t* dst, uint32_t max, uint32_t* arrival_ticks);

/// Queue whole 4-byte event packets (little-endian word order) from `src` for
/// transmission to slot (ip, d); returns the byte count accepted (a multiple
/// of 4, trailing partial packets are not consumed).
uint32_t bsp_usb_midi_write(uint8_t ip, uint8_t d, const uint8_t* src, uint32_t len);

/// Free space in slot (ip, d)'s send ring, in transport bytes (4 per packet).
uint32_t bsp_usb_midi_write_space(uint8_t ip, uint8_t d);

/// Bytes accepted for slot (ip, d) but not yet handed to the hardware (the send
/// ring's backlog, in transport bytes). Backs `deluge_midi_write_pending()`.
uint32_t bsp_usb_midi_write_pending(uint8_t ip, uint8_t d);

#ifdef __cplusplus
}
#endif
