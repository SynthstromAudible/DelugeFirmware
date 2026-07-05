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

#include "usb_midi.h"

#include "RZA1/usb/r_usb_basic/r_usb_basic_if.h" // usb_utr_t, USB_NUM_USBIP, usb_cb_t, USB_TRAN_END
#include "RZA1/usb/r_usb_basic/src/hw/inc/r_usb_bitdefine.h"
#include "RZA1/usb/r_usb_hmidi/src/inc/r_usb_hmidi.h"
#include "RZA1/usb/usb_midi_completion.h"           // HAL-owned pipe-completion queue (drained below)
#include "RZA1/usb/userdef/r_usb_hmidi_config.h"    // USB_CFG_HMIDI_INT_SEND, USB_CFG_USE_USBIP
#include "board_config.h"                           // MAX_NUM_USB_MIDI_DEVICES, PLACE_SDRAM_BSS
#include "drivers/usb/userdef/r_usb_pmidi_config.h" // USB_CFG_PMIDI_BULK_OUT/_IN
#include "libdeluge/system.h"                       // ENTER/EXIT_CRITICAL_SECTION

#include <string.h>

// This module owns the USB-MIDI transport: the per-device RX/TX buffers + send
// ring, the send path (ring, Renesas bulk-OUT pipe driving, send completion), the
// receive transfer setup + completion, and per-slot connection tracking (the
// attach/detach notifications called from the HAL enumeration paths). The
// application sees none of it: it moves 4-byte event packets through the
// <libdeluge/midi_io.h> byte-stream boundary (midi_io.c maps ports to (ip, d)
// slots here), and the receive transfers are re-armed as part of the port read.
//
// Completion is pulled, not pushed: the HAL bulk-IN/-OUT handlers record generic
// (pipe) completion events into a HAL-owned queue (usb_midi_completion.*), and
// bsp_usb_midi_service() below drains them. The app calls that from its USB pump,
// so there are no HAL -> BSP upcalls (see docs/dev/usb_midi_transport_relocation.md
// and docs/dev/usb_midi_boundary_unification.md).

// --- HAL globals/functions the send path drives (resolved at the final link). ---
extern usb_utr_t* g_p_usb_pipe[];
extern uint16_t g_usb_hmidi_tmp_ep_tbl[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES][(USB_EPL * 2) + 1];
extern uint16_t g_usb_peri_connected;
extern uint16_t g_usb_usbmode; // USB mode HOST/PERI
// usbLock is owned by midi_engine.cpp for now (shared with the receive path); the
// flush guards against the receive task running mid-flush, as the original did.
extern volatile uint32_t usbLock;
usb_regadr_t usb_hstd_get_usb_ip_adr(uint16_t ipno);
void change_destination_of_send_pipe(usb_utr_t* ptr, uint16_t pipe, uint16_t* tbl, int32_t sq);
void usb_send_start_rohan(usb_utr_t* ptr, uint16_t pipe, uint8_t const* data, int32_t size);
void usb_receive_start_rohan_midi(uint16_t pipe);
// The HAL USB-stack task. Its bulk-IN/-OUT handlers record pipe completions into
// the queue bsp_usb_midi_service() drains, so the BSP pumps it (rather than the
// app) to keep the Renesas USB task behind the boundary.
void usb_cstd_usb_task(void);

// Per-device USB-MIDI transport state. The receive DMA targets receiveData
// directly, so this stays in SDRAM (as it did inside connectedUSBMIDIDevices).
PLACE_SDRAM_BSS BspUsbMidiDevice g_usb_midi_devices[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES];

// Renesas USB transfer descriptors for the MIDI bulk pipes, moved here from
// midi_engine.cpp. The HAL and the application reference them via extern.
usb_utr_t g_usb_midi_send_utr[USB_NUM_USBIP];
usb_utr_t g_usb_midi_recv_utr[USB_NUM_USBIP][MAX_NUM_USB_MIDI_DEVICES];

