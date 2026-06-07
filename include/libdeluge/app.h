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

/// libdeluge/app.h — the inbound half of the boundary.
///
/// These functions are **implemented by the application** and **called by the
/// platform/BSP**. The platform owns `main` and the runtime; the application
/// never assumes it owns either (so an Embassy executor can own them later).
///
/// Today (C/C++ BSP): a tiny `main()` calls `deluge_platform_init()`, then
/// `deluge_app_init()`, then loops on `deluge_app_tick()`; the audio path calls
/// `deluge_app_render()`.
/// Endgame (Rust/Embassy BSP): the Rust `main` brings up Embassy, runs
/// `deluge_app_init()` + `deluge_app_tick()` inside one task, and the BSP audio
/// task calls `deluge_app_render()`. The application code is identical.
#ifndef LIBDELUGE_APP_H
#define LIBDELUGE_APP_H

#include "board.h"
#include "control_surface.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// One-time application setup. Called once after the platform and all BSP
/// services are initialised, before audio starts and before the first tick.
/// [task]
void deluge_app_init(const DelugeBoard* board);

/// Realtime audio callback. Render `frames` stereo frames into `out`, reading
/// the same number of input frames from `in` (either may be NULL if the board
/// has no input/output). Samples are Q31.
///
/// MUST NOT block, allocate, take locks, or call any non-`[audio]` boundary
/// function. Note: final volume / click-free fades are handled by the BSP
/// (e.g. the RZ/A1L SCUX DVU block) — render at unity here.
/// [audio]
void deluge_app_render(const DelugeStereoSample* in, DelugeStereoSample* out, uint32_t frames);

/// Cooperative work slice. Called repeatedly by the platform's run loop / task.
/// Runs the application's own scheduler and UI/housekeeping. Should return
/// promptly so the platform can service other work.
/// [task]
void deluge_app_tick(void);

/// Optional push delivery of an input event (pad/button/encoder/MIDI-in). May be
/// left unimplemented by applications that instead poll via control_surface.h /
/// midi_io.h; the BSP discovers support at link time or via a capability flag.
/// [task]
void deluge_app_on_event(const DelugeInputEvent* event);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_APP_H
