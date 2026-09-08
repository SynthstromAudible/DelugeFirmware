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

#include <cstdint>

class MIDICable;

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

/// @brief `SoundEditor::noteOnReceivedForMidiLearn`'s body (`gui/ui/sound_editor.cpp:1595-1597`),
///        for the ported menu.
///
/// Legacy's is one line -- `return getCurrentMenuItem()->learnNoteOn(cable, channel, note);` -- with
/// no mode guard and the item's own answer as the result. The one thing it does implicitly is what
/// this has to say out loud: `SoundEditor` is only ever the current UI with a menu open, so
/// "`getCurrentMenuItem()`" is total there. Under the guest the current UI is `ShellHostUI`
/// whatever is on the scene stack, so this asks first whether the input-frontmost scene really is
/// `menu_scene()` and declines otherwise -- a browser, a rename field or the marker editor is not
/// a menu item and has no `learnNoteOn`.
///
/// @param cable   The cable the note arrived on.
/// @param channel The channel or MPE zone, as `PlaybackHandler::noteMessageReceived` computed it
///                (`playback/playback_handler.cpp:2966`).
/// @param note    The note number.
/// @return The focused item's own answer, or false when the menu is not the frontmost scene.
bool shellGuestMenuLearnNoteOn(::MIDICable& cable, int32_t channel, int32_t note);

/// @brief `SoundEditor::pcReceivedForMidiLearn`'s body (`gui/ui/sound_editor.cpp:1588-1594`), for
///        the ported menu.
///
/// The same shape with legacy's two extra conditions kept: it acts only under
/// `currentUIMode == UI_MODE_MIDI_LEARN && !Buttons::isShiftButtonPressed()`, and it returns
/// **true** inside that gate whatever the item answered -- so a declining item still stops
/// `PlaybackHandler::programChangeReceived`'s `else` from handing the message to `view`
/// (`playback/playback_handler.cpp:2897-2901`). Both of those are legacy's, reproduced rather than
/// tidied. The mode test is redundant with the dispatcher's own gate one frame up; legacy tests it
/// twice and so does this.
///
/// @param cable   The cable.
/// @param channel The channel.
/// @param program The program number.
/// @return True if the gate was open and the menu was the frontmost scene.
bool shellGuestMenuLearnProgramChange(::MIDICable& cable, int32_t channel, int32_t program);

} // namespace deluge::gui::ui
