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
/// @brief The three real `shell::Service`s the guest runs, and the one call that wires them.
///
/// @par Why this is its own translation unit and not three members of `ShellHostUI`
///
/// Plan F1's file table says the guest "owns the `Shell`, the `GuestSurfaceSet`, and the three
/// services", and three members would be the obvious shape. It does not build: the host spec suite
/// compiles `gui/ui/shell_host_ui.cpp` **directly** and never builds the `deluge_scenes` closure
/// (`tests/spec/CMakeLists.txt:30-34`, which fakes `deluge::scenes::scene_registry()` for exactly
/// that reason), and `services::ModKnobs`, `services::GlobalControls` and `services::MidiLearn` are
/// concrete classes over `model/` that cannot be faked the way a free function can. Naming their
/// types in `shell_host_ui.cpp` would take `shell_host_ui_spec`'s 52 examples off the board.
///
/// So the guest keeps the `Shell` and this file keeps the services, joined by a single
/// no-arguments function the host suite stubs in `tests/spec/gui/support/fake_legacy_ui.cpp`. The
/// ownership is the same -- the services outlive nothing but the process, and they are installed
/// into `shellHostUI`'s own `Shell` -- and the seam is one symbol wide.
///
/// @par What this closes
///
/// Migration §10.2 item 7's boot half, recorded at `scenes/registry.h`'s boot-wiring note: each of
/// the three is constructed from the `Shell`'s own `SceneStack&` (two also from its
/// `InputLowering&`), so a real service cannot exist before the shell it serves, and until F1 task
/// 2's `Shell::set_services` it could not be installed after it either. This is the application
/// finally making that call.

namespace deluge::gui::ui {

/// @brief Construct the three services over `shellHostUI`'s `Shell` (once), re-seed everything they
///        borrow from legacy, and install them.
///
/// Idempotent and cheap: the services themselves are built on the first call and kept; every call
/// re-reads `currentSong`, `view`'s active mod controllable and mod region, and the two `ViewState`
/// blocks, then re-installs the slots. `Shell::set_services` is defined to be safe over a live set
/// (it rebuilds the deduplicated tick list from scratch), so re-calling costs three pointer stores
/// and a list rebuild.
///
/// Call it at boot -- so the slots are never empty -- and again from `ShellHostUI::opened()`, which
/// is the moment the borrowed model state can have moved since. Both callers exist; see the
/// function's own comments for what each field is read from and which of them are stated as
/// uncovered.
void wireShellGuestServices();

} // namespace deluge::gui::ui