// Send-path bookkeeping (moved from midi_engine.cpp). The first three are also
// read by the app's receive path until it moves down, so they keep
// external linkage; the rest are BSP-internal.
uint8_t stopSendingAfterDeviceNum[USB_NUM_USBIP];
uint8_t usbDeviceNumBeingSentToNow[USB_NUM_USBIP];
uint8_t anyUSBSendingStillHappening[USB_NUM_USBIP];

// One slot without, and one with, interrupt endpoints. Also reset by the HAL
// hmidi driver on (re)configure, so it keeps external linkage.
uint8_t currentDeviceNumWithSendPipe[USB_NUM_USBIP][2] = {MAX_NUM_USB_MIDI_DEVICES, MAX_NUM_USB_MIDI_DEVICES};

static bool anythingInUSBOutputBuffer = false;

// Timestamp of the last bulk-IN completion (BRDY), recorded by the HAL for MIDI
// clock timing. Defined here (moved from midi_engine.cpp); the HAL writes it and
// the application reads it. External linkage so both resolve it at the link.
uint32_t timeLastBRDY[USB_NUM_USBIP];

static void send_complete_callback(usb_utr_t* p_mess, uint16_t data1, uint16_t data2);
static void receive_complete_callback(usb_utr_t* p_mess, uint16_t data1, uint16_t data2);
static void setup_host_receive_transfer(int32_t ip, int32_t midiDeviceNum);
static void rearm_peripheral_receive(int32_t ip);

static BspUsbMidiDevice* bsp_usb_midi_device(uint8_t ip, uint8_t d) {
	return &g_usb_midi_devices[ip][d];
}

void bsp_usb_midi_init(void) {
	memset(g_usb_midi_devices, 0, sizeof g_usb_midi_devices);

	g_usb_peri_connected = 0; // Needs initializing with A2 driver

	for (int32_t ip = 0; ip < USB_NUM_USBIP; ip++) {
		// This might not be used: the HAL calls bsp_usb_midi_send_complete_as_host()
		// directly as host, and just sets variables as peripheral; kept for error
		// conditions / parity with the legacy descriptor setup.
		g_usb_midi_send_utr[ip].complete = (usb_cb_t)send_complete_callback;
		g_usb_midi_send_utr[ip].p_setup = 0;
		g_usb_midi_send_utr[ip].segment = USB_TRAN_END;
		g_usb_midi_send_utr[ip].ip = ip;
		g_usb_midi_send_utr[ip].ipp = usb_hstd_get_usb_ip_adr(ip);

		for (int32_t d = 0; d < MAX_NUM_USB_MIDI_DEVICES; d++) {
			// The receive DMA writes straight into this BSP buffer (zero-copy).
			g_usb_midi_recv_utr[ip][d].p_tranadr = g_usb_midi_devices[ip][d].receiveData;
			g_usb_midi_recv_utr[ip][d].complete = (usb_cb_t)receive_complete_callback;
			g_usb_midi_recv_utr[ip][d].p_setup = 0;
			g_usb_midi_recv_utr[ip][d].segment = USB_TRAN_END;
			g_usb_midi_recv_utr[ip][d].ip = ip;
			g_usb_midi_recv_utr[ip][d].ipp = usb_hstd_get_usb_ip_adr(ip);
		}
	}
}

// --- Send ring ----------------------------------------------------------------

// Move data from the ring buffer into the smaller dataSendingNow buffer the
// hardware driver consumes. Returns false if the ring was empty.
static bool consume_send_data(BspUsbMidiDevice* dev) {
	uint32_t queued = dev->ringBufWriteIdx - dev->ringBufReadIdx;
	if (queued == 0) {
		return false;
	}

	uint32_t max_size = MIDI_SEND_BUFFER_LEN_INNER;
	if (g_usb_usbmode == USB_HOST) {
		// Many devices do not accept more than 64 bytes at a time, even less with
		// hubs involved (the hydrasynth only takes 2 messages per transfer).
		max_size = MIDI_SEND_BUFFER_LEN_INNER_HOST;
	}

	uint32_t to_send = (queued < max_size) ? queued : max_size;
	for (uint32_t i = 0; i < to_send; i++) {
		memcpy(dev->dataSendingNow + (i * 4), &dev->sendDataRingBuf[dev->ringBufReadIdx & MIDI_SEND_RING_MASK], 4);
		dev->ringBufReadIdx++;
	}

	dev->numBytesSendingNow = to_send * 4;
	return true;
}

