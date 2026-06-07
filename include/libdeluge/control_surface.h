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

/// libdeluge/control_surface.h — pads, buttons, encoders, and their LEDs.
///
/// A *logical* surface: the application sees pad/button/encoder events and sets
/// pad/LED colours. The PIC co-processor and its UART protocol stay entirely
/// inside the BSP (today `drivers/pic`; on the Rust side `deluge-bsp::pic`,
/// `::pads`, `::controls`, `::encoder`). Other boards may have no PIC at all.
#ifndef LIBDELUGE_CONTROL_SURFACE_H
#define LIBDELUGE_CONTROL_SURFACE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// 24-bit RGB colour, one byte per channel.
typedef struct DelugeColour {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} DelugeColour;

typedef enum DelugeInputEventKind {
	DELUGE_EVENT_PAD = 0,     ///< main grid pad
	DELUGE_EVENT_BUTTON = 1,  ///< matrix button
	DELUGE_EVENT_ENCODER = 2, ///< encoder detent(s)
} DelugeInputEventKind;

/// A single decoded input event. Field meaning depends on `kind`:
///  - PAD:     x,y = grid coords; value = velocity/pressure (0 = release)
///  - BUTTON:  x,y = matrix coords; value = 1 pressed / 0 released
///  - ENCODER: x = encoder index; value = signed detent delta
typedef struct DelugeInputEvent {
	DelugeInputEventKind kind;
	uint8_t x;
	uint8_t y;
	int16_t value;
} DelugeInputEvent;

/// Poll one decoded input event into `out`. Returns true if an event was
/// dequeued, false if the queue is empty. The application drains this each tick.
/// (Push delivery is also available via `deluge_app_on_event`.) [task]
bool deluge_control_poll_event(DelugeInputEvent* out);

/// Set the colour of one main-grid pad. [task]
void deluge_control_set_pad(uint8_t x, uint8_t y, DelugeColour colour);

/// Set an indicator LED (by board-defined index) on or off. [task]
void deluge_control_set_led(uint8_t led, bool on);

/// Set a gold-knob / mod indicator ring level (0..max), `which` selects the
/// indicator. [task]
void deluge_control_set_indicator(uint8_t which, uint8_t level);

/// Flush any batched surface output (pad/LED) to the hardware. [task]
void deluge_control_flush(void);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_CONTROL_SURFACE_H
