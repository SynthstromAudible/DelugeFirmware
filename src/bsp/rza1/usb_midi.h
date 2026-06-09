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
/// Phase 2 (this file): the interface + inert stubs. The legacy transport path is
/// still in effect, so behaviour is unchanged until the migration phases wire it.

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Pump the USB-MIDI send/receive state machine and drain HAL pipe-completion
/// events. Backs `deluge_midi_service()`.
void bsp_usb_midi_service(void);

/// Whether USB-MIDI `port` currently has a live (enumerated) endpoint.
bool bsp_usb_midi_port_connected(uint8_t port);

/// Copy up to `max` received bytes from USB-MIDI `port` into `dst`; returns the
/// count copied. Backs `deluge_midi_read()` for USB ports.
uint32_t bsp_usb_midi_read(uint8_t port, uint8_t* dst, uint32_t max);

/// Queue `len` bytes for transmission on USB-MIDI `port`; returns the count
/// accepted. Backs `deluge_midi_write()` for USB ports.
uint32_t bsp_usb_midi_write(uint8_t port, const uint8_t* src, uint32_t len);

#ifdef __cplusplus
}
#endif