static bool has_buffered_send_data(BspUsbMidiDevice* dev) {
	return (dev->ringBufWriteIdx - dev->ringBufReadIdx) > 0;
}

static void bsp_usb_midi_buffer_message(BspUsbMidiDevice* dev, uint32_t word) {
	uint32_t queued = dev->ringBufWriteIdx - dev->ringBufReadIdx;
	if (queued > 16) {
		if (!anyUSBSendingStillHappening[0]) {
			bsp_usb_midi_flush_output();
		}
		queued = dev->ringBufWriteIdx - dev->ringBufReadIdx;
	}
	if (queued > MIDI_SEND_BUFFER_LEN_RING) {
		// TODO: show some error message
		return;
	}

	dev->sendDataRingBuf[dev->ringBufWriteIdx & MIDI_SEND_RING_MASK] = word;
	dev->ringBufWriteIdx++;

	anythingInUSBOutputBuffer = true;
}

// --- Send to the wire ---------------------------------------------------------

static void flush_to_hosted_device(int32_t ip, int32_t d) {
	BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, d);
	// The pipe can change even when resuming a transfer if hubs are involved (a hub
	// transaction can run before the send-complete handler), so always re-read it.
	int32_t pipeNumber = g_usb_hmidi_tmp_ep_tbl[USB_CFG_USE_USBIP][d][0];
	g_usb_midi_send_utr[USB_CFG_USE_USBIP].keyword = pipeNumber;
	g_usb_midi_send_utr[USB_CFG_USE_USBIP].tranlen = dev->numBytesSendingNow;
	g_usb_midi_send_utr[USB_CFG_USE_USBIP].p_tranadr = dev->dataSendingNow;

	usbDeviceNumBeingSentToNow[USB_CFG_USE_USBIP] = d;

	int32_t isInterrupt = (pipeNumber == USB_CFG_HMIDI_INT_SEND);

	if (d != currentDeviceNumWithSendPipe[USB_CFG_USE_USBIP][isInterrupt]) {
		currentDeviceNumWithSendPipe[USB_CFG_USE_USBIP][isInterrupt] = d;
		change_destination_of_send_pipe(&g_usb_midi_send_utr[USB_CFG_USE_USBIP], pipeNumber,
		                                g_usb_hmidi_tmp_ep_tbl[USB_CFG_USE_USBIP][d], dev->sq);
	}

	dev->sq = !dev->sq;

	g_p_usb_pipe[pipeNumber] = &g_usb_midi_send_utr[USB_CFG_USE_USBIP];

	usb_send_start_rohan(&g_usb_midi_send_utr[USB_CFG_USE_USBIP], pipeNumber, dev->dataSendingNow,
	                     dev->numBytesSendingNow);
}

