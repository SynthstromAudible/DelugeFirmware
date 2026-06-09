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

/// libdeluge/midi_io.h — raw MIDI byte streams.
///
/// Transport-agnostic: a "port" may be a DIN UART or a USB-MIDI virtual cable.
/// The boundary moves raw bytes; MIDI parsing stays in the application.
/// Backed today by `drivers/uart` + the USB MIDI stack; by `deluge-bsp::uart`,
/// `::midi_gate`, and `::usb` on the Rust side.
#ifndef LIBDELUGE_MIDI_IO_H
#define LIBDELUGE_MIDI_IO_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque MIDI port id, 0 .. deluge_midi_port_count()-1.
typedef uint8_t DelugeMidiPort;

/// Kind of MIDI transport behind a port.
typedef enum DelugeMidiPortKind {
	DELUGE_MIDI_DIN = 0,
	DELUGE_MIDI_USB_DEVICE = 1,
	DELUGE_MIDI_USB_HOST = 2,
} DelugeMidiPortKind;

/// Number of independent USB controllers (IP cores) the board has, each hosting
/// its own table of USB-MIDI devices. The application dimensions its
/// per-controller device arrays by this. Mirrors the RZ/A1L HAL's USB_NUM_USBIP
/// (=1); declared here so app code can size those arrays without including the
/// USB HAL.
#define DELUGE_USB_NUM_CONTROLLERS 1

/// Number of MIDI ports the board exposes. [task]
uint8_t deluge_midi_port_count(void);

/// Transport kind for a port. [task]
DelugeMidiPortKind deluge_midi_port_kind(DelugeMidiPort port);

/// Read up to `max` received bytes from `port` into `dst`. Non-blocking;
/// returns the number of bytes copied (0 if none available). [task] [isr]
uint32_t deluge_midi_read(DelugeMidiPort port, uint8_t* dst, uint32_t max);

/// Queue `len` bytes for transmission on `port`. Returns the number of bytes
/// accepted (may be < len if the TX buffer is full). Non-blocking. [task]
uint32_t deluge_midi_write(DelugeMidiPort port, const uint8_t* src, uint32_t len);

/// Free space (in bytes) in the port's TX buffer. [task]
uint32_t deluge_midi_write_space(DelugeMidiPort port);

/// Bytes currently buffered for transmission on `port` but not yet on the wire.
/// The app uses this to tell whether output is still pending. [task]
uint32_t deluge_midi_write_pending(DelugeMidiPort port);

/// Read the next received DIN/serial MIDI byte together with the hardware
/// timestamp of its arrival. Returns true and fills `*byte` and `*arrival_ticks`
/// when a byte was available; false (leaving the outputs untouched) when the RX
/// buffer is empty. The timestamp is in the board's foundation tick units — the
/// same source the app already uses to time incoming MIDI clock — so clock-in
/// jitter is preserved. Non-blocking. [task] [isr]
bool deluge_midi_din_read_timed(uint8_t* byte, uint32_t* arrival_ticks);

/// Push any buffered TX bytes toward the wire if idle. [task]
void deluge_midi_flush(DelugeMidiPort port);

/// Service the MIDI transport: pump the USB-MIDI send/receive state machine and
/// drain hardware completion events. The application calls this from its MIDI
/// routine; it is the pull point that replaces the USB stack calling up into the
/// app. (DIN has no state machine; this is a no-op for it.) [task]
void deluge_midi_service(void);

/// True if `port` currently has a connected/usable MIDI endpoint (a DIN port is
/// always present; a USB port is present only while a device is enumerated). The
/// app polls this instead of reading USB-stack connection globals. [task]
bool deluge_midi_port_connected(DelugeMidiPort port);

/// USB-MIDI role queries. On this board USB host is the MIDI-device transport, so
/// the app reads its role through the MIDI boundary rather than the USB-stack mode
/// globals.
///
/// True when the USB controller acts as a host (devices plug into the Deluge);
/// false when it is a peripheral (the Deluge plugs into a computer). The app uses
/// this to decide how many USB-MIDI devices it may enumerate per controller. [task]
bool deluge_midi_usb_is_host(void);

/// True when, as a USB peripheral, a host is attached (has enumerated us).
/// Meaningless in host mode. [task]
bool deluge_midi_usb_peripheral_connected(void);

/// A USB-host enumeration event observed since the last poll. USB host is the
/// MIDI-device transport on this board, so these surface through the MIDI
/// boundary.
typedef enum DelugeUsbHostEvent {
	DELUGE_USB_HOST_NONE = 0,
	DELUGE_USB_HOST_HUB_ATTACHED,
	DELUGE_USB_HOST_DEVICE_DETACHED,
	DELUGE_USB_HOST_DEVICE_NOT_RECOGNIZED,
	DELUGE_USB_HOST_DEVICES_MAX,
} DelugeUsbHostEvent;

/// Drain the next pending USB-host enumeration event (NONE if none pending).
///
/// Pull-based, not a callback: the USB host stack records events and the app
/// polls for them, then decides what to display and localize. The USB stack
/// does no UI/localization and never calls up into the app. [task]
DelugeUsbHostEvent deluge_midi_poll_usb_host_event(void);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_MIDI_IO_H
