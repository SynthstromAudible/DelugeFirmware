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
/// Phase 3 (this file): the transport *state* now lives here — the per-device
/// RX/TX buffers, the send ring and the completion counters (`BspUsbMidiDevice`),
/// plus the Renesas USB transfer descriptors (`g_usb_midi_*_utr`, defined in the
/// .c). The application's `ConnectedUSBMIDIDevice` keeps only the logical cable
/// fields and points at its `BspUsbMidiDevice` here. The transport *functions*
/// (send/receive state machine) still live in `midi_engine.cpp` and reach in via
/// this struct until phases 4–5 move them down; the read/write stubs below are
/// still inert.

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

/// The transport block for USB-MIDI device `d` on USB IP `ip`. The application's
/// `ConnectedUSBMIDIDevice` wires its `transport` pointer to this at startup, and
/// the HAL receive handlers record completions through it.
BspUsbMidiDevice* bsp_usb_midi_device(uint8_t ip, uint8_t d);

/// Zero the USB-MIDI transport state and initialise the send transfer
/// descriptors. Called once at startup, before the USB stack is opened.
void bsp_usb_midi_init(void);

// --- Send path (BSP-owned: the send ring, the Renesas bulk-OUT pipe driving and
// the send-completion state machine). The application appends pre-packed USB-MIDI
// event words to a device's ring and asks the BSP to flush; the BSP moves the
// bytes. Completion is still driven by the HAL calling
// bsp_usb_midi_send_complete_as_{host,peripheral}() (an upcall, retargeted from
// the app); converting that to a polled count is the remaining phase-4 step.

/// Append a pre-packed 4-byte USB-MIDI event word to `dev`'s send ring (flushing
/// first if the ring is getting full). Backs ConnectedUSBMIDIDevice::bufferMessage.
void bsp_usb_midi_buffer_message(BspUsbMidiDevice* dev, uint32_t word);

/// Free space in `dev`'s send ring, in bytes of serial MIDI (3 per event word).
int bsp_usb_midi_send_buffer_space(BspUsbMidiDevice* dev);

/// Flush buffered USB-MIDI output toward the wire if the pipe is idle (host: all
/// connected devices; peripheral: the upstream device). Safe to call from the
/// MIDI routine or the gate-output interrupt.
void bsp_usb_midi_flush_output(void);

/// Whether there is USB-MIDI output still buffered/in-flight.
bool bsp_usb_midi_anything_in_output_buffer(void);

/// Send-completion entry points, called by the HAL bulk-OUT (BEMP) handlers when a
/// transfer finishes: chain the next queued transfer for the device(s). Host and
/// peripheral variants.
void bsp_usb_midi_send_complete_as_host(int32_t ip);
void bsp_usb_midi_send_complete_as_peripheral(int32_t ip);

// --- Receive path (BSP-owned: the bulk-IN transfer setup + the receive DMA
// buffers). The HAL bulk-IN (BRDY) handlers write the received bytes straight into
// the device's receiveData and report the length here; the application drains and
// decodes that buffer, then asks the BSP to arm the next transfer.

/// Record a completed bulk-IN transfer for a device: `bytesReceived` bytes are now
/// in its receiveData buffer, and it is no longer waiting to receive. Called by the
/// HAL bulk-IN handler; the HAL reports the event and does not touch BSP state
/// directly.
void bsp_usb_midi_receive_complete(uint8_t ip, uint8_t deviceNum, uint16_t bytesReceived);

/// (Re)arm the receive transfer for a hosted device or for the upstream peripheral;
/// call with the USB lock held.
void bsp_usb_midi_setup_host_receive_transfer(int32_t ip, int32_t midiDeviceNum);
void bsp_usb_midi_rearm_peripheral_receive(int32_t ip);

/// Pump the USB-MIDI send/receive state machine and drain HAL pipe-completion
/// events. Backs `deluge_midi_service()`.
void bsp_usb_midi_service(void);

/// Whether USB-MIDI `port` currently has a live (enumerated) endpoint.
bool bsp_usb_midi_port_connected(uint8_t port);

/// USB role queries. On this board the USB controller is the MIDI-device
/// transport, so its host/peripheral role surfaces through the MIDI boundary.
/// `bsp_usb_midi_is_host()` is true when the controller hosts devices (they plug
/// into the Deluge); `bsp_usb_midi_peripheral_connected()` is true when, as a
/// peripheral, a host has enumerated us. These replace the app reading the USB
/// mode globals (g_usb_usbmode / g_usb_peri_connected) directly.
bool bsp_usb_midi_is_host(void);
bool bsp_usb_midi_peripheral_connected(void);

/// Copy up to `max` received bytes from USB-MIDI `port` into `dst`; returns the
/// count copied. Backs `deluge_midi_read()` for USB ports.
uint32_t bsp_usb_midi_read(uint8_t port, uint8_t* dst, uint32_t max);

/// Queue `len` bytes for transmission on USB-MIDI `port`; returns the count
/// accepted. Backs `deluge_midi_write()` for USB ports.
uint32_t bsp_usb_midi_write(uint8_t port, const uint8_t* src, uint32_t len);

#ifdef __cplusplus
}
#endif