// Warning - this will sometimes (not always) be called in an ISR
void bsp_usb_midi_flush_output(void) {
	// Make sure the interrupt doesn't fire mid flush.
	ENTER_CRITICAL_SECTION();
	if (usbLock) {
		EXIT_CRITICAL_SECTION();
		return;
	}

	anythingInUSBOutputBuffer = false;

	// anyUSBSendingStillHappening[ip] acts as the lock between routine and interrupt
	// on the sending side; usbLock guards the receive side.
	usbLock = 1;

	for (int32_t ip = 0; ip < USB_NUM_USBIP; ip++) {
		if (anyUSBSendingStillHappening[ip]) {
			// still sending, call me later maybe
			anythingInUSBOutputBuffer = true;
			continue;
		}

		bool potentiallyAHost = (g_usb_usbmode == USB_HOST);
		bool aPeripheral = g_usb_peri_connected;

		if (aPeripheral) {
			BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, 0);

			if (!consume_send_data(dev)) {
				continue;
			}

			g_usb_midi_send_utr[ip].keyword = USB_CFG_PMIDI_BULK_OUT;
			g_usb_midi_send_utr[ip].tranlen = dev->numBytesSendingNow;
			g_usb_midi_send_utr[ip].p_tranadr = dev->dataSendingNow;

			usbDeviceNumBeingSentToNow[ip] = 0;
			anyUSBSendingStillHappening[ip] = 1;

			g_p_usb_pipe[USB_CFG_PMIDI_BULK_OUT] = &g_usb_midi_send_utr[ip];
			usb_send_start_rohan(NULL, USB_CFG_PMIDI_BULK_OUT, dev->dataSendingNow, dev->numBytesSendingNow);

			// when done, bsp_usb_midi_send_complete_as_peripheral() will be called in an interrupt
		}

		else if (potentiallyAHost) {
			// Written with multiple devices on hubs in mind, but works for a single device too.
			int32_t midiDeviceNumToSendTo = currentDeviceNumWithSendPipe[ip][0]; // This will do
			if (midiDeviceNumToSendTo >= MAX_NUM_USB_MIDI_DEVICES) {
				midiDeviceNumToSendTo = 0; // In case it was set to "none", I think
			}

			int32_t newStopSendingAfter = midiDeviceNumToSendTo - 1;
			if (newStopSendingAfter < 0) {
				newStopSendingAfter += MAX_NUM_USB_MIDI_DEVICES;
			}

			// Make sure that's on a connected device - it probably would be...
			while (true) {
				BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, midiDeviceNumToSendTo);
				if (dev->connected && has_buffered_send_data(dev)) {
					break; // We found a connected one
				}
				if (midiDeviceNumToSendTo == newStopSendingAfter) {
					goto getOut; // If back where we started, none are connected.
				}
				midiDeviceNumToSendTo++;
				if (midiDeviceNumToSendTo >= MAX_NUM_USB_MIDI_DEVICES) {
					midiDeviceNumToSendTo = 0; // Wrap back to start of list
				}
			}

			// Stop after a device which we know is connected
			while (true) {
				BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, newStopSendingAfter);

				if (dev->connected && has_buffered_send_data(dev)) {
					break; // We found a connected one
				}

				newStopSendingAfter--;
				if (newStopSendingAfter < 0) {
					newStopSendingAfter += MAX_NUM_USB_MIDI_DEVICES;
				}
			}

			// Copy the buffers for all devices
			int32_t d = midiDeviceNumToSendTo;
			while (true) {
				BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, d);

				if (dev->connected) {
					consume_send_data(dev);
				}
				if (d == newStopSendingAfter) {
					break;
				}
				d++;
				if (d >= MAX_NUM_USB_MIDI_DEVICES) {
					d = 0;
				}
			}

			stopSendingAfterDeviceNum[ip] = newStopSendingAfter;
			anyUSBSendingStillHappening[ip] = 1;

			flush_to_hosted_device(ip, midiDeviceNumToSendTo);
		}
getOut: {}
	}

	usbLock = 0;
	EXIT_CRITICAL_SECTION();
}

// --- Send completion (called by the HAL bulk-OUT handlers) --------------------

