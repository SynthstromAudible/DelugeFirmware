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

/// libdeluge/display.h — main display output (OLED framebuffer or 7-segment).
///
/// The application renders into its own framebuffer and hands it to the BSP to
/// blit; the BSP owns the SPI/DMA transfer (today `drivers/oled` + `RZA1/oled`;
/// `deluge-bsp::oled` on the Rust side). Use `deluge_board()->display_kind` to
/// pick the right path.
#ifndef LIBDELUGE_DISPLAY_H
#define LIBDELUGE_DISPLAY_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Blit a 1-bpp OLED framebuffer. `pixels` is `width*height/8` bytes, packed
/// in the panel's native page/column order (matching the current canvas format).
/// Asynchronous: the BSP may DMA the transfer; see `deluge_display_busy`.
/// No-op on 7-segment boards. [task]
DelugeStatus deluge_display_blit_oled(const uint8_t* pixels, uint16_t width, uint16_t height);

/// True while a previous blit/transfer is still in flight. [task] [isr]
bool deluge_display_busy(void);

/// Write the 7-segment display. `digits` points to `count` raw segment bytes
/// (board-defined segment bit layout). No-op on OLED boards. [task]
DelugeStatus deluge_display_write_seven_segment(const uint8_t* digits, uint8_t count);

/// One-time display bring-up: configure the shared SPI for the panel and run
/// the panel's init sequence. Call once at boot, only when an OLED is fitted
/// (`deluge_board_probe_oled()`); no-op / not called on 7-segment boards. [task]
void deluge_display_init(void);

/// Service the display's pending output, pushing any queued transfer toward the
/// panel. The application calls this to keep the display updating during long
/// blocking operations (e.g. card access). No-op where there is no async
/// display queue. [task]
void deluge_display_service(void);

/// Advance the display's low-level transfer state machine — called when the
/// display's scheduled service timer fires. [task]
void deluge_display_timer_event(void);

/// Synchronously blit a 1-bpp OLED framebuffer to the panel and block until the
/// user requests resume. Used by the fault handler to show an error screen from
/// a frozen context, bypassing the normal async transfer queue. `pixels` is the
/// full framebuffer (`OLED_MAIN_WIDTH*HEIGHT/8` bytes). [task]
void deluge_display_freeze(const uint8_t* pixels);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_DISPLAY_H
