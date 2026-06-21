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

/// host_link — the emulator's I/O transport.
///
/// Frames spark's length-prefixed front-panel protocol (see docs/dev/host_emulator.md)
/// over an AF_UNIX socket or TCP loopback. `deluge_host` plays the *brain*: it emits the display/LED
/// `MessageToDeluge` frames and consumes the `MessageFromDeluge` input frames — the
/// inverse of spark's normal daemon→device direction, because here the real firmware
/// is the device that owns the UI.
///
/// Wire frame: [len_lo][len_hi][type][data...], len = 1 + N (type byte + N data bytes).

#ifndef BSP_HOST_LINK_H
#define BSP_HOST_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── MessageToDeluge type bytes (firmware → GUI: display + LEDs) ──────────────
#define HOST_LINK_MSG_UPDATE_DISPLAY 0x20
#define HOST_LINK_MSG_CLEAR_DISPLAY 0x21
#define HOST_LINK_MSG_SET_PAD_RGB 0x22
#define HOST_LINK_MSG_CLEAR_ALL_PADS 0x23
#define HOST_LINK_MSG_SET_LED 0x24
#define HOST_LINK_MSG_SET_ALL_PADS 0x27
#define HOST_LINK_MSG_SET_KNOB_INDICATOR 0x28
#define HOST_LINK_MSG_SET_SYNCED_LED 0x29
#define HOST_LINK_MSG_CLEAR_ALL_LEDS 0x2A

// ── MessageFromDeluge type bytes (GUI → firmware: input) ─────────────────────
#define HOST_LINK_MSG_PAD_PRESSED 0x01
#define HOST_LINK_MSG_PAD_RELEASED 0x02
#define HOST_LINK_MSG_BUTTON_PRESSED 0x03
#define HOST_LINK_MSG_BUTTON_RELEASED 0x04
#define HOST_LINK_MSG_ENCODER_ROTATED 0x05

/// Bring up the link. If `DELUGE_HOST_LINK` is set in the environment, bind+listen on
/// that AF_UNIX path and **block until the spark GUI connects** (so the first frame the
/// firmware emits is rendered, not dropped). Returns true if a link is active; false —
/// and a no-op transport — when the env var is unset, so the same binary still serves as
/// the headless offline WAV-capture harness. Idempotent.
bool host_link_init(void);

/// True once the GUI peer is connected and the link has not since broken.
bool host_link_active(void);

/// Send one framed message. No-op when the link is inactive. A broken pipe marks the
/// link inactive rather than aborting (the firmware keeps running headless).
void host_link_send(uint8_t type, const uint8_t* data, uint16_t n);

/// Non-blocking: dequeue one inbound frame. On success sets `*type`, copies up to
/// `*inout_len` payload (data, excluding the type byte) bytes into `data`, sets
/// `*inout_len` to the actual data length, and returns true. Returns false when no
/// complete frame is currently buffered.
bool host_link_recv(uint8_t* type, uint8_t* data, uint16_t* inout_len);

#ifdef __cplusplus
}
#endif

#endif // BSP_HOST_LINK_H