// We now bypass calling this for successful sends as peripheral on A1.
void bsp_usb_midi_send_complete_as_host(int32_t ip) {
	int32_t midiDeviceNum = usbDeviceNumBeingSentToNow[ip];
	BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, midiDeviceNum);

	dev->numBytesSendingNow = 0;

	// If there was more to send on the same device, resume sending.
	if (consume_send_data(dev)) {
		// TODO: cooperative scheduling so a flood on one device can't starve another.
		flush_to_hosted_device(ip, midiDeviceNum);
		return;
	}

	// If that was the last device we were going to send to, we're done.
	if (midiDeviceNum == stopSendingAfterDeviceNum[ip]) {
		anyUSBSendingStillHappening[ip] = 0;
		return;
	}

	while (true) {
		midiDeviceNum++;
		if (midiDeviceNum >= MAX_NUM_USB_MIDI_DEVICES) {
			midiDeviceNum -= MAX_NUM_USB_MIDI_DEVICES;
		}
		dev = bsp_usb_midi_device(ip, midiDeviceNum);
		if (dev->connected && dev->numBytesSendingNow) {
			// If here, we got a connected device, so flush
			flush_to_hosted_device(ip, midiDeviceNum);
			return;
		}
		if (midiDeviceNum
		    == stopSendingAfterDeviceNum[ip]) { // reached the end and the last one got disconnected (rare)
			usbDeviceNumBeingSentToNow[ip] = stopSendingAfterDeviceNum[ip];
			anyUSBSendingStillHappening[ip] = 0;
			return;
		}
	}
}

// Registered as the send descriptor's completion callback. May fire for error
// conditions even though the host path is driven directly.
static void send_complete_callback(usb_utr_t* p_mess, uint16_t data1, uint16_t data2) {
	(void)data1;
	(void)data2;

	// An error can occur if another device connected/disconnected from a hub during
	// fast MIDI sending; that is recoverable, so just carry on.

#if USB_NUM_USBIP == 1
	int32_t ip = 0;
#else
	int32_t ip = p_mess->ip;
#endif
	(void)p_mess;

	bsp_usb_midi_send_complete_as_host(ip);
}

void bsp_usb_midi_send_complete_as_peripheral(int32_t ip) {
	BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, 0);
	dev->numBytesSendingNow = 0; // Even easier!

	// This can happen as part of a detach, see detachedAsPeripheral().
	if (anyUSBSendingStillHappening[ip] == 0) {
		return;
	}

	if (consume_send_data(dev)) {
		g_usb_midi_send_utr[ip].tranlen = dev->numBytesSendingNow;
		g_usb_midi_send_utr[ip].p_tranadr = dev->dataSendingNow;

		usb_send_start_rohan(NULL, USB_CFG_PMIDI_BULK_OUT, dev->dataSendingNow, dev->numBytesSendingNow);
	}
	else {
		// Acts as a lock: the sending side of the device (incl. the read side of the
		// ring) "belongs" to ongoing/scheduled interrupts until this clears.
		anyUSBSendingStillHappening[0] = 0;
	}
}

// --- Receive completion + transfer setup --------------------------------------

// Record a completed bulk-IN transfer for a device: bytesReceived bytes are now in
// its receiveData buffer. (Sometimes, e.g. with a Teensy knob box, this is 0; cope.)
void bsp_usb_midi_receive_complete(uint8_t ip, uint8_t deviceNum, uint16_t bytesReceived) {
	BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, deviceNum);
	dev->numBytesReceived = bytesReceived;
	dev->currentlyWaitingToReceive = 0; // need to set up another receive
}

// Registered as the receive descriptor's completion callback. On the RZ/A1L the
// HAL bulk-IN handlers usually report completions via bsp_usb_midi_receive_complete()
// directly, so this is the parity path for when the callback is used. tranlen is how
// many bytes of the 64-byte request did NOT arrive, so 64 - tranlen were received.
static void receive_complete_callback(usb_utr_t* p_mess, uint16_t data1, uint16_t data2) {
	(void)data1;
	(void)data2;

#if USB_NUM_USBIP == 1
	int32_t ip = 0;
#else
	int32_t ip = p_mess->ip;
#endif

	if (p_mess->status == USB_DATA_ERR) {
		return; // Can happen if the user disconnects a device - totally normal
	}

	int32_t deviceNum = p_mess - &g_usb_midi_recv_utr[ip][0];
	bsp_usb_midi_receive_complete(ip, deviceNum, 64 - p_mess->tranlen);
}

