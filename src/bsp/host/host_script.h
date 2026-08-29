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

/// Level 3 of the UI test harness (spec G §4.2): drive the real headless `deluge_host` app from a
/// UI script and snapshot what it put on the panels, pads, LEDs and knob indicators.
///
/// The BSP is C, so everything here is C17 and depends on nothing in `src/deluge` — only on
/// `<libdeluge/...>` and on `tests/harness/ui_script.h`'s `extern "C"` façade. Three files:
///
///   - host_script.c         the script clock, the injector, the `.pbm`/`.state` writers.
///   - host_bsp.c            the shadows the writers read, and the two injection points
///                           (`host_script_push_event`, `host_script_add_encoder_edges`).
///   - host_script_tables.c  the button-matrix and encoder-order name tables, split out (with
///                           `host_script_tables.h`) so they compile dependency-free into both the
///                           host BSP and `deluge_spec` — see that header for why.
#ifndef HOST_SCRIPT_H
#define HOST_SCRIPT_H

#include "host_script_tables.h" // ui_script_button_xy / ui_script_encoder_index -- see that header
#include "libdeluge/control_surface.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ===========================================================================
// Shadow geometry — shared so host_bsp.c's arrays and host_script.c's readers
// cannot disagree about their shape.
// ===========================================================================

/// Panels the host board can describe (`HOST_MAX_DISPLAYS` in host_bsp.c).
#define HOST_SCRIPT_MAX_DISPLAYS 4
/// Bytes in the widest page-major panel the host supports: 128 × 64 / 8.
#define HOST_SCRIPT_OLED_BYTES 1024
/// Pad-grid columns shadowed. The Deluge sends 18 (`kDisplayWidth` 16 + `kSideBarWidth` 2, see
/// `pad_leds.cpp:260-274`); 32 leaves room for a wider board without another audit, and columns
/// nothing ever wrote stay black and so are never reported.
#define HOST_SCRIPT_PAD_COLS 32
/// Pad-grid rows (`kDisplayHeight`).
#define HOST_SCRIPT_PAD_ROWS 8
/// Indicator LEDs shadowed. Legacy indices are `x + y*9` over a 9 × 4 matrix
/// (`hid/led/indicator_leds.h:30-64`), so 36 are reachable; 64 rounds that up.
#define HOST_SCRIPT_LED_COUNT 64
/// Knob indicators the surface has (`deluge_control_set_indicator`'s `which`).
#define HOST_SCRIPT_RINGS 2
/// Segment levels per knob indicator (spark's `SetKnobIndicator` carries exactly 4).
#define HOST_SCRIPT_RING_LEVELS 4

// ===========================================================================
// host_script.c — the script runner
// ===========================================================================

/// @brief Load `DELUGE_HOST_SCRIPT`, apply any 0 ms `board` step, and validate the rest.
///
/// Must be called from `main()` before anything reads `deluge_board()`: a `board` step rewrites
/// the board descriptor, and the descriptor is latched by its first reader.
///
/// Exits the process with status 2 (after a message on stderr) when a script is set but cannot be
/// used: `DELUGE_HOST_DETERMINISTIC` unset, an unreadable or unparseable file, a shell-level
/// command (`model`/`notice`), a `board` step away from 0 ms, or a name no board table knows.
void host_script_init(void);

/// @brief True once `host_script_init()` has loaded a script.
bool host_script_active(void);

/// @brief Apply every step whose time has come, then write any snapshots they asked for.
///
/// The clock is the deterministic one: `now_ms = host_rendered_frames * 1000 / 44100`
/// (host_bsp.c's `DELUGE_HOST_DETERMINISTIC` clock, `host_bsp.c:104-153`). Called from
/// `host_input_pump()`; a no-op when no script is loaded. A `quit` step does not return.
void host_script_pump(void);

/// @brief Write the snapshots the pump has queued but not yet written, and forget them.
///
/// Normally a no-op: `host_script_pump()` flushes at the end of every call. It exists for the
/// exit paths (`quit`, and `main()`'s unreachable tail) so no snapshot is lost to a process that
/// stops between the pump's two halves.
void host_script_write_pending(void);

// `ui_script_button_xy` / `ui_script_encoder_index` are declared in `host_script_tables.h`
// (included above) rather than here: that header/TU pair is dependency-free so it can compile
// into `deluge_spec` for `tests/spec/harness/tables_spec.cpp`'s cross-check against
// `deluge_profile()`, which this header cannot (it pulls in `<libdeluge/control_surface.h>` and
// the BSP shadow accessors below).

// ===========================================================================
// host_bsp.c — the shadows and the injection points
// ===========================================================================

/// @brief Queue one decoded input event for `deluge_control_poll_event()`.
void host_script_push_event(DelugeInputEventKind kind, uint8_t x, uint8_t y, int16_t value);

/// @brief Add quadrature edges to one encoder and wake the encoder task, as a rotation does.
/// @param index Firmware encoder index (see `ui_script_encoder_index`).
/// @param edges Signed edge count; saturated into the `int8_t` the accumulator holds.
void host_script_add_encoder_edges(uint8_t index, int32_t edges);

/// @brief Rewrite the board's panel set from a `WxH+top[,WxH+top…]` spec (a `board` step).
/// @return 0 on success; -1 when the spec names no usable panel; -2 when `deluge_board()` has
/// already been read, so the descriptor is latched.
int host_script_set_displays(const char* spec);

/// @brief How many panels the board reports.
uint8_t host_script_display_count(void);

/// Sidebar columns to the right of the main pad grid.
///
/// `DelugeBoard` carries `pad_grid_width`/`pad_grid_height` but no sidebar width, and the app's
/// `kSideBarWidth` (`src/board_layout.hpp`) is a C++ header the BSP cannot include. So it is spelt
/// here, matching what the app actually sends: `pad_leds.cpp:260-274` drives columns `0..17` on a
/// 16-wide grid.
#define HOST_SCRIPT_SIDEBAR_COLS 2

/// @brief The pad grid a scripted `pad` step may address: `pad_grid_width + HOST_SCRIPT_SIDEBAR_COLS`
///        columns by `pad_grid_height` rows.
///
/// Deliberately does NOT latch the board descriptor the way `deluge_board()` does -- it is called
/// while validating the script, before a `board` step has been applied. Safe, because a `board`
/// step only ever rewrites the panel set; the pad grid is fixed by the board itself.
void host_script_pad_bounds(uint8_t* cols, uint8_t* rows);

/// @brief The last frame blitted to panel \p index, page-major, or NULL if it has never blitted.
/// @param index  Panel index.
/// @param width  Set to the frame's pixel width.
/// @param height Set to the frame's pixel height.
const uint8_t* host_script_oled_shadow(uint8_t index, uint16_t* width, uint16_t* height);

/// @brief The declared on/off state of every indicator LED, indexed by its legacy `x + y*9` id.
/// @return `HOST_SCRIPT_LED_COUNT` bytes, 0 or 1.
const uint8_t* host_script_led_shadow(void);

/// @brief Every pad's last colour, `[(col * HOST_SCRIPT_PAD_ROWS + row) * 3 + {r,g,b}]`.
const uint8_t* host_script_pad_shadow(void);

/// @brief Knob indicator \p which's segment levels, or NULL if the app never set it.
/// @return `HOST_SCRIPT_RING_LEVELS` bytes.
const uint8_t* host_script_ring_shadow(uint8_t which);

#ifdef __cplusplus
}
#endif

#endif // HOST_SCRIPT_H
