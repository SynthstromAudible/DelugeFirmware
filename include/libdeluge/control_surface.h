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
	DELUGE_EVENT_PAD = 0,        ///< main grid pad
	DELUGE_EVENT_BUTTON = 1,     ///< matrix button
	DELUGE_EVENT_ENCODER = 2,    ///< encoder detent(s)
	DELUGE_EVENT_NO_PRESSES = 3, ///< periodic "all controls released" report
} DelugeInputEventKind;

/// A single decoded input event. Field meaning depends on `kind`:
///  - PAD:     x,y = grid coords; value = velocity (0 = release, 255 = default-on)
///  - BUTTON:  x,y = matrix coords; value = on-state (0 = release, 255 = pressed)
///  - ENCODER: x = encoder index; value = signed detent delta
///  - NO_PRESSES: x,y,value unused
typedef struct DelugeInputEvent {
	DelugeInputEventKind kind;
	uint8_t x;
	uint8_t y;
	int16_t value;
} DelugeInputEvent;

/// One-shot control-surface info captured during boot.
typedef struct DelugeBootInfo {
	uint8_t pic_firmware_version; ///< co-processor firmware version (0 if unknown)
	bool oled_present;            ///< the surface reports an OLED is fitted
	bool factory_reset_requested; ///< the user held the reset control at power-on
} DelugeBootInfo;

/// Read the one-shot boot info from the control surface and store it in `*out`:
/// request the firmware version and a re-send of button states, then drain the
/// boot response burst to capture the firmware version, the OLED-present flag,
/// and whether the user is holding the reset control (select knob) for a factory
/// reset. The application reacts (popup, settings reset). Call once at boot. [task]
void deluge_control_read_boot_info(DelugeBootInfo* out);

/// Poll one decoded input event into `out`. Returns true if an event was
/// dequeued, false if the queue is empty. The application drains this each tick.
/// (Push delivery is also available via `deluge_app_on_event`.) [task]
bool deluge_control_poll_event(DelugeInputEvent* out);

/// Set the colour of one main-grid pad. [task]
void deluge_control_set_pad(uint8_t x, uint8_t y, DelugeColour colour);

/// Set an indicator LED (by board-defined index) on or off. [task]
void deluge_control_set_led(uint8_t led, bool on);

/// Set the per-LED levels of a gold-knob / mod indicator ring. `which` selects
/// the indicator; `levels` points to `count` brightness values (one per ring
/// LED). [task]
void deluge_control_set_indicator(uint8_t which, const uint8_t* levels, uint8_t count);

/// Flush any batched surface output (pad/LED) to the hardware. [task]
void deluge_control_flush(void);

/// One-time control-surface bring-up: configure the board's input controller
/// (debounce, interrupt interval, flash length, link speed). Call once at boot,
/// before reading input. The board owns the specific tuning values. [task]
void deluge_control_init(void);

/// Block until batched surface output has actually been sent to the hardware. [task]
void deluge_control_wait_for_flush(void);

/// Switch the control surface into pad-scanning mode (full-speed link). Call once
/// boot is far enough along to begin reading pads/buttons. [task]
void deluge_control_setup_for_pads(void);

/// Power/enable the OLED panel where it is driven through the control controller
/// (shared-SPI boards). No-op on boards whose display is independent. [task]
void deluge_control_enable_oled(void);

/// Poll for a "resume" request from the control surface — e.g. the select knob
/// pressed to dismiss a fault/freeze screen. Returns true once signalled. The
/// fault handlers spin on this. [task]
bool deluge_control_poll_resume(void);

/// Free space, in bytes, in the control surface's bulk pad-output buffer. The
/// application uses this as back-pressure before queueing large pad redraws, so
/// it does not overrun the link to the surface. [task]
uint32_t deluge_control_pad_output_space(void);

/// Set the colours of a main-grid column pair. The Deluge surface drives main
/// pads in 16-LED column pairs; `idx` is the column-pair index and `colours`
/// holds `count` entries (one per LED in the pair). [task]
void deluge_control_set_pad_columns(uint8_t idx, const DelugeColour* colours, uint8_t count);

/// Flash main pad `idx` using the surface's built-in flash timer (optionally in
/// a surface-defined palette colour index). [task]
void deluge_control_flash_pad(uint8_t idx);
void deluge_control_flash_pad_colour(uint8_t idx, int32_t colour_idx);

/// Smooth-scroll animation primitives: the surface keeps an off-screen
/// framebuffer it smears to animate scrolling. [task]
void deluge_control_scroll_horizontal(uint8_t flags);
void deluge_control_scroll_vertical(bool up, const DelugeColour* colours, uint8_t count);
void deluge_control_scroll_row(uint8_t row, DelugeColour colour);
void deluge_control_scroll_done(void);

/// Surface refresh / dimmer timing (board-defined units). [task]
void deluge_control_set_refresh_time(uint8_t ms);
void deluge_control_set_dimmer_interval(uint8_t interval);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_CONTROL_SURFACE_H