// Lock USB before calling this!
static void setup_host_receive_transfer(int32_t ip, int32_t midiDeviceNum) {
	(void)ip;
	g_usb_midi_devices[USB_CFG_USE_USBIP][midiDeviceNum].currentlyWaitingToReceive = 1;

	int32_t pipeNumber = g_usb_hmidi_tmp_ep_tbl[USB_CFG_USE_USBIP][midiDeviceNum][USB_EPL];

	g_usb_midi_recv_utr[USB_CFG_USE_USBIP][midiDeviceNum].keyword = pipeNumber;
	g_usb_midi_recv_utr[USB_CFG_USE_USBIP][midiDeviceNum].tranlen = 64;

	g_p_usb_pipe[pipeNumber] = &g_usb_midi_recv_utr[USB_CFG_USE_USBIP][midiDeviceNum];

	usb_receive_start_rohan_midi(pipeNumber);
}

// Lock USB before calling this!
static void rearm_peripheral_receive(int32_t ip) {
	g_usb_midi_recv_utr[ip][0].keyword = USB_CFG_PMIDI_BULK_IN;
	g_usb_midi_recv_utr[ip][0].tranlen = 64;

	g_usb_midi_devices[ip][0].currentlyWaitingToReceive = 1;

	g_p_usb_pipe[USB_CFG_PMIDI_BULK_IN] = &g_usb_midi_recv_utr[ip][0];
	usb_receive_start_rohan_midi(USB_CFG_PMIDI_BULK_IN);
}

// Pump the HAL USB-stack task (which records the completions), then drain the HAL
// pipe-completion queue and act on each event: record received bytes, or chain the
// next queued send. Called from the app's USB pump (via deluge_midi_service()) with
// the USB lock held, so it runs in one polled context.
void bsp_usb_midi_service(void) {
	// Pump the HAL USB-stack task first; its bulk-IN/-OUT handlers record pipe
	// completions into the queue we drain just below. The app no longer calls
	// usb_cstd_usb_task() directly — it pumps the transport via the boundary.
	usb_cstd_usb_task();

	UsbMidiCompletion c;
	while (usb_midi_completion_take(&c)) {
		switch ((UsbMidiCompletionKind)c.kind) {
		case USB_MIDI_COMPLETION_RECEIVE:
			bsp_usb_midi_receive_complete(0, c.deviceNum, c.bytesReceived);
			break;
		case USB_MIDI_COMPLETION_SEND_HOST:
			bsp_usb_midi_send_complete_as_host(USB_CFG_USE_USBIP);
			break;
		case USB_MIDI_COMPLETION_SEND_PERIPHERAL:
			bsp_usb_midi_send_complete_as_peripheral(0);
			break;
		}
	}
}

bool bsp_usb_midi_is_host(void) {
	return g_usb_usbmode == USB_HOST;
}

bool bsp_usb_midi_peripheral_connected(void) {
	return g_usb_peri_connected != 0;
}

// --- Connection tracking (called from the HAL enumeration paths) ---------------

// A hosted device was configured on slot (ip, d): reset the slot's transport
// state — the sequence bit and the transfer counters the application's setup()
// used to zero — and mark it a live send sink for the send loop above.
void bsp_usb_midi_device_attached(uint8_t ip, uint8_t d) {
	BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, d);
	dev->sq = 0;
	dev->numBytesSendingNow = 0;
	dev->currentlyWaitingToReceive = 0;
	dev->numBytesReceived = 0;
	dev->connected = 1;
}

void bsp_usb_midi_device_detached(uint8_t ip, uint8_t d) {
	if (d >= MAX_NUM_USB_MIDI_DEVICES) {
		return; // The HAL's device-address search can come up empty.
	}
	g_usb_midi_devices[ip][d].connected = 0;
}

void bsp_usb_midi_peripheral_configured(uint8_t ip) {
	BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, 0);
	dev->numBytesSendingNow = 0;
	dev->currentlyWaitingToReceive = 0;
	dev->numBytesReceived = 0;
	dev->connected = 1;
	anyUSBSendingStillHappening[ip] = 0; // Nothing is sending yet right now.
}

void bsp_usb_midi_peripheral_detached(uint8_t ip) {
	g_usb_midi_devices[ip][0].connected = 0;
	// Reset this so a half-finished send can't wedge the flush path across a
	// detach (see bsp_usb_midi_send_complete_as_peripheral).
	anyUSBSendingStillHappening[ip] = 0;
}

