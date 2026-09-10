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

#pragma once

/// @file
/// @brief Installs `deluge::scenes::menu::NoteBridge` (spec §5.4) -- `instrumentClipView`'s note
///        and note-row state, reached from `src/scenes` without an include.
///
/// This header names no `scenes/` type, only the one no-arguments function that installs the
/// bridge -- the same one-symbol-wide seam `shell_guest_services.h` draws around its own wiring
/// call. `shell_note_bridge.cpp` is where every bridge entry lives, and where the `scenes/menu/`
/// include actually happens; see that file for the transcription and its citations.

namespace deluge::gui::ui {

/// @brief Install the note bridge, once. A static `NoteBridge` of function pointers onto
///        `instrumentClipView` and legacy statics, installed via `deluge::scenes::menu::set_note_bridge`.
///
/// Unlike `wireShellGuestServices()`, this holds no `Song` and mirrors no view state -- every entry
/// reads or writes live legacy globals synchronously when called, the way legacy's own
/// `readCurrentValue`/`selectEncoderAction` overrides do. So it has no ordering constraint of its
/// own: it may run before or after `wireShellGuestServices()`, before `setupBlankSong()`, before
/// any UI opens -- calling it only wires the function pointers, and nothing dereferences them until
/// Task 5's leaves are reached, which cannot happen before a song and a clip exist.
void wireShellNoteBridge();

} // namespace deluge::gui::ui