bool bsp_usb_midi_device_connected(uint8_t ip, uint8_t d) {
	return g_usb_midi_devices[ip][d].connected != 0;
}

// --- The byte-stream surface backing <libdeluge/midi_io.h> ---------------------

// Copy the bytes of a completed bulk-IN transfer for slot (ip, d) into dst (up to
// max_bytes), clear the slot's received-count, and return the byte count. Call
// only while no transfer is in flight: the buffer is stable then.
static uint32_t drain_received(uint8_t ip, uint8_t d, uint8_t* dst, uint32_t max_bytes) {
	BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, d);
	uint32_t n = dev->numBytesReceived;
	if (n == 0) {
		return 0;
	}
	if (n > max_bytes) {
		n = max_bytes;
	}
	memcpy(dst, dev->receiveData, n);
	dev->numBytesReceived = 0;
	return n;
}

// True when controller ip is not mid-send through a chain of host devices, so it
// is safe to (re)arm a host receive transfer.
static bool host_send_idle(uint8_t ip) {
	return usbDeviceNumBeingSentToNow[ip] == stopSendingAfterDeviceNum[ip];
}

uint32_t bsp_usb_midi_read_timed(uint8_t ip, uint8_t d, uint8_t* dst, uint32_t max, uint32_t* arrival_ticks) {
	BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, d);
	if (dev->currentlyWaitingToReceive) {
		return 0; // A transfer is in flight; nothing stable to drain yet.
	}

	uint32_t n = drain_received(ip, d, dst, max);
	if (n && arrival_ticks) {
		// One timestamp covers the whole drained batch — the same value (and read
		// point) as the old timeLastBRDY read, so MIDI-clock-in timing is unchanged.
		*arrival_ticks = timeLastBRDY[ip];
	}

	// (Re)arm the slot's next receive transfer — formerly the application's job
	// after dispatching. Skipped when the USB lock is already held (an outer
	// context owns the stack); the next read retries, as the app's usbLockNow
	// guard did.
	if (!usbLock) {
		if (g_usb_usbmode == USB_HOST) {
			// Only re-arm while not mid-send through the chain of host devices.
			if (dev->connected && host_send_idle(ip)) {
				usbLock = 1;
				setup_host_receive_transfer(ip, d);
				usbLock = 0;
			}
		}
		else if (d == 0 && g_usb_peri_connected) {
			usbLock = 1;
			rearm_peripheral_receive(ip);
			usbLock = 0;
		}
	}

	return n;
}

uint32_t bsp_usb_midi_write(uint8_t ip, uint8_t d, const uint8_t* src, uint32_t len) {
	BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, d);
	uint32_t accepted = 0;
	// Whole 4-byte event packets only, in little-endian word order (the layout
	// the deluge_midi protocol library's UsbEventPacket::word() produces).
	while (accepted + 4 <= len) {
		uint32_t word = (uint32_t)src[accepted] | ((uint32_t)src[accepted + 1] << 8)
		                | ((uint32_t)src[accepted + 2] << 16) | ((uint32_t)src[accepted + 3] << 24);
		bsp_usb_midi_buffer_message(dev, word);
		accepted += 4;
	}
	return accepted;
}

uint32_t bsp_usb_midi_write_space(uint8_t ip, uint8_t d) {
	BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, d);
	uint32_t queued = dev->ringBufWriteIdx - dev->ringBufReadIdx;
	return (MIDI_SEND_BUFFER_LEN_RING - queued) * 4;
}

uint32_t bsp_usb_midi_write_pending(uint8_t ip, uint8_t d) {
	// The send ring's backlog: bytes accepted but not yet handed to the hardware
	// (bytes already moved into dataSendingNow belong to the wire).
	BspUsbMidiDevice* dev = bsp_usb_midi_device(ip, d);
	return (dev->ringBufWriteIdx - dev->ringBufReadIdx) * 4;
}
